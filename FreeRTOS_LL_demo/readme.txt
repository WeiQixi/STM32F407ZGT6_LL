FreeRTOS_LL_demo USB 与 USART1 LL 记录
======================================

一、目标与硬件环境
	MCU：STM32F407ZGT6，Cortex-M4F。
	外部晶振：HSE 8MHz。
	系统时钟：168MHz。
	PLLQ：7，USB 时钟为 48MHz。
	工具链：Keil MDK-ARM。

	本工程使用 USB_OTG_FS，工作在 Device Only 模式：
		PA11 = USB_OTG_FS_DM，复用功能 AF10。
		PA12 = USB_OTG_FS_DP，复用功能 AF10。
		PHY = Embedded PHY。
		Speed = Full Speed（12Mbit/s）。
		端点数量 = 4。
		DMA = 关闭。
		VBUS sensing = 关闭。PA9 现由 USART1_TX 使用，不再留给 USB VBUS。

	不使用 USB_OTG_HS。当前板卡通过 PA11/PA12 使用芯片内置 FS PHY，
	不需要外部 ULPI PHY。

二、实现边界
	USB_OTG_FS 由 Hardware/usb_fs/usb_fs_ll.c 直接管理。
	本工程不调用 HAL_PCD_Init，也没有把以下文件加入 Keil：
		stm32f4xx_hal_pcd.c
		stm32f4xx_hal_pcd_ex.c

	当前直接调用 ST 提供的 USB 底层函数：
		USB_DisableGlobalInt
		USB_CoreInit
		USB_SetCurrentMode
		USB_DevInit
		USB_DevDisconnect

	注意：
		stm32f4xx_ll_usb.c 不像 GPIO、USART LL 那样完全独立。
		它使用 HAL_StatusTypeDef、HAL_Delay() 等 HAL 基础设施，
		并受 HAL_PCD_MODULE_ENABLED 或 HAL_HCD_MODULE_ENABLED 条件编译控制。

	启用 HAL_PCD_MODULE_ENABLED 只是为了编译 USB LL 底层，
	不表示 USB_OTG_FS 已交给 HAL PCD 管理。同一个 USB 外设实例仍只由本工程的 LL 代码管理。

三、工程文件与 Keil 配置
	USB 文件：
		Hardware/usb_fs/usb_fs_ll.h
		Hardware/usb_fs/usb_fs_ll.c

	Keil Include Path：
		..\Hardware\usb_fs

	Keil 源文件：
		..\Drivers\STM32F4xx_HAL_Driver\Src\stm32f4xx_ll_usb.c
		..\Hardware\usb_fs\usb_fs_ll.c

	Core/Inc/stm32f4xx_hal_conf.h 中必须启用：
		#define HAL_PCD_MODULE_ENABLED

	不要启用：
		HAL_HCD_MODULE_ENABLED

	usb_fs_ll.h 必须包含：
		#include "stm32f4xx_hal.h"

	不要只包含 stm32f4xx_hal_def.h。在 USE_HAL_DRIVER 条件下，
	单独包含 hal_def 可能因循环包含顺序导致 HAL_StatusTypeDef 未定义，
	并进一步产生 RCC、GPIO、DMA 等一连串类型错误。

四、GPIO 初始化
	USB_FS_LL_GPIO_Init() 先开启 GPIOA 时钟，然后分别配置 PA11 和 PA12：
		模式：Alternate Function。
		输出类型：Push-Pull。
		速度：Very High。
		上下拉：No Pull。
		复用功能：AF10。

	当前使用 LL_GPIO_SetPinMode、LL_GPIO_SetPinOutputType、
	LL_GPIO_SetPinSpeed、LL_GPIO_SetPinPull 和 LL_GPIO_SetAFPin_8_15
	等单项内联函数。

	USB 引脚仍使用单项内联函数，不使用 LL_GPIO_InitTypeDef。
	USART1 改用结构体 Init 后，工程已定义 USE_FULL_LL_DRIVER，
	并加入了 stm32f4xx_ll_gpio.c。USB 的 GPIO 代码不必改成结构体。

五、USB_FS_LL_Init() 初始化顺序
	1. 调用 USB_FS_LL_GPIO_Init() 配置 PA11/PA12。
	2. 开启 USB_OTG_FS 外设时钟。
	3. 强制复位并释放 USB_OTG_FS。
	4. 填写 USB_OTG_CfgTypeDef：
		dev_endpoints = 4
		speed = USBD_FS_SPEED
		dma_enable = 0
		ep0_mps = 64
		phy_itface = USB_OTG_EMBEDDED_PHY
		Sof_enable = 0
		low_power_enable = 0
		lpm_enable = 0
		battery_charging_enable = 0
		vbus_sensing_enable = 0
		use_dedicated_ep1 = 0
		use_external_vbus = 0
	5. 调用 USB_DisableGlobalInt()，初始化期间保持 USB 全局中断关闭。
	6. 调用 USB_CoreInit() 初始化公共内核和内置 FS PHY。
	7. 调用 USB_SetCurrentMode(USB_DEVICE_MODE) 强制进入设备模式。
	8. 调用 USB_DevInit() 初始化 Device 模式寄存器和端点寄存器。
	9. 调用 USB_DevDisconnect() 保持软断开。

	正确函数名是 USB_DevInit，不是 USB_DeviceInit。
	每一步都检查 HAL_StatusTypeDef；任一步失败即返回 HAL_ERROR。

六、main.c 调用位置
	main.c 在 MX_GPIO_Init() 之后、进入 while(1) 之前调用 USB 和 USART1。
	建议先初始化 USART1，USB 失败进入 Error_Handler 时仍能先看到串口输出。

	该调用位于 FreeRTOS 调度器启动前。
	USB_SetCurrentMode() 内部会调用 HAL_Delay(10) 等待模式切换，
	此时 SysTick 已由 HAL_Init() 启动，SysTick_Handler 会执行 HAL_IncTick()，
	所以 HAL_Delay() 可以正常结束。

	这只是 USB LL 底层依赖 HAL 时基，不是 HAL 与 LL 同时管理 USB 外设。

七、当前状态
	已经完成：
		PA11/PA12 GPIO 配置。
		USB_OTG_FS 外设时钟与复位。
		USB 48MHz 时钟条件（PLLQ = 7）。
		公共内核初始化。
		强制 Device 模式。
		Device 模式寄存器初始化。
		保持全局中断关闭。
		保持设备软断开。

	尚未完成：
		Rx FIFO 和各 Tx FIFO 分配。
		EP0 激活。
		OTG_FS_IRQHandler。
		USB 中断源与 NVIC 配置。
		SETUP 包处理。
		标准设备请求处理。
		设备、配置和字符串描述符。
		USB_EnableGlobalInt。
		USB_DevConnect。
		上板枚举验证。

	因此，当前代码只建立了 USB FS Device 的初始化框架，
	还不是可枚举的 USB 设备。设备保持软断开，不应连接主机测试；
	电脑无法识别设备是当前阶段的正常结果。

八、下一步顺序
	1. 根据端点用途设计 Rx FIFO、EP0 Tx FIFO 和其他 IN 端点 Tx FIFO。
	2. 写入 FIFO 深度与起始地址。
	3. 激活 EP0 IN/OUT，并准备接收第一个 SETUP 包。
	4. 实现 OTG_FS_IRQHandler，先处理复位、枚举完成、Rx FIFO 非空和端点中断。
	5. 实现控制传输状态机与标准请求。
	6. 准备描述符。
	7. 完成中断处理后再调用 USB_EnableGlobalInt()。
	8. 所有必要路径完成后调用 USB_DevConnect()，再连接主机验证。

九、CubeMX Generate 后检查项
	CubeMX 重新生成代码后必须检查：
		1. main.c 中 usb_fs_ll.h 和 USB_FS_LL_Init() 是否保留。
		2. stm32f4xx_hal_conf.h 中 HAL_PCD_MODULE_ENABLED 是否保留。
		3. Keil Include Path 中 ..\Hardware\usb_fs 是否保留。
		4. stm32f4xx_ll_usb.c 和 usb_fs_ll.c 是否仍在 Keil 工程。
		5. PLLQ 是否仍为 7，USB 外设时钟是否仍为 48MHz。
		6. PA11/PA12 是否仍由 USB LL 代码独占。
		7. HAL PCD/HCD 源文件是否被误加入工程。
		8. USB 中断、FIFO、EP0 未完成时是否仍保持全局中断关闭和软断开。
		9. USE_FULL_LL_DRIVER 是否仍在 Keil 预处理器中。
		10. stm32f4xx_ll_gpio.c、stm32f4xx_ll_usart.c、stm32f4xx_ll_rcc.c 是否仍在工程中。
		11. usart1_ll.c、Include Path ..\Hardware\usart 是否保留。
		12. HAL_UART_MODULE_ENABLED 是否仍为注释。
		13. PA9/PA10 是否仍由 USART1 LL 独占，PA11/PA12 是否仍由 USB LL 独占。

十、使用约定
	HAL 与 LL 可以在同一工程中共存，但同一个外设实例只能由一方管理。
	本工程中时钟树、HAL_Init 和 SysTick 仍可使用 HAL；
	USB_OTG_FS 由用户 USB LL 代码独占；USART1 由用户 USART1 LL 代码独占。
	LL_RCC_GetSystemClocksFreq() 只读 PCLK，不重新配置 PLL。
	同一根 GPIO 脚、同一个 USART/USB 实例不能再被 HAL 初始化一遍。

	后续 USB 代码继续手写。未经确认，不自动补写 FIFO、EP0、中断或描述符实现。

十一、USART1 硬件
	原理图：启明欣欣407开发板(高配版)V6.1。
	USART1：PA9 = U1_TX，PA10 = U1_RX，复用 AF7。
	板级接口：
		P20 = UART1-TTL（U1_TX / U1_RX / GND）。
		J6 + MAX232 = UART1-RS232。
	参数：115200、8N1、无流控、16 倍过采样。
	USART1 挂在 APB2，当前 PCLK2 = 84MHz。

十二、USART1 LL 实现边界
	文件：
		Hardware/usart/usart1_ll.h
		Hardware/usart/usart1_ll.c

	头文件统一包含 stm32f4xx_hal.h，与 USB 相同。
	这不是 HAL 接管 USART1，只是拿到芯片定义和 HAL_StatusTypeDef。
	stm32f4xx_hal.h 本身不直接包含 stm32f4xx.h，链路为：
		stm32f4xx_hal.h
		→ stm32f4xx_hal_conf.h
		→ stm32f4xx_hal_rcc.h
		→ stm32f4xx_hal_def.h
		→ stm32f4xx.h

	不要启用 HAL_UART_MODULE_ENABLED、HAL_USART_MODULE_ENABLED。
	不要把 stm32f4xx_hal_uart.c 加入工程。
	不要调用 HAL_UART_Init。

	USART1 使用结构体：
		LL_GPIO_InitTypeDef + LL_GPIO_Init()
		LL_USART_InitTypeDef + LL_USART_Init()

	因此必须：
		1. Keil 预处理器增加 USE_FULL_LL_DRIVER。
		2. 工程加入 stm32f4xx_ll_gpio.c。
		3. 工程加入 stm32f4xx_ll_usart.c。
		4. 工程加入 stm32f4xx_ll_rcc.c。

	usart1_ll.c 里这四个 include 已经够编译本文件：
		stm32f4xx_ll_usart.h
		stm32f4xx_ll_gpio.h
		stm32f4xx_ll_bus.h
		stdio.h

	不必再 include stm32f4xx_ll_rcc.h。
	ll_rcc.c 是给 LL_USART_Init() 内部的 LL_RCC_GetSystemClocksFreq() 链接用的。
	只加宏、不加这三个 .c，链接会报 L6218E：Undefined symbol LL_GPIO_Init / LL_USART_Init。

	stm32f4xx_ll_bus.h 仍要引用，用来开 GPIOA/USART1 时钟并复位 USART1。
	LL_GPIO_Init() 不会自动开时钟。

十三、USART1 初始化顺序
	1. 开 GPIOA 时钟。
	2. 用 GPIO 结构体配置 PA9（TX，No Pull）和 PA10（RX，上拉），AF7。
	3. 开 USART1 时钟，ForceReset 后 ReleaseReset。
	4. LL_USART_Disable，再 LL_USART_ConfigAsyncMode。
	5. 填写 USART 结构体并 LL_USART_Init。
	6. LL_USART_Enable。
	7. USART1_IRQHandler 写好前不要开 RXNE 和 NVIC。

	发送：先等 TXE，再写 DR；一包结束再等 TC。
	printf 通过 fputc 调用 USART1_LL_SendByte。
	ARM Compiler 5 需要打开 Keil 的 Use MicroLIB，否则 printf 走半主机。
	换行写 \r\n。不要在关中断或临界区里 printf。

	网页版：_explain/usart1_ll_init.html
	USB 网页版仍是：_explain/usb_fs_ll_init.html
	FreeRTOS 移植网页不要覆盖：_explain/freertos_ll_demo_readme.html
