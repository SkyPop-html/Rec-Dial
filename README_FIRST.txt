ESP32-S3 N8R2 Voice Recorder + JMD 1.3A OLED
=============================================

This is the integrated project folder. Existing recorder source files were preserved from the source files previously supplied in the conversation, and oled_ui.c/.h plus app_main.c and CMake integration were added.

OLED wiring (proposed):
VCC -> 3V3
GND -> GND
SDA -> GPIO8
SCL -> GPIO9

OLED assumptions:
- 128x64 I2C OLED, SH1106-compatible controller
- 7-bit I2C address 0x3C (try 0x3D only if the address pads/configuration require it)
The photo confirms PCB label JMD 1.3A but does not conclusively prove the controller, so this driver may need adjustment if the screen stays blank.

Build:
idf.py set-target esp32s3
idf.py build
idf.py -p YOUR_PORT flash monitor

Important:
- Unplug USB before wiring.
- OLED failure is non-fatal.
- The SD-card initialization problem seen in previous logs is not fixed by adding OLED support. The firmware will show an error, but card/wiring/pin conflict still needs troubleshooting.
- This source package has not been compiled against your exact installed ESP-IDF version or tested on the physical hardware.
