#include "afsk1200.h"

#define GET_BIT(s, i) (((s)[(i) / 8] >> ((i) % 8)) & 1)
#define SET_BIT(s, i, v) ((s)[(i) / 8] = ((s)[(i) / 8] & ~(1 << ((i) % 8))) | ((v) << ((i) % 8)))

// 2200Hz
static int16_t SPACE_TONE_TABLE[22] = {
    -32767,
    -21280,
    5126,
    27938,
    31163,
    12539,
    -14876,
    -31862,
    -26509,
    -2571,
    23170,
    32666,
    19260,
    -7649,
    -29196,
    -30273,
    -10126,
    17121,
    32364,
    24916,
    0,
    -24916,
};
static const uint8_t SPACE_TONE_TABLE_FIRST_MAXIMUM_INDEX = 4;
static const uint8_t SPACE_TONE_TABLE_SECOND_PERIOD_INDEX = 8;
// 1200Hz
static int16_t MARK_TONE_TABLE[28] = {
    -32767,
    -29196,
    -19260,
    -5126,
    10126,
    23170,
    31163,
    32364,
    26509,
    14876,
    0,
    -14876,
    -26509,
    -32364,
    -31163,
    -23170,
    -10126,
    5126,
    19260,
    29196,
    32767,
    29196,
    19260,
    5126,
    -10126,
    -23170,
    -31163,
    -32364,
};
static const uint8_t MARK_TONE_TABLE_FIRST_MAXIMUM_INDEX = 7;
static const uint8_t MARK_TONE_TABLE_SECOND_PERIOD_INDEX = 14;

uint16_t get_crc_16_ccitt_x25(uint8_t *data, size_t byte_len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < byte_len; ++i)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; ++j)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0x8408;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

size_t bit_stuff(const uint8_t *source, size_t bit_len, uint8_t *dest)
{
    uint8_t count = 0;
    size_t write_index = 0;
    for (size_t i = 0; i < bit_len; ++i)
    {
        if (GET_BIT(source, i))
        {
            ++count;
            SET_BIT(dest, write_index, 1);
            ++write_index;
            if (count == 5)
            {
                SET_BIT(dest, write_index, 0);
                ++write_index;
                count = 0;
            }
        }
        else
        {
            SET_BIT(dest, write_index, 0);
            ++write_index;
            count = 0;
        }
    }
    return write_index;
}

size_t add_frame_flag(uint8_t *source, size_t bit_len)
{
    memset(source, 0x7E, 64);
    SET_BIT(source, bit_len + 8, 0);
    SET_BIT(source, bit_len + 9, 1);
    SET_BIT(source, bit_len + 10, 1);
    SET_BIT(source, bit_len + 11, 1);
    SET_BIT(source, bit_len + 12, 1);
    SET_BIT(source, bit_len + 13, 1);
    SET_BIT(source, bit_len + 14, 1);
    SET_BIT(source, bit_len + 15, 0);
    return bit_len + 520;
}

void nrzi_modulate(uint8_t *source, size_t bit_len)
{
    bool state = 1;
    for (size_t i = 0; i < bit_len; ++i)
    {
        if (!GET_BIT(source, i))
        {
            state = !state;
        }
        SET_BIT(source, i, state);
    }
}

void afsk1200_to_pwm(uint8_t *source, size_t bit_len)
{
    uint8_t point_count = 0;
    uint8_t tone_index;
    int16_t last_point = 0;
    bool decreasing = false;
    int16_t send_buf[512] = {0};
    size_t send_buf_write_index;
    send_to_queue(pwm_write_queue_handle, send_buf, sizeof(send_buf), 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    for (size_t i = 0; i < bit_len; i += 36) // every 36 bits
    {
        memset(send_buf, 0, sizeof(send_buf)); // clear the buf
        send_buf_write_index = 0;
        for (size_t j = 0; j < 36 && i + j < bit_len; ++j)
        {
            ++point_count;
            if (GET_BIT(source, i + j)) // generate pcm stream
            {
                // mark tone
                if (decreasing)
                {
                    for (tone_index = MARK_TONE_TABLE_FIRST_MAXIMUM_INDEX;
                         MARK_TONE_TABLE[tone_index] >= last_point && tone_index < MARK_TONE_TABLE_SECOND_PERIOD_INDEX;
                         ++tone_index)
                        ;
                }
                else
                {
                    for (tone_index = 0;
                         MARK_TONE_TABLE[tone_index] <= last_point && tone_index < MARK_TONE_TABLE_FIRST_MAXIMUM_INDEX;
                         ++tone_index)
                        ;
                }
                // 找到相位连续起始点, 向后查表填充一个码元
                for (int k = 0; k < 13 + point_count / 3; ++k)
                    send_buf[send_buf_write_index++] = MARK_TONE_TABLE[tone_index++];
                last_point = MARK_TONE_TABLE[tone_index - 1];
                decreasing = MARK_TONE_TABLE[tone_index - 2] > MARK_TONE_TABLE[tone_index - 1];
            }
            else
            {
                // space tone
                if (decreasing)
                {
                    for (tone_index = SPACE_TONE_TABLE_FIRST_MAXIMUM_INDEX;
                         SPACE_TONE_TABLE[tone_index] >= last_point && tone_index < SPACE_TONE_TABLE_SECOND_PERIOD_INDEX;
                         ++tone_index)
                        ;
                }
                else
                {
                    for (tone_index = 0;
                         SPACE_TONE_TABLE[tone_index] <= last_point && tone_index < SPACE_TONE_TABLE_FIRST_MAXIMUM_INDEX;
                         ++tone_index)
                        ;
                }
                // 找到相位连续起始点, 向后查表填充一个码元
                for (int k = 0; k < 13 + point_count / 3; ++k)
                    send_buf[send_buf_write_index++] = SPACE_TONE_TABLE[tone_index++];
                last_point = SPACE_TONE_TABLE[tone_index - 1];
                decreasing = SPACE_TONE_TABLE[tone_index - 2] > SPACE_TONE_TABLE[tone_index - 1];
            }
            if (point_count >= 3)
                point_count = 0;
        }
        send_to_queue(pwm_write_queue_handle, send_buf, send_buf_write_index * 2, 0); // write to pwm
        vTaskDelay(pdMS_TO_TICKS(30));                                                // equals to (36/3*40) / (16000/1000)
    }
    memset(send_buf, 0, sizeof(send_buf));
    send_to_queue(pwm_write_queue_handle, send_buf, sizeof(send_buf), 0);
    vTaskDelay(pdMS_TO_TICKS(30));
}
