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
    mfln_status[0] = api.probeMaxFragmentLength("api.vk.com", 443, 512);
    mfln_status[1] = lp.probeMaxFragmentLength("lp.vk.com", 443, 512);
    if (mfln_status[0]) {
        api.setBufferSizes(512, 512);
        debug->println(F("[INFO] api.vk.com supporting MFLN."));
    }
    if (mfln_status[1]) {
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
        free(data);
        return -1;
    }
    free(data);

    // получить результат
    int result = -1;
    char* response = readHTTPResponse(api);
    if (response == NULL) {
        api.stop();
        return result;
    }

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

    free(response);
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
    if (response == NULL) {
        api.stop();
        return false;
    }

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

    free(response);
    return true;
}

bool VKAPI::longPoll()
{
    // debug->print(F("[DEBUG] Free heap: "));
    // debug->println(ESP.getFreeHeap());

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
    if (events == NULL) {
        lp.stop();
        return false;
    }
    debug->println(events);

    deserializeJson(eventsJson, events);
    // ошибки 2 или 3 - необходимость обновления ключа Long Poll
    if (eventsJson["failed"] == 2 || eventsJson["failed"] == 3) return updateLongPoll();
    // в остальном требуется не забывать каждый раз обновлять значение ts
    else {
        if (!eventsJson.containsKey("ts")) {
            debug->println(F("[ERROR] Something went wrong... can't get new TS value"));
            free(events);

            return false;
        }
        // так как ВК иногда отдаёт старое значение ts при обновлении сервера LP,
        // пропускаем обработку событий при аномальной разности между текущим и новым ts
        uint32_t new_ts = eventsJson["ts"];
        if (eventsJson["failed"] != 1 && (new_ts - ts < 2)) {
            for (JsonObjectConst event : eventsJson["updates"].as<JsonArrayConst>()) {
                lp_callback(event);
            }
        } else debug->println(F("[WARNING] Skipping updates history."));
        ts = new_ts;
    }

    free(events);
    return true;
}

bool VKAPI::streamTimedWait(Stream& str)
{
    uint32_t timeout = millis();
    while (!str.available()) {
        if ((millis() - timeout) > 2000) {
            debug->println(F("[ERROR] Timed out waiting for available data."));
            return false;
        }
    }
    return true;
}

char* VKAPI::readHTTPResponse(Stream& str)
{
    // если ответ сервера не 200 - F
    if (!str.find("200 OK\r\n")) {
        debug->println(F("[ERROR] Request failed!!"));
        return NULL;
    }

    byte encodingType = 0;
    uint16_t cLengthClassic = 0;
    // извлечение и парсинг заголовков
    while (1) {
        char header[128], _ch;
        uint8_t h_pos = 0;
        // чтение заголовка в буфер
        while (streamTimedWait(str) && (_ch = str.read()) != '\r') {
            if (h_pos < 127) header[h_pos++] = _ch;
        }
        // отсеивание переноса строки из потока перед приёмом следующего хедера
        if (str.read() != '\n') {
            debug->println(F("[ERROR] Headers processing failed"));
            return NULL;
        }
        header[h_pos] = '\0';

        // пустая строка - заголовки кончились
        if (h_pos == 0) break;
        //debug->print(F("[DEBUG] Header "));
        //debug->println(header);

        // ищем заголовки Content-Length или Transfer-Encoding
        // на основе этого определяем, в каком кодировании пришли данные
        // сразу узнаём полный объём данных в байтах, когда можем
        if (strncmp(header, "Content-Length:", 15) == 0) {
            cLengthClassic = atoi(header + 15);
            //debug->print(F("[DEBUG] Using classic encoding. Length: "));
            //debug->println(cLengthClassic);
            encodingType = 1;
        } else if (strncmp(header, "Transfer-Encoding: chunked", 26) == 0) {
            //debug->println(F("[DEBUG] Using chunked encoding."));
            encodingType = 2;
        }
    }

    // UPD 11.04.2025 - ВК резко сделал chunked encoding принудительным...
    // теперь я всё таки вынужден принимать их порциями и делать realloc
    char* received = NULL;
    /* Обработка chunked encoding */
    if (encodingType == 2) {
        uint16_t chunk_size;
        uint16_t cLength = 1, pos = 0;
        // цикл, т.к. чанков может быть несколько
        while (1) {
            char ch;
            chunk_size = 0;
            uint8_t chunk_count = 0, digit;
            // тело ответа начинается с инфы о размере чанка, извлекаем
            while (streamTimedWait(str) && (ch = str.read()) != '\r') {
                //debug->print(F("ch: "));
                //debug->println(ch, HEX);
                // инфа представлена в виде hex в строке, преобразовываем
                digit = 0;
                if (ch >= 'a' && ch <= 'f')
                    digit = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F')
                    digit = ch - 'A' + 10;
                else if (ch >= '0' && ch <= '9')
                    digit = ch - '0';
                else {
                    debug->print(F("[ERROR] Not a HEX symbol, got "));
                    debug->println(ch, HEX);
                    return NULL;
                }
                chunk_size = chunk_size * 16 + digit;
            }

            if (str.read() != '\n') { // пропуск LF
                debug->println(F("[ERROR] Timed out or no new line after chunk size."));
                return NULL;
            }
            debug->print(F("[DEBUG] Chunk size: "));
            debug->println(chunk_size);

            // чанки в ответе заканчиваются передачей 0 в размере
            if (chunk_size == 0) return received;

            cLength += chunk_size;
            if (chunk_count == 0) // первый чанк - выделяем память с нуля
                received = (char*)malloc(sizeof(char) * cLength);
            else // последующие чанки - дополняем выделенную память
                received = (char*)realloc(received, sizeof(char) * cLength);
            chunk_count++;

            if (received == NULL) {
                debug->println(F("[ERROR] Can't (re)allocate memory for response..."));
                return NULL;
            }
            
            char recv;
            // читаем данные в буфер до CR+LF (после которого передаётся размер след чанка)
            while (streamTimedWait(str) && (recv = str.read()) != '\r') {
                //debug->print(F("recv: "));
                //debug->println(recv, HEX);
                received[pos++] = recv;
            }
            received[pos] = '\0';
            debug->print(F("[DEBUG] Received data: "));
            debug->println(received);

            if (str.read() != '\n') { // пропуск LF
                debug->println(F("[ERROR] Timed out or no new line after received chunk."));
                return NULL;
            }
        };
    }
    /* Classic encoding (без чанков) */
    else if (encodingType == 1) {
        received = (char*)malloc(cLengthClassic + 1);
        if (received == NULL) {
            debug->println(F("[ERROR] Can't allocate memory for response..."));
            return NULL;
        }

        for (uint16_t i = 0; i < cLengthClassic; i++) {
            if (!streamTimedWait(str)) return NULL;
            received[i] = str.read();
        }
        received[cLengthClassic] = '\0';
        return received;
    }
    /* неизвестно какой encoding */
    else {
        debug->println(F("[ERROR] Unknown transfer encoding type"));
        return NULL;
    }
}

void VKAPI::stop()
{
    lp.stop();
    api.stop();
    debug->println(F("[INFO] Stopping VK API."));
}