#!/usr/bin/env python3
"""Fine-tunes TinyTalk 2 on the corpus of make_data.py.

Follows cardputer-ai's tools/finetune_chat.py: samples are packed into blocks
of the context length, and the loss is only taken on what the bot says. Starts
from the finished chat model rather than from TinyStories-Instruct, with a
lower learning rate, since it is only to learn one more thing."""

import argparse
import math
import time

import torch
from torch.utils.data import DataLoader, TensorDataset
from transformers import GPT2TokenizerFast, GPTNeoForCausalLM

from common import BASE_MODEL, DATA, OUT, encode_sample


def load_blocks(path, tokenizer, seq_len):
    cache = path.with_suffix(f".{seq_len}.pt")
    if cache.exists() and cache.stat().st_mtime > path.stat().st_mtime:
        return torch.load(cache)
    ids, labels = [], []
    for sample in path.read_text().split("\n\n"):
        if sample.strip():
            i, l = encode_sample(sample.strip("\n"), tokenizer)
            ids.extend(i)
            labels.extend(l)
    n = len(ids) // seq_len
    x = torch.tensor(ids[:n * seq_len]).view(n, seq_len)
    y = torch.tensor(labels[:n * seq_len]).view(n, seq_len)
    torch.save((x, y), cache)
    return x, y


@torch.no_grad()
def evaluate(model, loader, device):
    model.eval()
    total, count = 0.0, 0
    for x, y in loader:
        x, y = x.to(device), y.to(device)
        n = (y[:, 1:] != -100).sum().item()
        total += model(x, labels=y).loss.item() * n
        count += n
    model.train()
    return total / max(count, 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", default=str(BASE_MODEL))
    ap.add_argument("--out-dir", default=str(OUT / "model"))
    ap.add_argument("--epochs", type=int, default=2)
    ap.add_argument("--seq-len", type=int, default=256)
    ap.add_argument("--batch-size", type=int, default=32, help="samples per optimizer step")
    ap.add_argument("--micro-batch", type=int, default=8,
                    help="samples per pass; the logits over 50K tokens are what fills the GPU")
    ap.add_argument("--lr", type=float, default=2e-4)
    ap.add_argument("--warmup", type=int, default=200)
    args = ap.parse_args()

    device = "cuda" if torch.cuda.is_available() else "cpu"
    tokenizer = GPT2TokenizerFast.from_pretrained(args.base)
    model = GPTNeoForCausalLM.from_pretrained(args.base).to(device)
    model.train()
    print(f"{sum(p.numel() for p in model.parameters()) / 1e6:.1f}M parameters on {device}")

    loaders = {}
    for name in ["train", "val_chat", "val_names"]:
        x, y = load_blocks(DATA / f"{name}.txt", tokenizer, args.seq_len)
        print(f"{name}: {len(x):,} blocks, {(y != -100).float().mean() * 100:.0f}% of the tokens in the loss")
        loaders[name] = DataLoader(TensorDataset(x, y), batch_size=args.micro_batch, shuffle=name == "train",
                                   drop_last=name == "train")

    def report(when):
        print(f"{when}: val loss {evaluate(model, loaders['val_chat'], device):.4f} on the chat corpus, "
              f"{evaluate(model, loaders['val_names'], device):.4f} on names", flush=True)

    accumulate = args.batch_size // args.micro_batch
    steps = len(loaders["train"]) // accumulate * args.epochs
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=0.01)

    def lr_at(step):
        if step < args.warmup:
            return args.lr * step / args.warmup
        progress = (step - args.warmup) / max(1, steps - args.warmup)
        return 1e-5 + 0.5 * (args.lr - 1e-5) * (1 + math.cos(math.pi * progress))

    report("before")
    step, start = 0, time.time()
    for epoch in range(args.epochs):
        for i, (x, y) in enumerate(loaders["train"]):
            loss = model(x.to(device), labels=y.to(device)).loss
            (loss / accumulate).backward()
            if (i + 1) % accumulate:
                continue
            for group in optimizer.param_groups:
                group["lr"] = lr_at(step)
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()
            optimizer.zero_grad(set_to_none=True)
            step += 1
            if step % 100 == 0:
                rate = step * args.batch_size * args.seq_len / (time.time() - start)
                print(f"step {step}/{steps} loss {loss.item():.4f} ({rate / 1e3:.0f}K tokens/s)", flush=True)
        report(f"epoch {epoch + 1}")

    model.cpu().save_pretrained(args.out_dir)
    tokenizer.save_pretrained(args.out_dir)
    print(f"saved to {args.out_dir}")


if __name__ == "__main__":
    main()
