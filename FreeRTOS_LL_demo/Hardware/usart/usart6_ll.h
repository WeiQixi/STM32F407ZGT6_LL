#ifndef USART6_LL_H
#define USART6_LL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

void USART6_LL_Init(void);
void USART6_LL_SendByte(uint8_t data);
void USART6_LL_Send(const uint8_t *data, uint16_t len);

int USART6_LL_ReadByte(uint8_t *data);
void USART6_LL_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif // USART6_LL_H
