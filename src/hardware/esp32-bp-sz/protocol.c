
#include "protocol.h"

pthread_mutex_t lock;

void init_mutex() {
    pthread_mutex_init(&lock, NULL);
}

void destroy_mutex() {
    pthread_mutex_destroy(&lock);
}

/*
* @brief Alokovanie deskriptorov pre kontex spracovávania logických kanálov
*/
bool allocate_logic_descriptors(struct dev_context * devc) {

    if (!devc) {
        return false;
    }

    struct logic_data * lg_data = NULL;
    struct logic_data * first = NULL;
    uint8_t * buffer = (uint8_t *) g_malloc0(LOGIC_CHANNELS_DESCRIPTORS * (LOGIC_DATA_SIZE + ANALOG_DATA_SIZE) * sizeof(uint8_t));

    if (!buffer) {
        return false;
    }

    for (int i = 0; i < LOGIC_CHANNELS_DESCRIPTORS; i ++) {

        lg_data = (struct logic_data *) g_malloc0(sizeof(struct logic_data));

        if (!lg_data) {
            return false;
        }

        lg_data->number = i;
        lg_data->data = &buffer[i * (LOGIC_DATA_SIZE + ANALOG_DATA_SIZE)];
        lg_data->filled = false;
        lg_data->next = NULL;

        if (!devc->logic_ptr) {
            first = lg_data;
            devc->logic_ptr = lg_data;
        } else {
            devc->logic_ptr->next = lg_data;
            devc->logic_ptr = lg_data;
        }

    }

    devc->logic_ptr->next = first;
    devc->logic_ptr = first;

    return true;

}

/*
* @brief Uvolnenie alokovanej pamäte deskriptorov
*/
void free_logic_descriptors(struct dev_context * devc) {

    struct logic_data * lg_data = devc->logic_ptr;

    while (lg_data->number != 0) {
        lg_data = lg_data->next;
    }

    g_free(lg_data->data);

    while (true) {

        if (lg_data->number == (LOGIC_CHANNELS_DESCRIPTORS - 1)) {
            g_free(lg_data);
            break;
        } else {
            struct logic_data * tmp = lg_data->next;
            g_free(lg_data);
            lg_data = tmp;
        }

    }

    devc->logic_ptr = NULL;

}

/*
* @brief Funkcia, ktorá vytvorí packet na odosielanie dát do fromtendovej aplikácie
*           PulseView na zobrazenie.
*/
void * process_send(void * data) {

    struct sr_dev_inst * sdi = (struct sr_dev_inst *) data;
    struct dev_context * devc = (struct dev_context *) sdi->priv;
    struct logic_data * descriptor = devc->logic_ptr;
    int result;

    /*struct sr_analog_encoding analog_enc = {
        .unitsize = sizeof(float),
        .is_signed = FALSE,
        .is_float  = TRUE,
        .is_bigendian = FALSE,
        .is_digits_decimal = TRUE,
        .digits = 0,
        .scale = {1, 1},
        .offset = {0, 1}
    };*/

    while (devc->running) {

        // Nájdenie plného deskriptora
        int starting_point = descriptor->number;
        pthread_mutex_lock(&lock);
        while (!descriptor->filled) {
            descriptor = descriptor->next;
            if (descriptor->number == starting_point) {
                break;
            }
        }
        pthread_mutex_unlock(&lock);

        if (!descriptor->filled) {
            continue;
        }

        sr_log(SR_LOG_ERR, "Found filled!");

        // Odoslanie dát do PV

        // Posielanie logickych signalov
        while (descriptor->pointer != LOGIC_DATA_SIZE) {

            struct sr_datafeed_logic pckt_l = {
                .length = sizeof(uint8_t) * LOGIC_CHANNELS / 8,
                .unitsize = sizeof(uint8_t) * LOGIC_CHANNELS / 8,
                .data = &descriptor->data[descriptor->pointer]
            };
    
            struct sr_datafeed_packet packet_logic = {
                .type = SR_DF_LOGIC,
                .payload = &pckt_l
            };
            result = sr_session_send(sdi, &packet_logic);
    
            if (result == SR_ERR_ARG) {
                sr_log(SR_LOG_ERR, "Couldn't send logic packet, invalid argument!");
                return NULL;
            }

            descriptor->pointer ++;

        }
        descriptor->filled = false;

        // Garantovanie času na YELD
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 10 * 1000L;

        nanosleep(&ts, NULL);

    }

    return NULL;

}

/*
* @brief Funkcia, ktorá je volaná na pozadí pri spracovaní USB dát prijaych z ESP32
*           Táto funkcia spracuje prijatédáta a spracovanie je delegované ďalšiemu procesu.
*/
int acquisition_callback(int fd, int events, void * cb_data) {

    (void) fd;
    (void) events;
    
    // Získanie potrebných premenných
    struct sr_dev_inst * sdi = (struct sr_dev_inst *) cb_data;
    struct dev_context * devc = (struct dev_context *) sdi->priv;
    struct logic_data * descriptor = devc->logic_ptr;

    // Nájdenie prázdneho deskriptora

    int starting_point = descriptor->number;
    pthread_mutex_lock(&lock);
    while (descriptor->filled) {
        descriptor = descriptor->next;
        if (descriptor->number == starting_point) {
            break;
        }
    }
    pthread_mutex_unlock(&lock);

    if (descriptor->filled) {
        return true;
    }

    sr_log(SR_LOG_ERR, "Found not filled!");

    // Prijatie dát
    int length = 0;
    int result = libusb_bulk_transfer(
        devc->usb_handle,
        DATA_ENDPOINT_IN,
        descriptor->data,
        sizeof(descriptor->data),
        &length,
        1000
    );

    if (result != LIBUSB_SUCCESS) {
        return SR_ERR;
    }

    // Spracovanie prijatých dát
    if (length == LOGIC_DATA_SIZE) {

        if (descriptor->data[TYPE_FIELD] != (uint8_t) DATA_LOGIC) {
            sr_log(SR_LOG_ERR, "The usb packet came in unexpected type!");
            return SR_ERR;
        }

        descriptor->filled = true;
        descriptor->pointer = HEADER_SIZE + 1;

    } else if (length == ANALOG_DATA_SIZE) {

        if (descriptor->data[TYPE_FIELD] != (uint8_t) DATA_ANALOG) {
            sr_log(SR_LOG_ERR, "The usb packet came in unexpected type!");
            return SR_ERR;
        }

        uint8_t channel_id = 0;
        for (int i = 0; i < (length - HEADER_SIZE) / ANALOG_CHANNEL_SIZE; i ++) {

            channel_id = descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE];
            if (channel_id & (uint8_t) VOLTAGE) {

                devc->voltage_data[descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE] & 0x7f] = \
                *((float * ) &descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE + 1]);

            } else {

                devc->current_data[descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE] & 0x7f] = \
                *((float * ) &descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE + 1]);

            }

        }

    } else if (length == (LOGIC_DATA_SIZE + ANALOG_DATA_SIZE)) {

        if (descriptor->data[TYPE_FIELD] == (uint8_t) DATA_ANALOG && descriptor->data[TYPE_FIELD + ANALOG_DATA_SIZE] == (uint8_t) DATA_LOGIC) {

            uint8_t channel_id = 0;
            for (int i = 0; i < (length - HEADER_SIZE) / ANALOG_CHANNEL_SIZE; i ++) {

                channel_id = descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE];
                if (channel_id & (uint8_t) VOLTAGE) {

                    devc->voltage_data[descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE] & 0x7f] = \
                    *((float * ) &descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE + 1]);

                } else {

                    devc->current_data[descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE] & 0x7f] = \
                    *((float * ) &descriptor->data[HEADER_SIZE + i * ANALOG_CHANNEL_SIZE + 1]);

                }

            }

            descriptor->filled = true;
            descriptor->pointer = ANALOG_DATA_SIZE + HEADER_SIZE + 1;

        } else {

            uint8_t channel_id = 0;
            for (int i = 0; i < (length - HEADER_SIZE - LOGIC_DATA_SIZE) / ANALOG_CHANNEL_SIZE; i ++) {

                channel_id = descriptor->data[HEADER_SIZE + LOGIC_DATA_SIZE + i * ANALOG_CHANNEL_SIZE];
                if (channel_id & (uint8_t) VOLTAGE) {

                    devc->voltage_data[descriptor->data[HEADER_SIZE + LOGIC_DATA_SIZE + i * ANALOG_CHANNEL_SIZE] & 0x7f] = \
                    *((float * ) &descriptor->data[HEADER_SIZE + LOGIC_DATA_SIZE + i * ANALOG_CHANNEL_SIZE + 1]);

                } else {

                    devc->current_data[descriptor->data[HEADER_SIZE + LOGIC_DATA_SIZE + i * ANALOG_CHANNEL_SIZE] & 0x7f] = \
                    *((float * ) &descriptor->data[HEADER_SIZE + LOGIC_DATA_SIZE + i * ANALOG_CHANNEL_SIZE + 1]);

                }

            }

            descriptor->filled = true;
            descriptor->pointer = HEADER_SIZE + 1;

        }

    } else {
        sr_log(SR_LOG_ERR, "The usb packet came in unexpected size!");
        return SR_ERR;
    }

    return TRUE;

}
