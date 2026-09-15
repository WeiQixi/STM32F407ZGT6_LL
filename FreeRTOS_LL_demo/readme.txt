FreeRTOS_LL_demo 移植记录
========================

一、工程环境
	MCU：STM32F407ZGT6，Cortex-M4F。
	外部晶振：HSE 8MHz。
	系统时钟：168MHz（AHB 168MHz、APB1 42MHz、APB2 84MHz、Flash Latency 5）。
	FreeRTOS：V11.3.1。
	工具链：Keil MDK-ARM，工程已启用硬件 FPU，使用 RVDS/ARM_CM4F 端口。
	CubeMX 只负责基础时钟和 SYS 配置，FreeRTOS 由本工程手工移植。

二、裁剪 FreeRTOS 内核
	从官方 FreeRTOS-Kernel 中保留：
		croutine.c
		event_groups.c
		list.c
		queue.c
		stream_buffer.c
		tasks.c
		timers.c

	portable 目录保留：
		MemMang/heap_4.c
		RVDS/ARM_CM4F

	注意：
		原记录中的 event_gtoups.c 是笔误，正确文件名是 event_groups.c。
		当前内核源文件位于 RTOS/Source。
		当前移植层位于 RTOS/Portable/RVDS/ARM_CM4F。
		当前头文件位于 RTOS/Include。

三、建立目录并复制文件
	在工程根目录建立 RTOS 文件夹，内部建立：
		RTOS/Include
		RTOS/Source
		RTOS/Portable

	将裁剪后的源文件复制到对应目录。
	FreeRTOSConfig.h 放在 RTOS/Include。
	本工程的配置以 FreeRTOS-Kernel_tailor/examples/template_configuration 为核对基准，
	但配置值按 STM32F407 和当前工程实际情况填写，不能直接照搬模板默认值。

四、Keil 工程配置
	Include Paths 添加：
		../RTOS/Include
		../RTOS/Portable/RVDS/ARM_CM4F

	工程中加入：
		RTOS/Source 下 7 个内核 .c 文件
		RTOS/Portable/MemMang/heap_4.c
		RTOS/Portable/RVDS/ARM_CM4F/port.c

	ARM_CM4F 端口要求启用 FPU；否则 port.c 会报错。
	FreeRTOSConfig.h 是头文件，Keil 工程树未单独显示不代表没有生效。

五、FreeRTOSConfig.h 当前关键值
	configCPU_CLOCK_HZ                           SystemCoreClock
	configTICK_RATE_HZ                           1000
	configMAX_PRIORITIES                         10
	configTOTAL_HEAP_SIZE                        30KB
	configUSE_16_BIT_TICKS                       0（V11 会映射为 32 位 TickType_t）
	configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
	configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
	configKERNEL_INTERRUPT_PRIORITY              240（0xF0）
	configMAX_SYSCALL_INTERRUPT_PRIORITY         80（0x50）

	注意：
		configKERNEL_INTERRUPT_PRIORITY 和 configMAX_SYSCALL_INTERRUPT_PRIORITY
		必须写成计算后的数字。port.c 的 ARM 汇编中存在 mov r0, #宏，
		不能把带“<<”的 C 表达式直接交给 armasm。

	保留以下映射：
		#define xPortPendSVHandler PendSV_Handler
		#define vPortSVCHandler SVC_Handler

	不要启用以下直接映射，本工程需要自己包装 SysTick_Handler：
		//#define xPortSysTickHandler SysTick_Handler

六、连接 Cortex-M 异常处理
	在 stm32f4xx_it.c 中包含：
		#include "FreeRTOS.h"
		#include "task.h"

	并声明：
		extern void xPortSysTickHandler(void);

	注释 CubeMX 生成的空 SVC_Handler 和 PendSV_Handler，
	由 FreeRTOS 的 port.c 提供真正实现。

七、HAL 与 FreeRTOS 共用 SysTick（当前采用）
	Cortex-M 只有一颗 SysTick。本工程不改 TIM4 时间基准，也不使用 DWT 重写 HAL_GetTick。
	同一个 1ms SysTick 中断分别维护 HAL tick 和 FreeRTOS tick：

	void SysTick_Handler(void)
	{
		HAL_IncTick();

		if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
		{
			xPortSysTickHandler();
		}
	}

	这样处理的原因：
		调度器启动前，HAL_IncTick 继续工作，初始化阶段的 HAL_GetTick/HAL_Delay 不会停住。
		调度器启动后，再调用 xPortSysTickHandler 推进 FreeRTOS 内核节拍。
		HAL 和 FreeRTOS 各维护自己的 32 位毫秒计数，共用硬件 SysTick 不构成冲突。
		正常无符号差值写法可以处理约 49.7 天一次的毫秒计数回绕。

	使用约定：
		任务中优先使用 vTaskDelay，不用 HAL_Delay 占住 CPU。
		不要在关闭中断或 FreeRTOS 临界区内调用 HAL_Delay，否则 SysTick 无法进入。
		CubeMX 的“SYS -> Timebase Source 改为 TIMx”是另一种分离方案，不是必选项。
		DWT 重写 HAL_GetTick 也是另一种方案，当前共用 SysTick 时不需要。

八、已经遇到并解决的编译/链接问题
	1. A1586E port.c:503 Bad operand types (UnDefOT, Constant)
	   原因：configMAX_SYSCALL_INTERRUPT_PRIORITY 使用了带移位的表达式。
	   处理：改为字面量 80；内核最低优先级使用字面量 240。

	2. L6200E SysTick_Handler multiply defined (port.o / stm32f4xx_it.o)
	   原因：把 xPortSysTickHandler 宏改名为 SysTick_Handler，同时 it.c 又定义了一份。
	   处理：注释 SysTick 改名宏，在 it.c 保留唯一的 SysTick_Handler，
	         其中先推进 HAL tick，调度器启动后再调用 xPortSysTickHandler。

九、CubeMX 重新生成后的检查项
	CubeMX Generate 会改写 main.c 和 stm32f4xx_it.c。每次生成后检查：
		1. SVC_Handler 是否重新出现；若出现，继续注释。
		2. PendSV_Handler 是否重新出现；若出现，继续注释。
		3. SysTick_Handler 是否仍保留 HAL_IncTick 和调度器状态判断。
		4. FreeRTOS.h、task.h 与 xPortSysTickHandler 声明是否仍在 USER CODE 区。
		5. FreeRTOSConfig.h 中 SysTick 直接改名宏是否仍为注释状态。
		6. Keil 的 RTOS 源文件、Include Paths、ARM_CM4F 和 FPU 设置是否仍正确。

十、当前进度与下一步
	当前工程已经消除上述编译和链接错误，尚未在开发板上运行验证。
	下一步先建立最小任务并调用 vTaskStartScheduler()，验证：
		1. 调度器能够启动。
		2. vTaskDelay(1) 的 1ms 节拍正常。
		3. HAL_GetTick 持续递增。
		4. 两个不同周期任务能够正常切换。

	板上验证通过后，再继续手写 UART LL 初始化和中断/DMA。