#ifndef USART1_LL_H
#define USART1_LL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

void USART1_LL_Init(void);
void USART1_LL_SendByte(uint8_t data);
void USART1_LL_Send(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // USART1_LL_H
