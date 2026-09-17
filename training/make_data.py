#!/usr/bin/env python3
"""Builds the corpus that teaches the model its name.

The firmware starts a conversation with "Name: <name>" if the device has been
given one. The model is to go by that name: say it when asked, introduce
itself with it, and otherwise carry on as before. Without the header it has no
name.

Takes the chat corpus of cardputer-ai as it is, so that nothing is forgotten,
and adds to it:

- a share of its dialogues again, under a header with a random name, with the
  names the bot gives itself in them replaced, and sometimes a question about
  the name added at the start or the end;
- short dialogues from templates, all about the name;
- the same without a header, where the answer is that there is no name.

Half of the names are made up of random syllables, so that there are too many
to learn by heart. The list of names for validation does not occur in training,
to see whether the model reads the name off the header."""

import argparse
import random
import re

from common import CARDPUTER, DATA, EOS, display_name

NAMES = """
Alice Bob Tom Lily Max Sam Anna Ben Mia Leo Emma Jack Lucy Tim Amy Dan Kate Joe Ella Finn Zoe Ned Ivy Gus May Ray
Eva Ian Liz Rob Sue Ted Uma Vic Wes Zed Abby Adam Alan Alex Andy Beth Bill Carl Chad Cody Dana Dave Dora Drew Earl
Eric Erin Fred Gail Gary Gina Glen Greg Hank Hugo Iris Jade Jane Jean Jeff Jill Joan Joel John Josh Judy June Kara
Karl Kent Kurt Kyle Lana Lars Leah Lena Lisa Lola Luke Lynn Mark Mary Matt Mike Nate Neil Nick Nina Noah Nora Omar
Otto Owen Paul Pete Phil Rita Rosa Ross Ruby Ruth Ryan Sara Sean Seth Tara Tess Theo Tina Todd Tony Troy Vera Walt
Will Yuki Zach Zara Aaron Abdul Aisha Akira Amber Andre Angel Anita Arjun Avery Bella Betty Billy Blake Brian Bruce
Caleb Carla Carol Casey Chloe Chris Clara Colin Craig Daisy David Diana Diego Dylan Eddie Edith Elena Elias Ellie
Emily Ethan Felix Fiona Frank Grace Hanna Harry Hazel Heidi Helen Henry Holly Irene Isaac Jacob James Jamie Janet
Jason Jenny Jesse Jimmy Julia Karen Keith Kelly Kevin Laura Layla Lewis Linda Logan Louis Lucas Maria Mario Megan
Molly Nancy Naomi Oscar Paula Pedro Peggy Penny Peter Polly Ralph Robin Roger Sally Sandy Sarah Scott Simon Sofia
Steve Susan Tammy Terry Tyler Wanda Wendy Willy Pixel Robo Atom Chip Byte Spark Widget Gizmo Bolt Nano Pico Dot
Beep Buzz Ziggy Momo Kiki Coco Lulu Nemo Pip Bo Al Jo Ky
""".split()

HELD_OUT_NAMES = """
Quinn Rufus Opal Basil Wilma Cedric Yara Mabel Hiro Kenji Sven Priya Tobias Juno Elsa Boris Nadia Percy Gwen Ozzy
Marvin Hal Tars Robby Twiki Mochi Taro Hana Sora Riku
""".split()

# Said by a bot that has a name. {n} is its name, {u} the user's.
NAME_TURNS = [
    ("What is your name?", ["My name is {n}.", "I'm {n}.", "My name is {n}. What is your name?"]),
    ("What's your name?", ["My name is {n}.", "I'm {n}. What's yours?", "It's {n}."]),
    ("Hi, what's your name?", ["Hi! My name is {n}.", "Hi! I'm {n}. What's your name?"]),
    ("So, what's your name?", ["I'm {n}.", "My name is {n}."]),
    ("Who are you?", ["I am {n}.", "I'm {n}. I am a little bot.", "My name is {n}."]),
    ("Do you have a name?", ["Yes, my name is {n}.", "Yes! I'm {n}."]),
    ("What are you called?", ["I am called {n}.", "My name is {n}."]),
    ("What should I call you?", ["You can call me {n}.", "Call me {n}."]),
    ("Tell me your name.", ["My name is {n}.", "Sure. I'm {n}."]),
    ("And you are?", ["I'm {n}.", "I am {n}. Nice to meet you!"]),
    ("Who am I talking to?", ["You are talking to {n}.", "This is {n}."]),
    ("What was your name again?", ["It's {n}.", "My name is {n}."]),
    ("My name is {u}. What's yours?", ["Hi {u}! My name is {n}.", "I'm {n}. Nice to meet you, {u}!"]),
    ("I'm {u}. Who are you?", ["Hi {u}, I'm {n}.", "I am {n}. Nice to meet you, {u}."]),
    ("Hello, I am {u}. And you?", ["Hello {u}! I am {n}.", "I'm {n}. It's nice to meet you, {u}."]),
    ("Hi! I'm {u}. What's your name?", ["Hi {u}! I'm {n}.", "My name is {n}. Nice to meet you, {u}!"]),
    ("Are you {n}?", ["Yes, I am {n}.", "Yes, that's me!", "Yes, I'm {n}. Who are you?"]),
    ("Is your name {n}?", ["Yes, my name is {n}.", "Yes, it is!"]),
    ("Are you {o}?", ["No, I am {n}.", "No, my name is {n}."]),
    ("Is your name {o}?", ["No, my name is {n}.", "No, it's {n}."]),
    ("Hi {n}!", ["Hi! How are you?", "Hello! It's nice to see you."]),
    ("Hello {n}, how are you?", ["I'm good, thank you! How are you?", "I am fine. And you?"]),
    ("Nice to meet you, {n}.", ["Nice to meet you too!", "It's nice to meet you, too."]),
    ("Good morning, {n}!", ["Good morning! Did you sleep well?", "Good morning!"]),
    ("Bye, {n}!", ["Bye! See you soon.", "Goodbye!"]),
    ("Can you spell your name?", ["Sure. It's {n}.", "My name is {n}."]),
    ("Do you like your name?", ["Yes, I like being {n}.", "Yes! {n} is a nice name."]),
]

# Said by a bot that has not been given one
NAMELESS_TURNS = [
    ("What is your name?", ["I don't have a name yet.", "I don't have a name. I am just a little bot."]),
    ("What's your name?", ["I don't have a name yet.", "I am just a little bot. I don't have a name."]),
    ("Who are you?", ["I am a little bot.", "I'm just a little bot. I don't have a name yet."]),
    ("Do you have a name?", ["No, not yet.", "No, I don't have a name yet."]),
    ("What should I call you?", ["I don't have a name yet. What do you want to call me?"]),
    ("My name is {u}. What's yours?", ["Hi {u}! I don't have a name yet.", "Nice to meet you, {u}. I am just a little bot."]),
    ("Hi! I'm {u}. What's your name?", ["Hi {u}! I don't have a name yet."]),
]

# "I'm Karen" in the mouth of the bot, to be replaced by the name of the header
SELF_INTRODUCTION = re.compile(r"\b([Mm]y name is|[Mm]y name's|I'm|I am|[Cc]all me|[Tt]his is)\s+([A-Z][a-z]+)\b")


ONSETS = "b c d f g h j k l m n p r s t v w z br ch cl dr fl gr kl pr sh st th tr".split()
VOWELS = "a e i o u ai ea ee ia io oo ou".split()
CODAS = ["", "", "", "l", "m", "n", "r", "s", "t", "x", "k", "th", "sh", "nd"]


def made_up_name(rng):
    """Pronounceable, and one of too many to learn by heart: "Trilo", "Moosh",
    "Kalendra". With a fixed list of names the model learns the list; it takes
    names it cannot know to make it read the header."""
    syllables = rng.choice([1, 2, 2, 2, 3])
    name = "".join(rng.choice(ONSETS) + rng.choice(VOWELS) for _ in range(syllables))
    return name + rng.choice(CODAS)


def device_name(rng):
    """The default name of a device, and the likes of it."""
    kind = rng.choice(["s3r-llm-{:04x}", "atom-{}", "bot-{}", "unit{}", "s3r-{:02x}"])
    return kind.format(rng.randrange(0x10000) if "x}" in kind else rng.randrange(100))


def pick_name(rng, names):
    kind = rng.random()
    if kind < 0.1:
        return display_name(device_name(rng))
    if kind < 0.6:
        return display_name(made_up_name(rng))
    return display_name(rng.choice(names))


def name_turn(rng, turns, names, name):
    question, answers = rng.choice(turns)
    others = [n for n in names if n != name]
    fill = {"n": name, "u": rng.choice(others), "o": rng.choice(others)}
    return f"User: {question.format(**fill)}\nBot: {rng.choice(answers).format(**fill)}{EOS}"


def under_header(rng, sample, names, known_names, p_name_turn):
    """A dialogue of the corpus as held by a bot with a name."""
    name = pick_name(rng, names)

    def rename(match):
        return f"{match.group(1)} {name}" if match.group(2) in known_names else match.group(0)

    lines = [SELF_INTRODUCTION.sub(rename, line) if line.startswith("Bot: ") else line
             for line in sample.split("\n")]
    if rng.random() < p_name_turn:
        turn = name_turn(rng, NAME_TURNS, names, name).split("\n")
        lines = turn + lines if rng.random() < 0.4 else lines + turn
    return f"Name: {name}\n" + "\n".join(lines)


def templated(rng, names, chit_chat):
    """A short dialogue all about the name, now and then after some small talk."""
    name = pick_name(rng, names)
    turns = [name_turn(rng, NAME_TURNS, names, name) for _ in range(rng.choice([1, 1, 1, 2]))]
    if rng.random() < 0.3:
        turns.insert(0, rng.choice(chit_chat))
    return f"Name: {name}\n" + "\n".join(turns)


def build(rng, chat, names, known_names, args):
    single_turns = [s for s in chat if s.count("\n") == 1 and len(s) < 160]
    out = []
    for sample in chat:
        if rng.random() < args.header_share:
            out.append(under_header(rng, sample, names, known_names, args.name_turn_share))
    out += [templated(rng, names, single_turns) for _ in range(args.templated)]
    out += [name_turn(rng, NAMELESS_TURNS, names, "") for _ in range(args.nameless)]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--header-share", type=float, default=0.3, help="share of the chat corpus repeated under a header")
    ap.add_argument("--name-turn-share", type=float, default=0.35, help="share of those with a turn about the name")
    ap.add_argument("--templated", type=int, default=40000)
    ap.add_argument("--nameless", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    rng = random.Random(args.seed)

    def read(name):
        text = (CARDPUTER / "data" / name).read_text()
        return [s for s in text.split("\n\n") if s.strip()]

    train, val = read("chat_train.txt"), read("chat_val.txt")
    is_chat = lambda s: s.startswith("User: ")
    # Names the corpus uses for people; "I'm Sorry" and "I am Doctor" are not among them
    known_names = set(NAMES) | set(HELD_OUT_NAMES)

    added = build(rng, [s for s in train if is_chat(s)], NAMES, known_names, args)
    mixed = train + added
    rng.shuffle(mixed)

    args.templated //= 20
    args.nameless //= 20
    val_names = build(rng, [s for s in val if is_chat(s)], HELD_OUT_NAMES, known_names, args)

    DATA.mkdir(exist_ok=True)
    for name, samples in [("train.txt", mixed), ("val_chat.txt", val), ("val_names.txt", val_names)]:
        (DATA / name).write_text("\n\n".join(samples) + "\n")
        print(f"{name}: {len(samples):,} samples, {sum(map(len, samples)) / 1e6:.1f}M characters")
    print(f"of train.txt, {len(added):,} samples are about the name or under a header")


if __name__ == "__main__":
    main()
