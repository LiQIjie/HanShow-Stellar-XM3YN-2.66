#include <stdint.h>
#include "tl_common.h"
#include "app.h"
#include "main.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/common/blt_common.h"

#include "battery.h"
#include "ble.h"
#include "flash.h"
#include "ota.h"
#include "epd.h"
#include "time.h"
#include "bart_tif.h"

// Battery and temperature readings (kept in RAM for runtime use)
RAM uint8_t battery_level;
RAM uint16_t battery_mv;
RAM int16_t temperature;

// Track last refresh times to avoid redundant EPD updates
// Initialized to invalid values so first comparison always triggers update
RAM uint8_t hour_refresh = 100;
RAM uint8_t minute_refresh = 100;

// Settings structure (defined elsewhere)
extern settings_struct settings;

_attribute_ram_code_ void user_init_normal(void)
{                            // executed once on power-up (normal boot)
    // Initialize hardware/randomness/time and communication stacks
    random_generator_init(); // must: seed RNG used by BLE stack
    init_time();             // initialize RTC/time handling
    init_ble();              // initialize BLE advertising / services
    init_flash();            // initialize persistent storage
    init_nfc();              // initialize NFC (if present)

    // Optional: display test image on EPD after boot
    // epd_display_tiff((uint8_t *)bart_tif, sizeof(bart_tif));
    // epd_display(3334533);
}

_attribute_ram_code_ void user_init_deepRetn(void)
{ // executed after deep retention wakeup (resume from sleep)
    // Reinitialize minimal MCU/LL state restored after retention
    blc_ll_initBasicMCU();
    rf_set_power_level_index(RF_POWER_P3p01dBm);
    blc_ll_recoverDeepRetention();
}

_attribute_ram_code_ void main_loop(void)
{
    // Core SDK processing (BLE, events)
    blt_sdk_main_loop();

    // Timekeeping/alarms handling
    handler_time();

    // Periodic sensor/battery update (every 30s)
    if (time_reached_period(Timer_CH_1, 30))
    {
        // Read battery and temperature, then update advertising payload and notify
        battery_mv = get_battery_mv();
        battery_level = get_battery_level(battery_mv);
        temperature = get_temperature_c();
        // Use EPD temperature read for advertising (scaled by 10)
        set_adv_data(EPD_read_temp() * 10, battery_level, battery_mv);
        ble_send_battery(battery_level);
        ble_send_temp(EPD_read_temp() * 10);
    }

    // Update display every minute (full update once per hour, partial otherwise)
    uint8_t current_minute = (get_time() / 60) % 60;
    if (current_minute != minute_refresh)
    {
        minute_refresh = current_minute;
        uint8_t current_hour = ((get_time() / 60) / 60) % 24;
        if (current_hour != hour_refresh)
        {
            // New hour: perform full refresh
            hour_refresh = current_hour;
            epd_display(get_time(), battery_mv, temperature, 1);
        }
        else
        {
            // Same hour: perform partial refresh
            epd_display(get_time(), battery_mv, temperature, 0);
        }
    }

    // Blink LED periodically to indicate connection state (every 10s)
    if (time_reached_period(Timer_CH_0, 10))
    {
        if (ble_get_connected())
            set_led_color(3); // connected color
        else
            set_led_color(2); // advertising/not connected color
        WaitMs(1);
        set_led_color(0); // off
    }

    // Power management: if EPD update is ongoing configure wakeup sources
    if (epd_state_handler()) // if epd_update is ongoing enable gpio wakeup to put the display to sleep as fast as possible
    {
        cpu_set_gpio_wakeup(EPD_BUSY, 1, 1); // wake on EPD busy pin
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
    }
    else
    {
        // Normal power management processing
        blt_pm_proc();
    }
}
