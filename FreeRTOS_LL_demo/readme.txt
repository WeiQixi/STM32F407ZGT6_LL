FreeRTOS_LL_demo 工程说明
========================

本工程不是「只加了串口」或「只加了 USB」。
当前仓库里三块都在，进度不同：

	FreeRTOS V11.3.1	内核已移植进 Keil，已创建 USART6 数据包任务并启动调度器。
	USB OTG FS LL	初始化框架已写，软断开，还不能枚举。
	USART6 LL	PC6/PC7，DMA 循环接收 + DMA 数据包发送 + 软件队列 + 数据包解析。

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
	数据包回发使用 DMA2 Stream6 Channel5；启动打印和 printf 仍轮询 TXE/TC。

	网页：
		_explain/usart/usart1/usart1_ll_init.html
		_explain/usart/usart6/driver/01_usart6_初始化.html
		_explain/usart/usart6/driver/03_usart6_DMA初始化.html
		_explain/usart/usart6/driver/04_usart6_DMA接收与轮询发送.html
		_explain/usart/usart6/driver/05_usart6_DMA发送.html
		_explain/usart/usart6/driver/06_usart6_双槽发送.html
		_explain/usart/usart6/driver/02_usart6_无DMA收发.html
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
	USART6	数据包发送改为两个静态槽。代码已接入，并已上板测试。
	USB	先 FIFO 和 EP0，再 OTG_FS_IRQHandler，完成前不连主机。
	FreeRTOS	最小数据包任务和调度器已运行；后续按模块增加任务。


USART6 环形队列与数据包解析
==========================

相关网页：
	初等环形队列：_explain/usart/basic/usart_初等环形队列.html
	USART6初始化：_explain/usart/usart6/driver/01_usart6_初始化.html
	DMA初始化：_explain/usart/usart6/driver/03_usart6_DMA初始化.html
	DMA接收与轮询发送：_explain/usart/usart6/driver/04_usart6_DMA接收与轮询发送.html
	DMA发送：_explain/usart/usart6/driver/05_usart6_DMA发送.html
	两个发送槽：_explain/usart/usart6/driver/06_usart6_双槽发送.html
	无DMA收发：_explain/usart/usart6/driver/02_usart6_无DMA收发.html
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
	USART6 中断入口调用 USART6_LL_IRQHandler。
	DMA2 Stream1 中断入口调用 USART6_LL_DmaRxIRQHandler。
	DMA2 Stream6 中断入口调用 USART6_LL_DmaTxIRQHandler。
	USART6 与 DMA2 Stream1 的 NVIC 优先级为 4，不调用 FreeRTOS API。
	DMA2 Stream6 的 NVIC 优先级为 5，完成中断调用 xSemaphoreGiveFromISR。

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
	DMA2_Stream6_IRQHandler 只调用 USART6_LL_DmaTxIRQHandler。
	DMA2 Stream1 Channel5 以循环模式接收 USART6_RX。
	DMA 半传输、传输完成或 USART IDLE 时，根据 NDTR 找出新字节，
	再调用 RxPush 搬入软件环形队列。
	ORE/FE/NE时按顺序读取SR、DR清除错误；错误字节不交给协议解析器。
	不要在 ISR 中等待 TXE、发送数据、printf 或解析数据包。

	main创建USART6_AppTask后调用vTaskStartScheduler。
	启动成功后不再回到 main 后面的循环。任务要在启动调度器之前创建。
	应用任务调用USART6_Packet_Read；解析成功后再调用
	USART6_Packet_Send按相同协议回发。
	没有完整数据包时调用 vTaskDelay(1)，不在任务中持续空转。

	USART6_LL_ReadByte仍保留，但它和USART6_Packet_Read共用tail。
	同一个接收通道只能选择一种读取方式，不能同时使用。

六、发送
	一次数据包发送的顺序：
	USART6_Packet_Send → USART6_LL_TxAlloc → 写入 data[] →
	USART6_LL_TxCommit → USART6_LL_TxKick →
	DMA2 Stream6 完成中断 USART6_LL_DmaTxIRQHandler。
	槽是 usart6_tx_slot[2]，每个元素含 data[68]、len、state。
	DMA 搬走的字节数是提交时的 len。

	USART6_Packet_Send(payload, len)：
	payload 为空且 len 不为 0，或 len 大于 64，直接返回，不领槽。
	TxAlloc 返回 0 也直接返回。
	frame_pos 从 0 起，frame[frame_pos++] 先写入再加下标。
	顺序为 AA、55、LEN、DATA、XOR。XOR 初值是 LEN，只异或 DATA。
	载荷 Hi 写完后 data[0..5] 为 AA 55 02 48 69 23，frame_pos 为 6。
	然后 TxCommit(slot_id, frame_pos)。空包提交长度为 4，最长为 68。

	USART6_LL_TxAlloc：
	slot_id 为 NULL、信号量句柄为 NULL，或调度器未运行，返回 NULL，槽不变。
	NULL 表示没传入槽号地址，不是槽号 0。
	接着 xSemaphoreTake(usart6_tx_free, portMAX_DELAY)。
	创建时计数为 2。减到 0 以后，下一个任务停在 Take 里，直到完成中断 GiveFromISR。
	正常醒来返回 pdTRUE。返回 NULL 只在 Take 失败时走到。
	临界区内 id = wr。该槽不是 EMPTY：退出临界区，Give 把计数还回，返回 NULL，wr 不变。
	是 EMPTY：状态改为 FILL，len 先写 0，wr = (wr + 1) % 2。
	退出临界区后把槽号写入 *slot_id，返回 data 地址。字节在回到 Packet_Send 之后才写入。

	USART6_LL_TxCommit：
	槽号大于等于 2、len 为 0 或 len 大于 68，进临界区前返回。
	Packet_Send 传来的 frame_pos 是 4 到 68，走不到这三句。
	临界区内状态必须已是 FILL，否则退出并返回，信号量不加。
	然后写入 len，状态改为 READY，调用 TxKick，再退出临界区。

	USART6_LL_TxKick，调用时已经在临界区：
	busy 不为 0，或 slot[rd] 不是 READY，直接返回。
	返回前 Commit 已经把本次槽写成 READY。
	第二句看的是 rd。刚提交的若是槽 1，而 rd 仍指着还在 FILL 的槽 0，就从这里返回，槽 1 的字节留着。
	两句都通过：该槽改为 SEND，busy = 1，need_tc = 0。
	关闭 Stream6 并等到 EN 为 0，清除 TC、TE、DME、FE。
	内存地址设为 slot[rd].data，NDTR 设为该槽 len，再打开 Stream6。

	USART6_LL_DmaTxIRQHandler，优先级 5：
	先看 TE、DME、TC。清这四个标志。三个事件都没有则返回，不改槽，不 Give。
	临界区内 slot[rd] 不是 SEND 也返回，不 Give。
	是 SEND：改回 EMPTY，len 清 0，rd = (rd + 1) % 2，busy 清 0。
	新的 slot[rd] 已是 READY 就再调用 TxKick，busy 会重新变为 1。
	不是 READY 则 need_tc = 1，printf 写 DR 前要先等 USART TC。
	退出临界区后 xSemaphoreGiveFromISR，需要时 portYIELD_FROM_ISR。
	Give 还的是刚释放的那一个槽。TE、DME 同样释放，不重发。

	USART6_LL_SendByte 给定启动信息和 printf，一次调用发送一个字节。
	调度器未运行：等 TXE，写 DR，返回。
	调度器已运行，进入 for(;;)：
	busy 为 1 则空等，另一个槽空着也等。
	need_tc 为 1 则再等 USART TC。
	进入临界区再看 busy。仍为 0：清 need_tc，等 TXE，写入这个字节，退出临界区并返回。
	已变为 1：退出临界区，for 的结尾回到循环开头。
	printf 每个字符经 fputc 调用一次 SendByte。一次返回之后，其他任务可以提交一整帧。
	SendByte 若停在挡住优先级 5 的临界区里，或停在 Stream6 中断函数里，完成中断无法把 busy 清 0。

	逐函数过程见 _explain/usart/usart6/driver/06_usart6_双槽发送.html。

	当前测试任务收到合法数据包后原样组包回发。
	普通键盘字符不符合协议，串口助手需发送完整 HEX 帧。

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

	同一帧连在一起：
		发送 AA 55 02 48 69 23 AA 55 02 48 69 23 AA 55 02 48 69 23
		每一帧 LEN 仍是 2，共 6 字节。整段按帧切开，应逐帧回同样的 6 字节。
		64 是单帧 DATA 的上限，不是助手里连在一起的总字节数。

八、当前边界
	接收使用 DMA2 Stream1 Channel5 循环模式，并由 HT、TC、IDLE 事件入队。
	队列满时直接丢新字节，当前没有溢出计数。
	XOR 适合发现常见传输错误；正式协议可改用 CRC16。
	数据包发送使用两个槽和 DMA2 Stream6。槽满时当前任务等待空槽，CPU 可运行其他任务。
	代码未保存 DMA 错误日志，失败的帧不重发。双槽发送已上板测试。
	USB 仍处于软断开状态，不连接主机测试枚举。
