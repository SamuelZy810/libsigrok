
#include "protocol.h"
#include "configurations.h"

static struct libusb_device * usb_device = NULL;
static struct libusb_device_handle * usb_handle = NULL;

// API
// Definícia API funkcií pre libsigrok hardware driver

// PROCESS: Inicializácia

/*
* @brief
* @param
* @param
*/
static int init(struct sr_dev_driver * di, struct sr_context * sr_ctx) {

    int res = std_init(di, sr_ctx);

    if (res != SR_OK) {
        return res;
    }

    res = libusb_init(&sr_ctx->libusb_ctx);

    if (res != LIBUSB_SUCCESS) {
        return SR_ERR;
    }

    return SR_OK;

}

/*
* @brief
* @param
*/
static int cleanup(const struct sr_dev_driver * di) {

    if (!di || !di->context) {
        return SR_ERR_ARG;
    }

    struct drv_context * drv_context = di->context;

    if (!drv_context) {
        return std_cleanup(di);
    }

    if (usb_device) {
        libusb_unref_device(usb_device);
        usb_device = NULL;
    }

    struct sr_context * sr_ctx = drv_context->sr_ctx;

    if (sr_ctx && sr_ctx->libusb_ctx) {
        libusb_exit(sr_ctx->libusb_ctx);
        sr_ctx->libusb_ctx = NULL;
    }

    return std_cleanup(di);

}

/*
* @brief
* @param
* @param
*/
static GSList * scan(struct sr_dev_driver * di, GSList * options) {

    (void) options;

    struct drv_context * dr_ctx = di->context;
    struct sr_context * sr_ctx = dr_ctx->sr_ctx;

    if (!sr_ctx) {
        sr_log(SR_LOG_ERR, "Context has not been initialized");
        return NULL;
    }

    libusb_context * ctx = sr_ctx->libusb_ctx;

    if (!ctx) {
        sr_log(SR_LOG_ERR, "libusb has not been initialized");
        return NULL;
    }

    libusb_device ** list = NULL;
    ssize_t count = libusb_get_device_list(ctx, &list);

    if (count < 0) {
        return NULL;
    }

    for (ssize_t i = 0; i < count; i ++) {

        libusb_device * device = list[i];
        struct libusb_device_descriptor descriptor;

        if (libusb_get_device_descriptor(device, &descriptor) < 0) {
            continue;
        }

        // Hladanie zariadenia ESP32
        if (descriptor.idVendor == VEND_ID && descriptor.idProduct == PROD_ID) {

            // Alokovanie
            struct sr_dev_inst * sdi = g_malloc0(sizeof(struct sr_dev_inst));
            if (!sdi) {
                sr_log(SR_LOG_ERR, "Couldn't allocate SDI!");
                return NULL;
            }

            struct dev_context * devc = g_malloc0(sizeof(struct dev_context));
            if (!devc) {
                sr_log(SR_LOG_ERR, "Couldn't allocate DEVC!");
                return NULL;
            }

            sdi->priv = devc;

            struct sr_channel_group * vchg = sr_channel_group_new(sdi, "Voltage", NULL);
            struct sr_channel_group * cchg = sr_channel_group_new(sdi, "Current", NULL);
            struct sr_channel_group * lchg = sr_channel_group_new(sdi, "Logic", NULL);
            struct sr_channel * ch = NULL;

            // Pridavanie analógových kanálov

            // Napätie
            for (size_t i = 0; i < VOLTAGE_CHANNELS; i ++) {

                // Nastavenie mena kanálu
                char name[32];
                sprintf(name, "Voltage ch %lu", i);

                // Vytvorenie kanálu
                ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, name);
                vchg->channels = g_slist_append(vchg->channels, ch);

            }

            // Prúd
            for (size_t i = VOLTAGE_CHANNELS; i < ANALOG_CHANNELS; i ++) {

                // Nastavenie mena kanálu
                char name[32];
                sprintf(name, "Current ch %lu", i - VOLTAGE_CHANNELS);

                // Vytvorenie kanálu
                ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, name);
                cchg->channels = g_slist_append(cchg->channels, ch);

            }

            // Pridávanie logických kanálov
            for (size_t i = ANALOG_CHANNELS; i < LOGIC_CHANNELS + ANALOG_CHANNELS; i ++) {

                // Nastavenie mena kanálu
                char name[32];
                sprintf(name, "Logic ch %lu", i - ANALOG_CHANNELS);

                // Vytvorenie kanálu
                ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, name);
                lchg->channels = g_slist_append(lchg->channels, ch);

            }

            // Nastavenie device controller štruktúry - devc
            devc->samplerate = SR_KHZ(500);
            devc->num_log_ch = LOGIC_CHANNELS;

            devc->num_cur_ch = VOLTAGE_CHANNELS;
            devc->voltage_quantity = VOLTS;

            devc->num_vol_ch = CURRENT_CHANNELS;
            devc->current_quantity = MILI_AMPERES;

            devc->driver = di;
            usb_device = libusb_ref_device(device);

            // Nastavenie sigrok device drivera - sdi
            sdi->inst_type = SR_INST_USB;
	        sdi->status = SR_ST_INITIALIZING;
            sdi->vendor = g_strdup("SZ-BP");
	        sdi->model = g_strdup("ESP32-S3");
            sdi->driver = di;

            sdi->conn = sr_usb_dev_inst_new (
                libusb_get_bus_number(usb_device),
                libusb_get_device_address(usb_device), 
                usb_handle
            );

            libusb_free_device_list(list, 1);

            return std_scan_complete(di, g_slist_append(NULL, sdi));

        }

    }

    libusb_free_device_list(list, 1);

    return NULL;

}

// PROCESS: Konfigurácia

/*
* @brief
* @param
* @param
* @param
* @param
*/
static int config_get(uint32_t key, GVariant ** data,
	const struct sr_dev_inst * sdi, const struct sr_channel_group * cg) {

    struct dev_context * devc = (struct dev_context *) sdi->priv;

    switch (key) {

        case SR_CONF_MEASURED_QUANTITY: {

            if (!cg) {
                return SR_ERR_CHANNEL_GROUP;
            }

            GVariant * arr[2];

            if (strcmp(cg->name, "Voltage") == 0) {

                arr[0] = g_variant_new_uint32(SR_MQ_VOLTAGE);
		        arr[1] = g_variant_new_uint64(SR_MQFLAG_DC);
                *data = g_variant_new_tuple(arr, 2);

            } else if (strcmp(cg->name, "Current") == 0) {

                arr[0] = g_variant_new_uint32(SR_MQ_CURRENT);
		        arr[1] = g_variant_new_uint64(SR_MQFLAG_DC);
                *data = g_variant_new_tuple(arr, 2);

            } else {

                return SR_ERR_NA;

            }
            break;
        }
        case SR_CONF_SAMPLERATE: {
            *data = g_variant_new_uint64(SR_KHZ(10));
            break;
        }
        case SR_CONF_CONTINUOUS: {
            *data = g_variant_new_boolean(true);
            break;
        }
        case SR_CONF_NUM_LOGIC_CHANNELS: {
            *data = g_variant_new_uint32(devc->num_log_ch);
            break;
        }
        case SR_CONF_NUM_ANALOG_CHANNELS: {
            *data = g_variant_new_uint32(devc->num_cur_ch + devc->num_vol_ch);
            break;
        }
        default: {
            return SR_ERR_NA;
        }

    }
    
    return SR_OK;

}

/*
* @brief
* @param
* @param
* @param
* @param
*/
static int config_set(uint32_t key, GVariant * data,
	const struct sr_dev_inst * sdi, const struct sr_channel_group * cg) {

    (void) data;
    (void) sdi;
    (void) cg;

    // Nič sa nenastavuje
    switch (key) {

        case SR_CONF_MEASURED_QUANTITY: {
            break;
        }
        case SR_CONF_SAMPLERATE: {
            break;
        }
        case SR_CONF_CONTINUOUS: {
            break;
        }
        case SR_CONF_NUM_LOGIC_CHANNELS: {
            break;
        }
        case SR_CONF_NUM_ANALOG_CHANNELS: {
            break;
        }
        default: {
            return SR_ERR_NA;
        }

    }
    
    return SR_OK;

}

/*
* @brief
* @param
* @param
* @param
* @param
*/
static int config_list(uint32_t key, GVariant ** data,
	const struct sr_dev_inst * sdi, const struct sr_channel_group * cg) {

    struct dev_context * devc = (struct dev_context *) sdi->priv;
    struct sr_channel * channel = NULL;

    (void) devc;

    if (cg && cg->channels) {
        channel = cg->channels->data;
    }

    switch (key) {
        case SR_CONF_SCAN_OPTIONS:
        case SR_CONF_DEVICE_OPTIONS: {

            if (!cg) {
                return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
            } else {
                if (channel->type == SR_CHANNEL_ANALOG) {
                    *data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_analog_channel));
                } else {
                    return SR_ERR_NA;
                }
            }
            break;

        }
        case SR_CONF_SAMPLERATE: {
            uint64_t samplerates[] = {SR_KHZ(10)};
            *data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
            break;
        }
        case SR_CONF_CONTINUOUS:{
            *data = g_variant_new_boolean(TRUE);
            break;
        }
        default: {
            return SR_ERR_NA;
        }
    }
    
    return SR_OK;

}

// PROCESS: Otvorenie a zatvorenie zariadenia

/*
* @brief
* @param
*/
static int dev_open(struct sr_dev_inst * sdi) {

    struct dev_context * devc = (struct dev_context *) sdi->priv;

    if (!devc && !usb_device) {
        sr_log(SR_LOG_ERR, "The device descriptor is not allocated!");
        return SR_ERR;
    }

    int result = 0;

    result = libusb_open(usb_device, &usb_handle);
    if (result != LIBUSB_SUCCESS) {
        sdi->status = SR_ST_STOPPING;
        return SR_ERR;
    }

    // Resetovanie USB kontextu pre zariadenia
    /*result = libusb_reset_device(usb_handle);
    if (result != LIBUSB_SUCCESS) {
        sr_log(SR_LOG_ERR, "Couldn't reset device - %s!", libusb_error_name(result));
        return SR_ERR;
    }*/

    result = libusb_set_configuration(usb_handle, 1);
    if (result != LIBUSB_SUCCESS) {
        sr_log(SR_LOG_ERR, "Couldn't get configuration - %s!", libusb_error_name(result));
        libusb_close(usb_handle);
        return SR_ERR;
    }  
    
    result = libusb_kernel_driver_active(usb_handle, DATA_INTERFACE);
    if (result) {

        result = libusb_detach_kernel_driver(usb_handle, DATA_INTERFACE);
        if (result != LIBUSB_SUCCESS) {
            sr_log(SR_LOG_ERR, "Kernel detach failed - DATA_INTERFACE!");
            libusb_close(usb_handle);
            return SR_ERR;
        }

    }

    result = libusb_kernel_driver_active(usb_handle, CONTROL_INTERFACE);
    if (result) {

        result = libusb_detach_kernel_driver(usb_handle, CONTROL_INTERFACE);
        if (result != LIBUSB_SUCCESS) {
            sr_log(SR_LOG_ERR, "Kernel detach failed - CONTROL_INTERFACE!");
            libusb_close(usb_handle);
            return SR_ERR;
        }

    }

    // Získanie USB interfacov zariadenia ESP32
    result = libusb_claim_interface(usb_handle, DATA_INTERFACE);
    if (result != LIBUSB_SUCCESS) {

        sr_log(SR_LOG_ERR, "Claim interface failed - DATA_INTERFACE!");
        destroy_mutex();
        return SR_ERR;

    }

    result = libusb_claim_interface(usb_handle, CONTROL_INTERFACE);
    if (result != LIBUSB_SUCCESS) {

        sr_log(SR_LOG_ERR, "Claim interface failed - CONTROL_INTERFACE!");
        libusb_release_interface(usb_handle, DATA_INTERFACE);
        destroy_mutex();
        return SR_ERR;

    }

    devc->usb_handle = usb_handle;

    sdi->status = SR_ST_ACTIVE;

    return SR_OK;

}

/*
* @brief
* @param
*/
static int dev_close(struct sr_dev_inst * sdi) {

    struct dev_context * devc = (struct dev_context *) sdi->priv;

    if (!devc && !usb_device) {
        sr_log(SR_LOG_ERR, "The device descriptor is not allocated!");
        return SR_ERR;
    }

    int result = 0;

    sdi->status = SR_ST_STOPPING;

    if (usb_handle) {
        
        // Uvolnenie rozhraní
        result = libusb_release_interface(devc->usb_handle, DATA_INTERFACE);
        if (result != LIBUSB_SUCCESS) {
            sr_log(SR_LOG_ERR, "Error releasing usb interface!");
        }
    
        result = libusb_release_interface(devc->usb_handle, CONTROL_INTERFACE);
        if (result != LIBUSB_SUCCESS) {
            sr_log(SR_LOG_ERR, "Error releasing usb interface!");
        }

        libusb_close(usb_handle);
        usb_handle = NULL;

    }

    return SR_OK;

}

// PROCESS: Akvizícia a čítanie

/*
* @brief
* @param
*/
static int dev_acquisition_start(const struct sr_dev_inst * sdi) {

    // Inicializácia potrebných premennách
    struct dev_context * devc = (struct dev_context *) sdi->priv;
    int result = 0;

    // Inicializácia mutexu a premenných pre protocol
    init_mutex();
    init_it(sdi);

    // Zapnutie procesu
    atomic_store(&devc->running, true);

    // Alokovanie pamäte pre merané Analógové veličiny
    devc->voltage_data = (float *) g_malloc0(VOLTAGE_CHANNELS * sizeof(float));
    if (!devc->voltage_data) {
        sr_log(SR_LOG_ERR, "Couldn't allocate buffer for Voltage channels!");

        libusb_release_interface(devc->usb_handle, DATA_INTERFACE);
        libusb_release_interface(devc->usb_handle, CONTROL_INTERFACE);
        destroy_mutex();

        return SR_ERR;
    }

    devc->current_data = (float *) g_malloc0(CURRENT_CHANNELS * sizeof(float));
    if (!devc->current_data) {
        sr_log(SR_LOG_ERR, "Couldn't allocate buffer for Current channels!");

        g_free(devc->voltage_data);

        libusb_release_interface(devc->usb_handle, DATA_INTERFACE);
        libusb_release_interface(devc->usb_handle, CONTROL_INTERFACE);
        destroy_mutex();

        return SR_ERR;
    }

    /*// Inicializovanie asynchrónneho spracovania prijatých USB packetov
    for (int i = 0; i < 4; i++) {
        submit_async_transfer(usb_handle);
    }

    while (true) {
        libusb_handle_events_completed(NULL, NULL);
    }*/

    // Vytvorenie callback funkcie pre spracovanie PulseView session
    struct sr_dev_driver * di = devc->driver;
    struct drv_context * dr_ctx = di->context;
    struct sr_context * sr_ctx = dr_ctx->sr_ctx;

    result = usb_source_add (
        sdi->session,
        sr_ctx,
        1000,
        acquisition_callback,
        NULL
    );

    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't create session!");

        g_free(devc->voltage_data);
        g_free(devc->current_data);

        destroy_mutex();

        result = usb_source_remove(sdi->session, sr_ctx);
        if (result != SR_OK) {
            sr_log(SR_LOG_ERR, "Couln't remove session!");
        }

        return SR_ERR;
    }

    // Odoslanie signalizačného packetu do PV
    result = std_session_send_df_header(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't send session header!");

        g_free(devc->voltage_data);
        g_free(devc->current_data);

        destroy_mutex();

        result = usb_source_remove(sdi->session, sr_ctx);
        if (result != SR_OK) {
            sr_log(SR_LOG_ERR, "Couln't remove session!");
        }

        return SR_ERR;
    }

    // Odoslanie signálu na začatie sreamu pre vytvorený session v PV
    result = std_session_send_df_frame_begin(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't start session!");

        g_free(devc->voltage_data);
        g_free(devc->current_data);

        destroy_mutex();

        result = usb_source_remove(sdi->session, sr_ctx);
        if (result != SR_OK) {
            sr_log(SR_LOG_ERR, "Couln't remove session!");
        }

        return SR_ERR;
    }

    // Odoslanie start bitu na zariadenie -> začne odosielať USB packety
    uint8_t signal [64] = {0};
    signal[0] = START_DATA_TRANSFER;
    int actual_length = 0;

    result = libusb_bulk_transfer (
        devc->usb_handle,
        CONTROL_ENDPOINT_OUT,
        signal,
        64,
        &actual_length,
        1000
    );

    if (result != LIBUSB_SUCCESS) {
        sr_log(SR_LOG_ERR, "Couldn't send start signal - %s!", libusb_error_name(result));

        g_free(devc->voltage_data);
        g_free(devc->current_data);
        
        destroy_mutex();

        return SR_ERR;
    }

    return SR_OK;

}

/*
* @brief
* @param
*/
static int dev_acquisition_stop(struct sr_dev_inst * sdi) {

    struct dev_context * devc = (struct dev_context *) sdi->priv;

    // Vypnutie procesu
    atomic_store(&devc->running, false);

    int result = 0;
    struct sr_dev_driver * di = devc->driver;
    struct drv_context * dr_ctx = di->context;
    struct sr_context * sr_ctx = dr_ctx->sr_ctx;

    // Odoslanie signálu na skončenie streamu
	result = std_session_send_df_frame_end(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't stop session!");
        return result;
    }

    // Odoslanie signalizačného packetu do PV
    result = std_session_send_df_end(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't send session end frame!");
        return result;
    }

    // Odstránenie a vypnutie Callback na spracovanie sigrok session
    result = usb_source_remove(sdi->session, sr_ctx);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't remove session!");
        return result;
    }

    // Spracovanie zostatkových packetov - všetkých, aj nedokončených
    //libusb_handle_events(NULL);

    // Uvolenie mutexu
    destroy_mutex();

    // Uvolnenie alokovanej pamäte
    g_free(devc->voltage_data);
    g_free(devc->current_data);

    return SR_OK;

}

// Driver Context
// Definície štruktúri pre libsigrok driver pre zariadenie ESP32

static struct sr_dev_driver esp32_samuel_zaprazny = {
    .name                   = "ESP32 BP",
    .longname               = "ESP32-S3 Device Driver Bachelor's thesis - Samuel Zaprazny",
    .api_version            = 1,
	.init                   = init,
	.cleanup                = cleanup,
	.scan                   = scan,
	.dev_list               = std_dev_list,
	.dev_clear              = std_dev_clear,
	.config_get             = config_get,
	.config_set             = config_set,
	.config_list            = config_list,
	.dev_open               = dev_open,
	.dev_close              = dev_close,
	.dev_acquisition_start  = dev_acquisition_start,
	.dev_acquisition_stop   = dev_acquisition_stop,
	.context                = NULL,
};

// Registrácia HW drivera

SR_REGISTER_DEV_DRIVER(esp32_samuel_zaprazny);
