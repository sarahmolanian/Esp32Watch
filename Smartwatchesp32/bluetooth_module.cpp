/*  bluetooth_module.cpp
 *
 *  Cross-platform BLE notification bridge for ESP32-C3.
 *  Compatible with NimBLE-Arduino 2.x
 *
 *  iOS    → ANCS  (bonds permanently, stays in My Devices)
 *  Android → Custom UART GATT service, works with Gadgetbridge
 *
 *  Fix history:
 *  - NVS init              → bond key survives watch restart
 *  - Public address type   → ESP32 keeps a stable address iOS can track
 *  - IRK key distribution  → watch resolves iOS's rotating RPA address
 *  - setOwnAddrType PUBLIC → iOS recognises the same device after reconnect
 *  - No delay() in BLE callbacks → no watchdog crashes
 *  - Re-advertise via flag from bt_update() (main task, not BLE task)
 */

#include "bluetooth_module.h"
#include <NimBLEDevice.h>
#include <nvs_flash.h>

// ============================================================
// UUIDs
// ============================================================

#define ANCS_SERVICE_UUID       "7905F431-B5CE-4E99-A40F-4B1E122D00D0"
#define ANCS_NOTIF_SOURCE_UUID  "9FBF120D-6301-42D9-8C58-25E699A21DBD"
#define ANCS_CONTROL_POINT_UUID "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9"
#define ANCS_DATA_SOURCE_UUID   "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB"

#define ANDROID_SERVICE_UUID    "6E400001-B5CE-4E99-A40F-4B1E122D00D0"
#define ANDROID_RX_UUID         "6E400002-B5CE-4E99-A40F-4B1E122D00D0"
#define ANDROID_TX_UUID         "6E400003-B5CE-4E99-A40F-4B1E122D00D0"

// HID stub — UUID 0x1812 makes iOS show device in Settings → Bluetooth
// Full set of mandatory characteristics required for iOS to save bond
#define HID_SERVICE_UUID        "1812"
#define HID_INFO_UUID           "2A4A"
#define HID_CONTROL_UUID        "2A4C"
#define HID_PROTOCOL_MODE_UUID  "2A4E"
#define HID_REPORT_MAP_UUID     "2A4B"
#define HID_REPORT_UUID         "2A4D"

// ============================================================
// Notification ring buffer
// ============================================================

static BTNotification s_notifs[BT_NOTIF_MAX];
static int            s_notifCount   = 0;
static bool           s_newNotifFlag = false;

static void pushNotification(const BTNotification& n) {
    if (s_notifCount < BT_NOTIF_MAX) s_notifCount++;
    for (int i = s_notifCount - 1; i > 0; i--)
        s_notifs[i] = s_notifs[i - 1];
    s_notifs[0]    = n;
    s_newNotifFlag = true;
}

// ============================================================
// BLE state
// ============================================================

static NimBLEServer*         s_server      = nullptr;
static NimBLECharacteristic* s_ancsControl = nullptr;
static bool                  s_enabled         = false;
static bool                  s_connected       = false;
static volatile bool         s_needReadvertise = false;

enum AncsState { ANCS_IDLE, ANCS_WAITING_ATTRS };
static AncsState s_ancsState = ANCS_IDLE;

// ============================================================
// Helpers
// ============================================================

static void safeCopy(char* dst, const char* src, size_t dstSize) {
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static bool jsonGetString(const char* json, const char* key,
                           char* out, size_t outSize) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i < outSize - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return true;
}

// ============================================================
// ANCS Notification Source parser
// [EventID, EventFlags, CategoryID, CategoryCount, UID x4]
// ============================================================

static void handleAncsNotifSource(const uint8_t* data, size_t len) {
    if (len < 8) return;

    uint8_t  eventID    = data[0];
    uint8_t  categoryID = data[2];
    uint32_t uid        = (uint32_t)data[4]
                        | ((uint32_t)data[5] << 8)
                        | ((uint32_t)data[6] << 16)
                        | ((uint32_t)data[7] << 24);

    if (eventID == 2) {
        for (int i = 0; i < s_notifCount; i++) {
            if (s_notifs[i].uid == uid) { s_notifs[i].unread = false; break; }
        }
        return;
    }
    if (eventID != 0 && eventID != 1) return;

    BTNotification n{};
    n.uid    = uid;
    n.unread = true;
    switch (categoryID) {
        case 1:  safeCopy(n.app, "Incoming Call",  BT_NOTIF_APP_LEN); break;
        case 2:  safeCopy(n.app, "Missed Call",    BT_NOTIF_APP_LEN); break;
        case 3:  safeCopy(n.app, "Voicemail",      BT_NOTIF_APP_LEN); break;
        case 4:  safeCopy(n.app, "Social",         BT_NOTIF_APP_LEN); break;
        case 5:  safeCopy(n.app, "Schedule",       BT_NOTIF_APP_LEN); break;
        case 6:  safeCopy(n.app, "Email",          BT_NOTIF_APP_LEN); break;
        case 7:  safeCopy(n.app, "News",           BT_NOTIF_APP_LEN); break;
        case 8:  safeCopy(n.app, "Health",         BT_NOTIF_APP_LEN); break;
        case 9:  safeCopy(n.app, "Business",       BT_NOTIF_APP_LEN); break;
        case 10: safeCopy(n.app, "Location",       BT_NOTIF_APP_LEN); break;
        case 11: safeCopy(n.app, "Entertainment",  BT_NOTIF_APP_LEN); break;
        default: safeCopy(n.app, "Notification",   BT_NOTIF_APP_LEN); break;
    }
    safeCopy(n.title, n.app,         BT_NOTIF_TITLE_LEN);
    safeCopy(n.body,  "Tap to view", BT_NOTIF_BODY_LEN);
    pushNotification(n);

    if (s_ancsControl && s_connected) {
        uint8_t cmd[12];
        cmd[0]  = 0x00;
        cmd[1]  =  uid        & 0xFF;
        cmd[2]  = (uid >>  8) & 0xFF;
        cmd[3]  = (uid >> 16) & 0xFF;
        cmd[4]  = (uid >> 24) & 0xFF;
        cmd[5]  = 0x00;
        cmd[6]  = 0x01; cmd[7]  = 48; cmd[8]  = 0;
        cmd[9]  = 0x03; cmd[10] = 80; cmd[11] = 0;
        s_ancsControl->setValue(cmd, 12);
        s_ancsControl->notify();
        s_ancsState = ANCS_WAITING_ATTRS;
    }
}

// ============================================================
// ANCS Data Source parser
// [CmdID, UID x4, AttrID, AttrLen x2, AttrData...]
// ============================================================

static void handleAncsDataSource(const uint8_t* data, size_t len) {
    if (len < 6 || data[0] != 0x00) return;

    uint32_t uid = (uint32_t)data[1]
                 | ((uint32_t)data[2] << 8)
                 | ((uint32_t)data[3] << 16)
                 | ((uint32_t)data[4] << 24);

    BTNotification* target = nullptr;
    for (int i = 0; i < s_notifCount; i++) {
        if (s_notifs[i].uid == uid) { target = &s_notifs[i]; break; }
    }
    if (!target) return;

    size_t pos = 5;
    while (pos + 2 < len) {
        uint8_t  attrID  = data[pos++];
        uint16_t attrLen = (uint16_t)data[pos] | ((uint16_t)data[pos+1] << 8);
        pos += 2;
        if (pos + attrLen > len) break;
        char tmp[81] = {};
        size_t cpLen = attrLen < 80 ? attrLen : 80;
        memcpy(tmp, data + pos, cpLen);
        if (strlen(tmp) > 0) {
            switch (attrID) {
                case 0x00: safeCopy(target->app,   tmp, BT_NOTIF_APP_LEN);   break;
                case 0x01: safeCopy(target->title, tmp, BT_NOTIF_TITLE_LEN); break;
                case 0x03: safeCopy(target->body,  tmp, BT_NOTIF_BODY_LEN);  break;
            }
        }
        pos += attrLen;
    }
    s_ancsState = ANCS_IDLE;
}

// ============================================================
// Android RX  {"app":"X","title":"Y","body":"Z"}
// ============================================================

static void handleAndroidMessage(const std::string& raw) {
    BTNotification n{};
    n.uid    = millis();
    n.unread = true;
    if (!jsonGetString(raw.c_str(), "app",   n.app,   BT_NOTIF_APP_LEN))
        safeCopy(n.app, "Android", BT_NOTIF_APP_LEN);
    if (!jsonGetString(raw.c_str(), "title", n.title, BT_NOTIF_TITLE_LEN))
        safeCopy(n.title, n.app, BT_NOTIF_TITLE_LEN);
    if (!jsonGetString(raw.c_str(), "body",  n.body,  BT_NOTIF_BODY_LEN))
        safeCopy(n.body, "New notification", BT_NOTIF_BODY_LEN);
    pushNotification(n);
}

// ============================================================
// NimBLE 2.x callbacks
// NEVER call delay() here — runs on BLE FreeRTOS task
// ============================================================

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* srv, NimBLEConnInfo& connInfo) override {
        s_connected       = true;
        s_needReadvertise = false;
        Serial.println("[BT] Phone connected");
        srv->setDataLen(connInfo.getConnHandle(), 185);
    }

    void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& connInfo, int reason) override {
        s_connected       = false;
        s_ancsState       = ANCS_IDLE;
        s_needReadvertise = true;
        Serial.printf("[BT] Disconnected reason=0x%02x\n", reason);
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        if (!connInfo.isEncrypted()) {
            Serial.println("[BT] Auth failed - disconnecting");
            NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
        } else {
            Serial.println("[BT] Bonded and encrypted OK");
        }
    }
};

class AncsNotifSrcCB : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
        auto val = chr->getValue();
        handleAncsNotifSource(
            reinterpret_cast<const uint8_t*>(val.data()), val.length());
    }
};

class AncsDataSrcCB : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
        auto val = chr->getValue();
        handleAncsDataSource(
            reinterpret_cast<const uint8_t*>(val.data()), val.length());
    }
};

class AndroidRxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
        handleAndroidMessage(chr->getValue());
    }
};

static ServerCallbacks  s_serverCB;
static AncsNotifSrcCB   s_ancsNotifSrcCB;
static AncsDataSrcCB    s_ancsDataSrcCB;
static AndroidRxCB      s_androidRxCB;

// ============================================================
// bt_init
// ============================================================

void bt_init() {
    // NVS must be ready before NimBLE so bond keys are saved to flash
    static bool initialised = false;
    if (initialised) {
        Serial.println("[BT] bt_init called twice — skipping");
        return;
    }
    initialised = true;
    
    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    NimBLEDevice::init("ESP32Watch");
    NimBLEDevice::setPower(3);

    // ── ADDRESS TYPE ────────────────────────────────────────────
    // Use the chip's fixed PUBLIC MAC address.
    // With a random/RPA address the ESP32 changes address on each
    // boot, so iOS sees a brand-new device every time and refuses
    // to reuse its saved bond keys → device disappears from My Devices.
    // A stable public address fixes this.
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);

    // ── SECURITY ────────────────────────────────────────────────
    // Just Works bonding — silent, no passkey prompt.
    // KEY DISTRIBUTION: must include BLE_SM_PAIR_KEY_DIST_ID (IRK)
    // on BOTH sides so the watch can resolve iOS's rotating RPA
    // address and iOS can resolve the watch address.
    // Without IRK exchange, iOS forgets the bond as soon as its
    // Bluetooth address rotates (every ~15 minutes).
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND |
                                  BLE_SM_PAIR_AUTHREQ_SC);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    // Distribute both LTK (encryption key) and IRK (identity key)
    // in both directions — this is the critical missing piece
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC |
                                     BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC |
                                     BLE_SM_PAIR_KEY_DIST_ID);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_serverCB);

    // ----------------------------------------------------------
    // HID stub service — full mandatory characteristic set
    // iOS validates ALL 5 are present before saving the bond
    // ----------------------------------------------------------
    NimBLEService* hidService = s_server->createService(HID_SERVICE_UUID);

    // 1. HID Information: bcdHID=1.11, country=0, flags=0x02
    NimBLECharacteristic* hidInfo =
        hidService->createCharacteristic(HID_INFO_UUID, NIMBLE_PROPERTY::READ);
    uint8_t hidInfoVal[4] = {0x11, 0x01, 0x00, 0x02};
    hidInfo->setValue(hidInfoVal, 4);

    // 2. HID Control Point
    hidService->createCharacteristic(HID_CONTROL_UUID, NIMBLE_PROPERTY::WRITE_NR);

    // 3. Protocol Mode — 0x01 = Report Protocol (required by iOS)
    NimBLECharacteristic* hidProtocol =
        hidService->createCharacteristic(HID_PROTOCOL_MODE_UUID,
                                         NIMBLE_PROPERTY::READ |
                                         NIMBLE_PROPERTY::WRITE_NR);
    uint8_t protocolMode = 0x01;
    hidProtocol->setValue(&protocolMode, 1);

    // 4. Report Map — minimal vendor descriptor
    NimBLECharacteristic* hidMap =
        hidService->createCharacteristic(HID_REPORT_MAP_UUID, NIMBLE_PROPERTY::READ);
    static const uint8_t reportMap[] = {
        0x06, 0x00, 0xFF,
        0x09, 0x01,
        0xA1, 0x01,
        0x09, 0x02,
        0x15, 0x00,
        0x26, 0xFF, 0x00,
        0x75, 0x08,
        0x95, 0x01,
        0x81, 0x02,
        0xC0
    };
    hidMap->setValue(reportMap, sizeof(reportMap));

    // 5. Input Report — no encryption requirement
    NimBLECharacteristic* hidReport =
        hidService->createCharacteristic(HID_REPORT_UUID,
                                         NIMBLE_PROPERTY::READ |
                                         NIMBLE_PROPERTY::NOTIFY);
    uint8_t emptyReport = 0x00;
    hidReport->setValue(&emptyReport, 1);

    hidService->start();

    // ----------------------------------------------------------
    // ANCS service
    // ----------------------------------------------------------
    NimBLEService* ancsService = s_server->createService(ANCS_SERVICE_UUID);

    NimBLECharacteristic* ancsNotifSrc =
        ancsService->createCharacteristic(ANCS_NOTIF_SOURCE_UUID,
                                          NIMBLE_PROPERTY::NOTIFY);
    ancsNotifSrc->setCallbacks(&s_ancsNotifSrcCB);

    s_ancsControl =
        ancsService->createCharacteristic(ANCS_CONTROL_POINT_UUID,
                                          NIMBLE_PROPERTY::WRITE);

    NimBLECharacteristic* ancsDataSrc =
        ancsService->createCharacteristic(ANCS_DATA_SOURCE_UUID,
                                          NIMBLE_PROPERTY::NOTIFY);
    ancsDataSrc->setCallbacks(&s_ancsDataSrcCB);

    ancsService->start();

    // ----------------------------------------------------------
    // Android UART service
    // ----------------------------------------------------------
    NimBLEService* androidService = s_server->createService(ANDROID_SERVICE_UUID);

    NimBLECharacteristic* androidRx =
        androidService->createCharacteristic(ANDROID_RX_UUID,
                                             NIMBLE_PROPERTY::WRITE |
                                             NIMBLE_PROPERTY::WRITE_NR);
    androidRx->setCallbacks(&s_androidRxCB);

    androidService->createCharacteristic(ANDROID_TX_UUID, NIMBLE_PROPERTY::NOTIFY);

    androidService->start();

    // ----------------------------------------------------------
    // Advertising
    // ----------------------------------------------------------
    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setName("ESP32Watch");
    advData.addServiceUUID(HID_SERVICE_UUID);

    NimBLEAdvertisementData scanData;
    scanData.addServiceUUID(ANCS_SERVICE_UUID);

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAdvertisementData(advData);
    adv->setScanResponseData(scanData);
    adv->setMinInterval(0x20);
    adv->setMaxInterval(0x40);

    NimBLEDevice::startAdvertising();
    s_enabled = true;

    Serial.println("[BT] Advertising as 'ESP32Watch'");
}

// ============================================================
// bt_update — call every loop tick from ui_update()
// ============================================================

void bt_update() {
    if (!s_enabled) return;

    if (s_needReadvertise) {
        s_needReadvertise = false;
        NimBLEDevice::startAdvertising();
        Serial.println("[BT] Re-advertising");
    }

    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.printf("[BT] enabled=%d connected=%d notifs=%d heap=%u\n",
                      s_enabled, s_connected, s_notifCount, ESP.getFreeHeap());
    }
}

// ============================================================
// Radio on / off
// ============================================================

void bt_enable() {
    if (s_enabled) return;
    NimBLEDevice::startAdvertising();
    s_enabled = true;
}

void bt_disable() {
    if (!s_enabled) return;
    if (s_connected) NimBLEDevice::getServer()->disconnect(0);
    NimBLEDevice::stopAdvertising();
    s_enabled = false;
}

bool bt_isEnabled()   { return s_enabled;   }
bool bt_isConnected() { return s_connected; }

// ============================================================
// Notification access
// ============================================================

int bt_getNotificationCount() { return s_notifCount; }

const BTNotification* bt_getNotification(int index) {
    if (index < 0 || index >= s_notifCount) return nullptr;
    return &s_notifs[index];
}

void bt_markRead(int index) {
    if (index < 0 || index >= s_notifCount) return;
    s_notifs[index].unread = false;
}

void bt_clearAll() {
    s_notifCount   = 0;
    s_newNotifFlag = false;
    memset(s_notifs, 0, sizeof(s_notifs));
}

bool bt_hasNewNotification() {
    bool flag      = s_newNotifFlag;
    s_newNotifFlag = false;
    return flag;
}

void bt_prepareForSleep() {
    if (!s_enabled) return;
    if (s_connected)
        NimBLEDevice::getServer()->disconnect(0);
    delay(200);
    NimBLEDevice::stopAdvertising();
    s_enabled   = false;
    s_connected = false;
}
