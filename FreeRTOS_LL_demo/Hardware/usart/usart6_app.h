#ifndef USART6_APP_H
#define USART6_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/* FreeRTOS测试任务：收到合法数据包后按相同协议回发。 */
void USART6_AppTask(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* USART6_APP_H */
