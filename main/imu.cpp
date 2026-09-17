#include "imu.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmi270_config.h"
#include "i2c_bus.hpp"

namespace imu
{

namespace
{

constexpr char TAG[] = "imu";

constexpr uint8_t REG_CHIP_ID = 0x00;
constexpr uint8_t REG_ACC_X_LSB = 0x0C;
constexpr uint8_t REG_INTERNAL_STATUS = 0x21;
constexpr uint8_t REG_ACC_CONF = 0x40;
constexpr uint8_t REG_ACC_RANGE = 0x41;
constexpr uint8_t REG_INIT_CTRL = 0x59;
constexpr uint8_t REG_INIT_ADDR_0 = 0x5B;
constexpr uint8_t REG_INIT_DATA = 0x5E;
constexpr uint8_t REG_PWR_CONF = 0x7C;
constexpr uint8_t REG_PWR_CTRL = 0x7D;
constexpr uint8_t REG_CMD = 0x7E;
constexpr uint8_t CHIP_ID = 0x24;

// +-8g over the int16 range
constexpr float G_PER_LSB = 8.0f / 32768.0f;

constexpr int SAMPLE_MS = 20;
// Per sample; halves the shake level in about two seconds
constexpr float LEVEL_DECAY = 0.993f;
// A shake is this many samples over the threshold within the window
constexpr float SHAKE_THRESHOLD_G = 0.8f;
constexpr int SHAKE_HITS = 4;
constexpr int SHAKE_WINDOW = 25;
constexpr int SHAKE_REARM_MS = 5000;
// Gravity along the screen normal, low-passed, with hysteresis
constexpr float GRAVITY_SMOOTHING = 0.1f;
constexpr float FLIP_THRESHOLD_G = 0.7f;

i2c_master_dev_handle_t dev = nullptr;
std::atomic<float> level{0.0f};
std::atomic<Event> pending{Event::None};

void write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t buf[] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(dev, buf, sizeof(buf), 100));
}

void read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev, &reg, 1, data, len, 100));
}

uint8_t read_reg(uint8_t reg)
{
    uint8_t value;
    read_regs(reg, &value, 1);
    return value;
}

// The sensor does nothing until it has been fed its configuration file.
void upload_config()
{
    constexpr size_t CHUNK = 256;
    uint8_t buf[1 + CHUNK];
    buf[0] = REG_INIT_DATA;

    write_reg(REG_INIT_CTRL, 0x00);
    for (size_t offset = 0; offset < bmi270_config_file_size; offset += CHUNK) {
        // The write address counts in words, split over a 4 and an 8 bit register
        const uint8_t addr[] = {REG_INIT_ADDR_0, uint8_t((offset / 2) & 0x0F), uint8_t((offset / 2) >> 4)};
        ESP_ERROR_CHECK(i2c_master_transmit(dev, addr, sizeof(addr), 100));

        const size_t len = std::min(CHUNK, bmi270_config_file_size - offset);
        memcpy(buf + 1, bmi270_config_file + offset, len);
        ESP_ERROR_CHECK(i2c_master_transmit(dev, buf, 1 + len, 100));
    }
    write_reg(REG_INIT_CTRL, 0x01);
}

bool init_sensor()
{
    i2c_master_bus_handle_t bus = i2c_bus::get();
    uint16_t address = 0;
    for (const uint16_t candidate : {uint16_t(0x68), uint16_t(0x69)}) {
        if (i2c_master_probe(bus, candidate, 50) == ESP_OK) {
            address = candidate;
            break;
        }
    }
    if (!address) {
        return false;
    }

    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 400 * 1000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &config, &dev));

    if (read_reg(REG_CHIP_ID) != CHIP_ID) {
        return false;
    }

    write_reg(REG_CMD, 0xB6); // soft reset
    vTaskDelay(pdMS_TO_TICKS(10));
    write_reg(REG_PWR_CONF, 0x00); // no power saving, or the upload fails
    vTaskDelay(pdMS_TO_TICKS(1));
    upload_config();

    bool ready = false;
    for (int retry = 0; retry < 20 && !ready; retry++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        ready = (read_reg(REG_INTERNAL_STATUS) & 0x0F) == 0x01;
    }
    if (!ready) {
        return false;
    }

    write_reg(REG_ACC_CONF, 0xA8);  // 100Hz, normal filter
    write_reg(REG_ACC_RANGE, 0x02); // +-8g
    write_reg(REG_PWR_CTRL, 0x04);  // accelerometer only
    return true;
}

void sample_task(void *)
{
    int hits[SHAKE_WINDOW] = {};
    int hit_index = 0;
    TickType_t last_shake = 0;
    float gravity_z = 0.0f;
    bool face_down = false;

    // The first samples after power-up are all zero
    vTaskDelay(pdMS_TO_TICKS(100));

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_MS));

        uint8_t raw[6];
        read_regs(REG_ACC_X_LSB, raw, sizeof(raw));
        float a[3];
        for (int i = 0; i < 3; i++) {
            a[i] = int16_t(raw[2 * i] | raw[2 * i + 1] << 8) * G_PER_LSB;
        }

        const float deviation = fabsf(sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) - 1.0f);
        level = std::max(level.load() * LEVEL_DECAY, deviation);

        hits[hit_index] = deviation > SHAKE_THRESHOLD_G;
        hit_index = (hit_index + 1) % SHAKE_WINDOW;
        int hit_count = 0;
        for (const int hit : hits) {
            hit_count += hit;
        }
        const TickType_t now = xTaskGetTickCount();
        if (hit_count >= SHAKE_HITS && now - last_shake > pdMS_TO_TICKS(SHAKE_REARM_MS)) {
            last_shake = now;
            pending = Event::Shake;
        }

        // The screen faces +z
        gravity_z += GRAVITY_SMOOTHING * (a[2] - gravity_z);
        if (!face_down && gravity_z < -FLIP_THRESHOLD_G) {
            face_down = true;
            pending = Event::FaceDown;
        } else if (face_down && gravity_z > FLIP_THRESHOLD_G) {
            face_down = false;
            pending = Event::FaceUp;
        }
    }
}

} // namespace

void init()
{
    if (!init_sensor()) {
        ESP_LOGE(TAG, "no BMI270, carrying on without motion sensing");
        return;
    }
    xTaskCreate(sample_task, "imu", 4096, nullptr, 5, nullptr);
}

float shake_level()
{
    return level;
}

Event poll_event()
{
    return pending.exchange(Event::None);
}

} // namespace imu
