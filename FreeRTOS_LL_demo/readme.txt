FreeRTOS_LL_demo USB OTG FS LL 初始化记录
==========================================

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
		VBUS sensing = 关闭，不配置 PA9。

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

	没有使用 LL_GPIO_InitTypeDef，因此不需要 USE_FULL_LL_DRIVER，
	也不需要把 stm32f4xx_ll_gpio.c 加入工程。

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
	main.c 在 MX_GPIO_Init() 之后、进入 while(1) 之前调用：

		if (USB_FS_LL_Init() != HAL_OK)
		{
			Error_Handler();
		}

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

十、使用约定
	HAL 与 LL 可以在同一工程中共存，但同一个外设实例只能由一方管理。
	本工程中时钟、HAL_Init 和 SysTick 仍可使用 HAL；
	USB_OTG_FS 的初始化、端点、FIFO、中断和协议处理由用户编写的 USB LL 代码独占。

	后续 USB 代码继续手写。未经确认，不自动补写 FIFO、EP0、中断或描述符实现。
