#pragma once
#include <Arduino.h>

enum WifiConnState {
    WCONN_IDLE,        // not trying to connect
    WCONN_CONNECTING,  // attempt in progress (never blocks the UI)
    WCONN_OK,          // connected
    WCONN_FAILED       // attempt failed, see wifi_getFailText()
};

enum WifiFailReason {
    WFAIL_NONE,
    WFAIL_WRONG_PASSWORD,
    WFAIL_NO_NETWORK,
    WFAIL_TIMEOUT
};

void        wifi_init();
void        wifi_toggle();
bool        wifi_isEnabled();
void        wifi_update();                 // call every UI tick, never blocks
int         wifi_getNetworkCount();
const char* wifi_getSSID(int index);

// Connecting (non-blocking)
void           wifi_beginConnect(const char* ssid, const char* pass);
void           wifi_cancelConnect();       // stops an attempt / clears an error
WifiConnState  wifi_getConnState();
WifiFailReason wifi_getFailReason();
const char*    wifi_getFailText();         // short text, max 20 chars