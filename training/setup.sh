#!/bin/sh
# Fetches what the training scripts build on: the cardputer-ai repository with
# the TinyTalk 2 checkpoint, its training corpus and the converter to the
# format the firmware reads. Everything lands in training/cardputer-ai, which
# is not part of this repository.
set -eu

COMMIT=6728fb73c1958aa1c7c8ff2d01ea1e4e15214456

cd "$(dirname "$0")"
if [ ! -d cardputer-ai ]; then
    git clone https://github.com/therezor/cardputer-ai.git
fi
git -C cardputer-ai fetch --quiet origin "$COMMIT" 2>/dev/null || true
git -C cardputer-ai checkout --quiet "$COMMIT"
echo "cardputer-ai is at $COMMIT"
