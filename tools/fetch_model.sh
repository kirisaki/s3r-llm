#!/bin/sh
# Downloads the TinyTalk 2 (8M) weights and tokenizer, pre-quantized to Q4_0
# for the ESP32-S3, from therezor/cardputer-ai into models/.
#
# The model is licensed CC BY-NC-SA 4.0 (https://huggingface.co/TheREZOR/TinyTalk-2)
# and is deliberately not part of this repository.
set -eu

COMMIT=6728fb73c1958aa1c7c8ff2d01ea1e4e15214456
BASE=https://raw.githubusercontent.com/therezor/cardputer-ai/$COMMIT/embed

cd "$(dirname "$0")/.."
mkdir -p models

fetch() {
    name=$1
    sha256=$2
    if [ ! -f "models/$name" ]; then
        curl -fL --progress-bar -o "models/$name.part" "$BASE/$name"
        mv "models/$name.part" "models/$name"
    fi
    echo "$sha256  models/$name" | sha256sum -c -
}

fetch model_neo_q4.bin 183fe4c212fd0fd17a8e048ee3eab4a0095a039df290f0c58cafff70ca17fe2a
fetch tok_neo.bin 7b915b9779c12fa896ad839a81ce315c757419e71a4dee7f7890d2578051429f
