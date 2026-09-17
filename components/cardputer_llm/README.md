# cardputer_llm

Inference engine from [therezor/cardputer-ai](https://github.com/therezor/cardputer-ai)
at commit `6728fb73c1958aa1c7c8ff2d01ea1e4e15214456` (`main/llm.cpp`, `main/llm.h`,
`main/dot_q4_pie.S`). MIT licensed, see [LICENSE](LICENSE); the rest of this
repository is 0BSD.

Local changes, marked with `s3r-llm:` in the source:

- The KV cache is allocated from PSRAM when there is any, so that the context
  window can cover all 256 positions of the model instead of the 72 that fit
  in internal RAM.
- `Transformer::skip_classifier` skips the logits for prompt tokens, which
  makes reading the prompt about a third faster.
