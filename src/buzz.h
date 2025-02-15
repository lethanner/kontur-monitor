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
    
    static const uint16_t bootOK = 900;
    static const uint16_t failed = 500;
}