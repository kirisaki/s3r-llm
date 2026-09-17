#pragma once

// Motion sensing with the AtomS3R's BMI270 accelerometer.
namespace imu
{

enum class Event
{
    None,
    Shake,
    FaceDown,
    FaceUp,
};

// Starts sampling in the background.
void init();
// How hard the device is being shaken, in g: the peak deviation from 1g,
// fading out over a few seconds. 0 at rest.
float shake_level();
// The latest event since the last call.
Event poll_event();

} // namespace imu
