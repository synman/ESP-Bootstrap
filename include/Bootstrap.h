/***************************************************************************
Copyright © 2023 Shell M. Shrader <shell at shellware dot com>
----------------------------------------------------------------------------
This work is free. You can redistribute it and/or modify it under the
terms of the Do What The Fuck You Want To Public License, Version 2,
as published by Sam Hocevar. See the COPYING file for more details.
****************************************************************************/
#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include <TelnetSpy.h>
#pragma GCC diagnostic ignored "-Wunused-variable"
static TelnetSpy *SandT;

#ifdef BS_USE_TELNETSPY
    #define LOG_PRINT(...)       SerialAndTelnet.print(__VA_ARGS__)
    #define LOG_PRINTLN(...)     SerialAndTelnet.println(__VA_ARGS__)
    #define LOG_PRINTF(...)      SerialAndTelnet.printf(__VA_ARGS__)
    #define LOG_HANDLE()         SerialAndTelnet.handle() ; checkForRemoteCommand()
    #define LOG_FLUSH()          SerialAndTelnet.flush()

    #define BS_LOG_BEGIN(baudrate)  SandT->begin(baudrate)
    #define BS_LOG_WELCOME_MSG(msg) SandT->setWelcomeMsg(msg)
    #define BS_LOG_PRINT(...)       SandT->print(__VA_ARGS__)
    #define BS_LOG_PRINTLN(...)     SandT->println(__VA_ARGS__)
    #define BS_LOG_PRINTF(...)      SandT->printf(__VA_ARGS__)
    #define BS_LOG_HANDLE()         SandT->handle() ; checkForRemoteCommand()
    #define BS_LOG_FLUSH()          SandT->flush()
#else
    #define LOG_PRINT(...) 
    #define LOG_PRINTLN(...)
    #define LOG_PRINTF(...) 
    #define LOG_HANDLE()
    #define LOG_FLUSH()

    #define BS_LOG_BEGIN(baudrate)
    #define BS_LOG_WELCOME_MSG(msg)
    #define BS_LOG_PRINT(...) 
    #define BS_LOG_PRINTLN(...)
    #define BS_LOG_PRINTF(...) 
    #define BS_LOG_HANDLE()
    #define BS_LOG_FLUSH()
#endif

// LED is connected to GPIO2 on these boards
#ifdef esp32
    #define INIT_LED { pinMode(2, OUTPUT); digitalWrite(2, LOW); }
    #define LED_ON   { digitalWrite(2, HIGH); }
    #define LED_OFF  { digitalWrite(2, LOW); }
#else
    #define INIT_LED { pinMode(2, OUTPUT); digitalWrite(2, HIGH); }
    #define LED_ON   { digitalWrite(2, LOW); }
    #define LED_OFF  { digitalWrite(2, HIGH); }
#endif

#include <Arduino.h>
#include <ArduinoOTA.h>
#include "time.h"

#include <Wire.h>
#include <SPI.h>
#include <mutex>

#define WATCHDOG_TIMEOUT_S 15

#ifdef esp32
    #include <WiFi.h>
    #include <AsyncTCP.h>

    #define WIFI_DISCONNECTED WIFI_EVENT_STA_DISCONNECTED

    static hw_timer_t* watchDogTimer = NULL;

#else
    #define USING_TIM_DIV256 true
    #define WIFI_DISCONNECTED WIFI_EVENT_STAMODE_DISCONNECTED
    #define FILE_READ "r"
    #define FILE_WRITE "w"
    #define FILE_APPEND "a"

    #include <ESP8266Wifi.h>
    #include <ESPAsyncTCP.h>
    #include "ESP8266TimerInterrupt.h"

    static volatile bool timer_pinged;
#endif

#include <DNSServer.h>
#include <EEPROM.h>
#include "LittleFS.h"

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

#define EEPROM_SIZE 4096
#define HOSTNAME_LEN 32
#define WIFI_SSID_LEN 32
#define WIFI_SSID_PWD_LEN 64

#define DEFAULT_HOSTNAME            HOSTNAME

#define CFG_NOT_SET                 0x0
#define CFG_SET                     0x9

typedef unsigned char tiny_int;

typedef struct config_type {
    tiny_int hostname_flag;
    char hostname[HOSTNAME_LEN];
    tiny_int ssid_flag;
    char ssid[WIFI_SSID_LEN];
    tiny_int ssid_pwd_flag;
    char ssid_pwd[WIFI_SSID_PWD_LEN];
} CONFIG_TYPE;

class Bootstrap {
    public:
        #ifdef BS_USE_TELNETSPY
            Bootstrap(String project_name, TelnetSpy *spy, long serial_baud_rate=1500000);
            void setExtraRemoteCommands(std::function<void(char c)> callable);
            const String builtInRemoteCommandsMenu = "\n\nCommands:\n\nC = Current Timestamp\nD = Disconnect WiFi\nF = Filesystem Info\nS - Set SSID / Password\nL = Reload Config\nW = Wipe Config\nX = Close Session\nR = Reboot ESP\n";
        #else
            Bootstrap(String project_name);
        #endif

        void setup();
        void loop();
        void watchDogRefresh();

        void setConfigSize(const short size);
        void cfg(void *cfg, short size);
        char* cfg();
        
        void wireConfig();
        void wipeConfig();

        void updateConfigItem(const String item, String value);
        void updateExtraConfigItem(std::function<void(const String item, String value)> callable);

        void saveConfig();
        void saveExtraConfig(std::function<void()> callable);

        void wireWebServerAndPaths();

        void requestReboot();
        void updateSetupHtml();
        void updateIndexHtml();

        void updateHtmlTemplate(String template_filename, bool show_time = true);
        void updateExtraHtmlTemplateItems(std::function<void(String *html)> callable);
        
        void blink();
        String getTimestamp();
        void setActiveAP();

        WiFiMode_t wifimode = WIFI_AP;
        int wifistate = WIFI_EVENT_MAX;

    private:
        void wireLittleFS();
        void wireWiFi();
        void wireArduinoOTA();
        void wireElegantOTA();
        const char* getHttpMethodName(const WebRequestMethodComposite method);

        #ifdef BS_USE_TELNETSPY
            void checkForRemoteCommand();
            long _serial_baud_rate;
            std::function<void(char c)> setExtraRemoteCommandsCallback = NULL;
        #endif

        String _project_name;

        SemaphoreHandle_t bs_mutex = xSemaphoreCreateMutex();

        DNSServer dnsServer;
        const byte DNS_PORT = 53;

        CONFIG_TYPE base_config;
        short config_size = sizeof(base_config);
        char config[EEPROM_SIZE];

        bool esp_reboot_requested;
        bool ap_mode_activity;
        bool setup_needs_update;
        bool index_needs_update;

        std::function<void(const String item, String value)> updateExtraConfigItemCallback = NULL;
        std::function<void()> saveExtraConfigCallback = NULL;
        std::function<void(String *html)> updateExtraHtmlTemplateItemsCallback = NULL;

        #ifdef esp32
            static void IRAM_ATTR watchDogInterrupt();
        #else
            ESP8266Timer iTimer;
            static void IRAM_ATTR timerHandler();
        #endif
};
#endif