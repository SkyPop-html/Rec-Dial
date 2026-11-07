ESP32-S3 VOICE RECORDER - INTERNAL FLASH BUILD
ESP-IDF: v5.5.6 | Target: ESP32-S3 N8R2

WHAT CHANGED
- Removed MicroSD/SPI storage code and all SD pin definitions.
- WAV recordings are stored in a FAT filesystem partition on the module's onboard 8 MB flash.
- The filesystem uses ESP-IDF wear levelling.
- INMP441, OLED, record button, and LED code are retained from the supplied project.

IMPORTANT STORAGE LIMIT
This project allocates a 3 MiB factory application partition and a 0x4E0000-byte
(about 4.9 MB raw) FAT storage partition. Actual usable recording space is lower
after filesystem overhead. At 16 kHz, 16-bit, mono PCM, audio uses 32,000 bytes/sec,
so expect roughly a couple of minutes, not hours. Stop recording before power is removed.
Do not treat internal flash as an SD-card replacement for long recordings; repeated
recording also consumes flash write endurance.

WIRING
- Remove the SD card module and microSD card entirely. No SD pins are used.
- Keep INMP441: SCK GPIO4, WS GPIO5, SD GPIO6, VDD 3V3, GND GND, L/R GND.
- Keep OLED: SDA GPIO8, SCL GPIO9, VCC 3V3, GND GND.
- Keep record button GPIO1 to GND. LED GPIO21 as already wired.

INSTALL / BUILD
1. Extract this project into a NEW folder. Do not merge it over the old project.
2. Open the new folder in VS Code using ESP-IDF v5.5.6.
3. Ensure the target is ESP32-S3. In ESP-IDF terminal run:
     idf.py set-target esp32s3
     idf.py fullclean
     idf.py build
4. Find the actual COM port in Device Manager, then flash (replace COMx):
     idf.py -p COMx flash
     idf.py -p COMx monitor

FIRST BOOT / DATA LOSS WARNING
The custom partition table changes flash layout. Flashing this build with erase-flash
will erase existing firmware, settings, and all data in the chip's flash. This is
expected for the first installation of this layout. Recordings are stored in the
internal flash and will be lost if the chip is erased or the partition is reformatted.
The mount config formats the storage partition if it cannot be mounted.

EXPECTED LOGS
- Mounting internal flash filesystem at /flash...
- Internal flash filesystem mounted...
- Internal flash filesystem read/write test passed.
- Press the record button (GPIO1) to start; press again to stop.

The source is adapted from the project files previously supplied. It has not been
compiled or hardware-tested in this environment.
