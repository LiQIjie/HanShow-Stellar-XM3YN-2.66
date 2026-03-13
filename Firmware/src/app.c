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
{                            // this will get executed one time after power up
    /*
     * Normal initialization executed once after a cold power-up.
     * - Initialize hardware/random number generator required by SDK
     * - Initialize timekeeping used for periodic tasks and display updates
     * - Initialize BLE stack and application-level BLE services
     * - Initialize flash storage for persistent settings and data
     * - Initialize NFC if present (used for configuration or wakeup)
     *
     * Note: example calls to draw a TIFF or display a raw image are left commented
     * as they are only used for debugging or initial visual verification.
     */
    random_generator_init(); // required by TLSR SDK
    init_time();
    init_ble();
    init_flash();
    init_nfc();

    // Optional: display test image on EPD after boot
    // epd_display_tiff((uint8_t *)bart_tif, sizeof(bart_tif));
    // epd_display(3334533);
    epd_display_default_meeting(0);
}

_attribute_ram_code_ void user_init_deepRetn(void)
{ // after sleep this will get executed
    /*
     * Initialization executed after a deep retention wakeup (wake from deep sleep
     * where some RAM/retention state is preserved but peripherals need re-init).
     * - Re-initialize basic MCU/LL subsystems required after retention
     * - Restore RF transmit power to a safe default
     * - Recover BLE link-layer retention state so BLE can resume
     */
    blc_ll_initBasicMCU();
    rf_set_power_level_index(RF_POWER_P3p01dBm);
    blc_ll_recoverDeepRetention();
}

_attribute_ram_code_ void main_loop(void)
{
    /*
     * Run the TLSR SDK main loop.
     * - Handles BLE link-layer processing and other SDK-internal periodic work.
     */
    blt_sdk_main_loop();

    /*
     * Update time base and handle timer callbacks.
     * - Keeps software timers and timekeeping updated for scheduled tasks.
     */
    handler_time();

    /*
     * Periodic tasks (triggered by Timer_CH_1 at a coarse interval, e.g. 30s):
     * - Sample battery voltage and update the global battery state
     * - Sample device/ambient temperature
     * - Update BLE advertising payload with the latest sensor values so
     *   phones or scanners can read them without a connection
     * - Send battery and temperature to connected BLE peers via notifications
     */
    if (time_reached_period(Timer_CH_1, 30))
    {
        /* Read battery voltage (mV) and convert to percentage level. */
        battery_mv = get_battery_mv();
        battery_level = get_battery_level(battery_mv);

        /* Read temperature in degrees Celsius. */
        temperature = get_temperature_c();

        /*
         * Update advertising data and notify connected peers.
         */
        int16_t epd_temp_scaled = EPD_read_temp() * 10;
        set_adv_data(epd_temp_scaled, battery_level, battery_mv);
        ble_send_battery(battery_level);
        ble_send_temp(epd_temp_scaled);
    }

    /*
     * EPD display refresh based on time: update EPD every minute.
     * - Fast refresh (quick black flash like e-readers) every 30 minutes to prevent ghosting
     * - Partial refresh (no flash) on other minutes to save power
     */
    uint8_t current_minute = (get_time() / 60) % 60;
    if (current_minute != minute_refresh)
    {
        minute_refresh = current_minute;
        uint8_t current_hour = ((get_time() / 60) / 60) % 24;
        if (current_hour != hour_refresh)
        {
            /* On the hour: perform a more comprehensive screen update (e.g., full refresh) */
            hour_refresh = current_hour;
            epd_display(get_time(), battery_mv, temperature, 0);
        }
        else
        {
            /* On non-hourly minute updates: perform a partial or less intensive screen update to save power and reduce wear. */
            epd_display(get_time(), battery_mv, temperature, 1);
        }
    }

    /*
     * LED status indicator: blink briefly on a fast timer to indicate
     * connection state.
     * - Color index 3 indicates BLE connected (example: connected LED)
     * - Color index 2 indicates not connected (advertising/idle)
     * The LED is lit for a very short time (1 ms) to provide a blink cue.
     */
    if (time_reached_period(Timer_CH_0, 10))
    {
        if (ble_get_connected())
            set_led_color(3); // connected color Blue
        else
			set_led_color(2); // advertising/not connected color Green
        WaitMs(1);
        set_led_color(0); // off
    }

    /*
     * Manage wakeup and power handling when EPD updates are active.
     * - If an EPD update is in progress (epd_state_handler() returns true),
     *   configure the EPD busy GPIO as an external wakeup source and disable
     *   deep suspend so the CPU remains available until the display
     *   operation completes.
     * - If no EPD update is active, yield control to the SDK power manager
     *   so the system can enter appropriate low-power states (blt_pm_proc()).
     */
    if (epd_state_handler()) // if epd_update is ongoing enable gpio wakeup to put the display to sleep as fast as possible
    {
        /* Configure the EPD busy pin as an external wakeup source.
         * cpu_set_gpio_wakeup(pin, polarity, enable)
         */
        cpu_set_gpio_wakeup(EPD_BUSY, 1, 1);
        /* Select external pad (GPIO) as the wakeup source. */
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);
        /* Disable system suspend to keep CPU active until EPD operation completes. */
        bls_pm_setSuspendMask(SUSPEND_DISABLE);
    }
    else
    {
        /* Delegate to SDK power management for normal low-power processing (sleep/timer wakeup). */
        blt_pm_proc();
    }
}
