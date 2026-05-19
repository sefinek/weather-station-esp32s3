#include <Arduino.h>
#include <WebSocketsClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "config.h"
#include "logger_net.h"
#include "timestamp.h"
#include "ws_client.h"

#ifndef WS_TXBUF_SZ
  #define WS_TXBUF_SZ           512
#endif
#ifndef WS_RETRY_MS
  #define WS_RETRY_MS           15000UL
#endif
#ifndef WS_HEARTBEAT_MS
  #define WS_HEARTBEAT_MS       25000UL
#endif
#ifndef WS_TIMEOUT_MS
  #define WS_TIMEOUT_MS         60000UL
#endif
#ifndef WS_AUTH_TIMEOUT_MS
  #define WS_AUTH_TIMEOUT_MS    25000UL
#endif
#ifndef WS_LOG_THROTTLE_MS
  #define WS_LOG_THROTTLE_MS    30000UL
#endif
#ifndef WS_OUTBOX_CAP
  #define WS_OUTBOX_CAP         8
#endif
#ifndef WS_FLUSH_PER_TICK
  #define WS_FLUSH_PER_TICK     4
#endif
#ifndef WS_USE_TASK
  #define WS_USE_TASK           1
#endif
#ifndef WS_TASK_CORE
  #define WS_TASK_CORE          0
#endif
#ifndef WS_TASK_STACK
  #define WS_TASK_STACK         8192
#endif
#ifndef WS_TASK_PRIO
  #define WS_TASK_PRIO          1
#endif

namespace {
  WebSocketsClient ws;
  uint8_t  state = 0;
  char     host_[64];
  char     path_[32];
  char     token_[129];
  uint16_t port_ = 0;

  uint32_t last_activity = 0;
  uint32_t last_disconnect_log = 0;
  uint32_t last_error_log = 0;
  uint32_t connect_at = 0;

  bool     auth_sent = false;
  TaskHandle_t ws_task = nullptr;

  struct OutMsg { uint16_t len; char data[WS_TXBUF_SZ]; };
  OutMsg   outbox[WS_OUTBOX_CAP];
  volatile uint8_t out_head = 0, out_tail = 0;
  SemaphoreHandle_t ob_mtx = nullptr;

  inline void reset_connection() {
    state = 0;
    connect_at = 0;
    last_activity = 0;
    auth_sent = false;
  }

  inline bool outbox_push(const char* buf, uint16_t len) {
    if (!buf || !len || len >= WS_TXBUF_SZ) return false;
    if (ob_mtx) xSemaphoreTake(ob_mtx, portMAX_DELAY);
    const uint8_t h  = out_head;
    const uint8_t nx = uint8_t((h + 1) % WS_OUTBOX_CAP);
    if (nx == out_tail) { if (ob_mtx) xSemaphoreGive(ob_mtx); return false; }
    OutMsg& m = outbox[h];
    m.len = len; memcpy(m.data, buf, len);
    out_head = nx;
    if (ob_mtx) xSemaphoreGive(ob_mtx);
    return true;
  }

  inline bool outbox_pop(OutMsg& out) {
    if (ob_mtx) xSemaphoreTake(ob_mtx, portMAX_DELAY);
    if (out_head == out_tail) { if (ob_mtx) xSemaphoreGive(ob_mtx); return false; }
    const uint8_t t = out_tail;
    out = outbox[t];
    out_tail = uint8_t((t + 1) % WS_OUTBOX_CAP);
    if (ob_mtx) xSemaphoreGive(ob_mtx);
    return true;
  }

  inline void flush_outbox_budgeted() {
    if (state < 1) return;
    uint8_t sent = 0;
    OutMsg m;
    while (sent < WS_FLUSH_PER_TICK && outbox_pop(m)) {
      ws.sendTXT(m.data, m.len);
      sent++;
    }
  }

  inline void send_auth_immediate() {
    if (state != 1 || !token_[0] || auth_sent) return;
    char buf[WS_TXBUF_SZ];
    const int len = snprintf(buf, sizeof(buf), "{\"token\":\"%s\"}", token_);
    if (len > 0 && len < (int)sizeof(buf)) {
      ws.sendTXT(buf, len);
      auth_sent = true;
      LOGLN("WebSocket auth sent");
    }
  }

  void handle_message(const char* payload, size_t len) {
    if (!payload || !len) return;

    if (strstr(payload, "\"status\":\"AUTH_FAILED\"")) LOGLN("Server: AUTH_FAILED");
    else if (strstr(payload, "\"status\":\"AUTH_ERROR\"")) LOGLN("Server: AUTH_ERROR");
    else if (strstr(payload, "\"status\":\"AUTH_TIMEOUT\"")) LOGLN("Server: AUTH_TIMEOUT");

    if (strstr(payload, "\"AUTH_TOKEN\"")) {
      send_auth_immediate();
    } else if (strstr(payload, "\"timeSync\":")) {
      const char* ts = strstr(payload, "\"timeSync\":") + 11;
      if (ts) {
        const uint64_t unix_ms = strtoull(ts, nullptr, 10);
        if (unix_ms > 0) timestamp_sync_from_unix_ms(unix_ms);
      }
    }

    if (strstr(payload, "\"status\":\"AUTH_OK\"")) {
      state = 2;
      LOGLN("WebSocket authenticated");
    }
  }

  void onEvent(WStype_t type, uint8_t* payload, size_t length) {
    const uint32_t now = millis();
    switch (type) {
      case WStype_CONNECTED:
        state = 1;
        last_activity = now;
        connect_at = now;
        auth_sent = false;
        LOGF("WebSocket connected to %s:%u%s\n", host_, port_, path_);
        send_auth_immediate();
        break;
      case WStype_DISCONNECTED:
        reset_connection();
        if (now - last_disconnect_log > WS_LOG_THROTTLE_MS) {
          LOGLN("WebSocket disconnected");
          last_disconnect_log = now;
        }
        break;
      case WStype_TEXT:
        last_activity = now;
        if (length && payload) handle_message(reinterpret_cast<const char*>(payload), length);
        break;
      case WStype_PING:
      case WStype_PONG:
        last_activity = now;
        break;
      case WStype_ERROR:
        reset_connection();
        if (now - last_error_log > WS_LOG_THROTTLE_MS) {
          LOGLN("WebSocket error");
          last_error_log = now;
        }
        break;
      default: break;
    }
  }

  inline bool expired_since(uint32_t since_ms, uint32_t timeout_ms, uint32_t now_ms) {
    return (int32_t)(now_ms - since_ms) > (int32_t)timeout_ms;
  }

  void ws_task_fn(void*) {
    for (;;) {
      ws.loop();
      const uint32_t now = millis();

      flush_outbox_budgeted();

      if (state == 1) {
        if (expired_since(connect_at, WS_AUTH_TIMEOUT_MS, now)) {
          LOGLN("WebSocket auth timeout -> disconnect()");
          ws.disconnect();
          reset_connection();
        }
      } else if (state == 2) {
        if (expired_since(last_activity, WS_TIMEOUT_MS, now)) {
          LOGLN("WebSocket idle timeout -> disconnect()");
          ws.disconnect();
          reset_connection();
        }
      }

      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void ws_begin() {
  if (!WS_HOST || !WS_PATH) return;
  if (strlen(WS_PASSWD) == 0) { LOGLN("WebSocket password cannot be empty"); return; }

  strlcpy(host_, WS_HOST, sizeof(host_));
  strlcpy(path_, WS_PATH, sizeof(path_));
  strlcpy(token_, WS_PASSWD, sizeof(token_));
  port_ = WS_PORT;

  ws.onEvent(onEvent);
  ws.enableHeartbeat(WS_HEARTBEAT_MS, 3000, 2);
  ws.setReconnectInterval(WS_RETRY_MS);

  out_head = out_tail = 0;
  if (!ob_mtx) ob_mtx = xSemaphoreCreateMutex();
  reset_connection();

  ws.begin(host_, port_, path_);

#if WS_USE_TASK
  if (!ws_task) {
    xTaskCreatePinnedToCore(ws_task_fn, "wscli", WS_TASK_STACK, nullptr, WS_TASK_PRIO, &ws_task, WS_TASK_CORE);
  }
#endif

  LOGF("WebSocket begin -> %s:%u%s (reconnect=%lums, core=%d)\n",
       host_, port_, path_, WS_RETRY_MS, WS_TASK_CORE);
}

void ws_service() {
#if WS_USE_TASK
#else
  ws.loop();
  flush_outbox_budgeted();
#endif
}

bool ws_is_ready()     { return state == 2; }
bool ws_is_connected() { return state >= 1; }
uint8_t ws_get_state() { return state; }

bool ws_send_data(const char* json_data) {
  if (!json_data) return false;
  const size_t l = strnlen(json_data, WS_TXBUF_SZ);
  if (l == 0 || l >= WS_TXBUF_SZ) return false;
  return outbox_push(json_data, (uint16_t)l);
}

bool ws_send_weather(float t, float rh, float p_hpa, float lx, int rain_raw) {
  if (!timestamp_is_synced()) return false;
  char buf[WS_TXBUF_SZ];
  const int len = snprintf(buf, sizeof(buf),
    "{\"timestamp\":%llu,\"temp\":%.2f,\"humid\":%.2f,\"press\":%.2f,\"light\":%.2f,\"rain\":%d}",
    (unsigned long long)timestamp_now_unix_ms(), t, rh, p_hpa, lx, rain_raw);
  if (len <= 0 || len >= (int)sizeof(buf)) return false;
  return outbox_push(buf, (uint16_t)len);
}

bool ws_send_as3935_event(const char* event_type, uint8_t distance, uint32_t magnitude) {
  if (!timestamp_is_synced() || !event_type) return false;
  char buf[WS_TXBUF_SZ];
  int len;
  if (strcmp(event_type, "lightning") == 0) {
    len = snprintf(buf, sizeof(buf),
      "{\"timestamp\":%llu,\"type\":\"lightning\",\"distance\":%u,\"magnitude\":%lu}",
      (unsigned long long)timestamp_now_unix_ms(), distance, (unsigned long)magnitude);
  } else {
    len = snprintf(buf, sizeof(buf),
      "{\"timestamp\":%llu,\"type\":\"%s\"}",
      (unsigned long long)timestamp_now_unix_ms(), event_type);
  }
  if (len <= 0 || len >= (int)sizeof(buf)) return false;
  return outbox_push(buf, (uint16_t)len);
}
