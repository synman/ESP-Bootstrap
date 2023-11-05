/***************************************************************************
Copyright © 2023 Shell M. Shrader <shell at shellware dot com>
----------------------------------------------------------------------------
This work is free. You can redistribute it and/or modify it under the
terms of the Do What The Fuck You Want To Public License, Version 2,
as published by Sam Hocevar. See the COPYING file for more details.
****************************************************************************/
#include "Bootstrap.h"

AsyncWebServer server(80);

#ifdef BS_USE_TELNETSPY
    Bootstrap::Bootstrap(String project_name, TelnetSpy *spy, long serial_baud_rate) {
        SandT = spy;
        _project_name = project_name;
        _serial_baud_rate = serial_baud_rate;
    }
#else
    Bootstrap::Bootstrap(String project_name) {
        _project_name = project_name;
    }
#endif

void Bootstrap::setup() {
    INIT_LED;

    BS_LOG_WELCOME_MSG("\n" + _project_name + " - Press ? for a list of commands\n");
    BS_LOG_BEGIN(_serial_baud_rate);
    BS_LOG_PRINTLN("\n\n" + _project_name + " Start Up\n");

    wireConfig();
    wireLittleFS();
    wireWiFi();
    wireArduinoOTA();
    wireElegantOTA();
    wireWebServerAndPaths();

    // defer updating setup.html
    updateSetupHtml();

    // wire up our custom watchdog
#ifdef esp32
    watchDogTimer = timerBegin(2, 80, true);
    timerAttachInterrupt(watchDogTimer, &watchDogInterrupt, true);
    timerAlarmWrite(watchDogTimer, WATCHDOG_TIMEOUT_S * 1000000, false);
    timerAlarmEnable(watchDogTimer);
#else
    iTimer.attachInterruptInterval(WATCHDOG_TIMEOUT_S * 1000000, timerHandler);
#endif

    BS_LOG_PRINTLN("Watchdog started");
}

void Bootstrap::loop() {
    // handle TelnetSpy if BS_USE_TELNETSPY is defined
    BS_LOG_HANDLE();

    // handle a reboot request if pending
    if (esp_reboot_requested) {
        ElegantOTA.loop();
        delay(1000);
        BS_LOG_PRINTLN("\nReboot triggered. . .");
        BS_LOG_HANDLE();
        BS_LOG_FLUSH();
        ESP.restart();
        while (1) {} // will never get here
    }

    // captive portal if in AP mode
    if (wifimode == WIFI_AP) {
        dnsServer.processNextRequest();
    } else {
        if (wifistate == WIFI_DISCONNECTED) {
            // LOG_PRINTLN("sleeping for 180 seconds. . .");
            // for (tiny_int x = 0; x < 180; x++) {
            //   delay(1000);
            //   watchDogRefresh();
            // }
            BS_LOG_PRINTLN("\nRebooting due to no wifi connection");
            esp_reboot_requested = true;
            return;
        }

        // check for OTA
        ArduinoOTA.handle();
        ElegantOTA.loop();
    }

    // reboot if in AP mode and no activity for 5 minutes
    if (wifimode == WIFI_AP && !ap_mode_activity && millis() >= 300000UL) {
        BS_LOG_PRINTF("\nNo AP activity for 5 minutes -- triggering reboot");
        esp_reboot_requested = true;
    }

    if (setup_needs_update) {
        updateHtmlTemplate("/setup.template.html", false);
        setup_needs_update = false;
    }

    if (index_needs_update) {
        updateHtmlTemplate("/index.template.html", false);
        index_needs_update = false;
    }

    watchDogRefresh();
}

void Bootstrap::watchDogRefresh() {
#ifdef esp32
    timerWrite(watchDogTimer, 0);
#else
    if (timer_pinged) {
        timer_pinged = false;
        BS_LOG_PRINTLN("PONG");
        BS_LOG_FLUSH();
    }
#endif    
}

void Bootstrap::wireConfig() {
    memset(config, CFG_NOT_SET, EEPROM_SIZE);

    // configuration storage
    EEPROM.begin(EEPROM_SIZE);
    uint8_t* p = (uint8_t*)(config);
    for (short i = 0; i < config_size; i++) {
        *(p + i) = EEPROM.read(i);
    }
    EEPROM.end();

    memcpy(&base_config, config, sizeof(base_config));

    if (base_config.hostname_flag != CFG_SET) {
        strcpy(base_config.hostname, DEFAULT_HOSTNAME);
    }

    if (base_config.ssid_flag == CFG_SET) {
        if (String(base_config.ssid).length() > 0) wifimode = WIFI_STA;
    } else {
        memset(base_config.ssid, CFG_NOT_SET, WIFI_SSID_LEN);
        wifimode = WIFI_AP;
    }

    if (base_config.ssid_pwd_flag != CFG_SET) memset(base_config.ssid_pwd, CFG_NOT_SET, WIFI_SSID_PWD_LEN);

    BS_LOG_PRINTLN();
    BS_LOG_PRINTLN("        EEPROM size: [" + String(EEPROM_SIZE) + "]");
    BS_LOG_PRINTLN("        config size: [" + String(config_size) + "]\n");
    BS_LOG_PRINTLN("        config host: [" + String(base_config.hostname) + "] stored: " + (base_config.hostname_flag == CFG_SET ? "true" : "false"));
    BS_LOG_PRINTLN("        config ssid: [" + String(base_config.ssid) + "] stored: " + (base_config.ssid_flag == CFG_SET ? "true" : "false"));
    BS_LOG_PRINTLN("    config ssid pwd: [" + String(base_config.ssid_pwd_flag == CFG_SET ? "********] stored: " : "] stored: ") + String(base_config.ssid_pwd_flag == CFG_SET ? "true" : "false"));
}

void Bootstrap::setConfigSize(const short size) {
    config_size = size;
}

void Bootstrap::cfg(void *cfg, short size) {
    memcpy(config, cfg, size);
    memcpy(config, &base_config, sizeof(base_config));
    memcpy(cfg, config, size);

    config_size = size;
}

char* Bootstrap::cfg() {
    char cfg[config_size];
    memset(&cfg, CFG_NOT_SET, config_size);
    memcpy(&cfg, &config, config_size);

    return config;
}

void Bootstrap::updateConfigItem(const String item, String value) {
    if (item == "hostname") {
        memset(base_config.hostname, CFG_NOT_SET, HOSTNAME_LEN);
        if (value.length() > 0) {
            base_config.hostname_flag = CFG_SET;
        } else {
            base_config.hostname_flag = CFG_NOT_SET;
            value = DEFAULT_HOSTNAME;
        }
        value.toCharArray(base_config.hostname, HOSTNAME_LEN);
        return;
    }
    if (item == "ssid") {
        memset(base_config.ssid, CFG_NOT_SET, WIFI_SSID_LEN);
        if (value.length() > 0) {
            value.toCharArray(base_config.ssid, WIFI_SSID_LEN);
            base_config.ssid_flag = CFG_SET;
        } else {
            base_config.ssid_flag = CFG_NOT_SET;
        }
        return;
    }
    if (item == "ssid_pwd") {
        memset(base_config.ssid_pwd, CFG_NOT_SET, WIFI_SSID_PWD_LEN);
        if (value.length() > 0) {
            value.toCharArray(base_config.ssid_pwd, WIFI_SSID_PWD_LEN);
            base_config.ssid_pwd_flag = CFG_SET;
        } else {
            base_config.ssid_pwd_flag = CFG_NOT_SET;
        }
        return;
    }
    if (updateExtraConfigItemCallback != NULL) updateExtraConfigItemCallback(item, value);
}
void Bootstrap::updateExtraConfigItem(std::function<void(const String item, String value)> callable) {
    updateExtraConfigItemCallback = callable;    
}

void Bootstrap::saveConfig() {
    if (saveExtraConfigCallback != NULL) saveExtraConfigCallback();
    memcpy(&config, &base_config, sizeof(base_config));

    EEPROM.begin(EEPROM_SIZE);
    uint8_t* p = (uint8_t*)(&config);
    for (short i = 0; i < config_size; i++) {
        EEPROM.write(i, *(p + i));
    }
    EEPROM.commit();
    EEPROM.end();

    updateSetupHtml();
}
void Bootstrap::saveExtraConfig(std::function<void()> callable) {
    saveExtraConfigCallback = callable;    
}

void Bootstrap::wipeConfig() {
    memset(&config, CFG_NOT_SET, EEPROM_SIZE);
    memset(&base_config, CFG_NOT_SET, sizeof(base_config));
    strcpy(base_config.hostname, DEFAULT_HOSTNAME);
    
    EEPROM.begin(EEPROM_SIZE);
    uint8_t* p = (uint8_t*)(&config);
    for (unsigned long i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, *(p + i));
    }
    EEPROM.commit();
    EEPROM.end();

    BS_LOG_PRINTF("\nConfig wiped\n");
}

void Bootstrap::wireLittleFS() {
    // start and mount our littlefs file system
    if (!LittleFS.begin()) {
        BS_LOG_PRINTLN("\nAn Error has occurred while initializing LittleFS\n");
    } else {
        #ifdef BS_USE_TELNETSPY
            #ifdef esp32
                    const size_t fs_size = LittleFS.totalBytes() / 1000;
                    const size_t fs_used = LittleFS.usedBytes() / 1000;
            #else
                    FSInfo fs_info;
                    LittleFS.info(fs_info);
                    const size_t fs_size = fs_info.totalBytes / 1000;
                    const size_t fs_used = fs_info.usedBytes / 1000;
            #endif
            BS_LOG_PRINTLN();
            BS_LOG_PRINTLN("    Filesystem size: [" + String(fs_size) + "] KB");
            BS_LOG_PRINTLN("         Free space: [" + String(fs_size - fs_used) + "] KB");
            BS_LOG_PRINTLN("          Free Heap: [" + String(ESP.getFreeHeap()) + "] B");
        #endif
    }
}

void Bootstrap::wireWiFi() {
    // Connect to Wi-Fi network with SSID and password
    // or fall back to AP mode
    WiFi.persistent(false);
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
    WiFi.hostname(base_config.hostname);
    WiFi.mode(wifimode);

    #ifdef esp32
        static const WiFiEventId_t disconnectHandler = WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) 
            {
                if (!esp_reboot_requested) {
                    BS_LOG_PRINTLN("\nWiFi disconnected");
                    BS_LOG_FLUSH();
                    wifistate = WIFI_DISCONNECTED;
                }
            }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    #else
        static const WiFiEventHandler disconnectHandler = WiFi.onStationModeDisconnected([this](WiFiEventStationModeDisconnected event)
            {
                if (!esp_reboot_requested) {
                    BS_LOG_PRINTF("\nWiFi disconnected - reason: %d\n", event.reason);
                    BS_LOG_FLUSH();
                    wifistate = WIFI_DISCONNECTED;
                }
            });
    #endif

    // WiFi.scanNetworks will return the number of networks found
    uint8_t nothing = 0;
    uint8_t* bestBssid;
    bestBssid = &nothing;
    short bestRssi = SHRT_MIN;

    BS_LOG_PRINTLN("\nScanning Wi-Fi networks. . .");
    int n = WiFi.scanNetworks();

    // arduino is too stupid to know which AP has the best signal
    // when connecting to an SSID with multiple BSSIDs (WAPs / Repeaters)
    // so we find the best one and tell it to use it
    if (n > 0 ) {
        for (int i = 0; i < n; ++i) {
            BS_LOG_PRINTF("   ssid: %s - rssi: %d\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
            if (base_config.ssid_flag == CFG_SET && WiFi.SSID(i).equals(base_config.ssid) && WiFi.RSSI(i) > bestRssi) {
                bestRssi = WiFi.RSSI(i);
                bestBssid = WiFi.BSSID(i);
            }
        }
    }

    if (wifimode == WIFI_STA && bestRssi != SHRT_MIN) {
        wifistate = WIFI_EVENT_MAX;
        BS_LOG_PRINTF("\nConnecting to %s / %d dB ", base_config.ssid, bestRssi);
        WiFi.begin(base_config.ssid, base_config.ssid_pwd, 0, bestBssid, true);
        for (tiny_int x = 0; x < 120 && WiFi.status() != WL_CONNECTED; x++) {
            blink();
            BS_LOG_PRINT(".");
            if (wifistate == WIFI_DISCONNECTED) break;
        }

        BS_LOG_PRINTLN();

        if (WiFi.status() == WL_CONNECTED) {
            // initialize time
            configTime(0, 0, "pool.ntp.org");
            setenv("TZ", "EST+5EDT,M3.2.0/2,M11.1.0/2", 1);
            tzset();

            BS_LOG_PRINT("\nCurrent Time: ");
            BS_LOG_PRINTLN(getTimestamp());
        }
    }

    if (WiFi.status() != WL_CONNECTED || wifimode == WIFI_AP) {
        wifimode = WIFI_AP;
        WiFi.mode(wifimode);
        WiFi.softAP(base_config.hostname);
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        BS_LOG_PRINTLN("\nSoftAP [" + String(base_config.hostname) + "] started");
    }

    BS_LOG_PRINTLN();
    BS_LOG_PRINT("    Hostname: "); BS_LOG_PRINTLN(base_config.hostname);
    BS_LOG_PRINT("Connected to: "); BS_LOG_PRINTLN(wifimode == WIFI_STA ? base_config.ssid : base_config.hostname);
    BS_LOG_PRINT("  IP address: "); BS_LOG_PRINTLN(wifimode == WIFI_STA ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
    BS_LOG_PRINT("        RSSI: "); BS_LOG_PRINTLN(String(WiFi.RSSI()) + " dB");    
}

void Bootstrap::wireArduinoOTA() {
    ArduinoOTA.setHostname(HOSTNAME);

    ArduinoOTA.onStart([this]()
        {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
                type = "sketch";
            else // U_SPIFFS
                type = "filesystem";

            // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
            BS_LOG_PRINTLN("\nOTA triggered for updating " + type);
        });

    ArduinoOTA.onEnd([this]()
        {
            BS_LOG_PRINTLN("\nOTA End");
            BS_LOG_FLUSH();
            requestReboot();
        });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total)
        {
            watchDogRefresh();
            BS_LOG_PRINTF("Progress: %u%%\r", (progress / (total / 100)));
            BS_LOG_FLUSH();
        });

    ArduinoOTA.onError([this](ota_error_t error)
        {
            BS_LOG_PRINTF("\nError[%u]: ", error);
            if (error == OTA_AUTH_ERROR) BS_LOG_PRINTLN("Auth Failed");
            else if (error == OTA_BEGIN_ERROR) BS_LOG_PRINTLN("Begin Failed");
            else if (error == OTA_CONNECT_ERROR) BS_LOG_PRINTLN("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) BS_LOG_PRINTLN("Receive Failed");
            else if (error == OTA_END_ERROR) BS_LOG_PRINTLN("End Failed");
            BS_LOG_FLUSH();
        });

    ArduinoOTA.begin();
    BS_LOG_PRINTLN("\nArduinoOTA started");
}

void Bootstrap::wireElegantOTA() {
    ElegantOTA.onStart([this]() {
        BS_LOG_PRINTLN("\nOTA update started!");
    });
    ElegantOTA.onProgress([this](size_t current, size_t final) {
        static unsigned long ota_progress_millis = 0;
        if (millis() - ota_progress_millis > 1000) {
            watchDogRefresh();
            ota_progress_millis = millis();
            BS_LOG_PRINTF("OTA Progress Current: %u bytes, Final: %u bytes\r", current, final);
            BS_LOG_FLUSH();
        }
    });
    ElegantOTA.onEnd([this](bool success) {
        if (success) {
            BS_LOG_PRINTLN("\nOTA update finished successfully!");
            requestReboot();
        } else {
            BS_LOG_PRINTLN("\nThere was an error during OTA update!");
        }
        BS_LOG_FLUSH();
    });

    ElegantOTA.begin(&server);
    BS_LOG_PRINTLN("ElegantOTA started");
}

void Bootstrap::wireWebServerAndPaths() {
    // define default document
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setActiveAP();
            AsyncWebServerResponse *response = request->beginResponse(301); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            response->addHeader("Location", "/index.html");
            request->send(response);
            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "redirected to /index.html");
        });

    // define setup document
    server.on("/setup", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setLockState(LOCK_STATE_LOCK);

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/setup.html", "text/html"); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);
        });

    // captive portal
    server.on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setActiveAP();

            setLockState(LOCK_STATE_LOCK);

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html"); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);
        });
    server.on("/library/test/success.html", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setActiveAP();

            setLockState(LOCK_STATE_LOCK);

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html"); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);
        });
    server.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setActiveAP();

            setLockState(LOCK_STATE_LOCK);

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html"); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);
        });
    server.on("/gen_204", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setActiveAP();

            setLockState(LOCK_STATE_LOCK);

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html"); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);
        });
    server.on("/ncsi.txt", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setActiveAP();

            setLockState(LOCK_STATE_LOCK);

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html"); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);
        });
    server.on("/check_network_status.txt", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setActiveAP();

            setLockState(LOCK_STATE_LOCK);

            AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html"); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);
        });

    // request reboot
    server.on("/reboot", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setLockState(LOCK_STATE_LOCK);

            AsyncWebServerResponse *response = request->beginResponse(302); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            response->addHeader("Location", "/index.html");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);

            requestReboot();
        });

    // save config
    server.on("/save", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setLockState(LOCK_STATE_LOCK);

            for (tiny_int i = 0; i < request->params(); i++) {
                updateConfigItem(request->getParam(i)->name(), request->getParam(i)->value());
            }

            saveConfig();

            AsyncWebServerResponse *response = request->beginResponse(302); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            response->addHeader("Location", "/index.html");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);
        });

    // load config
    server.on("/load", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setLockState(LOCK_STATE_LOCK);

            BS_LOG_PRINTLN();
            wireConfig();
            updateSetupHtml();

            AsyncWebServerResponse *response = request->beginResponse(302); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            response->addHeader("Location", "/index.html");
            request->send(response);

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);
        });

    // wipe config
    server.on("/wipe", HTTP_GET, [this](AsyncWebServerRequest* request)
        {
            setLockState(LOCK_STATE_LOCK);

            const boolean reboot = !request->hasParam("noreboot");

            AsyncWebServerResponse *response = request->beginResponse(302); 
            response->addHeader("Server", "ESP Async Web Server");
            response->addHeader("X-Powered-By", "ESP-Bootstrap");
            response->addHeader("Location", "/index.html");
            request->send(response);

            wipeConfig();

            BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), request->url().c_str(), "handled");

            setLockState(LOCK_STATE_UNLOCK);

            // trigger a reboot
            if (reboot) esp_reboot_requested = true;
        });

    // 404 (includes file handling)
    server.onNotFound([this](AsyncWebServerRequest* request)
        {
            setActiveAP();
            setLockState(LOCK_STATE_LOCK);

            String url = request->url(); url.toLowerCase();

            if (LittleFS.exists(request->url())) {
                AsyncWebServerResponse* response = request->beginResponse(LittleFS, request->url(), String());
                response->addHeader("Server", "ESP Async Web Server");
                response->addHeader("X-Powered-By", "ESP-Bootstrap");
    
                // only chache digital assets
                if (url.indexOf(".png") != -1 || url.indexOf(".jpg") != -1 || url.indexOf(".ico") != -1 || url.indexOf(".svg") != -1) {
                    response->addHeader("Cache-Control", "max-age=604800");
                } else {
                    response->addHeader("Cache-Control", "no-store");
                }

                request->send(response);

                BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), url.c_str(), "handled");
            } else {
                AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", request->url() + " not found!");
                response->addHeader("Server", "ESP Async Web Server");
                response->addHeader("X-Powered-By", "ESP-Bootstrap");
                request->send(response);
                
                BS_LOG_PRINTF("%s:%s: [%s] %s\n", request->client()->remoteIP().toString().c_str(), getHttpMethodName(request->method()), url.c_str(), "not found!");
            }

            setLockState(LOCK_STATE_UNLOCK);
        });

    // begin the web server
    server.begin();
    BS_LOG_PRINTLN("HTTP server started");
}

void Bootstrap::updateHtmlTemplate(String template_filename, bool show_time) {
    String output_filename = template_filename;
    output_filename.replace(".template", "");

    File _template = LittleFS.open(template_filename, FILE_READ);

    if (_template) {
        String html = _template.readString();
        _template.close();

        while (html.indexOf("{project_name}", 0) != -1) {
            html.replace("{project_name}", String(_project_name));
        }

        while (html.indexOf("{hostname}", 0) != -1) {
            html.replace("{hostname}", String(base_config.hostname));
        }

        while (html.indexOf("{ssid}", 0) != -1) {
            html.replace("{ssid}", String(base_config.ssid));
        }

        while (html.indexOf("{ssid_pwd}", 0) != -1) {
            html.replace("{ssid_pwd}", String(base_config.ssid_pwd));
        }

        if (html.indexOf("{timestamp}", 0) != 1) {
            String timestamp = getTimestamp();
            while (html.indexOf("{timestamp}", 0) != -1) {
                html.replace("{timestamp}", timestamp);
            }
            if (show_time)
                BS_LOG_PRINTLN("Timestamp   = " + timestamp);
        }

        if (html.indexOf("{ip_address}", 0) != 1) {
            const String ipAddr = wifimode == WIFI_STA ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
            while (html.indexOf("{ip_address}", 0) != -1) {
                html.replace("{ip_address}", ipAddr);
            }
        }

        if (html.indexOf("{chipset_icon}", 0) != 1) {
            #ifdef esp32
                const String iconFile = "/favicon-32x32.png";
            #else
                const String iconFile = "/esp8266.jpg";
            #endif
            while (html.indexOf("{chipset_icon}", 0) != -1) {
                html.replace("{chipset_icon}", iconFile);
            }
        }

        

        if (updateExtraHtmlTemplateItemsCallback != NULL) updateExtraHtmlTemplateItemsCallback(&html);

        setLockState(LOCK_STATE_LOCK);

        BS_LOG_PRINTF("----- rebuilding %s\n", output_filename.c_str());
        
        File _index = LittleFS.open(output_filename, FILE_WRITE);
        _index.print(html.c_str());
        _index.close();

        BS_LOG_PRINTF("----- %s rebuilt\n", output_filename.c_str());

        setLockState(LOCK_STATE_UNLOCK);
    }
}

void Bootstrap::updateExtraHtmlTemplateItems(std::function<void(String *html)> callable) {
    updateExtraHtmlTemplateItemsCallback = callable;
}

const char* Bootstrap::getHttpMethodName(const WebRequestMethodComposite method) {
    // typedef enum {
    // HTTP_GET     = 0b00000001,
    // HTTP_POST    = 0b00000010,
    // HTTP_DELETE  = 0b00000100,
    // HTTP_PUT     = 0b00001000,
    // HTTP_PATCH   = 0b00010000,
    // HTTP_HEAD    = 0b00100000,
    // HTTP_OPTIONS = 0b01000000,
    // HTTP_ANY     = 0b01111111,
    // } WebRequestMethod;
    switch (method) {
        case HTTP_GET:
            return "GET";
        case HTTP_POST:
            return "POST";
        case HTTP_DELETE:
            return "DELETE";
        case HTTP_PUT:
            return "PUT";
        case HTTP_PATCH:
            return "PATCH";
        case HTTP_HEAD:
            return "HEAD";
        case HTTP_OPTIONS:
            return "OPTIONS";
        case HTTP_ANY:
            return "ANY";
        default:
            return "UNKNOWN";
    }
}

void Bootstrap::setLockState(tiny_int state) {
    switch (state) {
        case LOCK_STATE_LOCK:
            #ifdef esp32
                while (xSemaphoreTake(bs_mutex, portMAX_DELAY) != pdTRUE) {};
            #endif
            break;
        case LOCK_STATE_UNLOCK:
            #ifdef esp32
                xSemaphoreGive(bs_mutex); 
            #endif
            break;
        default:
            break;
    }
}

#ifdef BS_USE_TELNETSPY
    void Bootstrap::checkForRemoteCommand() {
        if (SandT->available() > 0) {
            char c = SandT->read();
            switch (c) {
            case '\n':   
                BS_LOG_PRINTLN();         
                break;
            case 'D':
                BS_LOG_PRINTLN("\nDisconnecting Wi-Fi. . .");
                BS_LOG_FLUSH();
                WiFi.disconnect();
                break;
            case 'F':
                {
                    #ifdef esp32
                        const size_t fs_size = LittleFS.totalBytes() / 1000;
                        const size_t fs_used = LittleFS.usedBytes() / 1000;
                    #else
                        FSInfo fs_info;
                        LittleFS.info(fs_info);
                        const size_t fs_size = fs_info.totalBytes / 1000;
                        const size_t fs_used = fs_info.usedBytes / 1000;
                    #endif
                    BS_LOG_PRINTLN("\n    Filesystem size: [" + String(fs_size) + "] KB");
                    BS_LOG_PRINTLN("         Free space: [" + String(fs_size - fs_used) + "] KB\n");
                }
            break;
            case 'S':
                {
                    const unsigned long startTime = millis();

                    BS_LOG_PRINT("\n    Type SSID and press <ENTER>: ");
                    BS_LOG_FLUSH();

                    String ssid;
                    do {
                        if (SandT->available() > 0) {
                            c = SandT->read();
                            if (c != 10 && c != 13) {
                                BS_LOG_PRINT(c);
                                BS_LOG_FLUSH();
                                ssid = ssid + String(c);
                            }
                        }
                        if (startTime + 30000 < millis()) {
                            BS_LOG_PRINTLN("\n\nTimed out!\n");
                            BS_LOG_FLUSH();
                            return;
                        }
                        watchDogRefresh();
                    } while (c != 13);

                    BS_LOG_PRINT("\nType PASSWORD and press <ENTER>: ");
                    BS_LOG_FLUSH();
                    String ssid_pwd;
                    do {
                        if (SandT->available() > 0) {
                            c = SandT->read();
                            if (c != 10 && c != 13) {
                                BS_LOG_PRINT("*");
                                BS_LOG_FLUSH();
                                ssid_pwd = ssid_pwd + String(c);
                            }
                        }
                        if (startTime + 30000 < millis()) {
                            BS_LOG_PRINTLN("\n\nTimed out!\n");
                            BS_LOG_FLUSH();
                            return;
                        }
                        watchDogRefresh();
                    } while (c != 13);

                    BS_LOG_PRINTLN("\n\nSSID=[" + ssid + "] PWD=[********]\n");
                    BS_LOG_FLUSH();

                    while (SandT->available() > 0) {
                        SandT->read();
                    }

                    BS_LOG_PRINT("Type YES to confirm settings: ");

                    do {
                        if (SandT->available() > 0) {
                            c = SandT->read();
                            if (c != 89) {
                                BS_LOG_PRINTLN("\n\nAborted!\n");
                                BS_LOG_FLUSH();
                                return;
                            }
                        }
                        if (startTime + 30000 < millis()) {
                            BS_LOG_PRINTLN("\n\nTimed out!\n");
                            BS_LOG_FLUSH();
                            return;
                        }
                        watchDogRefresh();
                    } while (c != 89);

                    BS_LOG_PRINT("Y");
                    BS_LOG_FLUSH();

                    do {
                        if (SandT->available() > 0) {
                            c = SandT->read();
                            if (c != 69) {
                                BS_LOG_PRINTLN("\n\nAborted!\n");
                                BS_LOG_FLUSH();
                                return;
                            }
                        }
                        if (startTime + 30000 < millis()) {
                            BS_LOG_PRINTLN("\n\nTimed out!\n");
                            BS_LOG_FLUSH();
                            return;
                        }
                        watchDogRefresh();
                    } while (c != 69);

                    BS_LOG_PRINT("E");
                    BS_LOG_FLUSH();

                    do {
                        if (SandT->available() > 0) {
                            c = SandT->read();
                            if (c != 83) {
                                BS_LOG_PRINTLN("\n\nAborted!\n");
                                BS_LOG_FLUSH();
                                return;
                            }
                        }
                        if (startTime + 30000 < millis()) {
                            BS_LOG_PRINTLN("\n\nTimed out!\n");
                            BS_LOG_FLUSH();
                            return;
                        }
                        watchDogRefresh();
                    } while (c != 83);

                    BS_LOG_PRINTLN("S");
                    BS_LOG_FLUSH();                    

                    memset(base_config.ssid, CFG_NOT_SET, WIFI_SSID_LEN);
                    if (ssid.length() > 0) {
                        ssid.toCharArray(base_config.ssid, WIFI_SSID_LEN);
                        base_config.ssid_flag = CFG_SET;
                    } else {
                        base_config.ssid_flag = CFG_NOT_SET;
                    }

                    memset(base_config.ssid_pwd, CFG_NOT_SET, WIFI_SSID_PWD_LEN);
                    if (ssid_pwd.length() > 0) {
                        ssid_pwd.toCharArray(base_config.ssid_pwd, WIFI_SSID_PWD_LEN);
                        base_config.ssid_pwd_flag = CFG_SET;
                    } else {
                        base_config.ssid_pwd_flag = CFG_NOT_SET;
                    }

                    memcpy(&config, &base_config, sizeof(base_config));

                    EEPROM.begin(EEPROM_SIZE);
                    EEPROM.put(0, base_config);
                    EEPROM.commit();
                    EEPROM.end();

                    BS_LOG_PRINTLN("\nSSID and Password saved - reload config or reboot\n");
                    BS_LOG_FLUSH();
                }
                break;
            case 'L':
                wireConfig();
                BS_LOG_PRINTLN();
                setup_needs_update = true;
                break;
            case 'W':
                wipeConfig();
                BS_LOG_PRINTLN();
                break;
            case 'X':
                BS_LOG_PRINTLN(F("\r\nClosing session..."));
                SandT->disconnectClient();
                break;
            case 'R':
                BS_LOG_PRINTLN(F("\r\nSubmitting reboot request..."));
                esp_reboot_requested = true;
                break;
            case ' ':
                // do nothing -- just a simple echo
                break;
            case 'C':
                // current time
                BS_LOG_PRINTF("Current timestamp: [%s]\n\n", getTimestamp().c_str());
                break;
            default:
                if (setExtraRemoteCommandsCallback != NULL) setExtraRemoteCommandsCallback(c);
                break;
            }
            SandT->flush();
        }
    }

    void Bootstrap::setExtraRemoteCommands(std::function<void(char c)> callable) {
        setExtraRemoteCommandsCallback = callable;
}
#endif

void Bootstrap::requestReboot() {
    esp_reboot_requested = true;
}
void Bootstrap::updateSetupHtml() {
    setup_needs_update = true;
}
void Bootstrap::updateIndexHtml() {
    index_needs_update = true;
}

void Bootstrap::blink() {
    LED_ON;
    delay(200);
    LED_OFF;
    delay(100);
    LED_ON;
    delay(200);
    LED_OFF;
}

String Bootstrap::getTimestamp() {
    struct tm timeinfo;
    char timebuf[255];

    if (wifimode == WIFI_AP || !getLocalTime(&timeinfo)) {
        const unsigned long now = millis();
        sprintf(timebuf, "%06lu.%03lu", now / 1000, now % 1000);
    } else {
        sprintf(timebuf, "%4d-%2.2d-%2.2d %2.2d:%2.2d:%2.2d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    return String(timebuf);
}

void Bootstrap::setActiveAP() {
    ap_mode_activity = true;
}

#ifdef esp32
    void IRAM_ATTR Bootstrap::watchDogInterrupt() {
        BS_LOG_PRINTLN("watchdog triggered reboot");
        BS_LOG_FLUSH();
        ESP.restart();
    }
#else
    void IRAM_ATTR Bootstrap::timerHandler() {
        if (timer_pinged) {
            BS_LOG_PRINTLN("watchdog triggered reboot");
            BS_LOG_FLUSH();
            ESP.restart();
        } else {
            timer_pinged = true;
            BS_LOG_PRINTLN("PING");
        }
    }
#endif