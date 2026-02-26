#include "led.h"

#define TAG "LED"

led_indicator_handle_t led_handle;

static const blink_step_t blink_red[] = {
    {LED_BLINK_RGB, SET_RGB(255, 0, 0), 0},
    {LED_BLINK_BRIGHTNESS, LED_STATE_25_PERCENT, 0},
    {LED_BLINK_HOLD, LED_STATE_ON, 100},
    {LED_BLINK_LOOP, 0, 0},
};
static const blink_step_t blink_green[] = {
    {LED_BLINK_RGB, SET_RGB(0, 255, 0), 0},
    {LED_BLINK_BRIGHTNESS, LED_STATE_25_PERCENT, 0},
    {LED_BLINK_HOLD, LED_STATE_ON, 100},
    {LED_BLINK_LOOP, 0, 0},
};
static const blink_step_t blink_yellow_3[] = {
    {LED_BLINK_RGB, SET_RGB(255, 255, 0), 0},
    {LED_BLINK_BRIGHTNESS, LED_STATE_25_PERCENT, 0},
    {LED_BLINK_HOLD, LED_STATE_ON, 200},
    {LED_BLINK_HOLD, LED_STATE_OFF, 200},
    {LED_BLINK_HOLD, LED_STATE_ON, 200},
    {LED_BLINK_HOLD, LED_STATE_OFF, 200},
    {LED_BLINK_HOLD, LED_STATE_ON, 200},
    {LED_BLINK_HOLD, LED_STATE_OFF, 1000},
    {LED_BLINK_LOOP, 0, 0},
};
static const blink_step_t blink_off[] = {
    {LED_BLINK_HOLD, LED_STATE_OFF, 100},
    {LED_BLINK_LOOP, 0, 0},
};
blink_step_t const *led_indicator_blink_lists[] = {
    [BLINK_RED] = blink_red,
    [BLINK_GREEN] = blink_green,
    [BLINK_YELLOW_3] = blink_yellow_3,
    [BLINK_OFF] = blink_off,
    [BLINK_MAX] = NULL,
};

esp_err_t led_init(void)
{
    led_indicator_strips_config_t led_cfg = {
        .led_strip_cfg = {
            .strip_gpio_num = 48,
            .max_leds = 1,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .led_model = LED_MODEL_WS2812,
            .flags = {.invert_out = false},
        },
        .led_strip_driver = LED_STRIP_RMT,
        .led_strip_rmt_cfg = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 0,
            .mem_block_symbols = 0,
        },
    };
    led_indicator_config_t config = {
        .blink_lists = led_indicator_blink_lists,
        .blink_list_num = BLINK_MAX,
    };
    ERR_CHK(led_indicator_new_strips_device(&config, &led_cfg, &led_handle), "Failed to new led indicator device.");
    return ESP_OK;
}
