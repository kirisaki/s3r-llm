# s3r-llm

A chat bot with a tiny LLM that runs entirely on the
[M5Stack AtomS3R](https://docs.m5stack.com/en/core/AtomS3R): an ESP32-S3 with
8MB of flash, 8MB of PSRAM and a 128x128 screen. No cloud, no host computer
doing the thinking.

- Type a line into the USB serial port, or send it over WiFi, and the reply is
  streamed back and onto the screen at 5 to 9 tokens per second.
- Shake the device and the reply turns into word salad.
- Put two of them on the same network and they talk to each other.

The model is [TinyTalk 2](https://huggingface.co/TheREZOR/TinyTalk-2), an 8M
parameter GPT-Neo fine-tuned for small talk and quantized to 4 bits (5.7MB). It
knows about as much as a kindergartener and says "I don't know" a lot.

## Getting started

You need an AtomS3R and
[ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
v6.1 or later. A [Dev Container](.devcontainer/) based on the official
`espressif/idf` image is included, so you can also open this repository in
VS Code and choose "Reopen in Container".

```sh
tools/fetch_model.sh                                       # once: download the model into models/
idf.py build
ESPPORT=/dev/ttyACM0 idf.py tokenizer-flash model-flash    # once: write the model, takes about a minute
idf.py -p /dev/ttyACM0 flash monitor
```

Replace `/dev/ttyACM0` with the serial port of your device. The two targets
that write the model take the port from `ESPPORT`, not from `-p`; without it
they pick a port themselves, which matters once there is more than one device.

Once the `> ` prompt shows up, say hello. Any serial terminal will do in place
of `idf.py monitor`; the baud rate does not matter.

The target and the board-specific settings (flash size, octal PSRAM, USB
Serial/JTAG console) come from [`sdkconfig.defaults`](sdkconfig.defaults).
`idf.py menuconfig` has one setting of its own under "s3r-llm": the rotation of
the display, for a device that stands on its side.

Only AtomS3R units with an ST7735S display are supported. Some batches have a
GC9107 instead, which needs a different init sequence that is not in here.

## Using it

### On the serial port

Whatever you type is said to the model, except for lines that start with a
slash:

| Command | |
|---|---|
| `/new` | Starts a new conversation. So does pressing the screen, also in the middle of a reply. |
| `/stats` | Shows token counts and speed after each reply, or stops doing so. |
| `/wifi <ssid> <password>` | Connects to a 2.4GHz network and remembers it. An SSID with spaces in it goes into double quotes. |
| `/wifi` | Shows the address of the device, or why it is not connected. |
| `/wifi off` | Forgets the network. |
| `/name <name>` | Names the device: lowercase letters, digits and hyphens. `/name` shows the name. With a model [trained for it](training/), it also goes by that name in conversation. |
| `/talk <device> [opening line]` | Starts a talk with another device, see below. |
| `/stop` | Ends a talk. |

Input is ASCII only, up to 255 characters per line. The model remembers the
conversation for up to 256 tokens; after that it keeps only the last few turns,
which takes it a few seconds.

### By moving it

- Shaking the device raises the sampling temperature, also in the middle of a
  reply. The harder the shake, the more the reply turns into word salad, and
  the redder it is drawn.
- Shaking it or turning it face down while it is idle tells the model so, as
  if you had typed "I am shaking you!".

### Over WiFi

Once `/wifi` has been set up, the device serves a plain text HTTP API. Its name
and address are shown on the screen when it connects. The name is what it
answers to over mDNS: `s3r-llm-` and the end of its MAC address, until `/name`
gives it a better one.

| Request | |
|---|---|
| `POST /chat` | The body is the message. The reply is streamed back, and shown on the screen and the serial port like any other. One at a time; `503` while the device is busy with another request. |
| `POST /new` | Starts a new conversation, cutting short a reply that is under way. |
| `POST /talk` | The body is `<device> [opening line]`. Starts a talk with another device. |
| `POST /stop` | Cuts short whatever is being said, and ends a talk. |
| `GET /` | A short reminder of the above. |

```sh
curl -N -d 'Hello!' http://alice.local/chat
curl -X POST http://alice.local/new
```

It is plain HTTP without any authentication, so keep it to networks you trust.
The WiFi password is stored on the device in plain text, and echoed on the
serial port as you type it.

### With another device

Two devices can talk to each other with nothing in between:

```
/talk bob Do you like dogs?
```

on the serial port of `alice` (or `POST /talk` with `bob Do you like dogs?` as
the body) makes her say the opening line to `bob` and keep answering whatever
comes back, for up to 20 turns. The other device is given by name or address.
Each screen shows the conversation from its own side.

`/stop`, `POST /stop`, the button, or anything else said to either device ends
the talk. When the two go in circles, the one that started the talk thinks a
little less straight for a turn. `bob` needs to know nothing about all this: he
just gets API requests.

## How it works

- **Inference** is the engine of
  [cardputer-ai](https://github.com/therezor/cardputer-ai): Q4_0 weights against
  int8 activations, with a hand-written kernel for the vector instructions of
  the ESP32-S3, split over both cores. See
  [`components/cardputer_llm`](components/cardputer_llm) for the two changes
  made to it.
- **The weights** live in a flash partition of their own, so that flashing the
  app stays quick, and are copied to PSRAM at boot. Every token reads all of
  them, and PSRAM is the faster place to read them from: 8.8 tokens per second
  instead of 4.5 straight from flash. Replies slow down towards 5 as the
  context fills up.
- **The conversation** stays in the KV cache, also in PSRAM, so that each turn
  only reads what is new. The model has 256 learned positions; when they run
  out, the last few turns are read again from position 0.
- **The screen** is driven directly over `esp_lcd`'s SPI panel IO: a 32KB
  RGB565 framebuffer, a 5x7 ASCII font with 21 columns and 14 lines, and a chat
  log that wraps words as pieces of text arrive and scrolls by pixels.
- **Settings** that differ per device (network, name) are kept in NVS and set
  over the serial port, not compiled in.

`tools/demo.py` plays a short scripted conversation over the serial port.
[`training/`](training/) fine-tunes the model, so far to make it go by the name
of its device.

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

The [mDNS component](https://components.espressif.com/components/espressif/mdns)
is fetched by the ESP-IDF component manager at build time.
