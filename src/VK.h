#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

typedef void (*IncomingCallback)(JsonObjectConst);

class VKAPI
{
  public:
    VKAPI(const char* access_token, uint32_t groupID, Stream* debugstr)
      : token(access_token), group_id(groupID), debug(debugstr)
    {
        default_params_length = strlen(access_token) + 22;
    };
    void init();
    bool longPoll();
    int sendMessage(uint32_t peer_id, const char* text, const char* keyboard = "{\"buttons\": []}");
    void setIncomingMessagesCallback(IncomingCallback callback)
    {
        lp_callback = callback;
    }

  private:
    //bool apiStart(const char* method, uint16_t cLength = 0);
    //void apiEndGET(uint16_t cLength = 0);
    enum class Method { GET, POST };
    bool apiRequest(Method m, const char* method, const char* args);
    bool updateLongPoll();

    char* readHTTPResponse(Stream& str);

    const char* token;
    const uint32_t group_id;
    uint16_t default_params_length;
    IncomingCallback lp_callback = NULL;

    // кэш формата запроса к long poll api
    char lprequest[300] = "\0";
    uint32_t ts;

    Stream* debug;
};