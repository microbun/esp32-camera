#pragma once

// GOOUUU ESP32-S3-CAM, taken from the supplied board IO diagram.
// OV2640 Y2..Y9 correspond to the driver's D0..D7, respectively.
#define CAM_PIN_PWDN  (-1)
#define CAM_PIN_RESET (-1)
#define CAM_PIN_SIOD  4
#define CAM_PIN_SIOC  5
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF  7
#define CAM_PIN_XCLK  15
#define CAM_PIN_PCLK  13
#define CAM_PIN_D0    11
#define CAM_PIN_D1    9
#define CAM_PIN_D2    8
#define CAM_PIN_D3    10
#define CAM_PIN_D4    12
#define CAM_PIN_D5    18
#define CAM_PIN_D6    17
#define CAM_PIN_D7    16

// Adjust these if the physical mounting requires a different orientation.
#define CAM_VFLIP     0
#define CAM_HMIRROR   0
