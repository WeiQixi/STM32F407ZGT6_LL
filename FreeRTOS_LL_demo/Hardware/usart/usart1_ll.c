#include "usart1_ll.h"

#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_bus.h"
#include <stdio.h>

static void USART1_LL_GPIO_Init(void){
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);

    GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = LL_GPIO_AF_7;

    /* PA9: USART1_TX */
    GPIO_InitStruct.Pin = LL_GPIO_PIN_9;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PA10: USART1_RX, 空闲为高电平, 单独加上拉 */
    GPIO_InitStruct.Pin = LL_GPIO_PIN_10;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void USART1_LL_Init(void){
    LL_USART_InitTypeDef usart = {0};

    USART1_LL_GPIO_Init();

    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
    LL_APB2_GRP1_ForceReset(LL_APB2_GRP1_PERIPH_USART1);
    LL_APB2_GRP1_ReleaseReset(LL_APB2_GRP1_PERIPH_USART1);

    LL_USART_Disable(USART1);
    LL_USART_ConfigAsyncMode(USART1);

    usart.BaudRate = 115200;
    usart.DataWidth = LL_USART_DATAWIDTH_8B;
    usart.StopBits = LL_USART_STOPBITS_1;
    usart.Parity = LL_USART_PARITY_NONE;
    usart.TransferDirection = LL_USART_DIRECTION_TX_RX;
    usart.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    usart.OverSampling = LL_USART_OVERSAMPLING_16;

    LL_USART_Init(USART1, &usart);
    LL_USART_Enable(USART1);
}

void USART1_LL_SendByte(uint8_t data){
    while(!LL_USART_IsActiveFlag_TXE(USART1));
    LL_USART_TransmitData8(USART1, data);
}

void USART1_LL_Send(const uint8_t *data, uint16_t len){
    uint16_t i;
    if (data == 0) return;
    for (i = 0; i < len; i++) USART1_LL_SendByte(data[i]);
    while (!LL_USART_IsActiveFlag_TC(USART1));
}

int fputc(int ch, FILE *f){
    (void)f;
    USART1_LL_SendByte((uint8_t)ch);
    return ch;
}
