#pragma once

// ==== Board: LILYGO_T_ETH_ELITE_ESP32S3 ====

// Ethernet SPI
#define ETH_MISO_PIN    47
#define ETH_MOSI_PIN    21
#define ETH_SCLK_PIN    48
#define ETH_CS_PIN      45
#define ETH_INT_PIN     14
#define ETH_RST_PIN     -1
#define ETH_ADDR        1

// Shared SPI bus
#define SPI_MISO_PIN    9
#define SPI_MOSI_PIN    11
#define SPI_SCLK_PIN    10

// SD Card
#define SD_MISO_PIN     SPI_MISO_PIN
#define SD_MOSI_PIN     SPI_MOSI_PIN
#define SD_SCLK_PIN     SPI_SCLK_PIN
#define SD_CS_PIN       12

// I2C
#define I2C_SDA_PIN     17
#define I2C_SCL_PIN     18

// LoRa / Radio
#define RADIO_MISO_PIN  SPI_MISO_PIN
#define RADIO_MOSI_PIN  SPI_MOSI_PIN
#define RADIO_SCLK_PIN  SPI_SCLK_PIN
#define RADIO_CS_PIN    40
#define RADIO_RST_PIN   46
//#define RADIO_DIO1_PIN  16
#define RADIO_IRQ_PIN   8
#define RADIO_BUSY_PIN  16

// ADC buttons
#define ADC_BUTTONS_PIN 7

// LTE Modem
#define MODEM_RX_PIN    4
#define MODEM_TX_PIN    6
#define MODEM_DTR_PIN   5
#define MODEM_RI_PIN    1
#define MODEM_PWRKEY_PIN 3

// GPS
#define GPS_RX_PIN      39
#define GPS_TX_PIN      42

// LED
#define LED_PIN         38
