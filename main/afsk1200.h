#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "ws.h"

// parameters
#define SAMPLE_RATE 16000
#define BAUD_RATE 1200
#define MARK_FREQ 1200
#define SPACE_FREQ 2200
#define TABLE_SIZE 256
#define AMPLITUDE 32767

// sin table
extern int16_t sin_table[TABLE_SIZE];
void afsk_init(void);

typedef struct
{
    uint8_t *data;
    size_t len;
} afsk_data_t;

// data types
#define AFSK_DATA_BIN 0x01
#define AFSK_DATA_APRS 0x02
// data fields
#define APRS_FIELD_TOCALL 0x11
#define APRS_FIELD_FROMCALL 0x12
#define APRS_FIELD_PATH 0x13
#define APRS_FIELD_DATA 0x14

// functions
uint16_t get_crc_16_ccitt_x25(uint8_t *data, size_t byte_len);
size_t bit_stuff(const uint8_t *source, size_t bit_len, uint8_t *dest);
size_t add_frame_flag(uint8_t *source, size_t bit_len);
void nrzi_modulate(uint8_t *source, size_t bit_len);
void afsk1200_to_pwm(uint8_t *source, size_t bit_len);
