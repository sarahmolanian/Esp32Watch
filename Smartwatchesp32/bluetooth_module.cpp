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
 *  - ANCS role fix          → iOS is ALWAYS the ANCS GATT server, never the
 *                             accessory. We now open a GATT *client* role on
 *                             the same link (peripheral + client, one ACL
 *                             connection) to browse iOS's own ANCS service,
 *                             subscribe to its characteristics, and write to
 *                             its Control Point. The watch no longer hosts a
 *                             local (and useless) ANCS service.
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




static int findNotifByUid(uint32_t uid) {
    for (int i = 0; i < s_notifCount; i++) {
        if (s_notifs[i].uid == uid)
            return i;
    }
    return -1;
}


static void pushNotification(const BTNotification& n) {
    int existing = findNotifByUid(n.uid);

    // Already have this notification -> update it, don't duplicate it.
    if (existing >= 0) {
        s_notifs[existing] = n;
        s_newNotifFlag = true;
        return;
    }

    if (s_notifCount >= BT_NOTIF_MAX)
        return;

    for (int i = s_notifCount; i > 0; i--)
        s_notifs[i] = s_notifs[i - 1];

    s_notifs[0] = n;
    s_notifCount++;
    s_newNotifFlag = true;
}



// ============================================================
// BLE state
// ============================================================

static NimBLEServer*         s_server            = nullptr;
static bool                  s_enabled            = false;
static bool                  s_connected          = false;
static volatile bool         s_needReadvertise    = false;

// GATT *client* side — used to browse iOS's own ANCS service on the
// same link. iOS is the ANCS server; we are the client here, even
// though we're the peripheral at the link layer.
static NimBLEClient*             s_ancsClient        = nullptr;
static NimBLERemoteCharacteristic* s_ancsControlRemote = nullptr;

// Deferred-discovery flag. NimBLEClient::connect() blocks waiting on the
// NimBLE host task — calling it directly from a server callback (which
// itself runs ON the host task) deadlocks the host task. It stops
// servicing the link, and the controller eventually kills the connection
// with a supervision timeout (reason 0x08 / NimBLE 0x208). So we only
// set a flag + connHandle here, and do the actual connect()/discovery
// from bt_update() on the main Arduino loop task instead.
static volatile bool s_needAncsDiscovery = false;
static uint16_t      s_pendingConnHandle = 0;

enum AncsState { ANCS_IDLE, ANCS_WAITING_ATTRS };
static AncsState s_ancsState = ANCS_IDLE;

// ── Texts + calls only, for now ─────────────────────────────────
// ANCS has no dedicated "Messages" category — SMS/iMessage lands in
// the generic categoryID 0 ("Other") along with any other app that
// doesn't set a category. So we can't filter texts by category alone;
// we confirm via the AppIdentifier attribute (bundle ID) once the
// Data Source response comes back. Calls (1/2/3) ARE their own
// category, so those are trusted immediately.
static const char* MESSAGES_BUNDLE_ID = "com.apple.MobileSMS";
static const char* INSTAGRAM_BUNDLE_ID = "com.burbn.instagram";
static const char* TELEGRAM_BUNDLE_ID  = "ph.telegra.Telegraph";
static const char* GMAIL_BUNDLE_ID     = "com.google.Gmail";
static const char* OUTLOOK_BUNDLE_ID   = "com.microsoft.Office.Outlook";
static const char* WHATSAPP_BUNDLE_ID  = "net.whatsapp.WhatsApp";

static bool isAllowedApp(const char* appId) {
    if (!appId || !appId[0])
        return false;

    return
        strcmp(appId, MESSAGES_BUNDLE_ID)  == 0 ||
        strcmp(appId, INSTAGRAM_BUNDLE_ID) == 0 ||
        strcmp(appId, TELEGRAM_BUNDLE_ID)  == 0 ||
        strcmp(appId, GMAIL_BUNDLE_ID)     == 0 ||
        strcmp(appId, OUTLOOK_BUNDLE_ID)   == 0 ||
        strcmp(appId, WHATSAPP_BUNDLE_ID)  == 0;
}

static const char* appDisplayName(const char* appId) {
    if (!appId) return "Unknown";

    if (strcmp(appId, MESSAGES_BUNDLE_ID)  == 0) return "Messages";
    if (strcmp(appId, INSTAGRAM_BUNDLE_ID) == 0) return "Instagram";
    if (strcmp(appId, TELEGRAM_BUNDLE_ID)  == 0) return "Telegram";
    if (strcmp(appId, GMAIL_BUNDLE_ID)     == 0) return "Gmail";
    if (strcmp(appId, OUTLOOK_BUNDLE_ID)   == 0) return "Outlook";
    if (strcmp(appId, WHATSAPP_BUNDLE_ID)  == 0) return "WhatsApp";

    return "Unknown";
}

// Currently in-flight request (the one we've actually sent and are
// waiting on a Data Source response for).
static uint32_t s_pendingUid      = 0;
static uint8_t  s_pendingCategory = 0xFF;
static unsigned long s_pendingSentAt = 0;
static const unsigned long ANCS_ATTR_TIMEOUT_MS = 3000;

// Queue of notifications still waiting for their turn to have
// GetNotificationAttributes requested. This used to be a single
// overwritable variable — which meant a burst of notifications
// arriving faster than we could process them (completely normal
// during real phone usage) silently clobbered earlier ones before
// their request was ever sent. Those got stuck forever as
// "Checking..." placeholders, never resolved to KEEP or DROP,
// quietly inflating the notification count. A real queue + a
// timeout (in case a response never arrives) fixes both problems.
struct PendingAttr { uint32_t uid; uint8_t category; };
static const int ANCS_QUEUE_SIZE = 12;
static PendingAttr s_attrQueue[ANCS_QUEUE_SIZE];
static int s_attrQueueHead  = 0;
static int s_attrQueueCount = 0;

static bool enqueueAttrRequest(uint32_t uid, uint8_t category) {
    if (s_attrQueueCount >= ANCS_QUEUE_SIZE) {
        Serial.printf("[ANCS]   -> WARNING: attr queue full, dropping uid=%lu\n",
                      (unsigned long)uid);
        return false;
    }
    int tail = (s_attrQueueHead + s_attrQueueCount) % ANCS_QUEUE_SIZE;
    s_attrQueue[tail].uid      = uid;
    s_attrQueue[tail].category = category;
    s_attrQueueCount++;
    return true;
}

static bool dequeueAttrRequest(PendingAttr& out) {
    if (s_attrQueueCount == 0) return false;
    out = s_attrQueue[s_attrQueueHead];
    s_attrQueueHead = (s_attrQueueHead + 1) % ANCS_QUEUE_SIZE;
    s_attrQueueCount--;
    return true;
}

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

// Forward declarations (ServerCallbacks::onConnect needs this before
// its definition further down)
static void startAncsDiscovery(uint16_t connHandle);
static void teardownAncsClient();
static void removeNotifByUid(uint32_t uid);

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
        Serial.printf("[ANCS] Removed uid=%lu (cleared on phone)\n", (unsigned long)uid);
        removeNotifByUid(uid);
        return;
    }
    if (eventID != 0 && eventID != 1) return;

    const char* eventName = (eventID == 0) ? "Added" : "Modified";
    Serial.printf("[ANCS] %s uid=%lu categoryID=%d\n", eventName, (unsigned long)uid, categoryID);

    // Stage 1 filter — category. Calls (1/2/3) are trustworthy as-is.
    // categoryID 0 ("Other") MIGHT be a text — we can't tell without
    // the AppIdentifier, so it's provisionally allowed through.
    // Everything else (Social=4, Schedule=5, Email=6, News=7,
    // Health=8, Business=9, Location=10, Entertainment=11) is
    // dropped right here — no Control Point round-trip spent on it.
    // Calls:
    //   1 = Incoming Call
    //   2 = Missed Call
    //   3 = Voicemail
    //
    // Other apps we care about:
    //   0 = Other       -> Messages/SMS/iMessage, etc.
    //   4 = Social      -> Instagram, Telegram, WhatsApp
    //   6 = Email       -> Gmail, Outlook
    //
    // We MUST allow these categories through because the AppIdentifier
    // tells us which actual app generated the notification.

    bool isCallCategory =
        (categoryID == 1 ||
         categoryID == 2 ||
         categoryID == 3);

    bool isAllowedAppCategory =
        (categoryID == 0 ||
         categoryID == 4 ||
         categoryID == 6);

    if (!isCallCategory && !isAllowedAppCategory) {
        Serial.printf("[ANCS]   -> skipped category %d\n", categoryID);
        return;
    }





    BTNotification n{};
    n.uid    = uid;
    n.unread = true;
    switch (categoryID) {
        case 1:  safeCopy(n.app, "Incoming Call",  BT_NOTIF_APP_LEN); break;
        case 2:  safeCopy(n.app, "Missed Call",    BT_NOTIF_APP_LEN); break;
        case 3:  safeCopy(n.app, "Voicemail",      BT_NOTIF_APP_LEN); break;
        default: safeCopy(n.app, "Checking...",    BT_NOTIF_APP_LEN); break; // resolved once AppIdentifier arrives
    }
    safeCopy(n.title, n.app,         BT_NOTIF_TITLE_LEN);
    safeCopy(n.body,  "Tap to view", BT_NOTIF_BODY_LEN);
    pushNotification(n);

    // Ask iOS for AppIdentifier (to confirm texts), Title, and Message
    // via its Control Point. Do NOT write here — this function runs on
    // the BLE host task (it's a remote-characteristic notify callback),
    // and a write-with-response blocks waiting on that same task,
    // deadlocking it exactly like the earlier onAuthenticationComplete
    // issue. Just enqueue it; bt_update() sends requests one at a time
    // in order, since the ANCS Control Point only handles one
    // GetNotificationAttributes exchange at a time anyway.
    if (s_ancsControlRemote && s_connected) {
        if (enqueueAttrRequest(uid, categoryID)) {
            Serial.println("[ANCS]   -> queued AppIdentifier/Title/Message request");
        } else {
            // Queue was full — this notification will stay as
            // "Checking..." forever otherwise, so just drop it now
            // rather than let it linger unresolved.
            removeNotifByUid(uid);
        }
    } else {
        Serial.println("[ANCS]   -> WARNING: no Control Point available, can't fetch details");
    }
}

// ============================================================
// ANCS Data Source parser
// [CmdID, UID x4, AttrID, AttrLen x2, AttrData...]
// ============================================================

static void removeNotifByUid(uint32_t uid) {
    int idx = -1;
    for (int i = 0; i < s_notifCount; i++) {
        if (s_notifs[i].uid == uid) { idx = i; break; }
    }
    if (idx < 0) return;
    for (int i = idx; i < s_notifCount - 1; i++)
        s_notifs[i] = s_notifs[i + 1];
    s_notifCount--;
}

// Pulls the next queued notification (if any) and sends its
// GetNotificationAttributes request. MUST only be called from the main
// Arduino loop task (via bt_update()) — never from a BLE host task
// callback — since this is a blocking write-with-response. Only sends
// one at a time, gated on s_ancsState, since the ANCS Control Point
// doesn't support overlapping GetNotificationAttributes exchanges.
static void serviceAttrQueue() {
    // A response is already outstanding — check for timeout (in case
    // it was lost/never sent by iOS) before doing anything else.
    if (s_ancsState == ANCS_WAITING_ATTRS) {
        if (millis() - s_pendingSentAt > ANCS_ATTR_TIMEOUT_MS) {
            Serial.printf("[ANCS]   -> TIMEOUT waiting on uid=%lu, dropping and moving on\n",
                          (unsigned long)s_pendingUid);
            removeNotifByUid(s_pendingUid);
            s_ancsState = ANCS_IDLE;
        } else {
            return; // still waiting, don't send another yet
        }
    }

    if (!s_ancsControlRemote || !s_connected) return;

    PendingAttr next;
    if (!dequeueAttrRequest(next)) return; // queue empty, nothing to do

    s_pendingUid      = next.uid;
    s_pendingCategory = next.category;

    uint8_t cmd[12];
    cmd[0]  = 0x00;                 // CommandID: GetNotificationAttributes
    cmd[1]  =  next.uid        & 0xFF;
    cmd[2]  = (next.uid >>  8) & 0xFF;
    cmd[3]  = (next.uid >> 16) & 0xFF;
    cmd[4]  = (next.uid >> 24) & 0xFF;
    cmd[5]  = 0x00;                 // AttributeID: AppIdentifier
    cmd[6]  = 0x01; cmd[7]  = 48; cmd[8]  = 0;   // AttributeID: Title, max len 48
    cmd[9]  = 0x03; cmd[10] = 80; cmd[11] = 0;   // AttributeID: Message, max len 80
    s_ancsControlRemote->writeValue(cmd, sizeof(cmd), /*response=*/true);
    s_ancsState     = ANCS_WAITING_ATTRS;
    s_pendingSentAt = millis();
    Serial.printf("[ANCS]   -> sent AppIdentifier/Title/Message request for uid=%lu (queue depth now %d)\n",
                  (unsigned long)next.uid, s_attrQueueCount);
}

static void handleAncsDataSource(const uint8_t* data, size_t len) {
    if (len < 6 || data[0] != 0x00)
        return;

    uint32_t uid = (uint32_t)data[1]
                 | ((uint32_t)data[2] << 8)
                 | ((uint32_t)data[3] << 16)
                 | ((uint32_t)data[4] << 24);

    // Find the notification that this response belongs to.
    BTNotification* target = nullptr;

    for (int i = 0; i < s_notifCount; i++) {
        if (s_notifs[i].uid == uid) {
            target = &s_notifs[i];
            break;
        }
    }

    if (!target) {
        Serial.printf(
            "[ANCS] Data Source for unknown/already-dropped uid=%lu\n",
            (unsigned long)uid
        );
        return;
    }

    char appId[BT_NOTIF_APP_LEN] = {};

    size_t pos = 5;

    while (pos + 2 < len) {
        uint8_t attrID = data[pos++];

        uint16_t attrLen =
            (uint16_t)data[pos] |
            ((uint16_t)data[pos + 1] << 8);

        pos += 2;

        if (pos + attrLen > len)
            break;

        char tmp[81] = {};

        size_t cpLen = attrLen < 80 ? attrLen : 80;

        memcpy(tmp, data + pos, cpLen);
        tmp[cpLen] = '\0';

        switch (attrID) {

            case 0x00:
                // AppIdentifier / bundle ID
                safeCopy(appId, tmp, sizeof(appId));
                break;

            case 0x01:
                // Title
                safeCopy(
                    target->title,
                    tmp,
                    BT_NOTIF_TITLE_LEN
                );
                break;

            case 0x03:
                // Message/body
                safeCopy(
                    target->body,
                    tmp,
                    BT_NOTIF_BODY_LEN
                );
                break;
        }

        pos += attrLen;
    }

    // ------------------------------------------------------------
    // Calls are already trusted from their ANCS category.
    // Everything else must be verified using the bundle ID.
    // ------------------------------------------------------------

    bool wasCall =
        (s_pendingUid == uid) &&
        (s_pendingCategory == 1 ||
         s_pendingCategory == 2 ||
         s_pendingCategory == 3);

    bool allowedApp = isAllowedApp(appId);

    Serial.printf(
        "[ANCS] uid=%lu bundle=\"%s\"\n",
        (unsigned long)uid,
        appId
    );

    // ------------------------------------------------------------
    // KEEP
    // ------------------------------------------------------------

    if (wasCall || allowedApp) {

        if (allowedApp) {
            safeCopy(
                target->app,
                appDisplayName(appId),
                BT_NOTIF_APP_LEN
            );
        }

        Serial.printf(
            "[ANCS]   -> KEEP app=\"%s\" title=\"%s\" body=\"%s\"\n",
            target->app,
            target->title,
            target->body
        );

        s_newNotifFlag = true;
    }

    // ------------------------------------------------------------
    // DROP
    // ------------------------------------------------------------

    else {

        Serial.printf(
            "[ANCS]   -> DROP bundle=\"%s\"\n",
            appId
        );

        removeNotifByUid(uid);
    }

    // Current request is finished.
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
// Remote (iOS-hosted) ANCS characteristic notify callbacks
// These fire when iOS pushes data on Notification Source / Data Source
// ============================================================

static void ancsNotifSourceRemoteCB(NimBLERemoteCharacteristic* /*chr*/,
                                     uint8_t* data, size_t len, bool /*isNotify*/) {
    handleAncsNotifSource(data, len);
}

static void ancsDataSourceRemoteCB(NimBLERemoteCharacteristic* /*chr*/,
                                    uint8_t* data, size_t len, bool /*isNotify*/) {
    handleAncsDataSource(data, len);
}

// ============================================================
// GATT client callbacks (for the ANCS-browsing client role)
// ============================================================

class AncsClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* /*client*/, int reason) override {
        Serial.printf("[BT] ANCS client link dropped reason=0x%02x\n", reason);
        s_ancsControlRemote = nullptr;
    }
};
static AncsClientCallbacks s_ancsClientCB;

// ============================================================
// NimBLE 2.x server callbacks
// NEVER call delay() here — runs on BLE FreeRTOS task
// ============================================================

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* srv, NimBLEConnInfo& connInfo) override {
        s_connected       = true;
        s_needReadvertise = false;
        Serial.println("[BT] Phone connected");
        srv->setDataLen(connInfo.getConnHandle(), 185);

        // Explicitly request Apple-compliant connection parameters.
        // Without this, whatever defaults get negotiated can be
        // "marginal" — the link stays up under normal conditions but
        // gets dropped by iOS's radio arbitration the moment something
        // else needs priority (classic case: an incoming call spikes
        // cellular/audio radio activity and iOS sheds non-compliant
        // background BLE links via a supervision timeout).
        // Apple Accessory Design Guidelines: interval 15–30ms,
        // slave latency low, supervision timeout a few seconds.
        static const uint16_t kConnIntervalMin = 12;   // 12 * 1.25ms = 15ms
        static const uint16_t kConnIntervalMax = 24;   // 24 * 1.25ms = 30ms
        static const uint16_t kConnLatency     = 0;
        static const uint16_t kConnTimeout     = 600;  // 600 * 10ms = 6000ms
        srv->updateConnParams(connInfo.getConnHandle(),
                               kConnIntervalMin, kConnIntervalMax,
                               kConnLatency, kConnTimeout);
        Serial.println("[BT] Requested Apple-compliant conn params");

        // Actually TRIGGER pairing/bonding. setSecurityAuth() etc. only
        // configure what happens IF a Security Manager handshake occurs
        // — they don't start one. iOS won't spontaneously initiate
        // pairing just because it connected to a service (HID's
        // characteristics here don't require encryption to read), so
        // without this call the link stays open but unbonded forever:
        // no pairing dialog, no "Forget This Device" entry, no keys,
        // and onAuthenticationComplete() never fires — which is exactly
        // what was happening. This sends a Security Request from the
        // peripheral side, which is what actually makes iOS show the
        // pairing prompt and complete bonding.
        NimBLEDevice::startSecurity(connInfo.getConnHandle());
        Serial.println("[BT] Requested security/pairing");

        // Open a GATT client role on this same link to browse iOS's own
        // ANCS service. Must wait for bonding/encryption to complete
        // (onAuthenticationComplete) before iOS will expose ANCS, so we
        // kick discovery off there instead of here — see below.
    }

    void onConnParamsUpdate(NimBLEConnInfo& connInfo) override {
        Serial.printf("[BT] Conn params updated: interval=%.2fms latency=%d timeout=%dms\n",
                      connInfo.getConnInterval() * 1.25,
                      connInfo.getConnLatency(),
                      connInfo.getConnTimeout() * 10);
    }

    void onDisconnect(NimBLEServer* srv, NimBLEConnInfo& connInfo, int reason) override {
        s_connected       = false;
        s_ancsState       = ANCS_IDLE;
        s_needReadvertise = true;
        // Fresh BLE/ANCS session = don't keep stale notifications.
        s_notifCount = 0;
        s_newNotifFlag = false;
        memset(s_notifs, 0, sizeof(s_notifs));

        Serial.printf("[BT] Disconnected reason=0x%02x\n", reason);
        teardownAncsClient();
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        if (!connInfo.isEncrypted()) {
            Serial.println("[BT] Auth failed - disconnecting");
            NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
            return;
        }
        Serial.println("[BT] Bonded and encrypted OK");
        // Do NOT call startAncsDiscovery() here — this callback runs on
        // the NimBLE host task, and discovery needs to block-wait on
        // that same task. Defer it to bt_update() instead.
        s_pendingConnHandle  = connInfo.getConnHandle();
        s_needAncsDiscovery  = true;
    }
};

class AndroidRxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo& connInfo) override {
        handleAndroidMessage(chr->getValue());
    }
};

static ServerCallbacks  s_serverCB;
static AndroidRxCB      s_androidRxCB;

// ============================================================
// ANCS discovery (client role, reusing the existing peripheral link)
// ============================================================

static void teardownAncsClient() {
    // Per the library's design, this is created via NimBLEServer::getClient()
    // and must be torn down the same way — NOT NimBLEDevice::deleteClient().
    NimBLEServer* srv = NimBLEDevice::getServer();
    if (srv && s_ancsClient) {
        srv->deleteClient();
    }
    s_ancsClient        = nullptr;
    s_ancsControlRemote = nullptr;

    // A fresh connection means a fresh set of ANCS UIDs from iOS — any
    // queued/in-flight requests from the old session are meaningless now.
    s_attrQueueHead  = 0;
    s_attrQueueCount = 0;
    s_ancsState      = ANCS_IDLE;
}

static void startAncsDiscovery(uint16_t connHandle) {
    teardownAncsClient();

    // NimBLEServer::getClient() is the library's dedicated API (added for
    // exactly this ANCS-style dual-role case) for obtaining a NimBLEClient
    // attached to a peer we're already connected to as a peripheral — it
    // does the attach internally, no separate connect() call needed.
    // (NimBLEDevice::createClient()+connect() only works for connections
    // WE initiated as central, which is not our situation here.)
    NimBLEServer* srv = NimBLEDevice::getServer();
    s_ancsClient = srv ? srv->getClient(connHandle) : nullptr;

    if (!s_ancsClient) {
        Serial.printf("[BT] getClient() failed for connHandle=%u — no ANCS client available\n",
                      connHandle);
        return;
    }
    s_ancsClient->setClientCallbacks(&s_ancsClientCB, /*deleteCallbacks=*/false);

    NimBLERemoteService* ancs = s_ancsClient->getService(ANCS_SERVICE_UUID);
    if (!ancs) {
        Serial.println("[BT] iOS did not expose ANCS on this bond (retry may help)");
        return;
    }

    NimBLERemoteCharacteristic* notifSrc = ancs->getCharacteristic(ANCS_NOTIF_SOURCE_UUID);
    NimBLERemoteCharacteristic* dataSrc  = ancs->getCharacteristic(ANCS_DATA_SOURCE_UUID);
    s_ancsControlRemote = ancs->getCharacteristic(ANCS_CONTROL_POINT_UUID);

    if (notifSrc && notifSrc->canNotify()) {
        notifSrc->subscribe(true, ancsNotifSourceRemoteCB);
    } else {
        Serial.println("[BT] Notification Source characteristic missing/not notifiable");
    }

    if (dataSrc && dataSrc->canNotify()) {
        dataSrc->subscribe(true, ancsDataSourceRemoteCB);
    } else {
        Serial.println("[BT] Data Source characteristic missing/not notifiable");
    }

    if (!s_ancsControlRemote) {
        Serial.println("[BT] Control Point characteristic missing");
    }

    Serial.println("[BT] ANCS discovery complete, subscribed to iOS notifications");
}

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
    // iOS validates ALL 5 are present before saving the bond.
    // This is the ONLY local service we advertise for iOS: it
    // exists purely so iOS shows/pairs the accessory. ANCS itself
    // is browsed from iOS as a client (see startAncsDiscovery) —
    // we must NOT host a local "ANCS" service, since iOS never
    // looks for ANCS on the accessory side.
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
    // Android UART service — unaffected, still hosted locally since
    // Gadgetbridge/Android accessories DO expect the accessory to be
    // the GATT server for this custom service.
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
    // No longer advertise the ANCS UUID in scan response — we don't
    // host that service locally, so advertising it is misleading.

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

    if (s_needAncsDiscovery) {
        s_needAncsDiscovery = false;
        startAncsDiscovery(s_pendingConnHandle);
    }

    serviceAttrQueue();

    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.printf("[BT] enabled=%d connected=%d ancsReady=%d notifs=%d heap=%u\n",
                      s_enabled, s_connected, s_ancsControlRemote != nullptr,
                      s_notifCount, ESP.getFreeHeap());
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
    teardownAncsClient();
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
    teardownAncsClient();
    if (s_connected)
        NimBLEDevice::getServer()->disconnect(0);
    delay(200);
    NimBLEDevice::stopAdvertising();
    s_enabled   = false;
    s_connected = false;
}
