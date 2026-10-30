#pragma once

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

void BreathReceiver_Init(UART_HandleTypeDef *uart);
void BreathReceiver_OnByte(uint8_t byte);
void BreathReceiver_Tick(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
