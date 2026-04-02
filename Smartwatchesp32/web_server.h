#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>

// Call once after WiFi is connected (inside wifi_connect success path or ui_update)
void webserver_begin();

// Call every loop tick — handles incoming HTTP requests
void webserver_update();

// Returns true if the web server is currently running
bool webserver_isRunning();

// Call this when WiFi disconnects so the server stops cleanly
void webserver_stop();

// ── Notification inbox ──────────────────────────────────────────
// The webapp can POST a notification to /notify
// The watch stores it here and displays it on screen

#define WS_NOTIF_MAX  8
#define WS_NOTIF_APP_LEN   24
#define WS_NOTIF_TEXT_LEN  80
#define WS_TIME_LEN        8

struct WSNotification {
    char app[WS_NOTIF_APP_LEN];
    char text[WS_NOTIF_TEXT_LEN];
    char time[WS_TIME_LEN];
    bool unread;
};

int                   ws_getNotificationCount();
const WSNotification* ws_getNotification(int index);
void                  ws_markRead(int index);
void                  ws_clearAll();
bool                  ws_hasNewNotification();  // clears flag on read

#endif
