FreeRTOS_LL_demo 工程说明
========================

本工程不是「只加了串口」或「只加了 USB」。
当前仓库里三块都在，进度不同：

	FreeRTOS V11.3.1	内核已移植进 Keil，已创建 USART6 数据包任务并启动调度器。
	USB OTG FS LL	初始化框架已写，软断开，还不能枚举。
	USART6 LL	PC6/PC7，DMA 循环接收 + 软件队列 + 数据包解析与回发。

详细说明按模块分开，不要互相覆盖：

	FreeRTOS	_explain/freertos/freertos_ll_demo_readme.html
	USB FS		_explain/usb/usb_fs_ll_init.html
	USART6 初始化	_explain/usart/usart6/driver/01_usart6_初始化.html

环形队列网页：_explain/usart/basic/usart_初等环形队列.html
数据包解析教程：_explain/usart/usart6/packet/usart6_数据包解析教程.html
数据包解析动画：_explain/usart/usart6/packet/usart6_数据包解析动画.html

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

	当前 main.c 创建 USART6_AppTask 后调用 vTaskStartScheduler()。
	任务负责解析 USART6 数据包并按相同协议回发。
	任务中优先 vTaskDelay()；不要在关中断或临界区里 HAL_Delay() / printf。

	网页：_explain/freertos/freertos_ll_demo_readme.html

三、USB OTG FS（已经在工程里，未完成枚举）
	PA11 = DM，PA12 = DP，AF10，内置 FS PHY，Device Only。
	VBUS 检测关闭。PA9 已空出，仍不给 USB VBUS。
	USB_OTG_FS 由 Hardware/usb_fs 独占，不调用 HAL_PCD_Init。
	HAL_PCD_MODULE_ENABLED 只为编译 stm32f4xx_ll_usb.c。
	当前停在 USB_DevDisconnect，全局中断未开。
	FIFO、EP0、OTG_FS_IRQHandler、描述符都还没有。
	不要连接主机测枚举。

	网页：_explain/usb/usb_fs_ll_init.html

四、USART6
	原理图：PC6 = USART6_TX，PC7 = USART6_RX，AF8。
	P13 为 UART6-TTL，不要再插 P20。
	115200 8N1。文件在 Hardware/usart。
	使用 LL 结构体 Init，因此需要：
		USE_FULL_LL_DRIVER
		stm32f4xx_ll_gpio.c
		stm32f4xx_ll_usart.c
		stm32f4xx_ll_rcc.c
	不要启用 HAL_UART_MODULE_ENABLED。
	Hardware 头文件统一 #include "stm32f4xx_hal.h"。
	printf 走 fputc；Keil 打开 Use MicroLIB。
	DMA2 Stream1 Channel5 循环接收、IDLE 中断、256 格环形队列、
	数据包解析和 FreeRTOS 回发任务已接入。

	网页：
		_explain/usart/usart1/usart1_ll_init.html
		_explain/usart/usart6/driver/01_usart6_初始化.html
		_explain/usart/usart6/driver/02_usart6_无DMA收发.html
		_explain/usart/usart6/driver/03_usart6_DMA初始化.html
		_explain/usart/usart6/driver/04_usart6_DMA接收与轮询发送.html
		_explain/usart/usart6/packet/usart6_数据包解析教程.html
		_explain/usart/usart6/packet/usart6_数据包解析动画.html

五、HAL 与 LL
	HAL：系统时钟、HAL_Init、SysTick。
	LL USB：USB_OTG_FS。
	LL USART6：USART6 和 PC6/PC7。
	LL_RCC_GetSystemClocksFreq() 只读 PCLK，不改 PLL。
	同一个外设和同一根 GPIO 不能由 HAL、LL 两边重复初始化。

六、CubeMX Generate 后检查
	1. SVC/PendSV 空函数是否被重新生成；SysTick 共用逻辑是否还在。
	2. FreeRTOS 头文件、extern、Keil 的 RTOS 路径和 FPU 是否还在。
	3. HAL_PCD_MODULE_ENABLED、usb_fs 源文件和 Include Path 是否还在。
	4. USE_FULL_LL_DRIVER、ll_gpio.c / ll_usart.c / ll_rcc.c 是否还在。
	5. usart6_ll.c、usart6_packet.c、usart6_app.c 和 ..\Hardware\usart 是否还在。
	6. HAL_UART_MODULE_ENABLED 是否仍为注释。
	7. PC6/PC7 与 PA11/PA12 有没有被 Cube 改走。
	8. USB 未完成前是否仍软断开。

七、下一步（按模块，不要混成一件事）
	USART6	数据包回发已上板验证；RX DMA + IDLE 已接入，等待上板验证。
	USB	先 FIFO 和 EP0，再 OTG_FS_IRQHandler，完成前不连主机。
	FreeRTOS	最小数据包任务和调度器已运行；后续按模块增加任务。


USART6 环形队列与数据包解析
==========================

相关网页：
	初等环形队列：_explain/usart/basic/usart_初等环形队列.html
	USART6初始化：_explain/usart/usart6/driver/01_usart6_初始化.html
	无DMA收发：_explain/usart/usart6/driver/02_usart6_无DMA收发.html
	DMA初始化：_explain/usart/usart6/driver/03_usart6_DMA初始化.html
	DMA接收与轮询发送：_explain/usart/usart6/driver/04_usart6_DMA接收与轮询发送.html
	数据包解析教程：_explain/usart/usart6/packet/usart6_数据包解析教程.html
	交互解析动画：_explain/usart/usart6/packet/usart6_数据包解析动画.html

解析动画使用示例字节 AA 55 AA 55 02 48 69 23，可播放、暂停、
单步查看 Count、Peek、Drop、tail 移动、非法 LEN 重新同步和 XOR 校验。

一、当前硬件
	MCU：STM32F407ZGT6。
	USART6_TX = PC6，USART6_RX = PC7，AF8，RX 上拉。
	PCLK2 = 84MHz，115200 8N1。
	座子：原理图 P13 的 UART6-TTL，不要再插 P20。
	文件分层：
	- usart6_ll.c/.h：GPIO、USART、DMA、软件字节队列和中断处理。
	- usart6_packet.c/.h：AA 55 LEN DATA XOR协议解析和组包。
	- usart6_app.c/.h：FreeRTOS数据包回显任务。
	USART6 中断入口调用 USART6_LL_IRQHandler；
	DMA2 Stream1 中断入口调用 USART6_LL_DmaRxIRQHandler。
	USART6和DMA2 Stream1的NVIC优先级均为4。ISR不调用FreeRTOS API。

	PA9/PA10 已空出。USB 仍然不要把 PA9 配成 VBUS。

二、环形队列
	接收缓冲区共 256 格，故意留一格区分空和满，最多积压 255 字节。
	256 是同时积压量，不是累计接收上限；任务取走数据后格子可以反复使用。

	head：下一个空位，只由接收中断推进。
	tail：下一个未读字节，只由接收任务推进。
	空：head == tail。
	满：next == tail，其中 next = (head + 1) & USART6_RX_MASK。
	满时丢弃新字节，head 保持不变，不覆盖尚未读取的数据。

	USART6_RX_BUF_SIZE 必须是 2 的幂。
	USART6_RX_MASK = USART6_RX_BUF_SIZE - 1。
	容量为 256 时，(下标 + 1) & 255 与 (下标 + 1) % 256 结果相同；
	下标走到 255 后，下一次自动回到 0。

三、数据包格式
	线上格式：

		AA 55 LEN DATA... XOR

	SOF0 = 0xAA，SOF1 = 0x55。
	LEN 是 DATA 的字节数，不包含包头、LEN 自身和 XOR。
	DATA 最短 0 字节，最长 64 字节。
	XOR = LEN ^ DATA[0] ^ ... ^ DATA[LEN-1]，不包含包头。
	最大完整帧长度为 2 + 1 + 64 + 1 = 68 字节。

	例：载荷为字符 Hi，即 48 69：

		LEN = 02
		XOR = 02 ^ 48 ^ 69 = 23
		完整帧 = AA 55 02 48 69 23

四、接收与重新同步
	USART6_Packet_Read在任务中扫描环形队列：

	1. 包头不足：保留已有字节，等待 DMA 接收中断或 IDLE 中断继续入队。
	2. 包头不匹配：丢 1 字节，继续查找 AA 55。
	3. LEN 不合法：丢 1 字节，继续查找包头。
	4. 数据未收完整：保留半包，最多等待 20ms。
	5. 半包超时：丢 1 字节，重新同步。
	6. XOR 错误：丢 1 字节，重新同步。
	7. 校验通过：复制 DATA，消费完整数据包，返回成功。

	粘包不需要特殊分隔符。第一帧按 LEN 消费后，下一帧仍留在队列中，
	任务下一轮会继续解析。

	载荷中出现 AA 55 不会被误拆包。已经确认当前 LEN 合法时，
	解析器会按 LEN 取完整载荷，再检查 XOR。

	重复包头示例：

		AA 55 AA 55 02 48 69 23

	第一个 AA 被当成 LEN=170，超过 64，解析器只丢 1 字节；
	随后重新找到真正的 AA 55 02 48 69 23。

五、中断和任务分工
	USART6_IRQHandler 只调用 USART6_LL_IRQHandler。
	DMA2_Stream1_IRQHandler 只调用 USART6_LL_DmaRxIRQHandler。
	DMA2 Stream1 Channel5 以循环模式接收 USART6_RX。
	DMA 半传输、传输完成或 USART IDLE 时，根据 NDTR 找出新字节，
	再调用 RxPush 搬入软件环形队列。
	ORE/FE/NE时按顺序读取SR、DR清除错误；错误字节不交给协议解析器。
	不要在 ISR 中等待 TXE、发送数据、printf 或解析数据包。

	main创建USART6_AppTask后调用vTaskStartScheduler。
	应用任务调用USART6_Packet_Read；解析成功后再调用
	USART6_Packet_Send按相同协议回发。
	没有完整数据包时调用 vTaskDelay(1)，不在任务中持续空转。

	USART6_LL_ReadByte仍保留，但它和USART6_Packet_Read共用tail。
	同一个接收通道只能选择一种读取方式，不能同时使用。

六、发送
	USART6_Packet_Send依次发送SOF、LEN、DATA和XOR。
	发送函数会轮询等待 TXE/TC，只允许在任务或普通线程中调用，
	不能在 USART6 接收中断中调用。

	当前测试任务收到合法数据包后原样组包回发。
	普通键盘字符不符合协议，不再逐字节回显。

七、测试数据
	串口：P13，115200 8N1。
	发送区和接收区都选择 HEX；关闭本地回显。
	若发送区选成 ASCII，字符 AA 会在线上变成 41 41，不是真正的 0xAA。

	正常包：
		发送 AA 55 02 48 69 23
		应回 AA 55 02 48 69 23

	XOR 错误：
		发送 AA 55 02 48 69 00
		应无回包

	重复包头：
		发送 AA 55 AA 55 02 48 69 23
		应只回 AA 55 02 48 69 23

	粘包：
		发送 AA 55 02 48 69 23 AA 55 01 41 40
		应依次回两帧

	前置无效字节：
		发送 11 22 33 AA 55 02 48 69 23
		应丢弃 11 22 33，只回正常帧

	载荷包含包头：
		发送 AA 55 03 AA 55 01 FD
		应整帧原样回发，不拆包

	空包：
		发送 AA 55 00 00
		当前 LEN_MIN=0，应原样回发

八、当前边界
	接收已改为 DMA2 Stream1 Channel5 循环接收，并由 HT、TC、IDLE 事件入队。
	队列满时直接丢新字节，当前没有溢出计数。
	XOR 只能发现常见传输错误，不是强校验；正式协议可改用 CRC16。
	发送仍是轮询方式，发送期间当前任务会等待，但更高优先级任务仍可抢占。
	USB 仍处于软断开状态，不要连接主机测试枚举。
