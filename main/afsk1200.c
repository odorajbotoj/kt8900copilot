#include "afsk1200.h"

#define GET_BIT(s, i) (((s)[(i) / 8] >> ((i) % 8)) & 1)
#define SET_BIT(s, i, v) ((s)[(i) / 8] = ((s)[(i) / 8] & ~(1 << ((i) % 8))) | ((v) << ((i) % 8)))

// NCO
static uint32_t phase;
static uint32_t step_mark;
static uint32_t step_space;
static uint32_t step_current;
static uint32_t symbol_acc;

int16_t sin_table[TABLE_SIZE];
void afsk_init(void)
{
    for (size_t i = 0; i < TABLE_SIZE; ++i)
    {
        double v = sin(2.0 * M_PI * i / TABLE_SIZE);
        sin_table[i] = (int16_t)(v * AMPLITUDE);
    }
    step_mark = (uint32_t)((double)MARK_FREQ * 4294967296.0 / SAMPLE_RATE);
    step_space = (uint32_t)((double)SPACE_FREQ * 4294967296.0 / SAMPLE_RATE);
}

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
    SET_BIT(source, bit_len + 512, 0);
    SET_BIT(source, bit_len + 513, 1);
    SET_BIT(source, bit_len + 514, 1);
    SET_BIT(source, bit_len + 515, 1);
    SET_BIT(source, bit_len + 516, 1);
    SET_BIT(source, bit_len + 517, 1);
    SET_BIT(source, bit_len + 518, 1);
    SET_BIT(source, bit_len + 519, 0);
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
    int16_t send_buf[512] = {0};
    size_t bit_read_index = 0;
    step_current = step_mark;
    phase = 0;
    symbol_acc = 0;
    uint8_t index;
    uint16_t written_buf;
    uint8_t count = 0;
    send_to_queue(pwm_write_queue_handle, send_buf, sizeof(send_buf));
    vTaskDelay(pdMS_TO_TICKS(30));
    while (bit_read_index < bit_len)
    {
        ++count;
        for (written_buf = 0; written_buf < 512; ++written_buf)
        {
            if (bit_read_index >= bit_len)
                break;
            symbol_acc += BAUD_RATE;
            if (symbol_acc >= SAMPLE_RATE)
            {
                symbol_acc -= SAMPLE_RATE;
                if (GET_BIT(source, bit_read_index))
                    step_current = step_mark;
                else
                    step_current = step_space;
                ++bit_read_index;
            }
            phase += step_current;
            index = phase >> 24;
            send_buf[written_buf] = sin_table[index];
        }
        send_to_queue(pwm_write_queue_handle, send_buf, written_buf * 2);
        vTaskDelay(pdMS_TO_TICKS(30));
        if (count > 5)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            count = 0;
        }
    }
    memset(send_buf, 0, sizeof(send_buf));
    send_to_queue(pwm_write_queue_handle, send_buf, sizeof(send_buf));
    vTaskDelay(pdMS_TO_TICKS(30));
}
