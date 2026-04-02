#ifndef BLUETOOTH_MODULE_H
#define BLUETOOTH_MODULE_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Notification structure
// ============================================================

#define BT_NOTIF_APP_LEN    32
#define BT_NOTIF_TITLE_LEN  48
#define BT_NOTIF_BODY_LEN   80

struct BTNotification {
    char app[BT_NOTIF_APP_LEN];       // e.g. "Messages", "WhatsApp"
    char title[BT_NOTIF_TITLE_LEN];   // sender / subject
    char body[BT_NOTIF_BODY_LEN];     // message body (truncated to fit OLED)
    uint32_t uid;                      // notification UID (ANCS) or counter (Android)
    bool unread;                       // still unread?
};

// ============================================================
// Notification ring buffer — holds the last N notifications
// ============================================================

#define BT_NOTIF_MAX 8

// ============================================================
// Public API
// ============================================================

/**
 * Call once from setup() / ui_init().
 * Initialises NimBLE, registers GATT services, and starts advertising.
 */
void bt_init();

/**
 * Call every loop tick (or from ui_update).
 * Pumps the BLE stack and processes any pending ANCS / Android messages.
 */
void bt_update();

/** Enable / disable Bluetooth radio (persists until reboot). */
void bt_enable();
void bt_disable();

/** Returns true if the radio is switched on. */
bool bt_isEnabled();

/** Returns true if a phone is currently connected. */
bool bt_isConnected();

/**
 * Returns the number of unread notifications in the ring buffer.
 * Range: 0 – BT_NOTIF_MAX.
 */
int  bt_getNotificationCount();

/**
 * Returns a pointer to notification[index] (0 = most recent).
 * Returns nullptr if index is out of range.
 */
const BTNotification* bt_getNotification(int index);

/** Mark notification[index] as read. */
void bt_markRead(int index);

/** Clear all stored notifications. */
void bt_clearAll();

/**
 * Returns true when a NEW notification arrived since the last call.
 * Clears the flag automatically each call — poll once per frame.
 */
bool bt_hasNewNotification();

#endif // BLUETOOTH_MODULE_H
