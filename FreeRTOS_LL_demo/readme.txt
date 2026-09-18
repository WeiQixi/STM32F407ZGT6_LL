FreeRTOS_LL_demo 工程说明
========================

本工程不是「只加了串口」或「只加了 USB」。
当前仓库里三块都在，进度不同：

	FreeRTOS V11.3.1	内核已移植进 Keil，调度器尚未启动。
	USB OTG FS LL	初始化框架已写，软断开，还不能枚举。
	USART1 LL	PA9/PA10 轮询收发和 printf 已接入。

详细说明分开三页，不要互相覆盖：

	FreeRTOS	_explain/freertos_ll_demo_readme.html
	USB FS		_explain/usb_fs_ll_init.html
	USART1		_explain/usart1_ll_init.html

一、工程环境
	MCU：STM32F407ZGT6，Cortex-M4F。
	板卡：启明欣欣 407 开发板（高配版）V6.1。
	HSE 8MHz，SYSCLK 168MHz，APB1 42MHz，APB2 84MHz。
	PLLQ = 7，USB 48MHz。
	工具链：Keil MDK-ARM，硬件 FPU，RVDS/ARM_CM4F。
	时钟树、HAL_Init、SysTick 仍用 HAL。
	同一个外设实例只由一方管理。

二、FreeRTOS（已经在工程里）
	位置：
		RTOS/Source
		RTOS/Include
		RTOS/Portable/RVDS/ARM_CM4F
		RTOS/Portable/MemMang/heap_4.c

	关键配置：
		configCPU_CLOCK_HZ                          SystemCoreClock
		configTICK_RATE_HZ                          1000
		configMAX_PRIORITIES                        10
		configKERNEL_INTERRUPT_PRIORITY             240
		configMAX_SYSCALL_INTERRUPT_PRIORITY        80

	80 和 240 必须是字面量。port.c 的 armasm 不能处理带 << 的宏。

	保留：
		#define xPortPendSVHandler PendSV_Handler
		#define vPortSVCHandler SVC_Handler
		//#define xPortSysTickHandler SysTick_Handler

	SysTick 由 it.c 包装，HAL 与内核共用：
		HAL_IncTick();
		调度器启动后再调用 xPortSysTickHandler()。

	当前 main.c 还没有创建任务，也没有 vTaskStartScheduler()。
	内核在工程里，不等于调度器已经跑起来。
	任务中优先 vTaskDelay()；不要在关中断或临界区里 HAL_Delay() / printf。

	网页：_explain/freertos_ll_demo_readme.html

三、USB OTG FS（已经在工程里，未完成枚举）
	PA11 = DM，PA12 = DP，AF10，内置 FS PHY，Device Only。
	VBUS 检测关闭。PA9 给 USART1_TX，不给 USB VBUS。
	USB_OTG_FS 由 Hardware/usb_fs 独占，不调用 HAL_PCD_Init。
	HAL_PCD_MODULE_ENABLED 只为编译 stm32f4xx_ll_usb.c。
	当前停在 USB_DevDisconnect，全局中断未开。
	FIFO、EP0、OTG_FS_IRQHandler、描述符都还没有。
	不要连接主机测枚举。

	网页：_explain/usb_fs_ll_init.html

四、USART1（本次接入）
	原理图：PA9 = U1_TX，PA10 = U1_RX，AF7。
	P20 为 UART1-TTL；J6+MAX232 为 UART1-RS232。
	115200 8N1。文件在 Hardware/usart。
	使用 LL 结构体 Init，因此需要：
		USE_FULL_LL_DRIVER
		stm32f4xx_ll_gpio.c
		stm32f4xx_ll_usart.c
		stm32f4xx_ll_rcc.c
	不要启用 HAL_UART_MODULE_ENABLED。
	Hardware 头文件统一 #include "stm32f4xx_hal.h"。
	printf 走 fputc；Keil 打开 Use MicroLIB。
	RX 中断和 DMA 尚未做。

	网页：_explain/usart1_ll_init.html

五、HAL 与 LL
	HAL：系统时钟、HAL_Init、SysTick。
	LL USB：USB_OTG_FS。
	LL USART1：USART1 和 PA9/PA10。
	LL_RCC_GetSystemClocksFreq() 只读 PCLK，不改 PLL。
	GPIOA 时钟可以开两次；同一根脚不能两边各 Init 一次。

六、CubeMX Generate 后检查
	1. SVC/PendSV 空函数是否被重新生成；SysTick 共用逻辑是否还在。
	2. FreeRTOS 头文件、extern、Keil 的 RTOS 路径和 FPU 是否还在。
	3. HAL_PCD_MODULE_ENABLED、usb_fs 源文件和 Include Path 是否还在。
	4. USE_FULL_LL_DRIVER、ll_gpio.c / ll_usart.c / ll_rcc.c 是否还在。
	5. usart1_ll.c、..\Hardware\usart 是否还在。
	6. HAL_UART_MODULE_ENABLED 是否仍为注释。
	7. PA9/PA10 与 PA11/PA12 有没有被 Cube 改走。
	8. USB 未完成前是否仍软断开。

七、下一步（按模块，不要混成一件事）
	USART1	需要的话再写 RX 中断。先用 P20 确认 115200。
	USB	先 FIFO 和 EP0，再 OTG_FS_IRQHandler，完成前不连主机。
	FreeRTOS	上板验证后再建最小任务并 vTaskStartScheduler()。
