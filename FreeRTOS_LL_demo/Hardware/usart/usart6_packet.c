/**
 * @file    usart6_packet.c
 * @brief   USART6数据包协议的组包、发送和接收解析。
 *
 * 协议格式：AA 55 | LEN | DATA... | XOR。
 * 本模块使用底层字节队列，不直接配置GPIO、USART或DMA。
 */
#include "usart6_packet.h"
#include "usart6_ll.h"
#include "stm32f4xx_hal.h"

/* 半包状态：wait表示正在等待剩余字节，t0保存开始等待的毫秒时间。 */
static uint8_t usart6_packet_wait;
static uint32_t usart6_packet_t0;

/**
 * @brief 移动底层队头，并清除与原队头绑定的半包状态。
 * @param count 消费或丢弃的字节数。
 */
static void USART6_Packet_Drop(uint16_t count){
    USART6_LL_RxDrop(count);
    usart6_packet_wait = 0U;
}

/**
 * @brief 计算候选数据包的XOR校验值。
 * @param len DATA字节数。
 * @param data_offset DATA[0]相对软件队头的偏移。
 * @return LEN与全部DATA字节的异或结果。
 */
static uint8_t USART6_Packet_CalcXor(uint8_t len, uint16_t data_offset){
    uint8_t value = len;
    uint16_t index;
    for(index = 0U; index < len; index++){
        value ^= USART6_LL_RxPeek((uint16_t)(data_offset + index));
    }
    return value;
}

/**
 * @brief 按协议格式发送一个完整数据包。
 * @param payload DATA数组；len为0时允许为NULL。
 * @param len DATA字节数，最大为USART6_PKT_LEN_MAX。
 *
 * 参数无效时不发送；先在局部数组中组帧，再交给底层统一发送。
 */
void USART6_Packet_Send(const uint8_t *payload, uint16_t len){
    uint8_t frame[USART6_PKT_HDR_LEN + USART6_PKT_LEN_MAX + 1U];
    uint8_t value;
    uint16_t index;
    uint16_t frame_pos = 0U;

    /* 空包允许payload为NULL；非空包必须有DATA，LEN不能超过协议上限。 */
    if((payload == 0 && len != 0U) || (len > USART6_PKT_LEN_MAX)) return;

    /* 依次写入包头和LEN；frame_pos始终指向下一个可写位置。 */
    frame[frame_pos++] = USART6_PKT_SOF0;
#if USART6_PKT_SOF_LEN >= 2U
    frame[frame_pos++] = USART6_PKT_SOF1;
#endif
    frame[frame_pos++] = (uint8_t)len;

    /* XOR以LEN为初值，复制DATA的同时完成计算。 */
    value = (uint8_t)len;
    for(index = 0U; index < len; index++){
        frame[frame_pos++] = payload[index];
        value ^= payload[index];
    }

    /* XOR放在帧尾，最后把完整帧交给底层一次发送。 */
    frame[frame_pos++] = value;
    USART6_LL_Send(frame, frame_pos);
}

/**
 * @brief 从底层软件队列中提取一个完整合法数据包。
 * @param payload DATA输出数组，容量至少为USART6_PKT_LEN_MAX。
 * @param len DATA长度输出地址。
 * @return 成功提取一帧返回1；没有完整合法帧返回0。
 *
 * 依次检查包头、LEN、完整性、半包超时和XOR；错误时滑动一个字节重新同步。
 */
int USART6_Packet_Read(uint8_t *payload, uint16_t *len){
    uint16_t available;  /* 软件接收队列当前未处理的字节数 */
    uint8_t data_len;    /* 当前候选数据包的LEN字段 */
    uint16_t frame_len;  /* 完整候选数据包需要的总字节数 */
    uint16_t index;      /* 复制DATA时使用的数组下标 */
    uint32_t now;        /* 检查半包超时时的当前毫秒数 */

    /* 输出地址无效时不能返回DATA和长度。 */
    if(payload == 0 || len == 0) return 0;

    /* 一次调用可连续丢弃无效字节或无效候选帧，直到找到合法帧或需要等待新数据。 */
    for(;;){
        available = USART6_LL_RxCount();

        /* 不足两个字节时，连完整包头都无法判断，保留现有数据。 */
        if(available < USART6_PKT_SOF_LEN) return 0;

        /* 队头不是AA：该字节不可能成为包头，只丢一个字节后重试。 */
        if(USART6_LL_RxPeek(0U) != USART6_PKT_SOF0){
            USART6_Packet_Drop(1U);
            continue;
        }
#if USART6_PKT_SOF_LEN >= 2U
        /* 第二字节不是55时只丢前一个AA，当前第二字节仍可能是下个包头。 */
        if(USART6_LL_RxPeek(1U) != USART6_PKT_SOF1){
            USART6_Packet_Drop(1U);
            continue;
        }
#endif

        /* 包头正确但LEN尚未到达时，不能移动队头。 */
        if(available < USART6_PKT_HDR_LEN) return 0;

        /* LEN紧跟包头，只表示DATA字节数。 */
        data_len = USART6_LL_RxPeek(USART6_PKT_SOF_LEN);
        if(data_len > USART6_PKT_LEN_MAX){
            /* 非法LEN说明当前AA 55不是有效帧起点，滑动一个字节重新同步。 */
            USART6_Packet_Drop(1U);
            continue;
        }

        /* 完整帧长度 = 包头和LEN + DATA + 帧尾XOR。 */
        frame_len = (uint16_t)(USART6_PKT_HDR_LEN + data_len + 1U);

        /* 字节不足表示半包：首次记录时间，后续调用检查20ms超时。 */
        if(available < frame_len){
            now = HAL_GetTick();
            if(usart6_packet_wait == 0U){
                usart6_packet_wait = 1U;
                usart6_packet_t0 = now;
            }else if((USART6_PKT_TIMEOUT_MS != 0U) &&
                     ((uint32_t)(now - usart6_packet_t0) >= USART6_PKT_TIMEOUT_MS)){
                /* 半包长期不完整，丢一个字节，避免假包头永久堵住队列。 */
                USART6_Packet_Drop(1U);
                continue;
            }
            return 0;
        }

        /* 完整帧到齐后才比较本地XOR与接收到的帧尾XOR。 */
        if(USART6_Packet_CalcXor(data_len, USART6_PKT_HDR_LEN) !=
           USART6_LL_RxPeek((uint16_t)(USART6_PKT_HDR_LEN + data_len))){
            /* XOR错误时仍只滑动一个字节，避免跳过后面的合法包头。 */
            USART6_Packet_Drop(1U);
            continue;
        }

        /* 所有检查通过后再复制DATA，防止把错误帧交给调用者。 */
        for(index = 0U; index < data_len; index++){
            payload[index] = USART6_LL_RxPeek((uint16_t)(USART6_PKT_HDR_LEN + index));
        }
        *len = data_len;

        /* 一次只消费当前完整帧，后面的粘包继续留在队列中。 */
        USART6_Packet_Drop(frame_len);
        return 1;
    }
}
