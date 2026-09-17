#!/usr/bin/env python3
"""Asks a model for its name, under headers with names it was trained on, names
it has never seen, names like a device has by default, and no header at all.

Replies are sampled the way the firmware does it (temperature 0.8, top-p 0.9),
so the numbers are what to expect on the device, give or take quantization."""

import argparse
import random

import torch
from transformers import GPT2TokenizerFast, GPTNeoForCausalLM

from common import OUT, display_name
from make_data import HELD_OUT_NAMES, NAMES

QUESTIONS = [
    "What is your name?",
    "What's your name?",
    "Who are you?",
    "Hi! I'm Tom. What's your name?",
    "Do you have a name?",
    "What should I call you?",
]
SMALL_TALK = ["Hello! How are you?", "What sound does a dog make?", "Do you like apples?"]


@torch.no_grad()
def reply(model, tokenizer, device, name, turns):
    """turns: what the user says, one after the other; returns the last reply."""
    enc = lambda text: tokenizer(text, add_special_tokens=False).input_ids
    ids = enc(f"Name: {name}\n") if name else []
    text = ""
    for i, turn in enumerate(turns):
        if i > 0:
            ids += [tokenizer.eos_token_id] + enc("\n")
        ids += enc(f"User: {turn}\nBot:")
        out = model.generate(torch.tensor([ids], device=device), max_new_tokens=40, do_sample=True,
                             temperature=0.8, top_p=0.9, eos_token_id=tokenizer.eos_token_id,
                             pad_token_id=tokenizer.eos_token_id)
        new = out[0][len(ids):].tolist()
        if new and new[-1] == tokenizer.eos_token_id:
            new = new[:-1]
        ids += new
        text = tokenizer.decode(new)
    return text.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model-dir", default=str(OUT / "model"))
    ap.add_argument("--samples", type=int, default=5, help="per question and name")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    random.seed(args.seed)
    torch.manual_seed(args.seed)

    device = "cuda" if torch.cuda.is_available() else "cpu"
    tokenizer = GPT2TokenizerFast.from_pretrained(args.model_dir)
    model = GPTNeoForCausalLM.from_pretrained(args.model_dir).to(device).eval()

    groups = {
        "trained on": random.sample(NAMES, 8),
        "never seen": random.sample(HELD_OUT_NAMES, 8),
        "like a device": ["S3r-llm-39a8", "S3r-llm-9c30", "Atom-7", "Unit42"],
        "made up": ["Zorbix", "Plimsy", "Quendor", "Wibbet", "Yolanthe", "Fennick"],
    }
    for group, names in groups.items():
        hits = total = 0
        late_hits = late_total = 0
        examples = []
        for name in names:
            name = display_name(name)
            for question in QUESTIONS:
                for _ in range(args.samples):
                    text = reply(model, tokenizer, device, name, [question])
                    hits += name in text
                    total += 1
                examples.append(f"    [{name}] {question} -> {text}")
            # The name still has to be there after some small talk
            for _ in range(args.samples):
                text = reply(model, tokenizer, device, name, SMALL_TALK + ["What is your name?"])
                late_hits += name in text
                late_total += 1
        print(f"{group}: name given in {hits}/{total} replies, {late_hits}/{late_total} after three turns of small talk")
        print("\n".join(random.sample(examples, 5)))

    print("no header:")
    for question in QUESTIONS:
        print(f"    {question} -> {reply(model, tokenizer, device, None, [question])}")
    print("small talk under a header:")
    for question in SMALL_TALK + ["What is the capital of France?", "Tell me about your day."]:
        print(f"    [Alice] {question} -> {reply(model, tokenizer, device, 'Alice', [question])}")


if __name__ == "__main__":
    main()
