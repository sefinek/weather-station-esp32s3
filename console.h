#pragma once
#include <Print.h>
#include <WString.h>

class StringPrint : public Print {
public:
    String buf;
    using Print::write;
    size_t write(uint8_t c) override {
        buf += (char)c;
        return 1;
    }
};

void consolePrintWelcome(Print &out);
void consoleHandleLine(const String &line, Print &out);
void consoleHandleSerial();
bool consoleAuth(bool &authed, const String &line, Print &out);
void consoleProcessLine(bool &authed, const String &line, Print &out);
void consoleTick();
