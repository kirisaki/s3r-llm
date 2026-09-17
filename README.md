# s3r-llm

An LLM chat bot that runs on the [M5Stack AtomS3](https://docs.m5stack.com/en/core/AtomS3) series (ESP32-S3).

> **Status:** early work in progress. Nothing useful happens yet.

## Requirements

- M5Stack AtomS3 / AtomS3R
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) v6.1 or later

A [Dev Container](.devcontainer/) based on the official `espressif/idf` image is
included, so you can also just open this repository in VS Code and choose
"Reopen in Container".

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Replace `/dev/ttyACM0` with the serial port of your device.

## License

[0BSD](LICENSE)
