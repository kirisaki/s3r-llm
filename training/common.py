"""What the training scripts share: where things are, and how a sample turns
into tokens. The tokenization has to match the firmware (main/llm.cpp) piece
for piece, or the model sees something at run time that it never saw here."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
CARDPUTER = ROOT / "cardputer-ai"
BASE_MODEL = CARDPUTER / "data" / "chat_model_8m"  # TinyTalk 2
DATA = ROOT / "data"
OUT = ROOT / "out"

EOS = "<|endoftext|>"
NEWLINE_ID = 198
HEADER = re.compile(r"^Name: [^\n]*\n")


def display_name(name: str) -> str:
    """How the firmware writes a device name into the header: "alice" -> "Alice"."""
    return name[:1].upper() + name[1:]


def encode_sample(sample: str, tokenizer):
    """One blank-line-separated sample -> (ids, labels). Labels are -100 where
    there is nothing to learn: the header, what the user says, the "Bot:" cue.

    A chat sample is

        Name: Alice                      (optional)
        User: <message>
        Bot: <reply><|endoftext|>
        User: ...

    and is tokenized in the pieces the firmware feeds the model: the header,
    then per turn "User: <message>\\nBot:", " <reply>", eos, and "\\n" before
    the next turn. Anything else (the stories of the corpus) is handled like in
    cardputer-ai's finetune_chat.py, which this follows."""
    eos = tokenizer.eos_token_id
    enc = lambda text: tokenizer(text, add_special_tokens=False).input_ids
    ids, labels = [], []

    def put(tokens, learn):
        ids.extend(tokens)
        labels.extend(tokens if learn else [-100] * len(tokens))

    header = HEADER.match(sample)
    if header:
        put(enc(header.group(0)), False)
        sample = sample[header.end():]

    if sample.startswith("User: "):
        lines = sample.split("\n")
        for i in range(0, len(lines) - 1, 2):
            user, bot = lines[i], lines[i + 1]
            if not bot.startswith("Bot: "):
                break
            reply = " " + bot[len("Bot: "):].removesuffix(EOS)
            if i > 0:
                put([NEWLINE_ID], False)
            put(enc(user + "\nBot:"), False)
            put(enc(reply), True)
            put([eos], True)
    else:
        sample = sample.removesuffix(EOS)
        body = sample.find("\nStory:")
        if body >= 0:
            cut = body + len("\nStory:")
            put(enc(sample[:cut]), False)
            put(enc(sample[cut:]), True)
        else:
            put(enc(sample), True)
        put([eos], True)
    return ids, labels
