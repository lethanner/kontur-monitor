#include "VK.h"
#include "certs.h"
#include <WiFiClientSecure.h>

DynamicJsonDocument eventsJson(2048);
X509List globalsign(globalsign_root);
WiFiClientSecure api;
WiFiClientSecure lp;

void VKAPI::init()
{
    api.setTrustAnchors(&globalsign);
    lp.setTrustAnchors(&globalsign);
    skipHistory = true;

    // применить оптимизации, если возможно
    if (api.probeMaxFragmentLength("api.vk.com", 443, 512)) {
        api.setBufferSizes(512, 512);
        debug->println(F("[INFO] api.vk.com supporting MFLN."));
    }
    if (lp.probeMaxFragmentLength("lp.vk.com", 443, 512)) {
        lp.setBufferSizes(512, 512);
        debug->println(F("[INFO] lp.vk.com supporting MFLN."));
    }
}

bool VKAPI::apiRequest(Method m, const char* method, const char* data)
{
    if (!api.connected()) {
        debug->print(F("[INFO] Connecting to API..."));
        if (!api.connect("api.vk.com", 443)) {
            debug->println(F("failed!"));
            return false;
        }
        debug->print(F("connected"));
        debug->println(api.getMFLNStatus() ? F(" with MFLN!") : F("."));
    }
    api.setTimeout(1000);

    if (m == Method::GET) {
        api.printf("GET /method/%s?v=5.199&access_token="
                   "%s&%s HTTP/1.1\r\n"
                   "Host: api.vk.com\r\n"
                   "User-Agent: Espressif\r\n"
                   "Connection: Keep-alive\r\n\r\n",
                   method, token, data);
    } else {
        uint16_t cLength = strlen(data) + default_params_length;
        api.printf(
         "POST /method/%s HTTP/1.1\r\n"
         "Host: api.vk.com\r\n"
         "User-Agent: Espressif\r\n"
         "Connection: Keep-alive\r\n"
         "Content-Type: application/x-www-form-urlencoded\r\n"
         "Content-Length: %u\r\n\r\n"
         "v=5.199&access_token=%s&%s",
         method, cLength, token, data);
    }

    return true;
}

// отправка сообщения юзеру с ИД peer_id
int VKAPI::sendMessage(uint32_t peer_id, const char* text, const char* keyboard)
{
    // определиться с длиной запроса на сервер, выделить память
    uint16_t length = strlen(text) + strlen(keyboard) + 60;
    char* data = new char[length];
    if (data == NULL) return -1;

    // сформировать запрос, выслать на сервер
    snprintf(data, length, "random_id=0&peer_id=%u&keyboard=%s&message=%s", peer_id, keyboard, text);
    if (!apiRequest(Method::POST, "messages.send", data)) {
        delete[] data;
        return -1;
    }
    delete[] data;

    // получить результат
    int result = -1;
    char* response = readHTTPResponse(api);
    StaticJsonDocument<64> responseJson;
    deserializeJson(responseJson, response);

    if (responseJson.containsKey("response")) {
        result = responseJson["response"];
        debug->print(F("[INFO] Message sent: "));
        debug->println(result);
    } else {
        const char* error_msg = responseJson["error"]["error_msg"];
        int error_code = responseJson["error"]["error_code"];
        debug->print(F("[ERROR] Failed to send message: "));
        debug->print(error_code);
        debug->println(error_msg);
    }

    delete[] response;
    return result;
}

bool VKAPI::updateLongPoll()
{
    debug->println(F("[INFO] Requesting new Long Poll server..."));
    memset(lprequest, '\0', 300);

    char request[21];
    snprintf(request, 21, "group_id=%u", group_id);
    if (!apiRequest(Method::GET, "groups.getLongPollServer", request)) return false;

    char* response = readHTTPResponse(api);
    if (response == NULL) return false;

    debug->print(F("[DEBUG] "));
    debug->println(response);

    StaticJsonDocument<64> new_lp;
    deserializeJson(new_lp, response);

    // P.S. однажды ВК поменяет адрес сервера Long Poll и эта "оптимизация" даст о себе знать.
    // тут крч проверяется, начинается ли полученный адрес сервера с (видно чего)
    if (strncmp(new_lp["response"]["server"], "https://lp.vk.com", 17) != 0) {
        debug->print(F("[ERROR] Invalid server information received. Got: "));
        debug->println(response);

        // если это косяк именно нового адреса, но при этом ответ от сервера нормальный, то
        // блокируем дальнейшую работу до обновления ПО
        // P.S. я надеюсь, что я нормально сделаю автоподстройку адреса раньше, чем ВК что-то обновит...
        if (new_lp.containsKey("response")) {
            debug->println(F("[ERROR] Software update required. System halted."));
            while (1) { yield(); }
        }
        return false;
    }

    const char* server = new_lp["response"]["server"];
    const char* key = new_lp["response"]["key"];
    // сформировать запрос, срезая https://lp.vk.com из начала строки (с помощью сдвига указателя)
    snprintf(lprequest, 300, "GET %s?act=a_check&key=%s&wait=25&ts=", server + 17, key);
    ts = new_lp["response"]["ts"];

    debug->print(F("[DEBUG] "));
    debug->println(lprequest);

    delete[] response;
    return true;
}

bool VKAPI::longPoll()
{
    debug->print(F("[DEBUG] Free heap: "));
    debug->println(ESP.getFreeHeap());

    if (strlen(lprequest) < 1) return updateLongPoll();

    if (!lp.connected()) {
        debug->print(F("[INFO] Connecting to Long Poll server..."));
        if (!lp.connect("lp.vk.com", 443)) {
            debug->println(F("failed!"));
            return false;
        }
        debug->print(F("connected"));
        debug->println(lp.getMFLNStatus() ? F(" with MFLN!") : F("."));
    }
    lp.setTimeout(30000);

    // эта часть кода запускается не реже раза в 25 секунд
    // тут я использовать printf не рискну уже
    lp.print(lprequest);
    lp.print(ts);
    lp.println(F(" HTTP/1.1\r\n"
                 "Host: lp.vk.com\r\n"
                 "User-Agent: Espressif\r\n"
                 "Connection: Keep-alive\r\n"));

    char* events = readHTTPResponse(lp);
    if (events == NULL) return false;
    debug->println(events);

    deserializeJson(eventsJson, events);
    // ошибки 2 или 3 - необходимость обновления ключа Long Poll
    if (eventsJson["failed"] == 2 || eventsJson["failed"] == 3) return updateLongPoll();
    // в остальном требуется не забывать каждый раз обновлять значение ts
    else {
        if (!eventsJson.containsKey("ts")) {
            debug->println(F("[ERROR] Something went wrong... can't get new TS value"));
            delete[] events;

            return false;
        }
        ts = eventsJson["ts"];
    }

    if (!skipHistory) {
        for (JsonObjectConst event : eventsJson["updates"].as<JsonArrayConst>()) {
            lp_callback(event);
        }
    } else if (eventsJson["failed"] != 1) skipHistory = false;

    delete[] events;
    return true;
}

char* VKAPI::readHTTPResponse(Stream& str)
{
    // если ответ сервера не 200 - F
    if (!str.find("200 OK")) {
        debug->println(F("[ERROR] Request failed!!"));
        return NULL;
    }

    uint16_t cLength = 0;
    // ищем в заголовках инфу об объеме информации, которую предстоит получить
    // если такой нет - F
    if (str.find("Content-Length: ")) cLength = str.parseInt();
    else {
        debug->println(F("[ERROR] No Content-Length header!!"));
        return NULL;
    }
    // пролистываем stream до конца http-заголовков, если такого нет - F
    if (!str.find("\r\n\r\n")) {
        debug->println(F("[ERROR] No end of headers!"));
        return NULL;
    }

    // выделяем буфер под принятые данные
    char* received = new char[cLength + 1];
    if (received == NULL) {
        debug->println(F("[ERROR] Can't allocate memory for response..."));
        return NULL;
    }
    // .. и сохраняем их туда
    for (uint16_t i = 0; i < cLength; i++) {
        while (!str.available()) yield();
        received[i] = str.read();
    }

    // нуль-терминатор в конец строки
    received[cLength] = '\0';
    return received;
}