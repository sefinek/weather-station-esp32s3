#include "logger_net.h"
#include <stdarg.h>
#include "esp_log.h"
#include "mbedtls/sha512.h"
#include <string.h>

NetLogger::NetLogger() = default;
NetLogger::~NetLogger() { stop(); }

NetLogger& NetLogger::i() { static NetLogger inst; return inst; }

void NetLogger::sha512_hex(const char* data, size_t len, char out_hex[129]) {
    unsigned char sha512_bin[64];
    mbedtls_sha512_context ctx;
    mbedtls_sha512_init(&ctx);
    mbedtls_sha512_starts(&ctx, 0);
    mbedtls_sha512_update(&ctx, (const unsigned char*)data, len);
    mbedtls_sha512_finish(&ctx, sha512_bin);
    mbedtls_sha512_free(&ctx);
    for (int i = 0; i < 64; ++i) sprintf(out_hex + i * 2, "%02x", sha512_bin[i]);
    out_hex[128] = 0;
}

bool NetLogger::secure_compare(const char* a, const char* b, size_t len) {
    if (!a || !b) return false;
    uint8_t res = 0;
    for (size_t i = 0; i < len; i++) res |= a[i] ^ b[i];
    return res == 0;
}

void NetLogger::sendTelnetOptions() {
    static const uint8_t telnet_options[] = {
        0xFF, 0xFD, 0x22,
        0xFF, 0xFB, 0x01,
        0xFF, 0xFB, 0x03
    };
    if (client_ && client_.connected()) {
        if (mtx_) xSemaphoreTake(mtx_, portMAX_DELAY);
        client_.write(telnet_options, sizeof(telnet_options));
        if (mtx_) xSemaphoreGive(mtx_);
    }
}

void NetLogger::handleNewClient() {
    if (client_ && client_.connected()) {
        static const char* msg = "New client connecting...\r\n";
        if (mtx_) xSemaphoreTake(mtx_, portMAX_DELAY);
        client_.write((const uint8_t*)msg, strlen(msg));
        if (mtx_) xSemaphoreGive(mtx_);
        client_.stop();
    }

    client_ = server_->available();
    if (client_ && client_.connected()) {
        client_.setNoDelay(true);
        client_.setTimeout(1000);
        auth_ok_ = false;
        auth_state_ = AUTH_WAITING_PASSWORD;
        password_buffer_.clear();
        password_start_time_ = millis();

        sendTelnetOptions();

        static const char* banner =
            "======= Weather Station Telnet - Welcome! =======\r\n"
            "Author: Sefinek <contact@sefinek.net> (https://sefinek.net)\r\n\r\n"
            "Password: ";
        if (mtx_) xSemaphoreTake(mtx_, portMAX_DELAY);
        client_.write((const uint8_t*)banner, strlen(banner));
        if (mtx_) xSemaphoreGive(mtx_);
    }
}

bool NetLogger::readPasswordNonBlocking() {
    if (auth_state_ != AUTH_WAITING_PASSWORD) return false;

    uint32_t now = millis();
    if (now - password_start_time_ > 15000) {
        client_.stop();
        auth_state_ = AUTH_NONE;
        return false;
    }

    int available = client_.available();
    int processed = 0;
    const int MAX_CHARS_PER_CALL = 10;

    while (available > 0 && processed < MAX_CHARS_PER_CALL) {
        int c = client_.read();
        available--; processed++;

        if (c == 0xFF) {
            while (client_.available() && client_.read() != -1 && processed < MAX_CHARS_PER_CALL) processed++;
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (password_buffer_.length() > 0) {
                auth_state_ = AUTH_PROCESSING;
                return true;
            }
            continue;
        }

        if (c == 8 || c == 127) {
            if (password_buffer_.length() > 0) password_buffer_.remove(password_buffer_.length() - 1);
            continue;
        }

        if (c >= 32 && c <= 126 && password_buffer_.length() < MAX_PASSWORD_LENGTH) {
            password_buffer_ += (char)c;
        }

        password_start_time_ = now;
    }
    return false;
}

void NetLogger::processAuthentication() {
    if (auth_state_ != AUTH_PROCESSING) return;

    if (password_buffer_.length() == 0) {
        client_.stop();
        auth_state_ = AUTH_NONE;
        return;
    }

    char hash[129];
    sha512_hex(password_buffer_.c_str(), password_buffer_.length(), hash);

    if (secure_compare(hash, NL_PASSWD, 128)) {
        auth_ok_ = true;
        failed_attempts_ = 0;
        auth_state_ = AUTH_NONE;
        static const char* success = "OK\r\n> Type 'help' for commands\r\n\r\n";
        if (mtx_) xSemaphoreTake(mtx_, portMAX_DELAY);
        client_.write((const uint8_t*)success, strlen(success));
        if (mtx_) xSemaphoreGive(mtx_);
    } else {
        failed_attempts_++;
        last_fail_time_ = millis();
        auth_state_ = AUTH_NONE;
        static const char* fail = "Access Denied\r\n";
        if (mtx_) xSemaphoreTake(mtx_, portMAX_DELAY);
        client_.write((const uint8_t*)fail, strlen(fail));
        if (mtx_) xSemaphoreGive(mtx_);
        client_.stop();
    }

    password_buffer_.clear();
}

int NetLogger::idf_vprintf(const char* fmt, va_list ap) {
    if (!fmt) return 0;
    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0) writeOutput((const uint8_t*)buf, (size_t)strnlen(buf, sizeof(buf)));
    return n;
}

void NetLogger::writeOutput(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    if (mtx_) xSemaphoreTake(mtx_, portMAX_DELAY);

    if (log_uart_) Serial.write(data, len);

    if (log_telnet_ && client_ && client_.connected() && auth_ok_) {
        char out[OUTPUT_BUFFER_SIZE];
        size_t outlen = 0;
        for (size_t i = 0; i < len && outlen < OUTPUT_BUFFER_SIZE - 2; ++i) {
            uint8_t b = data[i];
            if (b >= 0x80 || (b < 0x20 && b != '\r' && b != '\n' && b != '\t')) b = '?';
            if (crlf_ && b == '\n' && (outlen == 0 || out[outlen - 1] != '\r')) out[outlen++] = '\r';
            out[outlen++] = (char)b;
        }
        if (outlen > 0) {
            size_t written = client_.write((const uint8_t*)out, outlen);
            if (written != outlen) client_.stop();
        }
    }

    if (mtx_) xSemaphoreGive(mtx_);
}

void NetLogger::processCommand(const String& cmd) {
    String command = cmd;
    command.trim();
    command.toLowerCase();
    if (command.length() == 0) return;

    if (command == "help") {
        println("Available commands:");
        println("  help     - Show this help");
        println("  sysinfo  - System information");
        println("  netinfo  - Network information");
        println("  status   - NetLogger status");
        println("  uptime   - System uptime");
        println("  reset    - Restart system");
    }
    else if (command == "sysinfo") {
        system_info_report();
    }
    else if (command == "netinfo") {
        netlogger_status_report();
    }
    else if (command == "status") {
        printf("NetLogger: %s, Ethernet: %s, Client: %s\n",
               started_ ? "RUNNING" : "STOPPED",
               eth_is_connected() ? "CONNECTED" : "DISCONNECTED",
               isConnected() ? "CONNECTED" : "NONE");
    }
    else if (command == "uptime") {
        unsigned long up = millis() / 1000;
        printf("Uptime: %lu days, %02lu:%02lu:%02lu\n",
               up / 86400, (up % 86400) / 3600, (up % 3600) / 60, up % 60);
    }
    else if (command == "reset") {
        println("Restarting system...");
        ESP.restart();
    }
    else {
        printf("Unknown command: %s (type 'help' for commands)\n", command.c_str());
    }
}

void NetLogger::handleSerial() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        processCommand(cmd);
    }
}

void NetLogger::readClientCommand() {
    static String buffer;
    int available = client_.available();
    int processed = 0;
    const int MAX_CHARS_PER_CALL = 20;

    while (available > 0 && processed < MAX_CHARS_PER_CALL) {
        char c = client_.read();
        available--; processed++;

        if (c == '\r' || c == '\n') {
            if (buffer.length() > 0) {
                processCommand(buffer);
                buffer.clear();
            }
        }
        else if (c == 8 || c == 127) {
            if (buffer.length() > 0) {
                buffer.remove(buffer.length() - 1);
                print("\b \b");
            }
        }
        else if (c >= 32 && c <= 126) {
            buffer += c;
            if (mtx_) xSemaphoreTake(mtx_, portMAX_DELAY);
            client_.write(c);
            if (mtx_) xSemaphoreGive(mtx_);
        }
    }
}

bool NetLogger::begin(uint16_t port) {
    if (started_) return true;
    if (!NL_PASSWD || strlen(NL_PASSWD) != 128) return false;

    mtx_ = xSemaphoreCreateMutex();

    server_ = new WiFiServer(port);
    if (!server_) return false;

    server_->begin();
    server_->setNoDelay(true);
    esp_log_set_vprintf(&NetLogger::idf_vprintf_trampoline);
    started_ = true;
    return true;
}

void NetLogger::stop() {
    if (!started_) return;

    if (client_ && client_.connected()) client_.stop();

    if (server_) {
        server_->stop();
        delete server_;
        server_ = nullptr;
    }

    esp_log_set_vprintf(vprintf);
    started_ = false;
    auth_ok_ = false;
    auth_state_ = AUTH_NONE;
    failed_attempts_ = 0;
    last_fail_time_ = 0;
    password_buffer_.clear();

    if (mtx_) { vSemaphoreDelete(mtx_); mtx_ = nullptr; }
}

void NetLogger::handle() {
    if (!started_ || !server_) return;

    handleSerial();

    if (failed_attempts_ < MAX_AUTH_ATTEMPTS ||
        millis() - last_fail_time_ > LOCKOUT_TIME_MS) {
        if (server_->hasClient()) handleNewClient();
    }

    if (client_ && client_.connected()) {
        if (!auth_ok_) {
            if (readPasswordNonBlocking()) processAuthentication();
        } else {
            readClientCommand();
        }
    }

    if (client_ && !client_.connected()) {
        client_.stop();
        client_ = WiFiClient();
        auth_ok_ = false;
        auth_state_ = AUTH_NONE;
        password_buffer_.clear();
    }
}

void NetLogger::setLogUART(bool on) { log_uart_ = on; }
void NetLogger::setLogTelnet(bool on) { log_telnet_ = on; }
void NetLogger::setCRLF(bool on) { crlf_ = on; }
bool NetLogger::isLogUART() const { return log_uart_; }
bool NetLogger::isLogTelnet() const { return log_telnet_; }
bool NetLogger::isCRLF() const { return crlf_; }
bool NetLogger::isConnected() { return client_ && client_.connected() && auth_ok_; }

void NetLogger::print(const char* s) { if (s) writeOutput((const uint8_t*)s, strlen(s)); }
void NetLogger::println(const char* s) { if (s) writeOutput((const uint8_t*)s, strlen(s)); const char nl = '\n'; writeOutput((const uint8_t*)&nl, 1); }
void NetLogger::print(const String& s) { writeOutput((const uint8_t*)s.c_str(), s.length()); }
void NetLogger::println(const String& s) { writeOutput((const uint8_t*)s.c_str(), s.length()); const char nl = '\n'; writeOutput((const uint8_t*)&nl, 1); }
void NetLogger::printf(const char* fmt, ...) {
    if (!fmt) return;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) writeOutput((const uint8_t*)buf, (size_t)strnlen(buf, sizeof(buf)));
}
void NetLogger::print(const __FlashStringHelper* s) { print_P((PGM_P)s); }
void NetLogger::println(const __FlashStringHelper* s) { println_P((PGM_P)s); }
void NetLogger::print_P(PGM_P s) {
    if (!s) return;
    char buf[256];
    strncpy_P(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    writeOutput((const uint8_t*)buf, strlen(buf));
}
void NetLogger::println_P(PGM_P s) { print_P(s); const char nl = '\n'; writeOutput((const uint8_t*)&nl, 1); }
void NetLogger::printf_PGM(PGM_P fmt, ...) {
    if (!fmt) return;
    char fmt_buf[128]; strncpy_P(fmt_buf, fmt, sizeof(fmt_buf) - 1); fmt_buf[sizeof(fmt_buf) - 1] = 0;
    char out_buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(out_buf, sizeof(out_buf), fmt_buf, ap);
    va_end(ap);
    if (n > 0) writeOutput((const uint8_t*)out_buf, (size_t)strnlen(out_buf, sizeof(out_buf)));
}

int NetLogger::idf_vprintf_trampoline(const char* fmt, va_list ap) {
    return NetLogger::i().idf_vprintf(fmt, ap);
}
