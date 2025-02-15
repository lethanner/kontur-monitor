#define TONE_PIN 4
#define BTN_PIN 5
#define DETECT_PIN 9
#define LED_PIN 10

#include "ESP8266WiFi.h"
#include "TZ.h"
#include "src/VK.h"
#include "src/cretendials.h"
#include "src/buzz.h"

VKAPI vk(access_token, group_id, &Serial);
time_t lastChangeTime = 0;
bool openFlag = false;

// эксплуатируем кнопку без payload, просто проверяя её текст
// в данном случае делать payload нет никакого смысла
static const char* default_button =
 "{\"buttons\": [[{\"action\": {\"type\": \"text\", \"label\": \"Клуб открыт?\"}}]]}";

static const char* green = "🟢 Клуб открыт.";
static const char* red = "🔴 Клуб закрыт.";

void buzz(const uint16_t table[][2], const uint8_t length)
{
    for (uint8_t i = 0; i < length; i++) {
        tone(TONE_PIN, table[i][0], table[i][1]);
        delay(table[i][1]);
    }
    noTone(TONE_PIN);
}

void processEvent(JsonObjectConst event)
{
    Serial.println(F("[DEBUG] processEvent() called"));
    const char* type = event["type"];
    if (strcmp(type, "message_new") == 0) {
        uint32_t from_id = event["object"]["message"]["from_id"];
        const char* text = event["object"]["message"]["text"];
        //const char* payload = event["object"]["message"]["payload"];

        Serial.printf("[MESSAGE] From id%u: %s\r\n", from_id, text);
        //vk.sendMessage(from_id, "Привет! Говорит ESP8266.");

        if (strncmp(text, "Клуб открыт", 21) == 0 || strncmp(text, "клуб открыт", 21) == 0) {
            char reply[256];
            struct tm* _now = localtime(&lastChangeTime);

            snprintf(reply, 256, "%s\r\nИнформация актуальна на %i:%i:%i",
                     openFlag ? green : red, _now->tm_hour, _now->tm_min, _now->tm_sec);
            vk.sendMessage(from_id, reply, default_button);
        }
    }
}

IRAM_ATTR void toggleKonturState()
{
    openFlag = !openFlag;
    digitalWrite(LED_PIN, openFlag);
    lastChangeTime = time(nullptr);

    tone(TONE_PIN, openFlag ? Buzz::bootOK : Buzz::failed, 250);
    delay(250);  // от дребезга кнопки
}

void setup()
{
    pinMode(TONE_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BTN_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BTN_PIN), toggleKonturState, FALLING);
    buzz(Buzz::startup, 4);

    Serial.begin(74880);
    Serial.println(F("\r\nKontur monitoring system v.1.0\r\n"
                     "Made by Lethanner.\r\n"));

    // подключение к wi-fi
    Serial.print(F("[WiFi] Waiting for connection..."));
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(125);
    }
    Serial.println(WiFi.localIP());

    // синхронизация часов по NTP
    Serial.print(F("[NTP] Waiting for time..."));
    configTime(TZ_Europe_Samara, 0, "pool.ntp.org", "ntp0.ntp-servers.net");
    time_t now = time(nullptr);
    while (now < 1000000000) {
        now = time(nullptr);
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(250);
    }
    Serial.println(now);

    vk.init();
    vk.setIncomingMessagesCallback(&processEvent);

    toggleKonturState();
}

void loop() { vk.longPoll(); }
