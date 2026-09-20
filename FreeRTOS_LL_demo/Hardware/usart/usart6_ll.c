#include "usart6_ll.h"

#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include <stdio.h>

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

int fputc(int ch, FILE *f){
    (void)f;
    USART6_LL_SendByte((uint8_t)ch);
    return ch;
}

#define USART6_RX_BUF_SIZE 128U
static uint8_t usart6_rx_buf[USART6_RX_BUF_SIZE];
static volatile uint16_t usart6_rx_head;
static volatile uint16_t usart6_rx_tail;

static void USART6_LL_RxPush(uint8_t data){
    uint16_t next = (uint16_t)((usart6_rx_head + 1) % USART6_RX_BUF_SIZE);
    if(next == usart6_rx_tail) return; // buffer full, discard data
    usart6_rx_buf[usart6_rx_head] = data;
    usart6_rx_head = next;
}

int USART6_LL_ReadByte(uint8_t *data){
    uint16_t tail;
    if(data == 0) return 0;
    if(usart6_rx_head == usart6_rx_tail) return 0; // buffer empty
    
    tail = usart6_rx_tail;
    *data = usart6_rx_buf[tail];
    usart6_rx_tail = (uint16_t)((tail + 1) % USART6_RX_BUF_SIZE);
    return 1;
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
