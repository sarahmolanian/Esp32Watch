#pragma once
#include <Arduino.h>

// ============================================================
//  OTA firmware updates from GitHub Releases
//
//  Release assets the watch looks for (exact names):
//    version.txt  -> a text file containing only a whole number, e.g. 2
//    firmware.bin -> the compiled sketch (.ino.bin, NOT .merged.bin)
// ============================================================

// ---- CHANGE THIS NUMBER FOR EVERY RELEASE -----------------------
// The watch offers an update when version.txt is higher than this.
#define FW_VERSION 2
// -----------------------------------------------------------------

#define OTA_VERSION_URL  "https://github.com/sarahmolanian/Esp32Watch/releases/latest/download/version.txt"
#define OTA_FIRMWARE_URL "https://github.com/sarahmolanian/Esp32Watch/releases/latest/download/firmware.bin"

#define OTA_MIN_BATTERY    30      // % - refuse to flash below this
#define OTA_MIN_FREE_HEAP  60000   // bytes - TLS needs a lot of RAM
#define OTA_TASK_STACK     12288   // bytes - TLS handshake needs a big stack

enum OtaState {
    OTA_IDLE,          // nothing happening
    OTA_CHECKING,      // asking GitHub for the latest version number
    OTA_UP_TO_DATE,    // already on the newest version
    OTA_AVAILABLE,     // newer version found, waiting for the user
    OTA_DOWNLOADING,   // downloading + flashing
    OTA_SUCCESS,       // flashed, about to restart
    OTA_ERROR          // see ota_getError()
};

void        ota_reset();               // call when opening the Update screen
void        ota_startCheck();          // non-blocking
void        ota_startUpdate();         // non-blocking
void        ota_cancel();              // abort a running download
void        ota_update();              // call every UI tick (never blocks)

OtaState    ota_getState();
int         ota_getProgress();         // 0-100
int         ota_getLatestVersion();
const char* ota_getError();

// Call in setup() after clock_init(): restores steps + time after an OTA restart
void        ota_restoreStateAfterReboot();
