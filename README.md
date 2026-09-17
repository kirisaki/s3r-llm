# s3r-llm

A chat bot with a tiny LLM that runs entirely on the [M5Stack AtomS3R](https://docs.m5stack.com/en/core/AtomS3R) (ESP32-S3, 8MB flash, 8MB PSRAM).

Type a line into the USB serial port and the reply is streamed back to the
serial port and onto the 128x128 screen, at roughly 5 to 9 tokens per second.

The model is [TinyTalk 2](https://huggingface.co/TheREZOR/TinyTalk-2), an 8M
parameter GPT-Neo fine-tuned for small talk, quantized to 4 bits (5.7MB). It
knows about as much as a kindergartener and says "I don't know" a lot. It
remembers the conversation for up to 256 tokens; after that it keeps only
the last few turns, which takes it a few seconds. Pressing the screen or
typing `/new` starts a new conversation, and `/stats` shows token counts and
speed after each reply.

It can also be reached over WiFi. `/wifi <ssid> <password>` on the serial port
stores the credentials on the device (`/wifi off` forgets them, `/wifi` shows
the address), and then:

```sh
curl -N -d 'Hello!' http://s3r-llm-39a8.local/chat    # the reply is streamed back as plain text
curl -X POST http://s3r-llm-39a8.local/new            # starts a new conversation
```

Every device has a name, which is what it answers to over mDNS: `s3r-llm-` and
the end of its MAC address, until `/name <name>` gives it a better one. Name
and address are shown on the screen once the device is connected.

It is plain HTTP without any authentication, so keep it to networks you trust.

The accelerometer has a say as well:

- Shaking the device raises the sampling temperature, also in the middle of a
  reply. The harder the shake, the more the reply turns into word salad, and
  the redder it is drawn.
- Shaking it or turning it face down while it is idle tells the model so, as
  if the user had typed "I am shaking you!".

## Requirements

- M5Stack AtomS3R
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v6.1 or later

A [Dev Container](.devcontainer/) based on the official `espressif/idf` image is
included, so you can also just open this repository in VS Code and choose
"Reopen in Container".

## Build and flash

```sh
tools/fetch_model.sh                                  # once: download the model into models/
idf.py build
idf.py -p /dev/ttyACM0 tokenizer-flash model-flash    # once: write the model, takes about a minute
idf.py -p /dev/ttyACM0 flash monitor
```

Replace `/dev/ttyACM0` with the serial port of your device. The target and the
board-specific settings (flash size, octal PSRAM, USB Serial/JTAG console) come
from [`sdkconfig.defaults`](sdkconfig.defaults).

## License

[0BSD](LICENSE), except for:

- [`components/cardputer_llm`](components/cardputer_llm), the inference engine
  from [therezor/cardputer-ai](https://github.com/therezor/cardputer-ai), which
  is MIT licensed.
- [`components/bmi270_config`](components/bmi270_config), the configuration
  file of the BMI270 from Bosch Sensortec, which is BSD-3-Clause licensed.
- The model, which is not part of this repository. `tools/fetch_model.sh`
  downloads the pre-quantized weights from cardputer-ai; TinyTalk 2 is licensed
  [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).
