#include "usart6_ll.h"

#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

static void USART6_LL_GPIO_Init(void){
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);

    GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = LL_GPIO_AF_8;

    /* PC6: USART6_TX */
    GPIO_InitStruct.Pin = LL_GPIO_PIN_6;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PC7: USART6_RX, 空闲为高电平, 单独加上拉 */
    GPIO_InitStruct.Pin = LL_GPIO_PIN_7;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
    LL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void USART6_LL_Init(void){
    LL_USART_InitTypeDef usart = {0};

    USART6_LL_GPIO_Init();

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART6);
    LL_APB2_GRP1_ForceReset(LL_APB2_GRP1_PERIPH_USART6);
    LL_APB2_GRP1_ReleaseReset(LL_APB2_GRP1_PERIPH_USART6);

    LL_USART_Disable(USART6);
    LL_USART_ConfigAsyncMode(USART6);

    usart.BaudRate = 115200;
    usart.DataWidth = LL_USART_DATAWIDTH_8B;
    usart.StopBits = LL_USART_STOPBITS_1;
    usart.Parity = LL_USART_PARITY_NONE;
    usart.TransferDirection = LL_USART_DIRECTION_TX_RX;
    usart.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    usart.OverSampling = LL_USART_OVERSAMPLING_16;

    LL_USART_Init(USART6, &usart);
    LL_USART_Enable(USART6);

    HAL_NVIC_SetPriority(USART6_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);
    LL_USART_EnableIT_RXNE(USART6);
}

void USART6_LL_SendByte(uint8_t data){
    while(!LL_USART_IsActiveFlag_TXE(USART6));
    LL_USART_TransmitData8(USART6, data);
}

void USART6_LL_Send(const uint8_t *data, uint16_t len){
    uint16_t i;
    if (data == 0) return;
    for (i = 0; i < len; i++) USART6_LL_SendByte(data[i]);
    while (!LL_USART_IsActiveFlag_TC(USART6));
}

/*
 * 按 SOF、LEN、DATA、XOR 的顺序发送一个数据包。
 * 本函数会等待发送完成，只能在任务中调用。
 */
void USART6_LL_SendPacket(const uint8_t *payload, uint16_t len){
    uint8_t x;
    uint16_t i;

    /* 空指针却声称有载荷，或超过协议上限，直接不发。 */
    if((payload == 0 && len != 0) || (len > USART6_PKT_LEN_MAX)) return;

    USART6_LL_SendByte(USART6_PKT_SOF0);
#if USART6_PKT_SOF_LEN >= 2U
    USART6_LL_SendByte(USART6_PKT_SOF1);
#endif

    USART6_LL_SendByte((uint8_t)len);   /* LEN 字段：后面有多少字节 DATA */
    x = (uint8_t)len;                   /* 校验从 LEN 开始累异或 */
    for(i = 0; i < len; i++){
        USART6_LL_SendByte(payload[i]);
        x ^= payload[i];
    }
    USART6_LL_SendByte(x);              /* 最后一个字节是 XOR */
    while(!LL_USART_IsActiveFlag_TC(USART6));
}

int fputc(int ch, FILE *f){
    (void)f;
    USART6_LL_SendByte((uint8_t)ch);
    return ch;
}

/*
 * 单生产者、单消费者环形队列：
 * head 只由接收中断推进，tail 只由接收任务推进；队列满时丢弃新字节。
 */
static uint8_t usart6_rx_buf[USART6_RX_BUF_SIZE];
static volatile uint16_t usart6_rx_head;
static volatile uint16_t usart6_rx_tail;

/* 半包等待状态及开始等待的时间。 */
static uint8_t usart6_pkt_wait;
static uint32_t usart6_pkt_t0;

/* 接收中断调用：将一个字节写入环形队列。 */
static void USART6_LL_RxPush(uint8_t data){
    uint16_t next = (uint16_t)((usart6_rx_head + 1) & USART6_RX_MASK);
    if(next == usart6_rx_tail) return; /* 队列满，丢弃新字节 */
    usart6_rx_buf[usart6_rx_head] = data;
    usart6_rx_head = next;
}

/* 返回环形队列中尚未读取的字节数。 */
static uint16_t USART6_LL_RxCount(void){
    uint16_t head = usart6_rx_head;
    uint16_t tail = usart6_rx_tail;
    return (uint16_t)((head - tail) & USART6_RX_MASK);
}

/* 查看指定偏移处的字节，不移动 tail。 */
static uint8_t USART6_LL_RxPeek(uint16_t offset){
    uint16_t idx = (uint16_t)((usart6_rx_tail + offset) & USART6_RX_MASK);
    return usart6_rx_buf[idx];
}

/* 从队头丢弃 n 个字节，并取消当前半包计时。 */
static void USART6_LL_RxDrop(uint16_t n){
    usart6_rx_tail = (uint16_t)((usart6_rx_tail + n) & USART6_RX_MASK);
    usart6_pkt_wait = 0; /* 解包起点已改变 */
}

int USART6_LL_ReadByte(uint8_t *data){
    uint16_t tail;
    if(data == 0) return 0;
    if(usart6_rx_head == usart6_rx_tail) return 0; // buffer empty
    
    tail = usart6_rx_tail;
    *data = usart6_rx_buf[tail];
    usart6_rx_tail = (uint16_t)((tail + 1) & USART6_RX_MASK);
    usart6_pkt_wait = 0; /* 解包起点已改变 */
    return 1;
}

/* 计算 LEN 与全部 DATA 字节的异或校验值。 */
static uint8_t USART6_LL_PktXor(uint8_t len, uint16_t data_off){
    uint8_t x = len;
    uint16_t i;
    for(i=0;i<len;i++){
        x ^= USART6_LL_RxPeek(data_off + i);
    }
    return x;
}

/*
 * 尝试从环形队列中提取一个完整数据包。
 *
 * 返回 1：解析成功，payload 和 len 已更新。
 * 返回 0：当前没有完整的合法数据包。
 *
 * 包头、长度或校验错误时丢弃一个字节并继续查找；
 * 数据未收完整时保留现有字节，等待后续数据或超时。
 */

int USART6_LL_ReadPacket(uint8_t *payload, uint16_t *len){
    uint16_t n;
    uint8_t dlen;
    uint16_t need;
    uint16_t i;
    uint32_t now;

    if(payload == 0 || len == 0) return 0;
    for(;;){
        n=USART6_LL_RxCount();
        if(n<USART6_PKT_SOF_LEN) return 0; /* 包头尚未收完整 */
        if(USART6_LL_RxPeek(0) != USART6_PKT_SOF0){
            USART6_LL_RxDrop(1); /* 丢弃一个字节并重新查找包头 */
            continue;
        }
        #if USART6_PKT_SOF_LEN >= 2U
        if(USART6_LL_RxPeek(1) != USART6_PKT_SOF1){
            /* 例如先到一个 AA，下一个不是 55：只丢掉这个 AA，
             * 后面那个字节可能自己就是新的 AA。 */
            USART6_LL_RxDrop(1); /* 丢弃一个字节并重新查找包头 */
            continue;
        }
        #endif
        if(n<USART6_PKT_HDR_LEN) return 0; /* 等待 LEN 字段 */
        dlen = USART6_LL_RxPeek(USART6_PKT_SOF_LEN);
        if((dlen < USART6_PKT_LEN_MIN) || (dlen > USART6_PKT_LEN_MAX)){
            /* 重复包头：AA 55 AA 55 02 ... 这里 LEN 读到 0xAA=170 > 64 */
            USART6_LL_RxDrop(1); /* 丢弃一个字节并重新查找包头 */
            continue;
        }
        /* 整帧长度 = 包头 + LEN 字段 + DATA + XOR */
        need = (uint16_t)(USART6_PKT_HDR_LEN + dlen + 1U);
        if(n<need){
            now = HAL_GetTick();
            if(usart6_pkt_wait == 0){
                usart6_pkt_wait=1;
                usart6_pkt_t0=now; /* 开始半包计时 */
            }else if((USART6_PKT_TIMEOUT_MS != 0u)&&((uint32_t)(now-usart6_pkt_t0)>=USART6_PKT_TIMEOUT_MS)){
                USART6_LL_RxDrop(1);
                continue;
            }
            return 0; /* 还在超时窗口内，把已有字节留在队列里 */
        }
        if (USART6_LL_PktXor(dlen,USART6_PKT_HDR_LEN) != USART6_LL_RxPeek((uint16_t)(USART6_PKT_HDR_LEN + dlen))){
            /* XOR 不对：丢 1 字节，继续搜 */
            USART6_LL_RxDrop(1);
            continue;
        }
        for(i=0;i<dlen;i++) payload[i] = USART6_LL_RxPeek((uint16_t)(USART6_PKT_HDR_LEN + i));
        *len = dlen;
        USART6_LL_RxDrop(need); /* 消费完整数据包 */
        return 1;
    }
}

void USART6_LL_IRQHandler(void){
    uint8_t data;
    if(LL_USART_IsActiveFlag_ORE(USART6)){
        data=LL_USART_ReceiveData8(USART6); // clear ORE flag by reading data
        USART6_LL_RxPush(data);
        return;
    }
    if(LL_USART_IsActiveFlag_RXNE(USART6) && LL_USART_IsEnabledIT_RXNE(USART6)){
        data=LL_USART_ReceiveData8(USART6);
        USART6_LL_RxPush(data);
    }
}

/* 接收测试任务：解析成功后按相同格式回发数据包。 */
void USART6_LL_PktTask(void *arg)
{
    uint8_t  payload[USART6_PKT_LEN_MAX];
    uint16_t n;
    (void)arg;
    for (;;) {
        if (USART6_LL_ReadPacket(payload, &n)) {
            USART6_LL_SendPacket(payload, n);
        } else {
            vTaskDelay(1);
        }
    }
}
