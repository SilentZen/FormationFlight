#pragma once
#include <main.h>
#include <ArduinoJson.h>

struct peer_t {
   uint8_t id;
   uint8_t host;
   uint8_t state;
   uint8_t lost;
   uint8_t broadcast;
   uint32_t updated;
   uint32_t lq_updated;
   uint8_t lq_tick;
   uint8_t lq;
   int rssi;
   float distance;
   int16_t direction;
   int16_t relalt;
   msp_raw_gps_t gps;
   msp_raw_gps_t gps_rec;
   msp_raw_gps_t gps_pre;
   uint32_t gps_pre_updated;
   msp_raw_gps_t gps_comp;
   msp_analog_t fcanalog;
   char name[LORA_NAME_LENGTH + 1];
   uint32_t packetsReceived;
};

class PeerManager
{
public:
    peer_t *getPeer(uint8_t index);
    void reset();
    uint8_t count(bool active = false);
    uint8_t count_active();
    void statusJson(JsonDocument *doc);
    void loop();
    static PeerManager *getSingleton();
private:
    peer_t peers[LORA_NODES_MAX];
};