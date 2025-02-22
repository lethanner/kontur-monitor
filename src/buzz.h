#pragma once
#include "pins.h"

namespace Buzz {
    static const uint16_t startup[4][2] = {
        { 500,  100 },
        { 1000, 100 },
        { 1500, 100 },
        { 2000, 100 }
    };

    static const uint16_t reminder[4][2] = {
        { 1000, 100 },
        { 2000, 100 },
        { 1000, 100 },
        { 2000, 100 }
    };

    static const uint16_t ota_ready[2][2] = {
        { 3000, 400 },
        { 1000, 400 },
    };

    static const uint16_t bootOK = 900;
    static const uint16_t failed = 500;
    static const uint16_t disabled = 400;
    static const uint16_t enabled = 2500;

    void sos()
    {
        for (byte i = 0; i < 3; i++) {
            for (byte j = 0; j < 3; j++) {
                uint16_t duration = (i == 1) ? 150 : 50;
                tone(TONE_PIN, Buzz::failed, duration);
                delay(duration + 50);
            }
        }
    }

    void warning()
    {
        for (byte i = 0; i < 2; i++) {
            tone(TONE_PIN, Buzz::failed, 70);
            delay(140);
        }
    }
}  //namespace Buzz