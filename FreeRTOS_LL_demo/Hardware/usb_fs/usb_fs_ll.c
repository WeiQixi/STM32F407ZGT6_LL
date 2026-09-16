#include "usb_fs_ll.h"

#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_usb.h"

static void USB_FS_LL_GPIO_Init(void){
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);

    /* PA11: USB_OTG_FS_DM */
    LL_GPIO_SetPinMode(GPIOA,LL_GPIO_PIN_11,LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(GPIOA,LL_GPIO_PIN_11,LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOA,LL_GPIO_PIN_11,LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOA,LL_GPIO_PIN_11,LL_GPIO_PULL_NO);
    LL_GPIO_SetAFPin_8_15(GPIOA,LL_GPIO_PIN_11,LL_GPIO_AF_10);
    /* PA12: USB_OTG_FS_DP */
    LL_GPIO_SetPinMode(GPIOA,LL_GPIO_PIN_12,LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(GPIOA,LL_GPIO_PIN_12,LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOA,LL_GPIO_PIN_12,LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetPinPull(GPIOA,LL_GPIO_PIN_12,LL_GPIO_PULL_NO);
    LL_GPIO_SetAFPin_8_15(GPIOA,LL_GPIO_PIN_12,LL_GPIO_AF_10);
}

HAL_StatusTypeDef USB_FS_LL_Init(void){
    USB_OTG_CfgTypeDef usb_config = {0};

    /* 1. 配置USB引脚 */
    USB_FS_LL_GPIO_Init();

    /* 2. 开启USB OTG FS外设时钟 */
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_OTGFS);

    /* 3. 复位USB OTG FS外设 */
    LL_AHB2_GRP1_ForceReset(LL_AHB2_GRP1_PERIPH_OTGFS);
    LL_AHB2_GRP1_ReleaseReset(LL_AHB2_GRP1_PERIPH_OTGFS);

    /* 4. 准备USB底层配置 */
    usb_config.dev_endpoints = 4u;          // 设备端点数量
    usb_config.speed = USBD_FS_SPEED;       // USB速度
    usb_config.dma_enable = 0u;             // 禁用DMA
    usb_config.ep0_mps = 64U;               // 端点0最大包大小
    usb_config.phy_itface = USB_OTG_EMBEDDED_PHY; // 内嵌PHY
    usb_config.Sof_enable = 0u;             // 禁用SOF输出
    usb_config.low_power_enable = 0u;       // 禁用低功耗模式
    usb_config.lpm_enable = 0u;             // 禁用LPM
    usb_config.battery_charging_enable = 0u; // 禁用电池充电
    usb_config.vbus_sensing_enable = 0u;    // 禁用VBUS感测
    usb_config.use_dedicated_ep1 = 0u;      // 禁用专用EP1中断
    usb_config.use_external_vbus = 0u;      // 禁用外部VBUS

    /* 5. 初始化期间关闭USB全局中断 
     * OTG_FS_IRQHandler写好前不能打开。
     */
    if (USB_DisableGlobalInt(USB_OTG_FS) != HAL_OK) return HAL_ERROR;

    /* 6. 初始化USB公共内核和内置FS PHY */
    if (USB_CoreInit(USB_OTG_FS, usb_config) != HAL_OK) return HAL_ERROR;

    /* 7. 强制USB控制器进入Device模式 */
    if (USB_SetCurrentMode(USB_OTG_FS, USB_DEVICE_MODE) != HAL_OK) return HAL_ERROR;

    /* 8. 初始化Device模式寄存器和端点 */
    if (USB_DevInit(USB_OTG_FS, usb_config) != HAL_OK) return HAL_ERROR;

    /* 9. 暂时保持软断开。
     * 等EP0、FIFO和中断处理完成后才能连接电脑
     */
    if (USB_DevDisconnect(USB_OTG_FS) != HAL_OK) return HAL_ERROR;

    return HAL_OK;
}
