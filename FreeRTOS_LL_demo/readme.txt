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

环形队列网页：_explain/usart_初等环形队列.html
环形队列说明合在本文件后半，不再另开 txt。

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


USART 初等环形队列 readme
========================

网页版：_explain/usart_初等环形队列.html

一、当前硬件（已从 USART1 改到 USART6）
	MCU：STM32F407ZGT6。
	USART6_TX = PC6，AF8。
	USART6_RX = PC7，AF8，上拉。
	PCLK2 = 84MHz，115200 8N1。
	座子：原理图 P13 的 UART6-TTL，不要再插 P20。
	文件：Hardware/usart/usart6_ll.c、usart6_ll.h。
	中断名必须是 USART6_IRQHandler，向量表不认 USART1_IRQHandler。

	PA9/PA10 已空出。USB 仍然不要把 PA9 配成 VBUS。

二、这是什么队列
	128 字节数组 + head + tail。
	head：下一个空位，只由中断 RxPush 写。
	tail：下一个未读字节，只由 main 里 ReadByte 写。
	绕圈用 (下标 + 1) % 128。

	这是单生产者、单消费者、满了丢新字节、留一格区分空/满的初等写法。
	不是环形队列终极版。当前串口回显够用。

三、空和满
	空：现在的 head == 现在的 tail。没有未读数据。
	满：(head + 1) % 128 == 现在的 tail。再写会盖掉还没读走的。

	先算 next，不要先改 head。
	next == tail 则满：不写缓冲，head 保持原值，next 是局部变量，return 后消失。
	128 格最多存 127 字节，故意留一格。否则满和空都会变成 head == tail。

四、两个函数
	RxPush（中断里）
		next = (head + 1) % 128
		若 next == tail，丢这一字节并 return
		否则写入 buf[head]，再 head = next

	ReadByte（main 里）
		data 为空指针则返回 0
		head == tail 则空，返回 0
		否则取出 buf[tail]，tail 前进一格，返回 1

	main 回显：
		if (USART6_LL_ReadByte(&ch)) USART6_LL_SendByte(ch);

五、为什么能收超过 128 字节
	128 是同时最多暂存 127 字节，不是累计上限。
	中断推进 head，main 取走后 tail 跟上，格子会反复使用。
	边收边回显时，缓冲里通常只有几字节，所以一次贴几百字节也能原样回来。
	只有来得比取走快、未读顶到 127 时，RxPush 才丢新字节。

六、中断和 main
	中断可以打断 main，但 ISR 只读 DR、入队，很快返回。
	main 从打断处继续，再 ReadByte、等 TXE 回发。
	不要在 ISR 里 while (!TXE) 发送，也不要在 ISR 里 printf。
	NVIC 优先级 5，对齐 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY。
	当前 ISR 不调 FreeRTOS API。ORE 先读 DR 再 return，避免中断卡死。

七、不是终极版
	现在：% 绕圈、留一格、丢新字节、逐字节。
	以后按需再换：2 的幂用 & 代替 %、DMA+IDLE、FreeRTOS 队列。
	DMA 尚未做。调度器尚未启动，回显仍在 main 循环。

八、上板
	P13 UART6，115200 8N1，关掉本地回显。
	复位后应先看到 Hello, World!，再键入原样回来。
