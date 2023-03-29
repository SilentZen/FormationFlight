//
// ESP32 Radar
// Github : https://github.com/OlivierC-FR/ESP32-INAV-Radar
// RCgroups : https://www.rcgroups.com/forums/showthread.php?3304673-iNav-Radar-ESP32-LoRa-modems
//
// -------------------------------------------------------------------------------------------

#include <targets.h>
#include <Arduino.h>
#ifdef PLATFORM_ESP32
#include <esp_system.h>
#endif
#include <lib/Display.h>
#include <EEPROM.h>
#include <main.h>
#include <lib/Helpers.h>
#include <lib/ConfigHandler.h>
#include <lib/CryptoManager.h>
#include <lib/Peers/PeerManager.h>
#include <lib/MSP/MSPManager.h>
// Radios
#include <lib/WiFi/WiFiManager.h>
#include <lib/Radios/RadioManager.h>
#include <lib/Radios/ESPNOW.h>
#include <lib/Radios/LoRa_SX128X.h>
#include <lib/Radios/LoRa_SX127X.h>

#define DEBUG 1

// -------- VARS

config_t cfg;
system_t sys;
stats_t stats;
curr_t curr;

// -------- INTERRUPTS

volatile int interruptCounter = 0;
int numberOfInterrupts = 0;

#ifdef PLATFORM_ESP32
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#endif

void IRAM_ATTR handleInterrupt()
{
#ifdef PLATFORM_ESP32
    portENTER_CRITICAL_ISR(&mux);
#endif

    if (sys.io_button_pressed == 0)
    {
        sys.io_button_pressed = 1;

        if (sys.phase > MODE_OTA_SYNC)
        {
            sys.display_page++;
        }
        else if (sys.phase == MODE_HOST_SCAN || sys.phase == MODE_OTA_SCAN)
        {
            sys.io_bt_enabled = 1; // Enable the Bluetooth if button pressed during host scan or lora scan
        }
        else if (sys.phase == MODE_START)
        {
            sys.forcereset = 1;
        }
        sys.io_button_released = millis();
    }
#ifdef PLATFORM_ESP32
    portEXIT_CRITICAL_ISR(&mux);
#endif
}

// ----------------------------- setup

void setup()
{

#ifdef PLATFORM_ESP32
    Serial.begin(921600);
#endif
    DBGLN("[main] start");
    DBGF("%s version %s UID %s\n", PRODUCT_NAME, VERSION, generate_id());

    sys.phase = MODE_START;
    sys.forcereset = 0;
    sys.io_bt_enabled = CFG_AUTOSTART_BT;
    sys.debug = 0;

    config_init();

    sys.lora_cycle = cfg.lora_nodes * cfg.slot_spacing;
    sys.cycle_stats = sys.lora_cycle * 2;

    pinMode(IO_LED_PIN, OUTPUT);
    sys.io_led_blink = 0;
#ifdef PIN_BUTTON
    pinMode(PIN_BUTTON, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), handleInterrupt, RISING);
#endif
    sys.io_button_pressed = 0;
    if (cfg.display_enable)
    {
        display_draw_intro();
    }

    delay(START_DELAY);



    // Create PeerManager
    DBGLN("[main] start PeerManager");
    PeerManager *peerManager = PeerManager::getSingleton();
    peerManager->reset();

    // Create CryptoManager
    CryptoManager *cryptoManager = CryptoManager::getSingleton();
    cryptoManager->setEnabled(false);
    
    // Create WiFiManager
    DBGLN("[main] start PeerManager");
    WiFiManager::getSingleton();

    // Create MSPManager
    DBGLN("[main] start MSPManager");
    MSPManager *mspManager = MSPManager::getSingleton();
#ifdef PLATFORM_ESP32
    mspManager->begin(Serial1);
    Serial1.begin(SERIAL_SPEED, SERIAL_8N1, SERIAL_PIN_RX, SERIAL_PIN_TX);
#elif defined(PLATFORM_ESP8266)
    mspManager->begin(Serial);
    Serial.begin(SERIAL_SPEED, SERIAL_8N1);
#endif

    // Create RadioManager
    DBGLN("[main] start RadioManager");
    RadioManager *radioManager = RadioManager::getSingleton();

    //DBGLN("[main] RadioManager::addRadio ESPNOW");
    //radioManager->addRadio(ESPNOW::getSingleton());

#ifdef LORA_FAMILY_SX128X
    DBGLN("[main] RadioManager::addRadio LoRa_SX128X");
    radioManager->addRadio(LoRa_SX128X::getSingleton());
#endif
#ifdef LORA_FAMILY_SX127X
    DBGLN("[main] RadioManager::addRadio LoRa_SX127X");
    radioManager->addRadio(LoRa_SX127X::getSingleton());
#endif

    if (cfg.display_enable)
    {
        display_draw_startup();
    }

    sys.cycle_scan_begin = millis();
    sys.now = millis();
    curr.host = HOST_NONE;
}

// ----------------------------------------------------------------------------- MAIN LOOP

void loop()
{
    sys.now = millis();
    // Run our periodic radio tasks
    RadioManager::getSingleton()->loop();
    // Periodic WiFi Tasks
    WiFiManager::getSingleton()->loop();
    // Periodic peer tasks
    PeerManager::getSingleton()->loop();

    // ---------------------- IO BUTTON

    if ((sys.now > sys.io_button_released + 150) && (sys.io_button_pressed == 1))
    {
        sys.io_button_pressed = 0;
    }

    // ---------------------- HOST SCAN

    if (sys.phase == MODE_START)
    {

        if (sys.forcereset)
        {
            display_draw_clearconfig();
            config_clear();
            delay(3000);
            ESP.restart();
        }
        sys.phase = MODE_HOST_SCAN;
    }

    // ---------------------- HOST SCAN

    if (sys.phase == MODE_HOST_SCAN)
    {

        if ((sys.now > (sys.cycle_scan_begin + HOST_MSP_TIMEOUT)) || (curr.host != HOST_NONE))
        {
            // End of the host scan - Ardu's craftname is used for DJI messages, so "INIT" will be everyone's craftname.
            // Better to use randomly generated names for Ardu
            if (curr.host != HOST_NONE && curr.host != HOST_ARDU)
            {
                MSPManager::getSingleton()->getName(curr.name, sizeof(curr.name));
            }
            if (curr.name[0] == '\0')
            {
                String chipIDString = generate_id();
                for (int i = 0; i < 3; i++)
                {
                    curr.name[i] = chipIDString.charAt(i + 3);
                }
                curr.name[3] = 0;
            }
            DBGF("[main] selected name %s\n", curr.name);

            curr.gps.fixType = 0;
            curr.gps.lat = 0;
            curr.gps.lon = 0;
            curr.gps.alt = 0;
            curr.id = 0;
            if (cfg.display_enable)
            {
                display_draw_scan(&sys);
            }

            sys.cycle_scan_begin = millis();
            sys.phase = MODE_OTA_SCAN;
        }
        else
        {
            // Still scanning
            if (sys.now > sys.display_updated + DISPLAY_CYCLE / 2)
            {
                delay(100);
                curr.host = MSPManager::getSingleton()->getFCVariant();
                curr.fcversion = MSPManager::getSingleton()->getFCVersion();
                if (cfg.force_gs)
                {
                    curr.host = HOST_GCS;
                }
                if (cfg.display_enable)
                {
                    display_draw_progressbar(100 * (millis() - sys.cycle_scan_begin) / HOST_MSP_TIMEOUT);
                }
                sys.display_updated = millis();
            }
        }
    }

    // ---------------------- LORA INIT

    if (sys.phase == MODE_OTA_SCAN)
    {

        if (sys.now > (sys.cycle_scan_begin + LORA_CYCLE_SCAN))
        {
            // End of the scan, set the ID then sync
            if (PeerManager::getSingleton()->count() >= cfg.lora_nodes || curr.host == HOST_GCS)
            { // Too many nodes already, or connected to a ground station: go to silent mode
                sys.lora_no_tx = 1;
            }
            else
            {
                sys.lora_no_tx = 0;
                pick_id();
            }
            sys.display_page = 0;
            sys.phase = MODE_OTA_SYNC;
        }
        else
        {
            // Still scanning
#ifdef HAS_OLED
            if (sys.now > sys.display_updated + DISPLAY_CYCLE / 2 && cfg.display_enable)
            {
                for (int i = 0; i < cfg.lora_nodes; i++)
                {
                    peer_t *peer = PeerManager::getSingleton()->getPeer(i);
                    if (peer->id > 0)
                    {
                        display_draw_peername(peer->id);
                    }
                }
                display_draw_progressbar(100 * (millis() - sys.cycle_scan_begin) / LORA_CYCLE_SCAN);
                sys.display_updated = millis();
            }
#endif
            delay(20);
        }
    }

    // ---------------------- LORA SYNC

    if (sys.phase == MODE_OTA_SYNC)
    {

        if (PeerManager::getSingleton()->count(false) == 0 || sys.lora_no_tx)
        {
            // Alone or Silent mode, no need to sync
            sys.next_tx = millis() + sys.lora_cycle;
        }
        else
        {
            // Not alone, sync by slot
            resync_tx_slot(cfg.lora_timing_delay);
        }

        sys.display_updated = sys.next_tx + sys.lora_cycle - 30;
        sys.stats_updated = sys.next_tx + sys.lora_cycle - 15;
        sys.pps = 0;
        sys.ppsc = 0;
        stats.packets_total = 0;
        stats.packets_received = 0;
        stats.percent_received = 0;
        digitalWrite(IO_LED_PIN, LOW);
        sys.phase = MODE_OTA_RX;
    }

    // ---------------------- LORA RX

    if ((sys.phase == MODE_OTA_RX) && (sys.now > sys.next_tx))
    {
        while (sys.now > sys.next_tx)
        {
            // In case we skipped some beats
            sys.next_tx += sys.lora_cycle;
        }

        if (sys.lora_no_tx)
        {
            sprintf(sys.message, "%s", "SILENT MODE (NO TX)");
        }
        else
        {
            sys.phase = MODE_OTA_TX;
        }
        sys.ota_nonce++;
    }

    // ---------------------- LORA TX

    if (sys.phase == MODE_OTA_TX)
    {
        if (MSPManager::hostTXCapable(curr.host))
        {
            // TODO: Replace with GNSSManager
            curr.gps = MSPManager::getSingleton()->getLocation();
        }
        else
        {
            curr.gps.lat = 0;
            curr.gps.lon = 0;
            curr.gps.alt = 0;
            curr.gps.groundCourse = 0;
            curr.gps.groundSpeed = 0;
        }
        if (curr.id != 0) {
            sys.last_tx = millis();
            air_type0_t packet = RadioManager::getSingleton()->prepare_packet();
            //DBGLN("[main] begin transmit");
            RadioManager::getSingleton()->transmit(&packet);
            //DBGLN("[main] end transmit");
            stats.last_tx_duration = millis() - sys.last_tx;
        }

        // Drift correction

        if (curr.id > 1 && PeerManager::getSingleton()->count_active() > 0)
        {
            int prev = curr.id - 2;
            peer_t *peer = PeerManager::getSingleton()->getPeer(prev);
            if (peer->id > 0)
            {
                sys.drift = sys.last_tx - peer->updated - cfg.slot_spacing;

                if ((abs(sys.drift) > LORA_DRIFT_THRESHOLD) && (abs(sys.drift) < cfg.slot_spacing))
                {
                    sys.drift_correction = constrain(sys.drift, -LORA_DRIFT_CORRECTION, LORA_DRIFT_CORRECTION);
                    sys.next_tx -= sys.drift_correction;
                    sprintf(sys.message, "%s %3d", "TIMING ADJUST", -sys.drift_correction);
                }
            }
        }

        sys.ota_slot = 0;
        sys.msp_next_cycle = sys.last_tx + cfg.msp_after_tx_delay;
        sys.phase = MODE_OTA_RX;
    }

    // ---------------------- DISPLAY

    if ((sys.now > sys.display_updated + DISPLAY_CYCLE) && sys.display_enable && (sys.phase > MODE_OTA_SYNC) && cfg.display_enable)
    {
        stats.timer_begin = millis();

        if (PeerManager::getSingleton()->count() == 0 && sys.display_page == 1)
        {
            // No need for timings graphs when alone
            sys.display_page++;
        }

        if (sys.display_page >= (3 + cfg.lora_nodes))
        {
            sys.display_page = 0;
        }

        display_draw_status(&sys);
        sys.message[0] = 0;
        stats.last_oled_duration = millis() - stats.timer_begin;
        sys.display_updated = sys.now;
    }

    // ---------------------- SERIAL / MSP

    if (sys.now > sys.msp_next_cycle && sys.phase > MODE_OTA_SYNC && sys.ota_slot < cfg.lora_nodes)
    {

        if (sys.ota_slot == 0 && (curr.host == HOST_INAV || curr.host == HOST_ARDU || curr.host == HOST_BTFL))
        {

            if (sys.ota_nonce % 6 == 0)
            {
                curr.state = MSPManager::getSingleton()->getState();
            }

            if ((sys.ota_nonce + 1) % 6 == 0)
            {
                curr.fcanalog = MSPManager::getSingleton()->getAnalogValues();
            }
        }

        stats.timer_begin = millis();
        // ----------------Send MSP to FC
        if (sys.ota_slot == 0)
        {
            //DBGLN("[main] sending msp");
            for (int i = 0; i < cfg.lora_nodes; i++)
            {
                peer_t *peer = PeerManager::getSingleton()->getPeer(i);
                // Only send if the peer has been seen and it's not us
                if (peer->id > 0 && i + 1 != curr.id)
                {
                    peer->gps_comp.lat = peer->gps.lat;
                    peer->gps_comp.lon = peer->gps.lon;
                    peer->gps_comp.alt = peer->gps.alt;
#ifndef DEBUG
                    MSPManager::getSingleton()->sendRadar(peer);
#endif
                }
            }
            //DBGLN("[main] finished sending msp");
        }
        stats.last_msp_duration = millis() - stats.timer_begin;
        sys.msp_next_cycle += cfg.slot_spacing;
        sys.ota_slot++;
    }

    // ---------------------- SERIAL CONFIG CHANNEL
    // handleConfig();

    // ---------------------- STATISTICS & IO

    if ((sys.now > (sys.cycle_stats + sys.stats_updated)) && (sys.phase > MODE_OTA_SYNC))
    {
        sys.pps = sys.ppsc;
        sys.ppsc = 0;

        // Timed-out peers + LQ
        // TODO: Move to PeerManager

        for (int i = 0; i < cfg.lora_nodes; i++)
        {
            peer_t *peer = PeerManager::getSingleton()->getPeer(i);
            if (sys.now > (peer->lq_updated + sys.lora_cycle * 4))
            {
                uint16_t diff = peer->updated - peer->lq_updated;
                peer->lq = constrain(peer->lq_tick * 4.2 * sys.lora_cycle / diff, 0, 4);
                peer->lq_updated = sys.now;
                peer->lq_tick = 0;
            }
            if (peer->id > 0 && ((sys.now - peer->updated) > LORA_PEER_TIMEOUT))
            {
                peer->lost = 2;
            }
        }

        stats.packets_total += PeerManager::getSingleton()->count_active() * sys.cycle_stats / sys.lora_cycle;
        stats.packets_received += sys.pps;
        stats.percent_received = (stats.packets_received > 0) ? constrain(100 * stats.packets_received / stats.packets_total, 0, 100) : 0;

// Screen management
#ifdef HAS_OLED
        if (!curr.state && !sys.display_enable)
        { // Aircraft is disarmed = Turning on the OLED
            display_off();
            sys.display_enable = 1;
        }
        else if (curr.state && sys.display_enable)
        { // Aircraft is armed = Turning off the OLED
            display_on;
            sys.display_enable = 0;
        }
#endif
        sys.stats_updated = sys.now;
    }

    // ---------------------- LED blinker

    if (sys.ota_nonce % 6 == 0)
    {
        if (PeerManager::getSingleton()->count_active() > 0)
        {
            sys.io_led_changestate = millis() + IO_LEDBLINK_DURATION;
            sys.io_led_count = 0;
            sys.io_led_blink = 1;
        }
    }

    if (sys.io_led_blink && millis() > sys.io_led_changestate)
    {

        sys.io_led_count++;
        sys.io_led_changestate += IO_LEDBLINK_DURATION;

        if (sys.io_led_count % 2 == 0)
        {
            digitalWrite(IO_LED_PIN, LOW);
        }
        else
        {
            digitalWrite(IO_LED_PIN, HIGH);
        }

        if (sys.io_led_count >= PeerManager::getSingleton()->count_active() * 2)
        {
            sys.io_led_blink = 0;
        }
    }
}