#include "wifi_module.h"
#include "web_server.h"
#include <WiFi.h>

static bool wifiEnabled = false;

#define MAX_NETWORKS 10
static String ssidList[MAX_NETWORKS];
static int networkCount = 0;

static bool scanning = false;

// ===== INIT =====
void wifi_init() {
    WiFi.mode(WIFI_OFF);
}

// ===== TOGGLE =====
void wifi_toggle() {

    wifiEnabled = !wifiEnabled;

    if (wifiEnabled) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        scanning = true;   // 🔥 start scan (not immediately blocking)
    } else {
        webserver_stop(); 
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        networkCount = 0;
        scanning = false;
    }
}

bool wifi_isEnabled() {
    return wifiEnabled;
}

// ===== UPDATE (call in loop) =====
void wifi_update() {

    if (scanning) {
        scanning = false;

        networkCount = WiFi.scanNetworks();

        if (networkCount > MAX_NETWORKS)
            networkCount = MAX_NETWORKS;

        for (int i = 0; i < networkCount; i++) {
            ssidList[i] = WiFi.SSID(i);
        }
    }
}
// ===== CONNECT FUNCTION =====
bool wifi_connect(const char* ssid, const char* pass) {

    WiFi.begin(ssid, pass);

    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < 8000) {
        delay(100);
    }

    bool ok = WiFi.status() == WL_CONNECTED;
    if (ok) {
        Serial.print("Connected! IP: ");
        Serial.println(WiFi.localIP());
        webserver_begin();          // ← ADD
    }
    return ok;
}

// ===== GETTERS =====
int wifi_getNetworkCount() {
    return networkCount;
}

const char* wifi_getSSID(int index) {
    if (index >= networkCount) return "";
    return ssidList[index].c_str();
}