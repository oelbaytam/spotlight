#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "led_driver.h"

#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_LS_TIMER          LEDC_TIMER_1
#define LEDC_LS_CH0_GPIO       (10)
#define LEDC_LS_CH0_CHANNEL    LEDC_CHANNEL_0

#define LEDC_MAX_DUTY          (600)   // The maximum duty of a 10-bit resolution PWM is 1023.
#define LEDC_FADE_TIME         (3000)   // How long each fade should take.
#define LEDC_TOTAL_DURATION    (30000)  //  Minutes in Milliseconds.

static ledc_timer_config_t ledc_timer;
static ledc_channel_config_t ledc_channel;
static ledc_cbs_t callbacks;
static SemaphoreHandle_t counting_sem;

static const char *TAG = "LED_CONTROLLER";

static IRAM_ATTR bool cb_ledc_fade_end_event(const ledc_cb_param_t *param, void *user_arg)
{
    BaseType_t taskAwoken = pdFALSE;

    if (param->event == LEDC_FADE_END_EVT) {
        SemaphoreHandle_t counting_sem = (SemaphoreHandle_t) user_arg;
        xSemaphoreGiveFromISR(counting_sem, &taskAwoken);
    }

    return (taskAwoken == pdTRUE);
}

void led_driver_init(void) {
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_10_BIT, // resolution of PWM duty
        .freq_hz = 32000,                      // frequency of PWM signal
        .speed_mode = LEDC_LS_MODE,           // timer mode
        .timer_num = LEDC_LS_TIMER,            // timer index
        .clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
    };
    ledc_timer_config(&ledc_timer);
        
    ledc_channel_config_t ledc_channel = {
        .channel    = LEDC_LS_CH0_CHANNEL,
        .duty       = 0,
        .gpio_num   = LEDC_LS_CH0_GPIO,
        .speed_mode = LEDC_LS_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_LS_TIMER,
        .flags.output_invert = 0
    };

    ledc_channel_config(&ledc_channel);

    // Initialize fade service.
    ledc_fade_func_install(0);
    ledc_cbs_t callbacks = {
        .fade_cb = cb_ledc_fade_end_event
    };
    SemaphoreHandle_t counting_sem = xSemaphoreCreateCounting(1, 0);

    ledc_cb_register(ledc_channel.speed_mode, ledc_channel.channel, &callbacks, (void *) counting_sem);
}

void start_fading(void) {
    if (ledc_channel == NULL || ledc_timer == NULL) {
        led_driver_init();
    }
    int time = LEDC_TOTAL_DURATION;

    for (int t = 0; t < time; t+= 2*LEDC_FADE_TIME) {
        ESP_LOGI(TAG, "Fading LED up to duty %d", LEDC_MAX_DUTY);
        ledc_set_fade_with_time(ledc_channel.speed_mode,
                                ledc_channel.channel, LEDC_MAX_DUTY, LEDC_FADE_TIME);
        ledc_fade_start(ledc_channel.speed_mode,
                        ledc_channel.channel, LEDC_FADE_NO_WAIT);   
        ESP_LOGI(TAG, "Fading LED down to duty %d", 0);
        ledc_set_fade_with_time(ledc_channel.speed_mode,
                                ledc_channel.channel, 0, LEDC_FADE_TIME);
        ledc_fade_start(ledc_channel.speed_mode,
                        ledc_channel.channel, LEDC_FADE_NO_WAIT);   
    }

}

void turn_on(void) {
    ESP_LOGI(TAG, "Set LED duty to %d", LEDC_MAX_DUTY);
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, LEDC_MAX_DUTY);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}

void turn_off(void) {
    ESP_LOGI(TAG, "Set LED duty to %d", 0);
    ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, 0);
    ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}