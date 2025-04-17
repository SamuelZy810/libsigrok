
#include "protocol.h"
#include "configurations.h"

// Statické premenné, ukazovatele na libusb štruktúry pre jednoduchšie alokovanie
static struct libusb_context * lib_ctx = NULL;
static struct libusb_device * usb_device = NULL;
static struct libusb_device_handle * usb_handle = NULL;

// API
// Definícia API funkcií pre libsigrok hardware driverS

// PROCESS: Inicializácia

/**
* @brief Inicializácia libusb a sigrok ovládaču
* @param di je povinný parameter, štruktúra predstavujúca ovládač "driver" zariadenia
* @param sr_ctx je povinný parameter, predtavuje kontext pre sigrok
*/
static int init(struct sr_dev_driver * di, struct sr_context * sr_ctx) {

    // Inicializovanie sigrok kontextu pre tento ovládač
    int res = std_init(di, sr_ctx);

    if (res != SR_OK) {
        return res;
    }

    // Inicializovanie libusb kontextu, štruktúra libusb_context
    res = libusb_init(&sr_ctx->libusb_ctx);

    if (res != LIBUSB_SUCCESS) {
        return SR_ERR;
    }

    // Uloženie smerníka na 
    lib_ctx = sr_ctx->libusb_ctx;

    return SR_OK;

}

/**
* @brief Opak inicializácie, uzavrie sa libusb kontext a sigrok kontext
* @param di je povinný parameter, ktorý predstavuje ovládač aktuálneho zariadenia
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

/**
* @brief Vyhľadávanie zariadenia, zabezpečuje, že libsigrok nájde zariadenie, taktiež ak je zariadenie nájdené, následne sa inicializujú kanáli pre PlseView
* @param di je povinný parameter, štruktúra predstavujúca ovládač "driver" zariadenia
* @param sr_ctx je povinný parameter, predtavuje kontext pre sigrok
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
            for (size_t i = 0; i < CURRENT_CHANNELS; i ++) {

                // Nastavenie mena kanálu
                char name[32];
                sprintf(name, "Current ch %lu", i);

                // Vytvorenie kanálu
                ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, name);
                cchg->channels = g_slist_append(cchg->channels, ch);

            }

            uint8_t logic_name = 0;
            // Pridávanie logických kanálov
            for (int i = LOGIC_CHANNELS - 1; i >= 0; i --) {

                // Nastavenie mena kanálu
                char name[32];
                sprintf(name, "Logic ch %u", logic_name);

                // Vytvorenie kanálu
                ch = sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, name);
                lchg->channels = g_slist_append(lchg->channels, ch);

                logic_name ++;

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
            sdi->session = NULL;

            libusb_free_device_list(list, 1);

            return std_scan_complete(di, g_slist_append(NULL, sdi));

        }

    }

    libusb_free_device_list(list, 1);

    return NULL;

}

// PROCESS: Konfigurácia

/**
* @brief Funkcia na získanie konfigurácie zariadenia, kanálov alebo PulseView aplikácie
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
            *data = g_variant_new_uint64(SR_KHZ(SAMPLE_RATE));
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

/**
* @brief Funkcia na nastavenie konfigurácie zariadenia, kanálov alebo PulseView aplikácie
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

/**
* @brief Funkcia na získanie všetkých možných konfigurácii
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
            uint64_t samplerates[] = {SR_KHZ(SAMPLE_RATE)};
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

/**
* @brief Otvorenie USB zariadenia nájdeného vo funkcii init()
* @param sdi je inštancia zariadenia
*/
static int dev_open(struct sr_dev_inst * sdi) {

    struct dev_context * devc = (struct dev_context *) sdi->priv;

    if (!devc && !usb_device) {
        sr_log(SR_LOG_ERR, "The device descriptor is not allocated!");
        return SR_ERR;
    }

    int result = SR_OK;

    result = libusb_open(usb_device, &usb_handle);
    if (result != LIBUSB_SUCCESS) {
        sdi->status = SR_ST_STOPPING;
        return SR_ERR;
    }

    devc->usb_handle = usb_handle;

    sdi->status = SR_ST_ACTIVE;

    return SR_OK;

}

/**
* @brief Na zatvorenie a dealokovanie USB zariadenia
* @param sdi je inštancia zariadenia
*/
static int dev_close(struct sr_dev_inst * sdi) {

    struct dev_context * devc = (struct dev_context *) sdi->priv;

    if (!devc && !usb_device) {
        sr_log(SR_LOG_ERR, "The device descriptor is not allocated!");
        return SR_ERR;
    }

    sdi->status = SR_ST_STOPPING;

    if (usb_handle) {

        libusb_close(usb_handle);
        usb_handle = NULL;

    }

    return SR_OK;

}

// PROCESS: Akvizícia a čítanie

/**
* @brief začatie prenosu, inicializovanie potrebných premenných, nastavenie zariadenia a vytvorenie relácie v PV
* @param sdi je inštancia zariadenia
*/
static int dev_acquisition_start(const struct sr_dev_inst * sdi) {

    // Inicializácia potrebných premenných
    struct dev_context * devc = (struct dev_context *) sdi->priv;

    // Zapnutie procesu
    atomic_store(&devc->running, true);

    // Inicializácia mutexu
    if (!init_mutex()) {
        return SR_ERR;
    }

    // Inicializácia premenných pre protocol
    if (!init_it(sdi, lib_ctx)) {
        return SR_ERR;
    }

    // Inicializácia zariadenia pred čítaním
    if (!reset_and_claim(devc->usb_handle)) {
        return SR_ERR;
    }

    // Alokovanie pamäte pre merané Analógové veličiny
    if (!allocate_analog_data(devc)) {
        return SR_ERR;
    }

    // Zapnutie USB prenosu
    if (!start_usb_transfer(devc->usb_handle)) {
        return SR_ERR;
    }

    // Vytvorenie PulseView aktívnej relácie
    if (!create_session(sdi)) {
        return SR_ERR;
    }

    return SR_OK;

}

/**
* @brief ukončenie prenosu, uvolnenie zariadenia a skončenie PV relácie
* @param sdi je inštancia zariadenia
*/
static int dev_acquisition_stop(struct sr_dev_inst * sdi) {

    // Inicializácia potrebných premenných
    struct dev_context * devc = (struct dev_context *) sdi->priv;

    // Vypnutie procesu
    atomic_store(&devc->running, false);

    // Vypnutie PulseView relácie
    destroy_session(sdi);

    // Uvolnenie rozhraní
    release_claimed(devc->usb_handle);

    // Uvolenie mutexu
    destroy_mutex();

    // Uvolnenie alokovanej pamäte
    free_analog_data(devc);

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
