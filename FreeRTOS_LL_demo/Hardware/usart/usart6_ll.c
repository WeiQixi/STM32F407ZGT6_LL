/**
 * @file    usart6_ll.c
 * @brief   USART6底层驱动：GPIO、USART、DMA、字节队列和中断处理。
 *
 * 接收路径：PC7 -> USART6->DR -> DMA循环缓冲区 -> 软件接收队列。
 * 本模块只运输原始字节，不识别包头、长度和校验字段。
 */
#include "usart6_ll.h"

#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_dma.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#define USART6_LL_BAUD_RATE            115200U
#define USART6_TX_BITS_PER_BYTE       10U
#define USART6_TX_TIMEOUT_MARGIN_MS   10U

static void USART6_LL_RxDmaInit(void);
static void USART6_LL_TxDmaInit(void);
static void USART6_LL_TxDmaClearFlags(void);

/**
 * @brief 配置PC6为USART6_TX，PC7为USART6_RX。
 */
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

/**
 * @brief 初始化GPIO、USART6、DMA收发和NVIC中断。
 *
 * USART6配置为115200、8N1；RX使用DMA2 Stream1 Channel5循环模式，
 * TX使用DMA2 Stream6 Channel5普通模式。
 */
void USART6_LL_Init(void){
    LL_USART_InitTypeDef usart = {0};

    USART6_LL_GPIO_Init();

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART6);
    LL_APB2_GRP1_ForceReset(LL_APB2_GRP1_PERIPH_USART6);
    LL_APB2_GRP1_ReleaseReset(LL_APB2_GRP1_PERIPH_USART6);

    LL_USART_ConfigAsyncMode(USART6);

    usart.BaudRate = USART6_LL_BAUD_RATE;
    usart.DataWidth = LL_USART_DATAWIDTH_8B;
    usart.StopBits = LL_USART_STOPBITS_1;
    usart.Parity = LL_USART_PARITY_NONE;
    usart.TransferDirection = LL_USART_DIRECTION_TX_RX;
    usart.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    usart.OverSampling = LL_USART_OVERSAMPLING_16;

    LL_USART_Init(USART6, &usart);

    /*
     * 先配置并启动 DMA，再打开 USART6 的 DMA 接收请求。
     * 这样 USART6 启用后，收到的第一个字节就可以由 DMA 自动搬运，
     * 不再为每个字节进入一次 RXNE 中断。
     */
    USART6_LL_RxDmaInit();
    USART6_LL_TxDmaInit();
    LL_USART_EnableDMAReq_RX(USART6);
    LL_USART_EnableDMAReq_TX(USART6);
    LL_USART_Enable(USART6);

    /* 清除初始化阶段可能残留的 IDLE，避免启用中断后立即误触发。 */
    LL_USART_ClearFlag_IDLE(USART6);

    /* 两个中断都不调用 FreeRTOS API。优先级设为 4，高于当前
     * configMAX_SYSCALL_INTERRUPT_PRIORITY 对应的 5，避免 IDLE 清标志
     * 被 FreeRTOS 临界区长时间推迟。两者同级，不能互相抢占。 */
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
    HAL_NVIC_SetPriority(USART6_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);

    /*
     * TX DMA完成中断会调用FreeRTOS的FromISR通知接口，优先级必须是
     * configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY允许的5或更低。
     */
    HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);

    /* IDLE 用来及时提交不足半个 DMA 缓冲区的短数据。 */
    LL_USART_EnableIT_IDLE(USART6);
    /* DMA 接收模式下，EIE 负责报告 ORE、FE 和 NE。 */
    LL_USART_EnableIT_ERROR(USART6);
}

/* TX DMA状态：一次只允许一个任务启动发送。 */
static volatile uint8_t usart6_tx_dma_busy;
static volatile int8_t usart6_tx_dma_result;
static volatile TaskHandle_t usart6_tx_dma_wait_task;

/**
 * @brief 轮询发送一个原始字节。
 * @param data 待发送字节。
 *
 * 该接口保留给调度器启动前的启动信息和printf。若TX DMA正在工作，
 * 先等待DMA释放USART6发送通道，避免轮询数据插入DMA数据中间。
 */
void USART6_LL_SendByte(uint8_t data){
    while(usart6_tx_dma_busy != 0U);
    while(!LL_USART_IsActiveFlag_TXE(USART6));
    LL_USART_TransmitData8(USART6, data);
}

/**
 * @brief 轮询发送一组原始字节，并等待最后一个停止位发送完成。
 * @param data 待发送数组；为NULL时不发送。
 * @param len 待发送字节数。
 */
void USART6_LL_Send(const uint8_t *data, uint16_t len){
    uint16_t i;
    if (data == 0) return;
    for (i = 0; i < len; i++) USART6_LL_SendByte(data[i]);
    while (!LL_USART_IsActiveFlag_TC(USART6));
}

/**
 * @brief 使用DMA发送一组原始字节，并阻塞当前任务等待完成。
 * @param data DMA读取的源数组；等待返回前必须保持有效。
 * @param len 字节数，范围为1至65535。
 * @return 完成返回1；参数无效、发送忙、超时或DMA错误返回0。
 *
 * 等待期间当前任务进入阻塞态，CPU可以运行其他任务。DMA的TC表示
 * 最后一个字节已经写入USART6->DR；唤醒后再等待USART TC，确保最后
 * 一个停止位已经从PC6发送完成。
 */
int USART6_LL_SendDma(const uint8_t *data, uint16_t len){
    uint32_t timeout_ms;
    TickType_t timeout_ticks;
    TaskHandle_t current_task;

    if((data == 0) || (len == 0U)) return 0;
    if(xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return 0;

    current_task = xTaskGetCurrentTaskHandle();
    (void)ulTaskNotifyTake(pdTRUE, 0U); /* 清除本任务上一次TX完成通知。 */

    taskENTER_CRITICAL();
    if(usart6_tx_dma_busy != 0U){
        taskEXIT_CRITICAL();
        return 0;
    }

    usart6_tx_dma_busy = 1U;
    usart6_tx_dma_result = 0;
    usart6_tx_dma_wait_task = current_task;

    LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_6);
    while(LL_DMA_IsEnabledStream(DMA2, LL_DMA_STREAM_6)){
        /* 等待硬件确认Stream6停止后再修改地址和NDTR。 */
    }

    USART6_LL_TxDmaClearFlags();
    LL_DMA_SetMemoryAddress(DMA2, LL_DMA_STREAM_6, (uint32_t)data);
    LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_6, len);
    LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_6);
    taskEXIT_CRITICAL();

    /* 115200 8N1每字节10位，再增加10ms余量用于调度和中断延迟。 */
    timeout_ms = (((uint32_t)len * USART6_TX_BITS_PER_BYTE * 1000U) +
                  (USART6_LL_BAUD_RATE - 1U)) / USART6_LL_BAUD_RATE;
    timeout_ms += USART6_TX_TIMEOUT_MARGIN_MS;
    timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if(timeout_ticks == 0U) timeout_ticks = 1U;

    if(ulTaskNotifyTake(pdTRUE, timeout_ticks) == 0U){
        taskENTER_CRITICAL();
        LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_6);
        while(LL_DMA_IsEnabledStream(DMA2, LL_DMA_STREAM_6)){
            /* 超时恢复：等待Stream6真正停止。 */
        }
        USART6_LL_TxDmaClearFlags();
        usart6_tx_dma_wait_task = 0;
        usart6_tx_dma_result = -1;
        usart6_tx_dma_busy = 0U;
        taskEXIT_CRITICAL();
        return 0;
    }

    if(usart6_tx_dma_result != 1){
        taskENTER_CRITICAL();
        usart6_tx_dma_busy = 0U;
        taskEXIT_CRITICAL();
        return 0;
    }

    /*
     * DMA TC只表示最后一个字节已经写入DR。保持busy直到USART TC置位，
     * 防止其他任务在最后一个停止位发送完以前启动下一笔发送。
     */
    while(!LL_USART_IsActiveFlag_TC(USART6));
    taskENTER_CRITICAL();
    usart6_tx_dma_busy = 0U;
    taskEXIT_CRITICAL();
    return 1;
}

/**
 * @brief 将标准库单字符输出重定向到USART6。
 * @param ch 待输出字符。
 * @param f 标准库文件对象，本实现不使用。
 * @return 已发送字符。
 */
int fputc(int ch, FILE *f){
    (void)f;
    USART6_LL_SendByte((uint8_t)ch);
    return ch;
}

/* 软件接收队列：head指向下一写入位置，tail指向第一未处理字节。 */
static uint8_t usart6_rx_buf[USART6_RX_BUF_SIZE];
static volatile uint16_t usart6_rx_head;
static volatile uint16_t usart6_rx_tail;

/**
 * @brief 将一个字节追加到软件接收队列。
 * @param data 待追加字节。
 *
 * 队列满时丢弃新字节。先写数组再更新head，避免上层读到未写好的数据。
 */
static void USART6_LL_RxPush(uint8_t data){
    uint16_t next = (uint16_t)((usart6_rx_head + 1U) & USART6_RX_MASK);

    if(next == usart6_rx_tail) return;

    usart6_rx_buf[usart6_rx_head] = data;
    usart6_rx_head = next;
}

/* DMA写入循环缓冲区；软件从usart6_dma_rx_pos开始读取新增字节。 */
static volatile uint8_t usart6_dma_rx_buf[USART6_RX_BUF_SIZE];
static uint16_t usart6_dma_rx_pos;

/**
 * @brief 将DMA新增字节复制到软件接收队列。
 *
 * NDTR表示本轮剩余传输数量；usart6_dma_rx_pos表示软件下一读取下标。
 */
static void USART6_LL_RxDmaDrain(void){
    uint16_t dma_pos;

    dma_pos = (uint16_t)((USART6_RX_BUF_SIZE -
              (uint16_t)LL_DMA_GetDataLength(DMA2, LL_DMA_STREAM_1)) &
              USART6_RX_MASK);

    while(usart6_dma_rx_pos != dma_pos){
        USART6_LL_RxPush(usart6_dma_rx_buf[usart6_dma_rx_pos]);
        usart6_dma_rx_pos =
            (uint16_t)((usart6_dma_rx_pos + 1U) & USART6_RX_MASK);
    }
}

/**
 * @brief 配置并启动USART6_RX的DMA循环接收。
 *
 * 固定映射为DMA2 Stream1 Channel5，启用HT、TC、TE和DME中断。
 */
static void USART6_LL_RxDmaInit(void){
    /* DMA2 属于 AHB1 外设，配置任何 DMA2 寄存器前必须先打开时钟。 */
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

    /*
     * F4 只有在 Stream 的 EN 位清零后，才允许修改通道、地址和 NDTR。
     * 先发出禁用命令，再等待硬件确认 Stream1 已经真正停止。
     */
    LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_1);
    while(LL_DMA_IsEnabledStream(DMA2, LL_DMA_STREAM_1)){
        /* 正常只等待很短时间；EN 没清零前不能继续改配置。 */
    }

    /*
     * STM32F407 数据手册规定：
     * USART6_RX 可以映射到 DMA2 Stream1 Channel5。
     * Stream 决定使用哪组寄存器，Channel 决定请求来自哪个外设。
     */
    LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_1, LL_DMA_CHANNEL_5);

    /*
     * 配置整条传输：
     * PERIPH_TO_MEMORY：从 USART6->DR 搬到内存；
     * CIRCULAR：搬满 256 字节后自动从缓冲区开头继续；
     * PERIPH_NOINCREMENT：外设地址永远是同一个 DR；
     * MEMORY_INCREMENT：每收到一个字节，内存地址加 1；
     * PDATAALIGN_BYTE / MDATAALIGN_BYTE：两端都按 8 位字节传输；
     * PRIORITY_HIGH：DMA 仲裁优先级设为高。
     */
    LL_DMA_ConfigTransfer(DMA2, LL_DMA_STREAM_1,
                          LL_DMA_DIRECTION_PERIPH_TO_MEMORY |
                          LL_DMA_MODE_CIRCULAR |
                          LL_DMA_PERIPH_NOINCREMENT |
                          LL_DMA_MEMORY_INCREMENT |
                          LL_DMA_PDATAALIGN_BYTE |
                          LL_DMA_MDATAALIGN_BYTE |
                          LL_DMA_PRIORITY_HIGH);

    /*
     * 关闭 DMA FIFO，使用直接模式。
     * USART6 每次只产生一个字节的 DMA 请求，直接模式已经足够。
     */
    LL_DMA_DisableFifoMode(DMA2, LL_DMA_STREAM_1);

    /*
     * 配置源地址和目标地址：
     * 源地址是 USART6 数据寄存器 DR，固定不变；
     * 目标地址是 256 字节 DMA 循环缓冲区的首地址。
     */
    LL_DMA_ConfigAddresses(DMA2, LL_DMA_STREAM_1,
                           (uint32_t)&USART6->DR,
                           (uint32_t)usart6_dma_rx_buf,
                           LL_DMA_DIRECTION_PERIPH_TO_MEMORY);

    /*
     * NDTR 初值设为 256，表示本轮需要搬运 256 个字节。
     * 循环模式下 NDTR 递减到 0 后，硬件会自动重新装载为 256。
     */
    LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_1, USART6_RX_BUF_SIZE);

    /*
     * 启动前清除 Stream1 的全部旧状态：
     * HT=半传输，TC=传输完成，TE=传输错误，
     * DME=直接模式错误，FE=FIFO 错误。
     */
    LL_DMA_ClearFlag_HT1(DMA2);
    LL_DMA_ClearFlag_TC1(DMA2);
    LL_DMA_ClearFlag_TE1(DMA2);
    LL_DMA_ClearFlag_DME1(DMA2);
    LL_DMA_ClearFlag_FE1(DMA2);

    /* 软件还没有处理 DMA 缓冲区中的任何字节，所以从下标 0 开始。 */
    usart6_dma_rx_pos = 0U;

    /*
     * HT：DMA 写完前 128 字节时进入中断，及时保护前半区数据；
     * TC：DMA 写完后 128 字节时进入中断，然后自动开始下一轮；
     * TE/DME：发生传输错误或直接模式错误时进入中断并执行恢复。
     */
    LL_DMA_EnableIT_HT(DMA2, LL_DMA_STREAM_1);
    LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_1);
    LL_DMA_EnableIT_TE(DMA2, LL_DMA_STREAM_1);
    LL_DMA_EnableIT_DME(DMA2, LL_DMA_STREAM_1);

    /* 地址、长度、模式和中断都准备好以后，最后才启动 Stream1。 */
    LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_1);
}

/**
 * @brief 清除DMA2 Stream6的TX状态标志。
 *
 * TC、TE、DME和FE是硬件保持型状态，软件确认后必须主动清除。
 * TX未启用半传输中断，因此这里不处理HT。
 */
static void USART6_LL_TxDmaClearFlags(void){
    LL_DMA_ClearFlag_TC6(DMA2);
    LL_DMA_ClearFlag_TE6(DMA2);
    LL_DMA_ClearFlag_DME6(DMA2);
    LL_DMA_ClearFlag_FE6(DMA2);
}

/**
 * @brief 配置USART6_TX的DMA发送通道。
 *
 * 固定映射为DMA2 Stream6 Channel5。Stream使用普通模式，每次发送前
 * 由USART6_LL_SendDma()重新设置源地址和NDTR。
 */
static void USART6_LL_TxDmaInit(void){
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

    LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_6);
    while(LL_DMA_IsEnabledStream(DMA2, LL_DMA_STREAM_6)){
        /* EN清零后才允许修改Stream6配置。 */
    }

    LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_6, LL_DMA_CHANNEL_5);
    LL_DMA_ConfigTransfer(DMA2, LL_DMA_STREAM_6,
                          LL_DMA_DIRECTION_MEMORY_TO_PERIPH |
                          LL_DMA_MODE_NORMAL |
                          LL_DMA_PERIPH_NOINCREMENT |
                          LL_DMA_MEMORY_INCREMENT |
                          LL_DMA_PDATAALIGN_BYTE |
                          LL_DMA_MDATAALIGN_BYTE |
                          LL_DMA_PRIORITY_HIGH);
    LL_DMA_DisableFifoMode(DMA2, LL_DMA_STREAM_6);
    LL_DMA_SetPeriphAddress(DMA2, LL_DMA_STREAM_6, (uint32_t)&USART6->DR);
    LL_DMA_SetMemoryAddress(DMA2, LL_DMA_STREAM_6, 0U);
    LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_6, 0U);

    USART6_LL_TxDmaClearFlags();

    LL_DMA_EnableIT_TC(DMA2, LL_DMA_STREAM_6);
    LL_DMA_EnableIT_TE(DMA2, LL_DMA_STREAM_6);
    LL_DMA_EnableIT_DME(DMA2, LL_DMA_STREAM_6);
}

/**
 * @brief 在TE或DME后重新启动DMA接收。
 *
 * 保留通道、方向和地址配置，重装NDTR并从DMA缓冲区首地址重新接收。
 */
static void USART6_LL_RxDmaRestart(void){
    /* 出错恢复前先停止 Stream；EN 未清零时不能重装 NDTR。 */
    LL_DMA_DisableStream(DMA2, LL_DMA_STREAM_1);
    while(LL_DMA_IsEnabledStream(DMA2, LL_DMA_STREAM_1)){
        /* 等待硬件确认停止。 */
    }

    /* 清除本次错误以及可能同时出现的 HT、TC、DME、FE 状态。 */
    LL_DMA_ClearFlag_HT1(DMA2);
    LL_DMA_ClearFlag_TC1(DMA2);
    LL_DMA_ClearFlag_TE1(DMA2);
    LL_DMA_ClearFlag_DME1(DMA2);
    LL_DMA_ClearFlag_FE1(DMA2);

    /* 从一轮完整的 256 字节接收重新开始。 */
    LL_DMA_SetDataLength(DMA2, LL_DMA_STREAM_1, USART6_RX_BUF_SIZE);
    usart6_dma_rx_pos = 0U; /* 软件读位置也回到 DMA 缓冲区开头。 */

    /* Channel、方向和地址配置仍然保留，直接重新启动即可。 */
    LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_1);
}

/**
 * @brief 获取软件接收队列中的未处理字节数。
 * @return 未处理字节数，范围为0至255。
 */
uint16_t USART6_LL_RxCount(void){
    uint16_t head = usart6_rx_head;
    uint16_t tail = usart6_rx_tail;
    /* 下标即使已经绕圈，& MASK 仍能得到 0～255 范围内的距离。 */
    return (uint16_t)((head - tail) & USART6_RX_MASK);
}

/**
 * @brief 查看队头之后的指定字节，不移动读取位置。
 * @param offset 相对当前队头的偏移；必须小于USART6_LL_RxCount()。
 * @return 指定位置的字节。
 */
uint8_t USART6_LL_RxPeek(uint16_t offset){
    /* offset 以当前 tail 为 0，& MASK 处理跨越数组末尾的情况。 */
    uint16_t idx = (uint16_t)((usart6_rx_tail + offset) & USART6_RX_MASK);
    return usart6_rx_buf[idx];
}

/**
 * @brief 从软件接收队列消费或丢弃指定数量的字节。
 * @param n 字节数；必须不大于USART6_LL_RxCount()。
 */
void USART6_LL_RxDrop(uint16_t n){
    usart6_rx_tail = (uint16_t)((usart6_rx_tail + n) & USART6_RX_MASK);
}

/**
 * @brief 从软件接收队列读取并消费一个字节。
 * @param data 输出地址。
 * @return 成功返回1；队列为空或data为NULL时返回0。
 *
 * 本接口与USART6_Packet_Read()共用读取位置，不能同时使用。
 */
int USART6_LL_ReadByte(uint8_t *data){
    uint16_t tail;
    if(data == 0) return 0;
    if(usart6_rx_head == usart6_rx_tail) return 0;

    tail = usart6_rx_tail;
    *data = usart6_rx_buf[tail];
    usart6_rx_tail = (uint16_t)((tail + 1) & USART6_RX_MASK);
    return 1;
}

/**
 * @brief 处理USART6的IDLE、ORE、FE和NE事件。
 *
 * STM32F4要求依次读取SR、DR清除标志；随后提交DMA中的新增字节。
 */
void USART6_LL_IRQHandler(void){
    uint32_t sr;

    sr = USART6->SR;
    if((sr & (LL_USART_SR_IDLE | LL_USART_SR_ORE |
              LL_USART_SR_FE | LL_USART_SR_NE)) == 0U){
        return;
    }

    /* 与上面的 SR 读取组成清标志序列；读出的错误字节不参与解析。 */
    (void)USART6->DR;

    /* 提交读取 DR 之前已经由 DMA 搬入内存的全部新字节。 */
    USART6_LL_RxDmaDrain();
}

/**
 * @brief 处理DMA2 Stream1的HT、TC、TE和DME事件。
 *
 * HT/TC提交新增数据；TE/DME保存已完成数据后重启DMA。
 */
void USART6_LL_DmaRxIRQHandler(void){
    uint32_t te = LL_DMA_IsActiveFlag_TE1(DMA2);
    uint32_t dme = LL_DMA_IsActiveFlag_DME1(DMA2);

    if((te != 0U) || (dme != 0U)){
        USART6_LL_RxDmaDrain();
        USART6_LL_RxDmaRestart();
        return;
    }

    if(LL_DMA_IsActiveFlag_HT1(DMA2)){
        LL_DMA_ClearFlag_HT1(DMA2);
    }
    if(LL_DMA_IsActiveFlag_TC1(DMA2)){
        LL_DMA_ClearFlag_TC1(DMA2);
    }

    LL_DMA_ClearFlag_DME1(DMA2);
    LL_DMA_ClearFlag_FE1(DMA2);
    USART6_LL_RxDmaDrain();
}


/**
 * @brief 处理DMA2 Stream6的TX完成、TE和DME事件。
 *
 * DMA中断优先级为5，可以调用FreeRTOS FromISR接口。处理结果写入状态
 * 后通知等待任务；任务被唤醒后检查结果并等待USART TC。
 */
void USART6_LL_DmaTxIRQHandler(void){
    BaseType_t higher_priority_task_woken = pdFALSE;
    TaskHandle_t wait_task;
    int8_t result;

    result = 0;
    if((LL_DMA_IsActiveFlag_TE6(DMA2) != 0U) ||
       (LL_DMA_IsActiveFlag_DME6(DMA2) != 0U)){
        result = -1;
    }else if(LL_DMA_IsActiveFlag_TC6(DMA2) != 0U){
        result = 1;
    }

    USART6_LL_TxDmaClearFlags();

    if(result == 0) return;

    wait_task = (TaskHandle_t)usart6_tx_dma_wait_task;
    usart6_tx_dma_result = result;
    usart6_tx_dma_wait_task = 0;

    /*
     * busy由等待任务在检查结果、等待USART TC以后清零。
     * 这样DMA TC到线路TC之间仍保持发送通道独占。
     */
    if(wait_task != 0){
        vTaskNotifyGiveFromISR(wait_task, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}
