
#ifndef CHANNEL_CONFIG_H
#define CHANNEL_CONFIG_H

// Počet kanálov pre napäťové merania
#define VOLTAGE_CHANNELS 1

// Počet kanálov pre prúdové merania
#define CURRENT_CHANNELS 1

// Počet kanálov pre logické merania
#define LOGIC_CHANNELS 8

// Počet analógových kanálov spolu
#define ANALOG_CHANNELS (VOLTAGE_CHANNELS + CURRENT_CHANNELS)

// Meraná frekvencia v kHz
#define SAMPLE_RATE 10

#endif
