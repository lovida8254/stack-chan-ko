#ifndef _CAMERA_VISION_H
#define _CAMERA_VISION_H

#include <Arduino.h>

// GPT-4o vision for the realtime mode. The Realtime API itself can't take images,
// so this captures a JPEG, sends it to /v1/chat/completions (gpt-4o-mini) as a
// separate REST call, then injects the short description back into the live
// conversation via pushUserText so Stack-chan speaks about what it saw.
//
// Only does anything in a build with -DENABLE_CAMERA. Always compiles (no-ops
// otherwise) so the web handler and sensor guards can call it unconditionally.
//
// ⚠️ The camera SCCB shares the CoreS3 internal I2C bus with the proximity / IMU /
// power / touch sensors. While a capture is in progress camera_is_busy() is true
// and those loop-tick sensors skip their I2C reads. Whether the camera and those
// sensors can coexist at all on this unit is UNVERIFIED — device test required.

void camera_vision_init();
bool camera_vision_look(const String& hint);   // capture + describe + inject; true if it spoke
bool camera_is_busy();                          // sensors skip internal-I2C while true

#endif  // _CAMERA_VISION_H
