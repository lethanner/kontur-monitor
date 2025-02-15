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

// отправка сообщения юзеру с ИД peer_id
int VKAPI::sendMessage(uint32_t peer_id, const char* text, const char* keyboard)
{
    if (!apiStart("messages.send", strlen(text) + strlen(keyboard) + countDigits(peer_id) + 9 + 21 + 10))
        return -1;

    api.print(F("&random_id=0&peer_id="));
    api.print(peer_id);
    api.print(F("&keyboard="));
    api.print(keyboard);
    api.print(F("&message="));
    api.print(text);

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

// начать GET/POST запрос
bool VKAPI::apiStart(const char* method, uint16_t cLength)
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

    api.print(cLength > 0 ? "POST" : "GET");
    api.print(" /method/");
    api.print(method);

    // для POST запроса вызываем сразу функцию apiEnd
    if (cLength > 0) apiEndGET(cLength);
    else {  // для GET запроса сразу передаём access token
        api.print(F("?v=5.199&access_token="));
        api.print(token);
    }

    return true;
}

// закончить начало запроса
void VKAPI::apiEndGET(uint16_t cLength)
{
    api.print(F(" HTTP/1.1\r\n"
                "Host: api.vk.com\r\n"
                "User-Agent: Espressif\r\n"
                "Connection: Keep-alive"));

    // если это POST запрос, добавим нужные заголовки
    if (cLength > 0) {
        api.print(F("\r\nContent-Type: application/x-www-form-urlencoded"
                    "\r\nContent-Length: "));
        api.print(cLength + default_params_length);
    }
    api.print("\r\n\r\n");
    api.print(F("v=5.199&access_token="));
    api.print(token);
}

bool VKAPI::updateLongPoll()
{
    debug->println(F("[INFO] Getting new Long Poll server..."));
    memset(lprequest, '\0', 300);
    strcpy(lprequest, "GET ");

    apiStart("groups.getLongPollServer");
    api.print("&group_id=");
    api.print(group_id);
    apiEndGET();

    char* response = readHTTPResponse(api);
    if (response == NULL) {
        debug->println(F("failed!"));
        return false;
    }
    debug->print(F("[INFO] Long Poll server successfully got\r\n[DEBUG] "));
    debug->println(response);

    StaticJsonDocument<64> new_lp;
    deserializeJson(new_lp, response);

    // P.S. однажды ВК поменяет адрес сервера Long Poll и эта "оптимизация" даст о себе знать.
    if (strncmp(new_lp["response"]["server"], "https://lp.vk.com", 17) != 0) return false;

    const char* server = new_lp["response"]["server"];
    strcat(lprequest, (server + 17));  // срезать https://lp.vk.com из начала строки (с помощью сдвига указателя)
    strcat(lprequest, "?act=a_check&key=");
    strcat(lprequest, new_lp["response"]["key"]);
    strcat(lprequest, "&wait=25&ts=");
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

    if (strlen(lprequest) < 1) updateLongPoll();

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

    lp.print(lprequest);
    lp.print(ts);
    lp.print(F(" HTTP/1.1\r\n"
               "Host: lp.vk.com\r\n"
               "User-Agent: Espressif\r\n"
               "Connection: Keep-alive\r\n\r\n"));

    char* events = readHTTPResponse(lp);
    if (events == NULL) return false;
    debug->println(events);

    deserializeJson(eventsJson, events);
    // ошибки 2 или 3 - необходимость обновления ключа Long Poll
    if (eventsJson["failed"] == 2 || eventsJson["failed"] == 3) updateLongPoll();
    // в остальном требуется не забывать каждый раз обновлять значение ts
    else ts = eventsJson["ts"];

    for (JsonObjectConst event : eventsJson["updates"].as<JsonArrayConst>()) {
        lp_callback(event);
    }

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

    // выделяем память под принятые данные
    char* received = new char[cLength + 1];
    for (uint16_t i = 0; i < cLength; i++) {
        while (!str.available())
            yield();
        received[i] = str.read();
    }

    // нуль-терминатор в конец строки
    received[cLength] = '\0';
    return received;
}

// на что только не пойдёшь ради избегания преобразования числа в строку для измерения его длины...
uint8_t VKAPI::countDigits(uint32_t number)
{
    uint8_t count = 0;
    while (number != 0) {
        number = number / 10;
        count++;
    }
    return count;
}