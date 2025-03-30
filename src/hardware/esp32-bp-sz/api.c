
#include "protocol.h"
#include "configurations.h"

static libusb_device * usb_device = NULL;
static libusb_device_handle * usb_handle = NULL;

// API
// Definícia API funkcií pre libsigrok hardware driver

// PROCESS: Inicializácia

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

    for (size_t i = 0; i < count; i ++) {

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

            struct sr_channel_group * achg = sr_channel_group_new(sdi, "Analog", NULL);
            struct sr_channel_group * lchg = sr_channel_group_new(sdi, "Logic", NULL);
            struct sr_channel * ch = NULL;

            // Pridavanie analógových kanálov

            // Napätie
            for (size_t i = 0; i < VOLTAGE_CHANNELS; i ++) {

                // Nastavenie mena kanálu
                char name[32];
                sprintf(name, "Voltage channel %d", i);

                // Vytvorenie kanálu
                ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, name);
                achg->channels = g_slist_append(achg->channels, ch);

            }

            // Prúd
            for (size_t i = VOLTAGE_CHANNELS; i < ANALOG_CHANNELS; i ++) {

                // Nastavenie mena kanálu
                char name[32];
                sprintf(name, "Current channel %d", i - VOLTAGE_CHANNELS);

                // Vytvorenie kanálu
                ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, name);
                achg->channels = g_slist_append(achg->channels, ch);

            }

            // Pridávanie logických kanálov
            for (size_t i = ANALOG_CHANNELS; i < LOGIC_CHANNELS + ANALOG_CHANNELS; i ++) {

                // Nastavenie mena kanálu
                char name[32];
                sprintf(name, "Channel %d", i - ANALOG_CHANNELS);

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

            libusb_free_device_list(list, 1);

            // Nastavenie sigrok device drivera - sdi
            sdi->inst_type = SR_INST_USB;
	        sdi->status = SR_ST_INITIALIZING;
            sdi->vendor = g_strdup("SZ-BP");
	        sdi->model = g_strdup("ESP32-S3");
            sdi->driver = di;

            return std_scan_complete(di, g_slist_append(NULL, sdi));

        }

    }

    libusb_free_device_list(list, 1);

    return NULL;

}

// PROCESS: Konfigurácia

static int config_get(uint32_t key, GVariant ** data,
	const struct sr_dev_inst * sdi, const struct sr_channel_group * cg) {
    
    return SR_OK;

}

static int config_set(uint32_t key, GVariant * data,
	const struct sr_dev_inst * sdi, const struct sr_channel_group * cg) {

    (void) key;
    (void) data;
    (void) sdi;
    (void) cg;

    // Nič sa nenastavuje
    
    return SR_OK;

}

static int config_list(uint32_t key, GVariant ** data,
	const struct sr_dev_inst * sdi, const struct sr_channel_group * cg) {

    struct dev_context * devc = (struct dev_context *) sdi->priv;
    struct sr_channel * channel = NULL;

    if (cg && cg->channels) {
        channel = cg->channels->data;
    }

    switch (key) {
        case SR_CONF_SCAN_OPTIONS:
        case SR_CONF_DEVICE_OPTIONS: {

            if (!cg) {
                return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
            } else {
                if (ch->type == SR_CHANNEL_ANALOG) {
                    *data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_analog_channel));
                } else {
                    return SR_ERR_NA;
                }
            }
            break;

        }
        default: {
            break;
        }
    }
    
    return SR_OK;

}

// PROCESS: Otvorenie a zatvorenie zariadenia

static int dev_open(struct sr_dev_inst * sdi) {

    struct dev_context * devc = (struct dev_context *) sdi->priv;

    if (!devc && !usb_device) {
        sr_log(SR_LOG_ERR, "The device descriptor is not allocated!");
        return SR_ERR;
    }

    int result = 0;

    result = libusb_open(usb_device, &usb_handle);
    if (result < 0) {
        sdi->status = SR_ST_STOPPING;
        return SR_ERR;
    }

    result = libusb_kernel_driver_active(usb_handle, DATA_INTERFACE);
    if (result == 1) {

        sr_log(SR_LOG_INFO, "Detaching kernel!");
        result = libusb_detach_kernel_driver(usb_handle, DATA_INTERFACE);

        if (result < 0) {

            sr_log(SR_LOG_ERR, "Kernel detach failed!");
            libusb_close(usb_handle);
            return SR_ERR;

        }

    }

    result = libusb_claim_interface(usb_handle, DATA_INTERFACE);
    if (result < 0) {

        sr_log(SR_LOG_ERR, "Claim interface failed!");
        libusb_close(usb_handle);
        return SR_ERR;

    }

    uint8_t line_coding[7] = {
        0x00, 0x1B, 0xB7, 0x00,
        0x00,
        0x00,
        0x08
    };

    result = libusb_control_transfer (
        usb_handle,
        (uint32_t) LIBUSB_REQUEST_TYPE_CLASS | (uint32_t) LIBUSB_RECIPIENT_INTERFACE,
        CDC_REQUEST_SET_LINE_CODING,
        0,
        CONT_INTERFACE,
        line_coding,
        sizeof(line_coding),
        1000
    );
    if (result < 0) {

        sr_log(SR_LOG_ERR, "Couldn't set line coding!");
        libusb_release_interface(usb_handle, DATA_INTERFACE);
        libusb_close(usb_handle);
        return SR_ERR;

    }

    sdi->conn = sr_usb_dev_inst_new (
        libusb_get_bus_number(usb_device),
        libusb_get_device_address(usb_device), 
        usb_handle
    );

    sdi->status = SR_ST_INACTIVE;

    return SR_OK;

}

static int dev_close(struct sr_dev_inst * sdi) {

    int result = 0;

    sdi->status = SR_ST_STOPPING;

    if (usb_handle) {

        result = libusb_release_interface(usb_handle, DATA_INTERFACE);
        if (result != LIBUSB_SUCCESS) {
            sr_log(SR_LOG_ERR, "Error releasing usb interface!");
        }

        result = libusb_attach_kernel_driver(usb_handle, DATA_INTERFACE);
        if (result != LIBUSB_SUCCESS) {
            sr_log(SR_LOG_ERR, "LIBUSB_SUCCESS = %d", LIBUSB_SUCCESS);
            sr_log(SR_LOG_ERR, "Error attaching kernel driver to usb interface!");
        }

        libusb_close(usb_handle);
        usb_handle = NULL;

    }

    if (sdi->conn) {
        sr_usb_dev_inst_free(sdi->conn);
    }

    return SR_OK;

}

// PROCESS: Akvizícia a čítanie

static int dev_acquisition_start(const struct sr_dev_inst * sdi) {

    int result = 0;

    result = libusb_control_transfer(
        usb_handle,
        (uint32_t) LIBUSB_REQUEST_TYPE_CLASS | (uint32_t) LIBUSB_RECIPIENT_INTERFACE,
        CDC_REQUEST_SET_CONTROL_LINE_STATE,
        LINE_STATE_START,
        CONT_INTERFACE,
        NULL,
        0,
        1000
    );
    if (result < 0) {
        sr_log(SR_LOG_ERR, "Couldn't start bulk transfer from recieving device!");
        return SR_ERR;
    }

    struct sr_dev_driver * di = devc->driver;
    struct drv_context * dr_ctx = di->context;
    struct sr_context * sr_ctx = dr_ctx->sr_ctx;

    result = usb_source_add (
        sdi->session,
        sr_ctx,
        1000,
        acquisition_callback,
        sdi
    );

    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't create session!");
        return SR_ERR;
    }

    result = std_session_send_df_header(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't send session header!");
        return SR_ERR;
    }

    result = std_session_send_df_frame_begin(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't start session!");
        return SR_ERR;
    }

    sdi->status = SR_ST_ACTIVE;
    return SR_OK;

}

static int dev_acquisition_stop(struct sr_dev_inst * sdi) {

    sdi->status = SR_ST_INACTIVE;

    int result = 0;

    result = libusb_control_transfer(
        devc->usb_handle,
        (uint32_t) LIBUSB_REQUEST_TYPE_CLASS | (uint32_t) LIBUSB_RECIPIENT_INTERFACE,
        CDC_REQUEST_SET_CONTROL_LINE_STATE,
        LINE_STATE_STOP,
        CONT_INTERFACE,
        NULL,
        0,
        1000
    );
    if (result < 0) {
        sr_log(SR_LOG_ERR, "Couldn't stop bulk transfer from recieving device!");
        return SR_ERR;
    }

    struct sr_dev_driver * di = devc->driver;
    struct drv_context * dr_ctx = di->context;
    struct sr_context * sr_ctx = dr_ctx->sr_ctx;

	result = std_session_send_df_frame_end(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't stop session!");
        return result;
    }

    result = std_session_send_df_end(sdi);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't send session end frame!");
        return result;
    }

    result = usb_source_remove(sdi->session, sr_ctx);
    if (result != SR_OK) {
        sr_log(SR_LOG_ERR, "Couln't remove session!");
        return result;
    }

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
