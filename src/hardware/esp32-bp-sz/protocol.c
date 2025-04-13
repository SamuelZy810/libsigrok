
#include "protocol.h"

// Mutex: pre atomicitu vykreslovania / interpolovania / zobrazovania
pthread_mutex_t lock_analog;
pthread_mutex_t lock_logic;

// Atomická premenná na kontrolovanie stavu
// zabezpečuje chod prijamia a vykreslovania
static atomic_bool * running = NULL;

// Premenné potrebné na odosielanie packetov na frontend
static struct sr_dev_inst * sdi_out = NULL;
static struct dev_context * devc_out = NULL;
static GSList * voltage_channels = NULL;
static GSList * current_channels = NULL;

// Nastavenie kódovania analógových kanálov
static struct sr_analog_encoding analog_enc = {
    .unitsize = sizeof(float),
    .is_signed = FALSE,
    .is_float  = TRUE,
    .is_bigendian = FALSE,
    .is_digits_decimal = TRUE,
    .digits = 0,
    .scale = {1, 1},
    .offset = {0, 1}
};

// Definície protokolových API funkcií pre ESP32

// PROCESS: Nastavenie a zdeštruovanie mutexov
//          Ideálne volať pri začatí / skončení akvizície zariadenia

void init_mutex() {
    pthread_mutex_init(&lock_analog, NULL);
    pthread_mutex_init(&lock_logic, NULL);
}

void destroy_mutex() {
    pthread_mutex_destroy(&lock_analog);
    pthread_mutex_destroy(&lock_logic);
}

// PROCESS: Spracovávanie USB bulk transakcií

void submit_async_transfer(libusb_device_handle * handle) {

    struct libusb_transfer * transfer = libusb_alloc_transfer(0);
    if (!transfer) {
        fprintf(stderr, "Failed to allocate libusb transfer!\n");
        libusb_free_transfer(transfer);
        return;
    }

    uint8_t * buffer = g_malloc0(64 * sizeof(uint8_t));
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer!\n");
        libusb_free_transfer(transfer);
        return;
    }

    libusb_fill_bulk_transfer(
        transfer,
        handle,
        DATA_ENDPOINT_IN,
        buffer,
        64,
        async_callback,
        NULL,
        0
    );

    const int res = libusb_submit_transfer(transfer);
    if (res != LIBUSB_SUCCESS) {
        fprintf(stderr, "Failed to submit transfer: %s\n", libusb_error_name(res));
        free(buffer);
        libusb_free_transfer(transfer);
    }

}

void LIBUSB_CALL async_callback(struct libusb_transfer * transfer) {

    // Uvolniť data a odistť ak nastala chyba
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {

        free(transfer->buffer);
        libusb_free_transfer(transfer);
        return;

    }

    // Packet obsahuje LOGICKÉ DATA
    if (transfer->buffer[0] == 0b00110011 && transfer->buffer[1] == 0b00110011) {
        
        // Spracovať logické data
        send_logic(&transfer->buffer[10], 54);

    }
    // Packet obsahuje ANALÓGOVÉ DATA
    else if (transfer->buffer[0] == 0b11001100 && transfer->buffer[1] == 0b11001100) {

        uint8_t offset = 10;
       
        // Spracovať analógové data
        for (int i = 0; i < ANALOG_CHANNELS; i++) {

            float value = * ((float *) transfer->buffer[offset + i * ANALOG_CHANNEL_SIZE + 1]);

            if (transfer->buffer[offset + i * ANALOG_CHANNEL_SIZE] & CH_MASK == CURRENT_CH) {

                // Uloženie hodnoty prúdu na svoje miesto
                devc_out->current_data[i] = value;

            } else {

                // Uloženie hodnoty napätia na svoje miesto
                devc_out->voltage_data[i] = value;

            }

        }

        send_analog();
        
    }

    // Resubmit transfer
    if (atomic_load(running)) submit_async_transfer(transfer->dev_handle);

    // Clean up previous buffer and transfer struct
    free(transfer->buffer);
    libusb_free_transfer(transfer);
}

// PROCESS: Odosielanie packetov na frontend - PulseView

void send_analog() {

    int result = SR_OK;

    pthread_mutex_lock(&lock_analog);

    for (int i = 0; i < VOLTAGE_CHANNELS; i++) {

        struct sr_analog_meaning meaning = {
            .mq = SR_MQ_VOLTAGE,
            .unit = SR_UNIT_VOLT,
            .mqflags = SR_MQFLAG_DC,
            .channels = g_slist_nth(voltage_channels, i)
        };
    
        struct sr_analog_spec spec = {
            .spec_digits = 0
        };
    
        struct sr_datafeed_analog pckt = {
            .data = &devc_out->voltage_data[i],
            .num_samples = 1,
            .encoding = &analog_enc,
            .meaning = &meaning,
            .spec = &spec
        };
    
        struct sr_datafeed_packet packet = {
            .type = SR_DF_ANALOG,
            .payload = &pckt
        };

        result = sr_session_send(sdi, &packet);
        if (result == SR_ERR_ARG) {
            sr_log(SR_LOG_ERR, "Couldn't send voltage packet!");
            return NULL;
        }

    }

    for (int i = 0; i < CURRENT_CHANNELS; i++) {

        struct sr_analog_meaning meaning = {
            .mq = SR_MQ_CURRENT,
            .unit = SR_UNIT_AMPERE,
            .mqflags = SR_MQFLAG_DC,
            .channels = g_slist_nth(current_channels, i)
        };
    
        struct sr_analog_spec spec = {
            .spec_digits = 0
        };
    
        struct sr_datafeed_analog pckt = {
            .data = &devc_out->current_data[i],
            .num_samples = 1,
            .encoding = &analog_enc,
            .meaning = &meaning,
            .spec = &spec
        };
    
        struct sr_datafeed_packet packet = {
            .type = SR_DF_ANALOG,
            .payload = &pckt
        };

        result = sr_session_send(sdi, &packet);
        if (result == SR_ERR_ARG) {
            sr_log(SR_LOG_ERR, "Couldn't send voltage packet!");
            return NULL;
        }

    }

    pthread_mutex_unlock(&lock_analog);

}

void send_logic(uint8_t * data, uint16_t size) {

    pthread_mutex_lock(&lock_logic);

    for (uint16_t i = 0; i < size; i ++) {

        struct sr_datafeed_logic pckt_l = {
            .length = sizeof(uint8_t) * LOGIC_CHANNELS / 8,
            .unitsize = sizeof(uint8_t) * LOGIC_CHANNELS / 8,
            .data = &data[i]
        };
    
        struct sr_datafeed_packet packet_logic = {
            .type = SR_DF_LOGIC,
            .payload = &pckt_l
        };
        result = sr_session_send(sdi_out, &packet_logic);
    
        if (result == SR_ERR_ARG) {
            sr_log(SR_LOG_ERR, "Couldn't send logic packet, invalid argument!");
            break;
        }

    }

    pthread_mutex_unlock(&lock_logic);

}

// PROCESS: Riadenie začatia akvizície

int acquisition_callback(int fd, int events, void * cb_data) {

    (void) fd;
    (void) events;
    
    // Získanie potrebných premenných
    struct sr_dev_inst * sdi = (struct sr_dev_inst *) cb_data;
    struct dev_context * devc = (struct dev_context *) sdi->priv;

    // Nastavenie globálnych premenných
    running = &devc->running;
    sdi_out = sdi;
    devc_out = devc;

    // Inicializovanie analógových kanálov
    GSList * channels = sdi->channels;

    for (struct sr_channel * ch; channels != NULL; channels = channels->next) {

        ch = channels->data;
        if (ch->type == SR_CHANNEL_ANALOG) {
            if (ch->name[0] == 'V') {
                voltage_channels = g_slist_append(voltage_channels, ch);
            } else {
                current_channels = g_slist_append(current_channels, ch);
            }
        }

    }
    
    while (atomic_load(&devc->running)) {
        
        libusb_handle_events_completed(NULL, NULL);
        
    }

    return SR_OK;

}
