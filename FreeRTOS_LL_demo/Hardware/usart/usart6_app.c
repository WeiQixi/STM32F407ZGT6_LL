#include "usart6_app.h"
#include "usart6_packet.h"
#include "FreeRTOS.h"
#include "task.h"

void USART6_AppTask(void *arg){
    uint8_t payload[USART6_PKT_LEN_MAX];
    uint16_t length;
    (void)arg;
    for(;;){
        if(USART6_Packet_Read(payload, &length)){
            USART6_Packet_Send(payload, length);
        }else{
            vTaskDelay(1U);
        }
    }
}
