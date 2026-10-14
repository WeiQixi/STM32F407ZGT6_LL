#ifndef USART6_PACKET_H
#define USART6_PACKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 数据包格式：AA 55 | LEN | DATA... | XOR。 */
#define USART6_PKT_SOF0        0xAAU
#define USART6_PKT_SOF1        0x55U
#define USART6_PKT_SOF_LEN     2U
#define USART6_PKT_LEN_MIN     0U
#define USART6_PKT_LEN_MAX     64U
#define USART6_PKT_TIMEOUT_MS  20U
#define USART6_PKT_HDR_LEN     (USART6_PKT_SOF_LEN + 1U)

#if (USART6_PKT_SOF_LEN != 1U) && (USART6_PKT_SOF_LEN != 2U)
#error "USART6_PKT_SOF_LEN must be 1 or 2"
#endif

void USART6_Packet_Send(const uint8_t *payload, uint16_t len);
int USART6_Packet_Read(uint8_t *payload, uint16_t *len);

#ifdef __cplusplus
}
#endif

#endif /* USART6_PACKET_H */
