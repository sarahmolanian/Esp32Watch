#include "wifi_module.h"
#include <WiFi.h>
#include "esp_wifi.h"

#define MAX_NETWORKS        10
#define CONNECT_TIMEOUT_MS  15000

static String ssidList[MAX_NETWORKS];
static int    networkCount  = 0;
static bool   wifiEnabled   = false;
static bool   scanRequested = false;
static bool   scanRunning   = false;

static volatile WifiConnState  s_connState  = WCONN_IDLE;
static volatile WifiFailReason s_failReason = WFAIL_NONE;
static unsigned long           s_connStart  = 0;
static bool                    s_eventsRegistered = false;

// connection request waiting to be started by wifi_update()
static char          s_ssid[33]       = "";
static char          s_pass[65]       = "";
static volatile bool s_beginPending   = false;
static unsigned long s_beginAt        = 0;

// ============================================================
// WiFi event: the router tells us WHY a connection failed.
// Runs on the WiFi event task, so it only sets two flags.
// ============================================================
static void onStaDisconnected(arduino_event_id_t event, arduino_event_info_t info) {
    if (s_connState != WCONN_CONNECTING || s_beginPending) return;   // ignore our own disconnects

    switch (info.wifi_sta_disconnected.reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            s_failReason = WFAIL_WRONG_PASSWORD;
            s_connState  = WCONN_FAILED;
            break;

        case WIFI_REASON_NO_AP_FOUND:
            s_failReason = WFAIL_NO_NETWORK;
            s_connState  = WCONN_FAILED;
            break;

        default:
            break;   // anything else: keep waiting, the timeout covers it
    }
}

static void registerEvents() {
    if (s_eventsRegistered) return;
    WiFi.onEvent(onStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    s_eventsRegistered = true;
}

// ============================================================
// INIT / TOGGLE
// ============================================================
void wifi_init() {
    WiFi.mode(WIFI_OFF);
}

void wifi_toggle() {
    wifiEnabled = !wifiEnabled;

    if (wifiEnabled) {
        registerEvents();
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        scanRequested = true;
    } else {
        if (scanRunning) esp_wifi_scan_stop();
        WiFi.scanDelete();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        networkCount   = 0;
        scanRequested  = false;
        scanRunning    = false;
        s_beginPending = false;
        s_connState    = WCONN_IDLE;
        s_failReason   = WFAIL_NONE;
    }
}

bool wifi_isEnabled() {
    return wifiEnabled;
}

// ============================================================
// CONNECT
// This only records the request and returns at once. wifi_update()
// starts the real connection ~150 ms later, so the screen can switch
// to "Connecting..." first and never looks frozen.
// ============================================================
void wifi_beginConnect(const char* ssid, const char* pass) {
    registerEvents();

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    strncpy(s_pass, pass, sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = '\0';

    scanRequested  = false;
    s_failReason   = WFAIL_NONE;
    s_connState    = WCONN_CONNECTING;
    s_connStart    = millis();
    s_beginAt      = millis() + 150;
    s_beginPending = true;
}

void wifi_cancelConnect() {
    WifiConnState st = s_connState;
    if (st == WCONN_CONNECTING || st == WCONN_FAILED) {
        if (!s_beginPending) WiFi.disconnect();   // only if the attempt really started
        s_beginPending = false;
        s_connState    = WCONN_IDLE;
        s_failReason   = WFAIL_NONE;
    }
    // if already connected (WCONN_OK) we stay connected
}

WifiConnState  wifi_getConnState()  { return s_connState; }
WifiFailReason wifi_getFailReason() { return s_failReason; }

const char* wifi_getFailText() {
    switch (s_failReason) {
        case WFAIL_WRONG_PASSWORD: return "Wrong password";
        case WFAIL_NO_NETWORK:     return "Network not found";
        case WFAIL_TIMEOUT:        return "Connection timed out";
        default:                   return "Could not connect";
    }
}

// ============================================================
// UPDATE (call every UI tick)
// ============================================================
void wifi_update() {
    if (!wifiEnabled) return;

    // ---- start a requested connection ----
    if (s_beginPending && (int32_t)(millis() - s_beginAt) >= 0) {
        s_beginPending = false;
        if (scanRunning) { esp_wifi_scan_stop(); scanRunning = false; }
        WiFi.setAutoReconnect(false);   // one attempt, no endless background retries
        WiFi.begin(s_ssid, s_pass);
        s_connStart = millis();         // the 15 s timer starts now
    }

    // ---- async network scan ----
    if (scanRequested && !scanRunning && s_connState != WCONN_CONNECTING) {
        scanRequested = false;
        WiFi.scanNetworks(true);          // true = async
        scanRunning = true;
    }

    if (scanRunning) {
        int n = WiFi.scanComplete();
        if (n != WIFI_SCAN_RUNNING) {     // still scanning while -1
            scanRunning = false;
            if (n > 0) {
                networkCount = (n < MAX_NETWORKS) ? n : MAX_NETWORKS;
                for (int i = 0; i < networkCount; i++) ssidList[i] = WiFi.SSID(i);
            }
            WiFi.scanDelete();
        }
    }

    // ---- connection attempt ----
    if (s_connState == WCONN_CONNECTING && !s_beginPending) {
        if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == s_ssid) {
            WiFi.setAutoReconnect(true);  // normal behaviour once connected
            s_connState = WCONN_OK;
        } else if (millis() - s_connStart > CONNECT_TIMEOUT_MS) {
            s_failReason = WFAIL_TIMEOUT;
            s_connState  = WCONN_FAILED;
        }
    }

    // after a failure make sure the radio stops trying in the background
    static WifiConnState lastSeen = WCONN_IDLE;
    WifiConnState cur = s_connState;
    if (cur == WCONN_FAILED && lastSeen != WCONN_FAILED) WiFi.disconnect();
    lastSeen = cur;
}

// ============================================================
// GETTERS
// ============================================================
int wifi_getNetworkCount() {
    return networkCount;
}

const char* wifi_getSSID(int index) {
    if (index < 0 || index >= networkCount) return "";
    return ssidList[index].c_str();
}