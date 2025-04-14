
#ifndef ESP32_BP_SZ
#define ESP32_BP_SZ

// Hlavičkové súbory

#pragma once

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#include <config.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <math.h>

#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

#include "channel_config.h"

// Deklarovanie konštant

#define START_DATA_TRANSFER 1
#define HEADER_SIZE 10

#define LOGIC_CHANNELS_DESCRIPTORS 10
#define LOGIC_DATA_SIZE 64
#define LOGIC_DATA_BUFFER 64

#define ANALOG_CHANNEL_SIZE 5
#define ANALOG_DATA_SIZE 19
#define ANALOG_DATA_BUFFER 64

#define VEND_ID 0x1a86
#define PROD_ID 0x5678

#define DATA_INTERFACE 0
#define DATA_ENDPOINT_IN 0x81
#define DATA_ENDPOINT_OUT 0x01

#define CONTROL_INTERFACE 1
#define CONTROL_ENDPOINT_IN 0x82
#define CONTROL_ENDPOINT_OUT 0x02

enum measured_quantity {
    VOLTS, MILI_VOLTS, AMPERES, MILI_AMPERES, MICRO_AMPERES
};

enum channel_type {
    CURRENT_CH = 0, VOLTAGE_CH = 0x80
};

#define CH_MASK 0x80
#define RUN 1

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
    struct libusb_device * usb_device;
    struct libusb_device_handle * usb_handle;

    // sigrok device driver
    struct sr_dev_driver * driver;

    // Zber dát
    float * voltage_data;
    float * current_data;

    // Stop worker thread
    atomic_bool running;

};

// Deklarovanie protokolových funkcii

/**
* @brief
*/
void init_mutex(void);

/**
* @brief
*/
void destroy_mutex(void);

/**
* @brief
* @param
*/
void init_it(const struct sr_dev_inst * sdi, struct libusb_context * lib_ctx);

/**
* @brief
* @param handle
*/
void submit_async_transfer(libusb_device_handle * handle);

/**
* @brief
* @param
*/
void LIBUSB_CALL async_callback(struct libusb_transfer * transfer);

/**
* @brief
*/
void send_analog_packet(void);

/**
* @brief
* @param
* @param
*/
void send_logic_packet(uint8_t * data, uint16_t size);

/**
* @brief Funkcia, ktorá je volaná na pozadí pri spracovaní USB dát prijaych z ESP32
*           Táto funkcia spracuje prijatédáta a spracovanie je delegované ďalšiemu procesu.
* @param
* @param
* @param
*/
int acquisition_callback(int fd, int events, void * cb_data);

/**
* @brief
* @param
*/
void * acquisition_function(void * nic);

#endif
