#include "ESP8266WiFi.h"
#include "TZ.h"
#include "src/VK.h"
#include "src/cretendials.h"

VKAPI vk(access_token, group_id, &Serial);

void processEvent(JsonObjectConst event)
{
    Serial.println(F("[DEBUG] processEvent() called"));
    const char* type = event["type"];
    if (strcmp(type, "message_new") == 0) {
        uint32_t from_id = event["object"]["message"]["from_id"];
        const char* text = event["object"]["message"]["text"];

        Serial.printf("[MESSAGE] From id%u: %s\r\n", from_id, text);
        vk.sendMessage(from_id, "Привет! Говорит ESP8266.");
    }
}

void setup()
{
    Serial.begin(74880);
    Serial.println(F("\r\nKontur monitoring system v.1.0\r\n"
                     "Made by Lethanner.\r\n"));

    // подключение к wi-fi
    Serial.print(F("[WiFi] Waiting for connection"));
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print('.');
    }
    Serial.println(WiFi.localIP());

    // синхронизация часов по NTP
    Serial.print(F("[NTP] Waiting for time"));
    configTime(TZ_Europe_Samara, 0, "pool.ntp.org", "ntp0.ntp-servers.net");
    time_t now = time(nullptr);
    while (now < 1000000000) {
        now = time(nullptr);
        Serial.print('.');
        delay(1000);
    }
    Serial.println(now);

    vk.init();
    vk.setIncomingMessagesCallback(&processEvent);
}

void loop()
{
    vk.longPoll();
}
