#ifndef USART6_LL_H
#define USART6_LL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/*
 * USART6 数据包格式：
 *   SOF0 SOF1 LEN DATA... XOR
 *
 * LEN 表示 DATA 的字节数；XOR 为 LEN 与全部 DATA 字节的异或值。
 * 接收中断只负责字节入队，数据包在任务中解析。
 */

#define USART6_RX_BUF_SIZE     256U   /* 环形队列容量，必须是 2 的幂 */
#define USART6_PKT_SOF0        0xAAU  /* 包头第一个字节 */
#define USART6_PKT_SOF1        0x55U  /* 包头第二个字节 */
#define USART6_PKT_SOF_LEN     2U     /* 包头字节数 */
#define USART6_PKT_LEN_MIN     0U     /* 最小载荷长度，0 表示允许空包 */
#define USART6_PKT_LEN_MAX     64U    /* 最大载荷长度 */
#define USART6_PKT_TIMEOUT_MS  20U    /* 半包等待时间，0 表示不超时 */

#if (USART6_RX_BUF_SIZE & (USART6_RX_BUF_SIZE - 1)) != 0
#error "USART6_RX_BUF_SIZE must be a power of 2"
#endif
#if (USART6_PKT_SOF_LEN != 1) && (USART6_PKT_SOF_LEN != 2)
#error "USART6_PKT_SOF_LEN must be 1 or 2"
#endif

/* 容量为 2 的幂时，用按位与完成队列下标回绕。 */
#define USART6_RX_MASK      (USART6_RX_BUF_SIZE - 1U)
/* 包头和 LEN 字段的总长度，即 DATA 的起始偏移。 */
#define USART6_PKT_HDR_LEN  (USART6_PKT_SOF_LEN + 1U)

void USART6_LL_Init(void);
void USART6_LL_SendByte(uint8_t data);
void USART6_LL_Send(const uint8_t *data, uint16_t len);
void USART6_LL_SendPacket(const uint8_t *payload, uint16_t len);

int USART6_LL_ReadByte(uint8_t *data);
int USART6_LL_ReadPacket(uint8_t *payload, uint16_t *len);
void USART6_LL_PktTask(void *arg);
void USART6_LL_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif // USART6_LL_H
