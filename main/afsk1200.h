#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ws.h"

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
