#ifndef USART6_LL_H
#define USART6_LL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* 软件接收队列和DMA循环缓冲区都使用256个位置。 */
#define USART6_RX_BUF_SIZE  256U
#define USART6_RX_MASK      (USART6_RX_BUF_SIZE - 1U)

#if (USART6_RX_BUF_SIZE & (USART6_RX_BUF_SIZE - 1U)) != 0
#error "USART6_RX_BUF_SIZE must be a power of 2"
#endif

/* GPIO、USART6、DMA2 Stream1和中断初始化。 */
void USART6_LL_Init(void);

/* 轮询发送原始字节，不添加数据包格式。 */
void USART6_LL_SendByte(uint8_t data);
void USART6_LL_Send(const uint8_t *data, uint16_t len);

/*
 * 软件接收队列接口。
 * RxCount返回未处理字节数；RxPeek只查看；RxDrop才移动读取位置。
 * 调用RxPeek(offset)前，调用者必须确认offset小于RxCount()。
 */
uint16_t USART6_LL_RxCount(void);
uint8_t USART6_LL_RxPeek(uint16_t offset);
void USART6_LL_RxDrop(uint16_t count);
int USART6_LL_ReadByte(uint8_t *data);

/* 硬件中断入口调用的内部处理函数。 */
void USART6_LL_IRQHandler(void);
void USART6_LL_DmaRxIRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* USART6_LL_H */
