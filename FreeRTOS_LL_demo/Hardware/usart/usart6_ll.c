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
 * 按协议发送一帧：SOF0、SOF1、LEN、DATA、XOR。
 *
 * payload：只指向 DATA，不包含包头、长度和校验字节。
 * len：DATA 的字节数，允许为 0，但不能超过 USART6_PKT_LEN_MAX。
 *
 * 发送过程使用轮询等待 TXE/TC，因此只能在任务或普通线程中调用，
 * 不能放进 USART6 接收中断。
 */
void USART6_LL_SendPacket(const uint8_t *payload, uint16_t len){
    uint8_t x;
    uint16_t i;

    /* len=0 时允许 payload 为空；len>0 时 payload 必须有效。
     * 长度超过协议上限时不发送，防止 LEN 截断成 uint8_t。 */
    if((payload == 0 && len != 0) || (len > USART6_PKT_LEN_MAX)) return;

    /* 先发送固定包头，接收端靠它查找一帧的起点。 */
    USART6_LL_SendByte(USART6_PKT_SOF0);
#if USART6_PKT_SOF_LEN >= 2U
    USART6_LL_SendByte(USART6_PKT_SOF1);
#endif

    /* LEN 只描述后面 DATA 的长度，不包含 SOF、LEN 本身和 XOR。 */
    USART6_LL_SendByte((uint8_t)len);

    /* XOR 的初值是 LEN，随后再依次异或每个 DATA 字节。 */
    x = (uint8_t)len;
    /* 发送载荷的同时计算 XOR，不需要再次遍历 payload。 */
    for(i = 0; i < len; i++){
        USART6_LL_SendByte(payload[i]);
        x ^= payload[i];
    }
    /* XOR 固定放在帧尾，供接收端校验 LEN 和全部 DATA。 */
    USART6_LL_SendByte(x);

    /* TC=1 表示最后一个停止位已经离开串口引脚，不只是 DR 已空。 */
    while(!LL_USART_IsActiveFlag_TC(USART6));
}

int fputc(int ch, FILE *f){
    (void)f;
    USART6_LL_SendByte((uint8_t)ch);
    return ch;
}

/*
 * 单生产者、单消费者环形队列。
 *
 * head 指向“下一个可写位置”，只由 USART6 接收中断推进；
 * tail 指向“下一个待读位置”，只由接收任务推进。
 *
 * 两个下标由不同执行上下文修改，所以不需要共同修改一个 count；
 * volatile 保证每次判断都重新读取实际的 head/tail。
 */
static uint8_t usart6_rx_buf[USART6_RX_BUF_SIZE];
static volatile uint16_t usart6_rx_head;
static volatile uint16_t usart6_rx_tail;

/*
 * 半包等待状态。
 * wait=1：已经找到合法包头和 LEN，但完整帧尚未到齐；
 * t0：第一次发现半包时的 HAL 毫秒计数。
 */
static uint8_t usart6_pkt_wait;
static uint32_t usart6_pkt_t0;

/*
 * 接收中断调用：尝试把一个字节写入环形队列。
 *
 * next 先算出 head 的下一个位置。next==tail 表示再写就会覆盖未读数据，
 * 因此队列满时丢弃本次新字节，head 保持不变。
 */
static void USART6_LL_RxPush(uint8_t data){
    /* & MASK 负责下标回绕：255 的下一个位置是 0。 */
    uint16_t next = (uint16_t)((usart6_rx_head + 1) & USART6_RX_MASK);
    if(next == usart6_rx_tail) return; /* 队列满，丢弃新字节 */
    /* 必须先写数据，再发布新的 head；任务看到新 head 后数据已经就绪。 */
    usart6_rx_buf[usart6_rx_head] = data;
    usart6_rx_head = next;
}

/*
 * 返回当前未读字节数。
 *
 * 先把 volatile 下标复制到局部变量，再完成一次一致的计算。
 * 若 ISR 在复制 head 后又收到新字节，本次结果最多暂时少算，任务下一轮会看到。
 */
static uint16_t USART6_LL_RxCount(void){
    uint16_t head = usart6_rx_head;
    uint16_t tail = usart6_rx_tail;
    /* 下标即使已经绕圈，& MASK 仍能得到 0～255 范围内的距离。 */
    return (uint16_t)((head - tail) & USART6_RX_MASK);
}

/*
 * 查看队头之后第 offset 个字节，但不移动 tail。
 *
 * 解包器用 Peek 先检查 SOF、LEN 和 XOR；只有确认应丢弃或成功解包后，
 * 才调用 RxDrop 推进 tail，避免检查过程中提前破坏队列内容。
 */
static uint8_t USART6_LL_RxPeek(uint16_t offset){
    /* offset 以当前 tail 为 0，& MASK 处理跨越数组末尾的情况。 */
    uint16_t idx = (uint16_t)((usart6_rx_tail + offset) & USART6_RX_MASK);
    return usart6_rx_buf[idx];
}

/*
 * 从队头消费或丢弃 n 个字节。
 *
 * tail 改变后，原来的半包起点已经不存在，所以必须同时清除等待状态。
 * 调用者必须保证 n 不超过当前未读字节数。
 */
static void USART6_LL_RxDrop(uint16_t n){
    usart6_rx_tail = (uint16_t)((usart6_rx_tail + n) & USART6_RX_MASK);
    usart6_pkt_wait = 0; /* 解包起点已改变 */
}

/*
 * 兼容旧的逐字节读取接口：成功取出 1 字节返回 1，队列空或参数无效返回 0。
 * ReadByte 和 ReadPacket 都会推进同一个 tail，同一接收通道不能同时使用两者。
 */
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

/*
 * 根据队列里的 DATA 计算本协议的 XOR。
 *
 * data_off 是 DATA[0] 相对 tail 的偏移；当前协议中等于 HDR_LEN。
 * 初值使用 LEN，因此空包的 XOR 就是 0。
 */
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
 * 成功返回 1：payload 得到 DATA，*len 得到 DATA 长度，整帧从队列消费。
 * 返回 0：当前没有完整合法帧；可能是队列空，也可能正在等待半包。
 *
 * 解析始终从 tail 开始。遇到噪声、非法 LEN 或错误 XOR 时只丢 1 字节，
 * 然后从新的 tail 重新寻找包头；这样不会把紧随其后的正常帧一起丢掉。
 */

int USART6_LL_ReadPacket(uint8_t *payload, uint16_t *len){
    uint16_t n;
    uint8_t dlen;
    uint16_t need;
    uint16_t i;
    uint32_t now;

    /* 输出地址无效时不能写结果，直接返回。 */
    if(payload == 0 || len == 0) return 0;

    /* 一次调用可能连续跳过多个噪声字节，直到找到帧或需要等待新数据。 */
    for(;;){
        /* n 是本轮判断可安全 Peek 的未读字节数。 */
        n=USART6_LL_RxCount();
        if(n<USART6_PKT_SOF_LEN) return 0; /* 包头尚未收完整 */
        /* 第一个字节不是 AA：它不可能成为本协议帧头，丢掉后继续。 */
        if(USART6_LL_RxPeek(0) != USART6_PKT_SOF0){
            USART6_LL_RxDrop(1); /* 丢弃一个字节并重新查找包头 */
            continue;
        }
#if USART6_PKT_SOF_LEN >= 2U
        /* 第一个字节是 AA、第二个不是 55：只丢 AA。
         * 第二个字节仍可能是下一次包头的 AA，不能两个一起丢。 */
        if(USART6_LL_RxPeek(1) != USART6_PKT_SOF1){
            USART6_LL_RxDrop(1); /* 丢弃一个字节并重新查找包头 */
            continue;
        }
#endif
        /* 包头已经匹配，但 LEN 还没到时必须原样保留队列。 */
        if(n<USART6_PKT_HDR_LEN) return 0; /* 等待 LEN 字段 */
        /* LEN 紧跟在 SOF 后面，表示本帧应包含多少 DATA。 */
        dlen = USART6_LL_RxPeek(USART6_PKT_SOF_LEN);
        if((dlen < USART6_PKT_LEN_MIN) || (dlen > USART6_PKT_LEN_MAX)){
            /* 重复包头：AA 55 AA 55 02 ... 这里 LEN 读到 0xAA=170 > 64 */
            USART6_LL_RxDrop(1); /* 丢弃一个字节并重新查找包头 */
            continue;
        }
        /* need 是收到一整帧所需的总字节数：SOF + LEN + DATA + XOR。 */
        need = (uint16_t)(USART6_PKT_HDR_LEN + dlen + 1U);
        /* 当前字节数不足 need，说明这是半包，不能提前消费任何字节。 */
        if(n<need){
            now = HAL_GetTick();
            /* 第一次发现半包只记录起点；后续调用再判断等待时间。
             * now-t0 使用无符号减法，HAL Tick 回绕时也能正确计算。 */
            if(usart6_pkt_wait == 0){
                usart6_pkt_wait=1;
                usart6_pkt_t0=now; /* 开始半包计时 */
            }else if((USART6_PKT_TIMEOUT_MS != 0u)&&((uint32_t)(now-usart6_pkt_t0)>=USART6_PKT_TIMEOUT_MS)){
                /* 等待超时：当前包头可能是假头，只丢 1 字节重新同步。 */
                USART6_LL_RxDrop(1);
                continue;
            }
            return 0; /* 还在超时窗口内，把已有字节留在队列里 */
        }
        /* DATA 后面的一个字节是接收到的 XOR；与本地计算结果比较。 */
        if (USART6_LL_PktXor(dlen,USART6_PKT_HDR_LEN) != USART6_LL_RxPeek((uint16_t)(USART6_PKT_HDR_LEN + dlen))){
            /* XOR 不对：丢 1 字节，继续搜 */
            USART6_LL_RxDrop(1);
            continue;
        }
        /* 到这里说明长度和 XOR 都正确，才把 DATA 复制给调用者。 */
        for(i=0;i<dlen;i++) payload[i] = USART6_LL_RxPeek((uint16_t)(USART6_PKT_HDR_LEN + i));
        *len = dlen;

        /* 一次只消费当前完整帧；粘在后面的下一帧仍留在队列中。 */
        USART6_LL_RxDrop(need); /* 消费完整数据包 */
        return 1;
    }
}

/*
 * USART6 中断内部处理。
 *
 * ORE 表示上一个字节未及时读取又来了新字节。读取 DR 可清除 ORE/RXNE，
 * 读出的字节仍尝试入队；随后立即返回，避免同一次中断重复处理。
 * 正常 RXNE 则读取一个字节并入队。ISR 不解包、不发送、不调用 printf。
 */
void USART6_LL_IRQHandler(void){
    uint8_t data;
    if(LL_USART_IsActiveFlag_ORE(USART6)){
        data=LL_USART_ReceiveData8(USART6); /* 读取 DR 清除 ORE */
        USART6_LL_RxPush(data);
        return;
    }
    if(LL_USART_IsActiveFlag_RXNE(USART6) && LL_USART_IsEnabledIT_RXNE(USART6)){
        data=LL_USART_ReceiveData8(USART6);
        USART6_LL_RxPush(data);
    }
}

/*
 * USART6 数据包测试任务。
 *
 * ReadPacket 成功时立即按相同协议回发；如果后面还有粘包，下一轮会继续解析。
 * 当前没有完整帧时延时 1 个 Tick，让出 CPU，避免任务持续空转。
 */
void USART6_LL_PktTask(void *arg)
{
    /* 每次只保存一帧 DATA，数组大小与协议最大载荷一致。 */
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
