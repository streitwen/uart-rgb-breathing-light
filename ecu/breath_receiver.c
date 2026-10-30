#include "breath_receiver.h"

#include <stdint.h>

#define BREATH_HEADER 0xA5u
#define BREATH_PACKET_SIZE 6u
#define BREATH_TIMEOUT_MS 200u
#define BREATH_PWM_MAX 1000u

/*
 * Adapt these three definitions to the electronics project.
 * The example assumes:
 *   red   -> TIM2 channel 1
 *   green -> TIM2 channel 2
 *   blue  -> TIM2 channel 3
 */
extern TIM_HandleTypeDef htim2;

#define LED_TIMER htim2
#define LED_R_CHANNEL TIM_CHANNEL_1
#define LED_G_CHANNEL TIM_CHANNEL_2
#define LED_B_CHANNEL TIM_CHANNEL_3

static UART_HandleTypeDef *s_uart;
static uint8_t s_rx_byte;
static uint8_t s_rx_buffer[BREATH_PACKET_SIZE];
static uint8_t s_rx_index;
static uint8_t s_has_received;
static uint8_t s_timed_out;
static uint32_t s_last_valid_ms;

static uint16_t crc16_ccitt_false(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFFu;
    uint16_t index;
    uint8_t bit;

    for (index = 0u; index < length; index++) {
        crc ^= (uint16_t)((uint16_t)data[index] << 8);
        for (bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static uint32_t rgb_to_compare(uint8_t value) {
    return ((uint32_t)value * BREATH_PWM_MAX + 127u) / 255u;
}

static void write_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    __HAL_TIM_SET_COMPARE(&LED_TIMER, LED_R_CHANNEL, rgb_to_compare(red));
    __HAL_TIM_SET_COMPARE(&LED_TIMER, LED_G_CHANNEL, rgb_to_compare(green));
    __HAL_TIM_SET_COMPARE(&LED_TIMER, LED_B_CHANNEL, rgb_to_compare(blue));
}

static void restart_search_at_next_header(void) {
    do {
        uint8_t index;
        for (index = 1u; index < s_rx_index; index++) {
            s_rx_buffer[index - 1u] = s_rx_buffer[index];
        }
        s_rx_index--;
    } while ((s_rx_index > 0u) && (s_rx_buffer[0] != BREATH_HEADER));
}

void BreathReceiver_OnByte(uint8_t byte) {
    if ((s_rx_index == 0u) && (byte != BREATH_HEADER)) {
        return;
    }

    s_rx_buffer[s_rx_index++] = byte;
    if (s_rx_index < BREATH_PACKET_SIZE) {
        return;
    }

    const uint16_t received_crc =
        (uint16_t)s_rx_buffer[4] | ((uint16_t)s_rx_buffer[5] << 8);
    const uint16_t calculated_crc =
        crc16_ccitt_false(s_rx_buffer, BREATH_PACKET_SIZE - 2u);

    if (received_crc != calculated_crc) {
        restart_search_at_next_header();
        return;
    }

    write_rgb(s_rx_buffer[1], s_rx_buffer[2], s_rx_buffer[3]);
    s_rx_index = 0u;
    s_has_received = 1u;
    s_timed_out = 0u;
    s_last_valid_ms = HAL_GetTick();
}

void BreathReceiver_Tick(uint32_t now_ms) {
    if ((s_has_received != 0u) && (s_timed_out == 0u) &&
        ((uint32_t)(now_ms - s_last_valid_ms) > BREATH_TIMEOUT_MS)) {
        write_rgb(0u, 0u, 0u);
        s_timed_out = 1u;
        s_rx_index = 0u;
    }
}

void BreathReceiver_Init(UART_HandleTypeDef *uart) {
    s_uart = uart;
    s_rx_index = 0u;
    s_has_received = 0u;
    s_timed_out = 0u;
    s_last_valid_ms = HAL_GetTick();

    HAL_TIM_PWM_Start(&LED_TIMER, LED_R_CHANNEL);
    HAL_TIM_PWM_Start(&LED_TIMER, LED_G_CHANNEL);
    HAL_TIM_PWM_Start(&LED_TIMER, LED_B_CHANNEL);
    write_rgb(0u, 0u, 0u);
    HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1u);
}

/*
 * If the electronics project already defines this callback, merge the body
 * into that existing function instead of compiling a second definition.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart) {
    if (uart == s_uart) {
        BreathReceiver_OnByte(s_rx_byte);
        HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1u);
    }
}
