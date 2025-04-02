
#ifndef ESP_CONFIG_H
#define ESP_CONFIG_H

static const uint32_t scanopts[] = {
	SR_CONF_NUM_LOGIC_CHANNELS,
	SR_CONF_NUM_ANALOG_CHANNELS
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_OSCILLOSCOPE
};

static const uint32_t devopts[] = {
	SR_CONF_SAMPLERATE          | SR_CONF_LIST | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONTINUOUS          | SR_CONF_LIST | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_analog_channel[] = {
    SR_CONF_MEASURED_QUANTITY   | SR_CONF_GET | SR_CONF_SET,
};

#endif
