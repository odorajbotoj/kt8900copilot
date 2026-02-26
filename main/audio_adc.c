#include "audio_adc.h"

#define TAG "AUDIO_ADC"

adc_continuous_handle_t adc_handle;

esp_err_t adc_init(void)
{
    adc_continuous_handle_cfg_t adc_handle_cfg = {
        .max_store_buf_size = 2 * ADC_BUF_SIZE,
        .conv_frame_size = ADC_BUF_SIZE,
        .flags = {
            .flush_pool = true,
        },
    };
    ERR_CHK(adc_continuous_new_handle(&adc_handle_cfg, &adc_handle), "Failed to new ADC handle.");

    adc_digi_pattern_config_t pattern_cfg[1] = {
        {
            .atten = ADC_ATTEN_DB_6,
            .channel = ADC_CHANNEL_0,
            .unit = ADC_UNIT_1,
            .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
        },
    };
    adc_continuous_config_t adc_cfg = {
        .sample_freq_hz = 32000,
        .pattern_num = 1,
        .adc_pattern = pattern_cfg,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    ERR_CHK(adc_continuous_config(adc_handle, &adc_cfg), "Failed to set ADC config.");

    ERR_CHK(adc_continuous_start(adc_handle), "Failed to start ADC.");

    return ESP_OK;
}
