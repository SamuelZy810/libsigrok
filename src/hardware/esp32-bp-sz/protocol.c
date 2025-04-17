
#include "protocol.h"

// Mutex: pre atomicitu vykreslovania / interpolovania / zobrazovania
pthread_mutex_t lock_analog;
pthread_mutex_t lock_logic;

// Atomická premenná na kontrolovanie stavu
// zabezpečuje chod prijamia a vykreslovania
static atomic_bool * running = NULL;

// Premenné potrebné na odosielanie packetov na frontend
static const struct sr_dev_inst * sdi_out = NULL;
static struct dev_context * devc_out = NULL;
static struct libusb_context * ctx = NULL;

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

bool init_mutex(void) {

    if (pthread_mutex_init(&lock_analog, NULL) != 0) {

        sr_log(SR_LOG_ERR, "Couldn't create a mutex for analog channels!");
        return false;

    }

    if (pthread_mutex_init(&lock_logic, NULL) != 0) {
        
        sr_log(SR_LOG_ERR, "Couldn't create a mutex for logic channels!");
        return false;

    }

    return true;

}

void destroy_mutex(void) {

    if (pthread_mutex_destroy(&lock_analog) != 0) {

        sr_log(SR_LOG_ERR, "Couldn't remove mutex for analog channels!");

    }

    if (pthread_mutex_destroy(&lock_logic) != 0) {

        sr_log(SR_LOG_ERR, "Couldn't remove muttex for logic channels!");

    }

}

// PROCESS: Inicializácia potrebných parametrov a premenných

bool init_it(const struct sr_dev_inst * sdi, struct libusb_context * lib_ctx) {

    if (!sdi) {
        sr_log(SR_LOG_ERR, "The sdi cannot be empty!");
        return false;
    }

    if (!lib_ctx) {
        sr_log(SR_LOG_ERR, "The libusb context cannot be empty!");
        return false;
    }

    struct dev_context * devc = (struct dev_context *) sdi->priv;

    // Nastavenie globálnych premenných
    running = &devc->running;
    sdi_out = sdi;
    devc_out = devc;
    ctx = lib_ctx;

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

    return true;

}

// PROCESS: Resetovanie a ziskanie zariadenia

bool reset_and_claim(struct libusb_device_handle * devc_usb_handle) {

    int result = SR_OK;

    // Resetovanie USB deskriptora a alokácii zariadenia
    result = libusb_reset_device(devc_usb_handle);
    if (result != LIBUSB_SUCCESS) {

        sr_log(SR_LOG_ERR, "Couldn't reset device - %s!", libusb_error_name(result));
        return false;

    }

    // Nastavenie aktívnej konfigurácie zariadenia
    result = libusb_set_configuration(devc_usb_handle, 1);
    if (result != LIBUSB_SUCCESS) {

        sr_log(SR_LOG_ERR, "Couldn't get configuration - %s!", libusb_error_name(result));
        return false;

    }  
    
    // Kontrolovonaie aktívneho kernel modulu pre DATA_INTERFACE
    result = libusb_kernel_driver_active(devc_usb_handle, DATA_INTERFACE);
    if (result) {

        result = libusb_detach_kernel_driver(devc_usb_handle, DATA_INTERFACE);
        if (result != LIBUSB_SUCCESS) {

            sr_log(SR_LOG_ERR, "Kernel detach failed - DATA_INTERFACE!");
            return false;

        }

    }

    // Kontrolovonaie aktívneho kernel modulu pre CONTROL_INTERFACE
    result = libusb_kernel_driver_active(devc_usb_handle, CONTROL_INTERFACE);
    if (result) {

        result = libusb_detach_kernel_driver(devc_usb_handle, CONTROL_INTERFACE);
        if (result != LIBUSB_SUCCESS) {

            sr_log(SR_LOG_ERR, "Kernel detach failed - CONTROL_INTERFACE!");
            return false;

        }

    }

    // Získanie USB rozhraní zariadenia ESP32
    // Rozhranie pre tok dát
    result = libusb_claim_interface(devc_usb_handle, DATA_INTERFACE);
    if (result != LIBUSB_SUCCESS) {

        sr_log(SR_LOG_ERR, "Claim interface failed - DATA_INTERFACE!");
        return false;

    }

    // Rozhranie pre tok kontrolnýchsignálov
    result = libusb_claim_interface(devc_usb_handle, CONTROL_INTERFACE);
    if (result != LIBUSB_SUCCESS) {

        sr_log(SR_LOG_ERR, "Claim interface failed - CONTROL_INTERFACE!");
        return false;

    }

    return true;

}

void release_claimed(struct libusb_device_handle * devc_usb_handle) {

    int result = SR_OK;

    // Uvolnenie rozhraní
    // Rozhranie pre tok dát
    result = libusb_release_interface(devc_usb_handle, DATA_INTERFACE);
    if (result != LIBUSB_SUCCESS) {
        sr_log(SR_LOG_ERR, "Error releasing usb interface!");
    }

    // Rozhranie pre tok kontrolnýchsignálov
    result = libusb_release_interface(devc_usb_handle, CONTROL_INTERFACE);
    if (result != LIBUSB_SUCCESS) {
        sr_log(SR_LOG_ERR, "Error releasing usb interface!");
    }

}

// PROCESS: Vytvorenie a ukončenie PulseView session

bool create_session(const struct sr_dev_inst * sdi) {

    int result = SR_OK;

    // Vytvorenie session pre PV
    result = sr_session_source_add (
        sdi->session,
        -1,
        0,
        0,
        acquisition_callback,
        NULL
    );
        
    if (result != SR_OK) {

        sr_log(SR_LOG_ERR, "Couln't CREATE session!");
        return false;

    }

    // Odoslanie signalizačného packetu do PV
    result = std_session_send_df_header(sdi);
    if (result != SR_OK) {

        sr_log(SR_LOG_ERR, "Couln't send session header!");
        return false;

    }

    // Odoslanie signálu na začatie sreamu pre vytvorený session v PV
    result = std_session_send_df_frame_begin(sdi);
    if (result != SR_OK) {

        sr_log(SR_LOG_ERR, "Couln't start session!");
        return false;

    }

    return true;

}

void destroy_session(struct sr_dev_inst * sdi) {

    int result = SR_OK;

    // Odstránenie a vypnutie Callback na spracovanie sigrok session
    if (sdi->session) {

        result = sr_session_source_remove(sdi->session, -1);
        if (result != SR_OK) {

            sr_log(SR_LOG_ERR, "Couln't remove session!");

        } else {

            // Spracovanie zostatkových packetov
            libusb_handle_events(ctx);

        }

    }

    // Odoslanie signálu na skončenie streamu
	result = std_session_send_df_frame_end(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't stop session!");
    }

    // Odoslanie signalizačného packetu do PV
    result = std_session_send_df_end(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't send session end frame!");
    }

}

// PROCESS: Alokovanie a uvolnenie pamäte pre meranie

bool allocate_analog_data(struct dev_context * devc) {

    // Alokovanie pamäte pre merané Analógové veličiny

    devc->voltage_data = (float *) g_malloc0(VOLTAGE_CHANNELS * sizeof(float));
    if (!devc->voltage_data) {

        sr_log(SR_LOG_ERR, "Couldn't allocate buffer for Voltage channels!");
        return false;

    }

    devc->current_data = (float *) g_malloc0(CURRENT_CHANNELS * sizeof(float));
    if (!devc->current_data) {

        sr_log(SR_LOG_ERR, "Couldn't allocate buffer for Current channels!");
        return false;

    }

    return true;

}

void free_analog_data(struct dev_context * devc) {

    // Uvolnenie alokovanej pamäte
    if (devc->voltage_data) {
        g_free(devc->voltage_data);
        devc->voltage_data = NULL;
    }

    if (devc->current_data) {
        g_free(devc->current_data);
        devc->current_data = NULL;
    }

}

// PROCESS: Spracovávanie USB bulk transakcií

bool start_usb_transfer(libusb_device_handle * handle) {

    int result = SR_OK;

    // Inicializovanie asynchrónneho spracovania prijatých USB packetov
    for (int i = 0; i < 4; i++) {
        submit_async_transfer(handle);
    }

    // Odoslanie start bitu na zariadenie -> začne odosielať USB packety
    uint8_t signal [64] = {0};
    signal[0] = START_DATA_TRANSFER;
    int actual_length = 0;

    result = libusb_bulk_transfer (
        handle,
        CONTROL_ENDPOINT_OUT,
        signal,
        64,
        &actual_length,
        1000
    );

    if (result != LIBUSB_SUCCESS) {

        sr_log(SR_LOG_ERR, "Couldn't send start signal - %s!", libusb_error_name(result));
        return false;

    }

    return true;

}

void submit_async_transfer(libusb_device_handle * handle) {

    // Alokovanie asynchrónneho transferu
    struct libusb_transfer * transfer = libusb_alloc_transfer(0);
    if (!transfer) {
        sr_log(SR_LOG_ERR, "Failed to allocate libusb transfer!");
        libusb_free_transfer(transfer);
        return;
    }

    // Alokovanie zásobniku pre alokováný transfer
    uint8_t * buffer = g_malloc0(64 * sizeof(uint8_t));
    if (!buffer) {
        sr_log(SR_LOG_ERR, "Failed to allocate buffer!");
        libusb_free_transfer(transfer);
        return;
    }

    // Inicializovanie alokovaného transferu
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

    // Potvrdenie inicializovaného trasferu pre aktívne USB zariadenie
    const int res = libusb_submit_transfer(transfer);
    if (res != LIBUSB_SUCCESS) {
        sr_log(SR_LOG_ERR, "Failed to submit transfer: %s", libusb_error_name(res));
        free(buffer);
        libusb_free_transfer(transfer);
    }

}

void LIBUSB_CALL async_callback(struct libusb_transfer * transfer) {

    // Uvolniť data a odistť ak nastala chyba
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {

        free(transfer->buffer);
        libusb_free_transfer(transfer);
        sr_log(SR_LOG_ERR, "Error in packet handle");
        return;

    }

    uint8_t * buffer = NULL;

    if (transfer->actual_length > 0) {

        // Packet obsahuje LOGICKÉ DATA
        if (transfer->buffer[0] == 0b00110011 && transfer->buffer[1] == 0b00110011) {
            
            // Spracovať logické data
            buffer = &transfer->buffer[10];

        }
        // Packet obsahuje ANALÓGOVÉ DATA
        else if (transfer->buffer[0] == 0b11001100 && transfer->buffer[1] == 0b11001100) {

            uint8_t offset = 10;
            uint8_t v_ch = 0;
            uint8_t c_ch = 0;

            pthread_mutex_lock(&lock_analog);
        
            // Spracovať analógové data
            for (int i = 0; i < ANALOG_CHANNELS; i++) {

                float value = * ((float *) &transfer->buffer[offset + i * ANALOG_CHANNEL_SIZE + 1]);

                if ((transfer->buffer[offset + i * ANALOG_CHANNEL_SIZE] & CH_MASK) == CURRENT_CH) {

                    // Uloženie hodnoty prúdu na svoje miesto
                    devc_out->current_data[c_ch] = value;
                    c_ch ++;

                } else {

                    // Uloženie hodnoty napätia na svoje miesto
                    devc_out->voltage_data[v_ch] = value;
                    v_ch ++;

                }

            }

            pthread_mutex_unlock(&lock_analog);
            
        }

        if (buffer) send_to_front(buffer, DATA_PAYLOAD_SIZE);

    }

    // Resubmit transfer
    if (atomic_load(running)) submit_async_transfer(transfer->dev_handle);

    // Clean up previous buffer and transfer struct
    free(transfer->buffer);
    libusb_free_transfer(transfer);
}

// PROCESS: Odosielanie packetov na frontend - PulseView

void send_to_front(uint8_t * data, uint16_t size) {

    pthread_mutex_lock(&lock_analog);
    pthread_mutex_lock(&lock_logic);

    for (uint16_t i = 0; i < size; i += UNITS) {

        send_analog_packet();
    
        send_logic_packet(&data[i]);

    }

    pthread_mutex_unlock(&lock_logic);
    pthread_mutex_unlock(&lock_analog);

}

void send_analog_packet(void) {

    int result = SR_OK;

    // Odosielanie packetov s analógovími dátami pre napäťové kanáli
    for (int i = 0; i < VOLTAGE_CHANNELS; i++) {

        // Štruktúra definújúca meranú veličinu, jednotku a charakteristiku signálu pre analógový kanál 
        struct sr_analog_meaning meaning = {
            .mq = SR_MQ_VOLTAGE,
            .unit = SR_UNIT_VOLT,
            .mqflags = SR_MQFLAG_DC,
            .channels = g_slist_nth(voltage_channels, i)
        };
    
        struct sr_analog_spec spec = {
            .spec_digits = 0
        };
    
        // Dáta pre sr packet
        struct sr_datafeed_analog pckt = {
            .data = &devc_out->voltage_data[i],
            .num_samples = 1,
            .encoding = &analog_enc,
            .meaning = &meaning,
            .spec = &spec
        };
    
        // sr packet
        struct sr_datafeed_packet packet = {
            .type = SR_DF_ANALOG,
            .payload = &pckt
        };

        // Odoslanie packetu s dátami na frontend
        result = sr_session_send(sdi_out, &packet);
        if (result == SR_ERR_ARG) {
            sr_log(SR_LOG_ERR, "Couldn't send voltage packet!");
            break;
        }

    }

    // Odosielanie packetov s analógovími dátami pre prúdové kanáli
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

        result = sr_session_send(sdi_out, &packet);
        if (result == SR_ERR_ARG) {
            sr_log(SR_LOG_ERR, "Couldn't send voltage packet!");
            break;
        }

    }

}

void send_logic_packet(void * data) {

    int result = SR_OK;

    // Definovanie dát pre ligické kanály
    struct sr_datafeed_logic pckt_l = {
        .length = UNITS,
        .unitsize = UNITS,
        .data = data
    };

    // Definovanie packetu pre logické kanály
    struct sr_datafeed_packet packet_logic = {
        .type = SR_DF_LOGIC,
        .payload = &pckt_l
    };

    // Odoslanie packetu na frontend
    result = sr_session_send(sdi_out, &packet_logic);
    if (result == SR_ERR_ARG) {
        sr_log(SR_LOG_ERR, "Couldn't send logic packet, invalid argument!");
    }

}

// PROCESS: Riadenie začatia akvizície

int acquisition_callback(int fd, int events, void * cb_data) {

    (void) fd;
    (void) events;
    (void) cb_data;

    //libusb_handle_events_completed(ctx, NULL);
    libusb_handle_events(ctx);

    return RUN;

}
