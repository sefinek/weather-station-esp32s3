#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "eth_ota.h"

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3,0,0)
  #include <ETHClass2.h>
  #define ETH ETH2
#else
  #include <ETH.h>
#endif

#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class NetLogger {
private:
    static constexpr uint8_t  MAX_AUTH_ATTEMPTS   = NL_MAX_ATTEMPTS;
    static constexpr uint32_t LOCKOUT_TIME_MS     = NL_LOCKOUT_TIME * 1000;
    static constexpr size_t   MAX_PASSWORD_LENGTH = 64;
    static constexpr size_t   OUTPUT_BUFFER_SIZE  = 1024;

    WiFiServer*   server_ = nullptr;
    WiFiClient    client_;
    bool          started_ = false;
    bool          log_uart_ = NL_LOG_UART;
    bool          log_telnet_ = NL_LOG_TELNET;
    bool          crlf_ = NL_CRLF;
    bool          auth_ok_ = false;
    uint8_t       failed_attempts_ = 0;
    uint32_t      last_fail_time_ = 0;

    enum AuthState { AUTH_NONE, AUTH_WAITING_PASSWORD, AUTH_PROCESSING };
    AuthState      auth_state_ = AUTH_NONE;
    String         password_buffer_ = "";
    uint32_t       password_start_time_ = 0;

    SemaphoreHandle_t mtx_ = nullptr;

    NetLogger();
    ~NetLogger();
    NetLogger(const NetLogger&) = delete;
    NetLogger& operator=(const NetLogger&) = delete;

    static void sha512_hex(const char* data, size_t len, char out_hex[129]);
    static bool secure_compare(const char* a, const char* b, size_t len);
    void sendTelnetOptions();
    void handleNewClient();
    bool readPasswordNonBlocking();
    void processAuthentication();
    int idf_vprintf(const char* fmt, va_list ap);
    void writeOutput(const uint8_t* data, size_t len);
    void processCommand(const String& cmd);
    void handleSerial();
    void readClientCommand();

public:
    static NetLogger& i();

    bool begin(uint16_t port);
    void stop();
    void handle();

    void setLogUART(bool on);
    void setLogTelnet(bool on);
    void setCRLF(bool on);
    bool isLogUART() const;
    bool isLogTelnet() const;
    bool isCRLF() const;
    bool isConnected();

    void print(const char* s);
    void println(const char* s);
    void print(const String& s);
    void println(const String& s);
    void printf(const char* fmt, ...);
    void print(const __FlashStringHelper* s);
    void println(const __FlashStringHelper* s);
    void print_P(PGM_P s);
    void println_P(PGM_P s);
    void printf_PGM(PGM_P fmt, ...);

    static int idf_vprintf_trampoline(const char* fmt, va_list ap);
};

#define LOGF(...)        NetLogger::i().printf(__VA_ARGS__)
#define LOGLN(s)         NetLogger::i().println(s)
#define LOG(s)           NetLogger::i().print(s)
#define LOGF_P(fmt, ...) NetLogger::i().printf_PGM((PGM_P)(fmt), ##__VA_ARGS__)
#define LOGLN_P(s)       NetLogger::i().println_P((PGM_P)(s))
#define LOG_P(s)         NetLogger::i().print_P((PGM_P)(s))
