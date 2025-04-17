
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

// USB Packet
#define START_DATA_TRANSFER 1
#define HEADER_SIZE 10
#define PACKET_SIZE 64
#define DATA_PAYLOAD_SIZE (PACKET_SIZE - HEADER_SIZE)

// Veľkosť jednej vzorky LOGICKÉHO merania v Bajtoch
#define UNITS ((uint8_t) (LOGIC_CHANNELS + 7) / 8)

// Informácie Logické kanály
#define LOGIC_CHANNELS_DESCRIPTORS 10
#define LOGIC_DATA_SIZE 64
#define LOGIC_DATA_BUFFER 64

// Informácie Analógové kanály
#define ANALOG_CHANNEL_SIZE 5
#define ANALOG_DATA_SIZE 19
#define ANALOG_DATA_BUFFER 64

// USB Vendor a Produkt ID
#define VEND_ID 0x1a86
#define PROD_ID 0x5678

// USB Rozhrania
// Dátové rozhranie
#define DATA_INTERFACE 0
#define DATA_ENDPOINT_IN 0x81
#define DATA_ENDPOINT_OUT 0x01

// Kontrolne rozhranie
#define CONTROL_INTERFACE 1
#define CONTROL_ENDPOINT_IN 0x82
#define CONTROL_ENDPOINT_OUT 0x02

// Enumerátor pre merané veličiny analógových kanálov
enum measured_quantity {
    VOLTS, MILI_VOLTS, AMPERES, MILI_AMPERES, MICRO_AMPERES
};

// Enumerátor pre typ analógového kanálu
enum channel_type {
    CURRENT_CH = 0, VOLTAGE_CH = 0x80
};

#define CH_MASK 0x80    // Maska pre zistenie typu kanálu

#define RUN 1           // Definovanie logickej jednotky pre nekonečné cykli :)

// Definovanie deskriptora zariadenia

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

// PROCESS: Nastavenie a zdeštruovanie mutexov

/**
* @brief Inicializácia mutexu pre logické a analógové merania, pre zabezpečenie atomicty čítania a vykreslovania
*/
bool init_mutex(void);

/**
* @brief Dealokácia mutexu
*/
void destroy_mutex(void);

// PROCESS: Inicializácia potrebných parametrov a premenných

/**
* @brief Funkcia, ktorá inicializuje statické premenné spadajúce pod protocol.c
* @param sdi je povinný parameter a nesmie byt NULL
* @param lib_ctx je povinný parameter a nesmiebyť NULL
*/
bool init_it(const struct sr_dev_inst * sdi, struct libusb_context * lib_ctx);

// PROCESS: Resetovanie a ziskanie zariadenia

/**
* @brief Funkcia, ktorá resetuje USB zariadenie, deskriptor usb zariadenia,
*   reinicializuje konfiguráciu zariadenia a pripraví kontrolné a dátové rozhranie na prenos dát
* @param devc_usb_handle je povinný parameter, ide o otvorené zariadenie pre platný libusb_context
*/
bool reset_and_claim(struct libusb_device_handle * devc_usb_handle);

/**
* @brief Uvolnenie získaných USB rozhraní
* @param platí rovnako ako v @see reset_and_claim()
*/
void release_claimed(struct libusb_device_handle * devc_usb_handle);

// PROCESS: Vytvorenie a ukončenie PulseView session

/**
* @brief Vytvorenie session, teda relácie pre spracovanie a vykreslenie dát v programe PulseView
* @param sdi povinný parameter, tu sa uchováva a riadí aktívna / neaktívna relácia
*/
bool create_session(const struct sr_dev_inst * sdi);

/**
* @brief Ukončenie aktívnej relácie. Odstráni reláciu a ukonči všetky procesy, ktoré patria pre reláciu.
* @param sdi @see create_session()
*/
void destroy_session(struct sr_dev_inst * sdi);

// PROCESS: Alokovanie a uvolnenie pamäte pre meranie

/**
* @brief Dynamické alokovanie polí pre analógové kanály
* @param devc je povinný parameter, obsahuje smerníky na alokovanie pamäte pre analógové kanály
*/
bool allocate_analog_data(struct dev_context * devc);

/**
* @brief Uvolnenie pamaäte alokovanej pre analógové kanály
* @param devc je povinný parameter
*/
void free_analog_data(struct dev_context * devc);

// PROCESS: Spracovávanie USB bulk transakcií

/**
* @brief Odoslanie signálu na začatie USB prenosu na pripojené zariadenie
* @param handle, povinný parameter, otvorené USB zariadenie
*/
bool start_usb_transfer(libusb_device_handle * handle);

/**
* @brief alokovanie asynchrónneho prenosu ang. transfer na prijatie a spracovanie usb packetu
* @param handle, povinný parameter, otvorené USB zariadenie
*/
void submit_async_transfer(libusb_device_handle * handle);

/**
* @brief funkcia, ktorá sa zavolá pri dokončení usb prenosu, na zodpovednosť má spracovanie prijatých dát a alokovanie ďalšieho prenosu
* @param handle, povinný parameter, otvorené USB zariadenie
*/
void LIBUSB_CALL async_callback(struct libusb_transfer * transfer);

// PROCESS: Odosielanie packetov na frontend - PulseView

/**
* @brief Obálková funkcia pre send_analog_packet() a send_logic_packet()
* @param data logické dáta, ktoré boli prijaté
* @param size veľkosť logických dát, ktoré majú byť vykreslené
*/
void send_to_front(uint8_t * data, uint16_t size);

/**
* @brief Funkcia na odoslanie analógových dát na frontend, na vykreslenie. Dáta, ktoré má vykresliť
*   čerpá z premennej devc_con inicializovanej funkciou @see init_it()
*/
void send_analog_packet(void);

/**
* @brief Funkcia na odoslanie logických dát na frontend, ny vakreslenie
* @param
*/
void send_logic_packet(void * data);

// PROCESS: Riadenie začatia akvizície

/**
* @brief Funkcia, ktorá je volaná na pozadí pri spracovaní USB dát prijaych z ESP32
*           Táto funkcia spracuje prijatédáta a spracovanie je delegované ďalšiemu procesu.
* @param fd = file descriptor, v podstate ide o identifikátor otvprenej session
* @param events sú sigrok eventy, ktoré treba spracovať, tento proces nie je povinný
* @param cb_data callback data, ktoré sú v tomto prípade NULL
*/
int acquisition_callback(int fd, int events, void * cb_data);

#endif
