#ifdef WIFI_CONFIG
#ifndef WIFICONFIG_H
#define WIFICONFIG_H

#include "ConfigHandler.h"

void initConfigInterface();
void handleConfig();
void debugPrintln(String input);

#endif
#endif