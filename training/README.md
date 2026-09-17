# Training

Fine-tunes the model so that it goes by the name of its device.

Out of the box, [TinyTalk 2](https://huggingface.co/TheREZOR/TinyTalk-2) makes
up a new name for itself whenever it is asked, and it is too small to pick one
up from the conversation. The firmware therefore starts a conversation with a
header if the device has been given a name with `/name`:

```
Name: Alice
User: What is your name?
Bot: My name is Alice.
```

and the model is taught what to do with it. Without a header it says that it
has no name yet.

## Running it

Needs PyTorch with a GPU and `transformers`. On an 8GB RTX 2070 SUPER one epoch
takes ten minutes; the default is two.

```sh
training/setup.sh                   # once: fetch cardputer-ai with checkpoint, corpus and converter
python3 training/make_data.py       # the corpus, into training/data/
python3 training/finetune.py        # the model, into training/out/model/
python3 training/evaluate.py        # asks it for its name a few hundred times

python3 training/cardputer-ai/tools/convert_tinystories_instruct.py \
    --model-dir training/out/model --corpus training/data/train.txt \
    --min-count 30 --keep-bin --no-cpp --out-dir training/out/convert

idf.py -DS3R_MODEL_DIR=training/out/convert/embed build
ESPPORT=/dev/ttyACM0 idf.py tokenizer-flash model-flash
```

`--min-count` decides how much of the GPT-2 vocabulary survives into the
tokenizer of the device: tokens that occur less often in the corpus are
dropped. 30 leaves about 10,000 tokens and a model of 5.4MB; it has to stay
below the 5.5MB of the `model` partition.

## How it works

- `make_data.py` keeps the chat corpus of cardputer-ai as it is, so that
  nothing is forgotten, and adds a share of its dialogues again under a header,
  dialogues from templates that are all about the name, and the same without a
  header. Half of the names are made up of random syllables: with a list of a
  few hundred names the model just learns the list, and gives some other name
  from it when the header has one it does not know.
- `finetune.py` follows cardputer-ai's `finetune_chat.py`: samples packed into
  blocks of 256 tokens, loss only on what the bot says. It starts from TinyTalk
  2 rather than from its base model. Batches are split up because the logits
  over 50,257 tokens are what fills the GPU.
- `common.py` tokenizes a sample in exactly the pieces the firmware feeds the
  model (`main/llm.cpp`). Where the two disagree, the model meets token
  sequences on the device that it has never seen in training.
- `evaluate.py` samples replies the way the firmware does and counts how often
  the name comes out right, for names from training, names it has never seen,
  made-up names and default device names, also after some small talk.

## License

The scripts are 0BSD like the rest of the repository; `common.py` and
`finetune.py` follow cardputer-ai's MIT licensed `tools/finetune_chat.py`
closely. What comes out is a derivative of TinyTalk 2 and its training data,
and with that CC BY-NC-SA 4.0. Neither data nor models are part of this
repository.
