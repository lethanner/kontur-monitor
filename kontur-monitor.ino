#include "ESP8266WiFi.h"
#include "TZ.h"
#include "src/VK.h"
#include "src/cretendials.h"
#include "src/buzz.h"
#include "src/pins.h"

VKAPI vk(access_token, group_id, &Serial);
//time_t lastChangeTime = 0;
bool openFlag = false;
volatile bool buttonPress = false;

// эксплуатируем кнопку без payload, просто проверяя её текст
// в данном случае делать payload нет никакого смысла
static const char* default_button =
 "{\"buttons\": [[{\"action\": {\"type\": \"text\", \"label\": \"Клуб открыт?\"}}]]}";

static const char* green = "🟢 Да, клуб открыт! Приходи.";
static const char* red = "🔴 Клуб закрыт. :(";

// счётчики ошибок
byte fail_counter[2];

void buzz(const uint16_t table[][2], const uint8_t length)
{
    for (uint8_t i = 0; i < length; i++) {
        tone(TONE_PIN, table[i][0], table[i][1]);
        delay(table[i][1]);
    }
    noTone(TONE_PIN);
}

void terminate()
{
    detachInterrupt(digitalPinToInterrupt(BTN_PIN));
    digitalWrite(LED_PIN, 0);
    // после неудачных попыток восстановления связи следует замораживание системы
    // с периодическим звуковым сигналом. спустя 5 минут - автоперезагрузка
    Serial.println(F("[ERROR] Communications epic fail. System halted."));
    byte autoRestartCounter = 0;
    while (1) {
        if (++autoRestartCounter > 100) ESP.restart();
        Buzz::sos();
        delay(3000);
    }
}

void processEvent(JsonObjectConst event)
{
    Serial.println(F("[DEBUG] processEvent() called"));
    const char* type = event["type"];
    if (strcmp(type, "message_new") == 0) {
        uint32_t from_id = event["object"]["message"]["from_id"];
        uint32_t peer_id = event["object"]["message"]["peer_id"];
        const char* text = event["object"]["message"]["text"];
        //const char* payload = event["object"]["message"]["payload"];

        Serial.printf("[MESSAGE] From id%u: %s\r\n", from_id, text);

        // ответ на сообщение "клуб открыт?"
        // PS: фиг там, а не strcasecmp - ибо юникод
        if (strstr(text, "Клуб открыт") != NULL || strstr(text, "клуб открыт") != NULL) {
            //time_t diff = time(nullptr) - lastChangeTime;
            vk.sendMessage(peer_id, openFlag ? green : red, default_button);
        }
        // ответ на запрос состояния бота
        else if (strcmp(text, "/status") == 0) {
            char reply[128];
            snprintf_P(
             reply, 256,
             PSTR("uptime: %u s\r\n"
                  "wi-fi rssi: %i dBm\r\n"
                  "wi-fi errors: %u\r\n"
                  "request errors: %u\r\n"
                  "free heap: %u bytes\r\n"),
             millis() / 1000, WiFi.RSSI(), fail_counter[1], fail_counter[0], ESP.getFreeHeap());
            vk.sendMessage(peer_id, reply, default_button);
        }
        // команда для удалённой перезагрузки (только для админского чата)
        else if (strcmp(text, "/reboot") == 0 && peer_id == sa_dialog_id) {
            ESP.restart();
        }
    }
}

IRAM_ATTR void toggleKonturState()
{
    if (buttonPress) return;

    buttonPress = true;
    tone(TONE_PIN, openFlag ? Buzz::disabled : Buzz::enabled, 250);
}

void setup()
{
	rst_info* resetInfo;
    resetInfo = ESP.getResetInfoPtr();
    uint32_t startup_counter = millis();

    pinMode(TONE_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BTN_PIN, INPUT_PULLUP);
    buzz(Buzz::startup, 4);

    Serial.begin(74880);
    Serial.println(F("\r\nKontur monitoring system v.1.0\r\n"
                     "Made by Lethanner.\r\n"));

    // подключение к wi-fi
    Serial.print(F("[WiFi] Waiting for connection..."));
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
    uint16_t attempt_counter = 0;

    while (WiFi.status() != WL_CONNECTED) {
        // если установка связи с wi-fi затягивается на 30 секунд - периодический звуковой сигнал
        if (++attempt_counter > 240 && attempt_counter % 24 == 0) Buzz::warning();
        // если затянулась уже примерно на 5 минут - остановка
        // ПС: да, я не учёл задержку от функции warning()
        else if (attempt_counter > 2400) terminate();

        // мигание светодиодиком
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(125);
    }
    Serial.println(WiFi.localIP());

    attempt_counter = 0;
    // синхронизация часов по NTP
    Serial.print(F("[NTP] Waiting for time..."));
    configTime(TZ_Europe_Samara, 0, "pool.ntp.org", "ntp0.ntp-servers.net");
    time_t now = time(nullptr);
    while (now < 1000000000) {
        now = time(nullptr);
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        // аналогичные варнинги, как при подключении к вайфаю
        if (++attempt_counter > 120 && attempt_counter % 12 == 0) Buzz::warning();
        if (attempt_counter > 1200) terminate();
        delay(250);
    }
    Serial.println(now);

    vk.init();
    vk.setIncomingMessagesCallback(&processEvent);

    // делаем пять попыток установить связь с сервером Long Poll
    // если не удаётся - всё, извините, halt
    for (byte i = 0; i < 5; i++) {
        if (vk.longPoll()) break;
        else if (i == 2) terminate();
        Buzz::warning();
    }

    // отправить админам сообщение об инициализации
    char startup_msg[100];
    snprintf_P(
     startup_msg, 100, PSTR("успешный запуск: %u мс\r\nmfln: %d/%d\r\nrst reason: %u"),
     millis() - startup_counter, vk.getApiMFLNStatus(), vk.getLpMFLNStatus(), resetInfo->reason
	);
    vk.sendMessage(sa_dialog_id, startup_msg);

    tone(TONE_PIN, Buzz::bootOK, 250);
    digitalWrite(LED_PIN, false);
    //lastChangeTime = time(nullptr);

    attachInterrupt(digitalPinToInterrupt(BTN_PIN), toggleKonturState, FALLING);
}

void loop()
{
    static byte fail_count = 0, fail_flag = 0;

    // сообщения об ошибках
    if (fail_flag > 0 && fail_count == 0) {
        char msg[100];
        switch (fail_flag) {
        case 1:
            strcpy_P(msg, PSTR("боту стало худо, но он смог прекратить свои страдания."));
            break;
        case 2: strcpy_P(msg, PSTR("кратковременный сбой wi-fi")); break;
        }

        vk.sendMessage(sa_dialog_id, msg);
        fail_counter[fail_flag - 1]++;
        fail_flag = 0;
    }

    // спокойно опрашиваем сервер ВК на предмет новых сообщений.
    // если 5 подряд неудачных попыток связи - останавливаем работу
    if (!vk.longPoll()) {
        fail_flag = 1;
        Buzz::warning();
        if (++fail_count > 5) terminate();
    } else fail_count = 0;

    // если вдруг пропала связь с wi-fi - пищим и подмигиваем диодиком
    // а после 3 минут отсутствия связи - terminate
    while (WiFi.status() != WL_CONNECTED) {
        fail_flag = 2;
        delay(2000);
        if (++fail_count > 90) terminate();
        digitalWrite(LED_PIN, !openFlag);
        Buzz::warning();
        digitalWrite(LED_PIN, openFlag);
    }

    // обрабатываем кнопочку
    if (buttonPress) {
        openFlag = !openFlag;
        digitalWrite(LED_PIN, openFlag);
        //lastChangeTime = time(nullptr);

        buttonPress = false;
    }
}
