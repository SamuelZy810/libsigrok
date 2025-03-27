
#ifndef ESP32_BP_SZ
#define ESP32_BP_SZ

// Hlavičkové súbory

#pragma once

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

// Deklarovanie konštant

#define CDC_REQUEST_SET_LINE_CODING 0x20
#define CDC_REQUEST_SET_CONTROL_LINE_STATE 0x22
#define LINE_STATE_START 0x03
#define LINE_STATE_STOP 0x00

#define VEND_ID 0x1a86
#define PROD_ID 0x5678

#define DATA_INTERFACE 1
#define CONT_INTERFACE 0

#define CONT_ENDPOINT_IN 0x80
#define CONT_ENDPOINT_OUT 0x00

#define DATA_ENDPOINT_IN 0x82
#define DATA_ENDPOINT_OUT 0x02

#define VOLTAGE_CHANNELS 1
#define CURRENT_CHANNELS 1

#define LOGIC_CHANNELS 8
#define ANALOG_CHANNELS (VOLTAGE_CHANNELS + CURRENT_CHANNELS)

#define HEADER_SIZE 9
#define ANALOG_CHANNEL_SIZE 5
#define LOGIC_DATA_SIZE 266
#define ANALOG_DATA_SIZE (HEADER_SIZE + ANALOG_CHANNELS * ANALOG_CHANNEL_SIZE)

// Deklarovanie enumeratorov

enum packet_data {
    DATA_LOGIC = 1, DATA_ANALOG = 2
};

#define TYPE_MASK 0x80

enum channel_type {
    CURRENT = 0, VOLTAGE = 0x80
};

enum measured_quantity {
    VOLTS, MILI_VOLTS, AMPERES, MILI_AMPERES, MICRO_AMPERES
}

// Deklarovanie deskriptora zariadenia

struct dev_context {

    // Konfigurácia zariadenia
    uint32_t samplerate;    // Vzorkovacia frekvencia zariadenia
    uint32_t num_cur_ch;    // Počet analógových kanálov - prúd
    uint32_t num_vol_ch;    // Počet analógových kanálov - napätie
    uint32_t num_log_ch;    // Počet logických kanálov (8 | 16)
    enum measured_quantity current_quantity;    // Meraná veličina prúdu
    enum measured_quantity voltage_quantity;    // Meraná veličina napätia

    // Konfigurácia USB
    libusb_device * usb_device;

    // sigrok device driver
    struct sr_dev_driver * driver;

};

// Deklarovanie protokolových funkcii
int acquisition_callback(int fd, int events, void * cb_data);

#endif
