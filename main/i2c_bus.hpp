#pragma once

#include "driver/i2c_master.h"

// The AtomS3R's internal I2C bus: backlight driver (LP5562) and IMU (BMI270).
namespace i2c_bus
{

// Sets the bus up on first use.
i2c_master_bus_handle_t get();

} // namespace i2c_bus
