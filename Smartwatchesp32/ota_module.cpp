#include "ota_module.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include "battery.h"
#include "bluetooth_module.h"
#include "stepcounter.h"
#include "clock.h"
#include "esp_rom_sys.h"

// ============================================================
// State (written by the worker task, read by the UI task)
// ============================================================
static volatile OtaState s_state    = OTA_IDLE;
static volatile int      s_progress = 0;
static volatile bool     s_busy     = false;   // worker task is running
static volatile bool     s_cancel   = false;
static bool              s_download = false;   // false = version check, true = flash
static int               s_latest   = 0;
static char              s_error[24] = "";
static bool              s_btWasOn  = false;   // Bluetooth was on before the update
static uint32_t          s_rebootAt = 0;

// ============================================================
// Error helpers (text must fit on the 21-char OLED line)
// ============================================================
static void fail(const char* msg) {
    strncpy(s_error, msg, sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = '\0';
    s_state = OTA_ERROR;
}

static void failHttp(int code) {
    if (code < 0) snprintf(s_error, sizeof(s_error), "Network error %d", code);
    else          snprintf(s_error, sizeof(s_error), "HTTP error %d", code);
    s_state = OTA_ERROR;
}

static void failUpdate() {
    uint8_t e = Update.getError();
    if (e == UPDATE_ERROR_SPACE)            strcpy(s_error, "Firmware too big");
    else if (e == UPDATE_ERROR_MAGIC_BYTE)  strcpy(s_error, "Not a firmware file");
    else snprintf(s_error, sizeof(s_error), "Flash error %u", (unsigned)e);
    s_state = OTA_ERROR;
}

// ============================================================
// Keep steps + time across the OTA restart
// (RTC variables are not reliable across a software reset,
//  so this goes through flash/NVS instead)
// ============================================================
static void saveStateBeforeReboot() {
    Preferences p;
    p.begin("ota", false);
    ClockTime t = clock_getTime();
    p.putUInt("steps", sc_getSteps());
    p.putUChar("h", t.hour);
    p.putUChar("m", t.minute);
    p.putUChar("s", t.second);
    p.putBool("pending", true);
    p.end();
}

void ota_restoreStateAfterReboot() {
    Preferences p;
    p.begin("ota", false);
    if (p.getBool("pending", false)) {
        sc_setSteps(p.getUInt("steps", 0));
        clock_setTime(p.getUChar("h", 12), p.getUChar("m", 0), p.getUChar("s", 0));
        p.putBool("pending", false);
    }
    p.end();
}

// ============================================================
// Worker: version check
// ============================================================
static void runCheck() {
    if (ESP.getFreeHeap() < OTA_MIN_FREE_HEAP) {
        snprintf(s_error, sizeof(s_error), "Low RAM (%uK free)", (unsigned)(ESP.getFreeHeap() / 1024));
        s_state = OTA_ERROR;
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();                 // no certificate check (see notes)
    client.setTimeout(10);                // seconds

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // GitHub redirects
    http.setTimeout(10000);               // ms
    if (!http.begin(client, OTA_VERSION_URL)) { fail("Bad URL"); return; }

    int code = http.GET();
    if (code != HTTP_CODE_OK) { failHttp(code); http.end(); return; }

    String body = http.getString();
    http.end();

    int v = body.toInt();
    if (v <= 0) { fail("Bad version file"); return; }

    s_latest = v;
    s_state  = (v > FW_VERSION) ? OTA_AVAILABLE : OTA_UP_TO_DATE;
}

// ============================================================
// Worker: download + flash
// ============================================================

// Copies the HTTP body into the Update library. Returns true if everything arrived.
static bool pump(WiFiClient* stream, HTTPClient& http, size_t total) {
    const size_t CHUNK = 1460;
    uint8_t* buf = (uint8_t*)malloc(CHUNK);
    if (!buf) { fail("Out of memory"); return false; }

    size_t   written  = 0;
    uint32_t lastData = millis();
    bool     ok       = true;

    while (written < total) {
        if (s_cancel) { fail("Cancelled"); ok = false; break; }

        size_t avail = stream->available();
        if (avail == 0) {
            if (!http.connected())           { fail("Connection lost");  ok = false; break; }
            if (millis() - lastData > 15000) { fail("Download timeout"); ok = false; break; }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        int n = stream->read(buf, avail < CHUNK ? avail : CHUNK);
        if (n <= 0) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }

        if (Update.write(buf, (size_t)n) != (size_t)n) { failUpdate(); ok = false; break; }

        written   += n;
        s_progress = (int)(written * 100 / total);
        lastData   = millis();
        vTaskDelay(1);   // let the UI and idle task breathe
    }

    free(buf);
    return ok;
}

static void runDownload() {
    if (ESP.getFreeHeap() < OTA_MIN_FREE_HEAP) {
        snprintf(s_error, sizeof(s_error), "Low RAM (%uK free)", (unsigned)(ESP.getFreeHeap() / 1024));
        s_state = OTA_ERROR;
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(10000);
    if (!http.begin(client, OTA_FIRMWARE_URL)) { fail("Bad URL"); return; }

    int code = http.GET();
    if (code != HTTP_CODE_OK) { failHttp(code); http.end(); return; }

    int total = http.getSize();
    if (total <= 0) { fail("Unknown file size"); http.end(); return; }

    if (!Update.begin((size_t)total)) { failUpdate(); http.end(); return; }

    bool ok = pump(http.getStreamPtr(), http, (size_t)total);
    http.end();

    if (!ok)                  { Update.abort(); return; }   // error already set
    if (!Update.end(true))    { failUpdate();   return; }
    if (!Update.isFinished()) { fail("Update incomplete"); return; }

    s_progress = 100;
    s_state    = OTA_SUCCESS;
}

static void otaTask(void*) {
    if (s_download) runDownload();
    else            runCheck();
    s_busy = false;
    vTaskDelete(nullptr);   // all objects live inside runX(), so they are already destroyed
}

// ============================================================
// Public API (called from the UI task)
// ============================================================
static bool launch(bool download) {
    s_cancel   = false;
    s_progress = 0;
    s_download = download;
    s_state    = download ? OTA_DOWNLOADING : OTA_CHECKING;
    s_busy     = true;
    if (xTaskCreate(otaTask, "ota", OTA_TASK_STACK, nullptr, 1, nullptr) != pdPASS) {
        s_busy = false;
        fail("Out of memory");
        return false;
    }
    return true;
}

void ota_reset() {
    if (!s_busy && s_state != OTA_SUCCESS) s_state = OTA_IDLE;
}

void ota_startCheck() {
    if (s_busy) return;
    if (WiFi.status() != WL_CONNECTED) { fail("WiFi not connected"); return; }
    launch(false);
}

void ota_startUpdate() {
    if (s_busy) return;
    if (WiFi.status() != WL_CONNECTED) { fail("WiFi not connected"); return; }
    if ((int)battery_getPercentage() < OTA_MIN_BATTERY) { fail("Battery below 30%"); return; }

    // Bluetooth + WiFi + TLS at once is too much for the C3's RAM and radio
    if (bt_isEnabled()) { s_btWasOn = true; bt_disable(); }

    launch(true);
}

void ota_cancel() {
    if (s_state == OTA_DOWNLOADING) s_cancel = true;
}

void ota_update() {
    // Bring Bluetooth back after a failed or cancelled update
    if (s_btWasOn && !s_busy && (s_state == OTA_ERROR || s_state == OTA_IDLE)) {
        s_btWasOn = false;
        bt_enable();
    }

    // Restart into the new firmware after showing "Done!" for a moment
    if (s_state == OTA_SUCCESS) {
        if (s_rebootAt == 0) {
            s_rebootAt = millis() + 2500;
        } else if ((int32_t)(millis() - s_rebootAt) >= 0) {
            saveStateBeforeReboot();
            delay(100);
            esp_rom_software_reset_system();   // hard reset, skips the slow shutdown
            while (true) { }
        }
    }
}

OtaState    ota_getState()         { return s_state; }
int         ota_getProgress()      { return s_progress; }
int         ota_getLatestVersion() { return s_latest; }
const char* ota_getError()         { return s_error; }
