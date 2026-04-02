#ifndef WIFI_MODULE_H
#define WIFI_MODULE_H

#include <Arduino.h>

void wifi_init();
void wifi_update();

void wifi_toggle();
bool wifi_isEnabled();
bool wifi_connect(const char* ssid, const char* pass);

void wifi_scanNetworks();
int  wifi_getNetworkCount();
const char* wifi_getSSID(int index);

#endif