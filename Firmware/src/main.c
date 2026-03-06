#include <stdint.h>

/*
 * 主程序入口文件
 *
 * ?明：
 *  - 此文件?? MCU ??后的全局初始化，包括??、RF、GPIO、外?（UART、I2C、LED 等）
 *  - 包含了?深度休眠?醒（deep retention wakeup）的判断，并在不同?醒?景下?用??的初始化函数
 *  - irq_handler 放在 RAM 中以?保中断?理在 flash 不可用或?行速度要求高的?景下仍能可靠?行
 */

#include "tl_common.h"            // TLSR 系列通用?文件（芯片/SDK 公共定?）
#include "drivers.h"              // 硬件??抽象（timers、adc、pwm 等）
#include "stack/ble/ble.h"       // BLE ????文件
#include "vendor/common/user_config.h" // 用?配置（?????与宏）
#include "drivers/8258/gpio_8258.h"    // GPIO 8258 芯片??
#include "app_config.h"          // ?用配置（?目?常量）
#include "main.h"
#include "app.h"
#include "battery.h"
#include "ble.h"
#include "cmd_parser.h"
#include "epd.h"                 // ?子墨水?示??相?
#include "flash.h"
#include "i2c.h"                 // I2C ????
#include "led.h"                 // LED 控制封装
#include "nfc.h"
#include "ota.h"                 // OTA 更新相?
#include "uart.h"                // 串口??

/*
 * 中断服?入口
 *
 * 放到 RAM 中?行的原因：
 *  - 在某些低功耗或 flash 不可用的情形下，要求中断?理函数能可靠?行
 *  - 将??中断?理放在 RAM，可以降低延?并避免 flash ??引起的??
 *
 * __attribute__((optimize("-Os"))) 保?函数在???使用合??化??
 */
_attribute_ram_code_ __attribute__((optimize("-Os"))) void irq_handler(void)
{
    /* ?用 SDK 提供的通用中断?理器，??分?底?外?中断 */
    irq_blt_sdk_handler();
}

/*
 * 主函数（必??行在 RAM 中以便于低功耗/快速?醒?景）
 *
 * 初始化?序?明：
 *  1. ??并??低功耗 32k 晶振（内部或外部），用于睡眠期?保持系??基
 *  2. CPU ?醒初始化，恢?或配置必要的寄存器/?源域
 *  3. ??是否从深度保留（deep retention）?醒，以便??不同的初始化路径
 *  4. 初始化射???并?置 BLE 模式
 *  5. 根据是否? deep retention 来决定是否重新初始化 GPIO（deep retention 下模??阻仍然保持）
 *  6. 根据系?主?初始化??
 *  7. 加?定制参数（例如 BLE 广播参数、?接参数等）
 *  8. 初始化?用所需外?（LED、UART、I2C）
 *  9. 根据?醒?型?用不同的用?初始化??：
 *     - user_init_deepRetn(): 从 deep retention ?醒??做必要恢?，避免重?初始化耗?耗能操作
 *     - user_init_normal(): 正常上????的完整初始化流程
 * 10. ?用中断并?入主循?，主循?交由 main_loop() ?理事件/任?
 */
_attribute_ram_code_ int main (void)    // must run in ramcode
{
    /* ??内部 32K 晶振作?系?低功耗?? */
    blc_pm_select_internal_32k_crystal();

    /* CPU ?醒相?初始化（上?或从睡眠?醒?的必要?置） */
    cpu_wakeup_init();

    /* 判断是否?深度保留?醒（deep retention wakeup） */
    int deepRetWakeUp = pm_is_MCU_deepRetentionWakeup();  // MCU deep retention wakeUp

    /* 初始化射???，?置? BLE 1M 模式 */
    rf_drv_init(RF_MODE_BLE_1M);

    /*
     * 初始化 GPIO
     *  - deep retention ?醒?，模??阻/引脚状?会被保留，因此通常不需要重新初始化
     *  - 否?在正常???需要配置 GPIO 初始状?
     */
    gpio_init( !deepRetWakeUp );  // analog resistance will keep available in deepSleep mode, so no need initialize again

#if (CLOCK_SYS_CLOCK_HZ == 16000000)
    /* 根据???系???宏????的晶振??配置 */
    clock_init(SYS_CLK_16M_Crystal);
#elif (CLOCK_SYS_CLOCK_HZ == 24000000)
    clock_init(SYS_CLK_24M_Crystal);
#endif

    /* 加? SDK/?用中自定?的参数（如 BLE 参数等） */
    blc_app_loadCustomizedParameters();
    
    /* 初始化?用外?：LED、UART、I2C（按需增加其他外?初始化） */
    init_led();
    init_uart();
    init_i2c();
        
    /*
     * 根据是否? deep retention ?醒??不同的用?初始化流程：
     *  - deepRetWakeUp: 只做从睡眠恢?需要的初始化，尽量?少耗?操作
     *  - normal: ?行全量????
     */
    if( deepRetWakeUp ){
        user_init_deepRetn ();
        printf("\r\n\r\n\r\nBooting deep\r\n\r\n\r\n\r\n");
    }
    else{
        printf("\r\n\r\n\r\nBooting\r\n\r\n\r\n\r\n");
        user_init_normal ();
    }    

    /* ?用中断并?入主循? */
    irq_enable();
    while (1) {
        /* 将大部分?行?工作交? main_loop() 以?理事件、BLE 堆?、定?器等 */
        main_loop ();
    }
}

