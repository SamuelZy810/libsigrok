
#include "protocol.h"

// API
// Definícia API funkcií pre libsigrok hardware driver

// PROCESS: Inicializácia

static int init(struct sr_dev_driver * di, struct sr_context * sr_ctx) {

    return SR_OK;

}

static int cleanup(const struct sr_dev_driver * di) {

    return SR_OK;

}

static GSList * scan(struct sr_dev_driver * di, GSList * options) {

    return NULL;

}

// PROCESS: Konfigurácia

static int config_get(uint32_t key, GVariant ** data,
	const struct sr_dev_inst * sdi, const struct sr_channel_group * cg) {
    
    return SR_OK;

}

static int config_set(uint32_t key, GVariant * data,
	const struct sr_dev_inst * sdi, const struct sr_channel_group *cg) {
    
    return SR_OK;

}

static int config_list(uint32_t key, GVariant ** data,
	const struct sr_dev_inst * sdi, const struct sr_channel_group * cg) {
    
    return SR_OK;

}

// PROCESS: Akvizícia a čítanie

static int dev_acquisition_start(const struct sr_dev_inst * sdi) {

    return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst * sdi) {

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
