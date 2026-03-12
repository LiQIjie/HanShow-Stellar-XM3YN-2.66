#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwy_266.h"
#include "drivers.h"
#include "stack/ble/ble.h"

// SSD1675 mixed with SSD1680 EPD Controller

// Fast refresh LUT - Similar to e-book readers (quick black flash)
uint8_t LUT_bwy_266_fast[] = {
    // Single strong transition - clears ghosting with one black flash
    0x10, 0x18, 0x18, 0x08, 0x18, 0x18, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x88, 0x88, 0x80, 0x88, 0x88, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    // Fast timing - minimal duration for quick refresh
    0x14, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00
};

// Partial refresh LUT - gentle update without flash
uint8_t LUT_bwy_266_part[] = {
        // 帧控制 - 针对2.66寸屏幕优化
        0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        // 黑色到各种状态的转换 - 增强边缘清晰度
        0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // 白色到各种状态的转换 - 优化白色纯度
        0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // 红色到各种状态的转换 - 保持红色饱和度
        0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // 特殊转换 - 增强过渡效果
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        // 局部刷新波形控制 - 针对高分辨率优化
        0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

        // 帧时间控制 - 平衡速度和质量
        0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
        0x00, 0x00, 0x00
};

#define EPD_BWY_266_test_pattern 0xA5
_attribute_ram_code_ uint8_t EPD_BWY_266_detect(void)
{
    // SW Reset
    EPD_WriteCmd(0x12);
    WaitMs(10);

    EPD_WriteCmd(0x32);
    int i;
    for (i = 0; i < 153; i++)  // FIXME  DETECT MODEL 296
    {
        EPD_WriteData(EPD_BWY_266_test_pattern);
    }
    EPD_WriteCmd(0x33);
    for (i = 0; i < 153; i++)
    {
        if (EPD_SPI_read() != EPD_BWY_266_test_pattern)
            return 0;
    }
    return 1;
}

// Common controller initialization sequence shared by read_temp and Display
static _attribute_ram_code_ uint8_t epd_bwy_266_controller_init(void)
{
    uint8_t temperature = 0;

    // SW Reset
    EPD_WriteCmd(0x12);
    EPD_CheckStatus_inverted(100);

    // Set Analog Block control
    EPD_WriteCmd(0x74);
    EPD_WriteData(0x54);
    // Set Digital Block control
    EPD_WriteCmd(0x7E);
    EPD_WriteData(0x3B);

    // Booster soft start
    EPD_WriteCmd(0x0C);
    EPD_WriteData(0x8B);
    EPD_WriteData(0x9C);
    EPD_WriteData(0x96);
    EPD_WriteData(0x0F);

    // Driver output control: 296 lines
    EPD_WriteCmd(0x01);
    EPD_WriteData(0x28);  // (296-1) low byte
    EPD_WriteData(0x01);  // (296-1) high byte
    EPD_WriteData(0x01);

    // Data entry mode setting
    EPD_WriteCmd(0x11);
    EPD_WriteData(0x01);

    // Set RAM X- Address Start/End: 0 to 18 (19 bytes, 152 pixels)
    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData(0x12);  // 18

    // Set RAM Y- Address Start/End: 0 to 295
    EPD_WriteCmd(0x45);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
    EPD_WriteData(0x27);  // 295 low byte
    EPD_WriteData(0x01);  // 295 high byte

    // Border waveform control
    EPD_WriteCmd(0x3C);
    EPD_WriteData(0x05);

    // Display update control
    EPD_WriteCmd(0x21);
    EPD_WriteData(0x00);
    EPD_WriteData(0x80);

    // Temperature sensor control - use internal sensor
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);

    // Display update control
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xB1);

    // Master Activation
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(100);

    // Temperature sensor read from register
    EPD_WriteCmd(0x1B);
    temperature = EPD_SPI_read();
    EPD_SPI_read();

    WaitMs(5);
    return temperature;
}

// Set RAM address cursor to (0, 0)
static _attribute_ram_code_ void epd_bwy_266_set_ram_cursor(void)
{
    EPD_WriteCmd(0x4E);
    EPD_WriteData(0x00);
    EPD_WriteCmd(0x4F);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
}

_attribute_ram_code_ uint8_t EPD_BWY_266_read_temp(void)
{
    uint8_t temperature = epd_bwy_266_controller_init();

    // Enter deep sleep
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);

    return temperature;
}

_attribute_ram_code_ uint8_t EPD_BWY_266_Display(unsigned char *image, int size, uint8_t full_or_partial)
{
    uint8_t epd_temperature = epd_bwy_266_controller_init();

    // Load black/white image data into RAM (0x24)
    epd_bwy_266_set_ram_cursor();
    EPD_LoadImage(image, size, 0x24);

    // Clear yellow/red layer (0x26 RAM) to ensure pure B/W display
    epd_bwy_266_set_ram_cursor();
    EPD_WriteCmd(0x26);
    int i;
    for (i = 0; i < size; i++)
        EPD_WriteData(0x00);

    // Load LUT based on refresh type
    if (full_or_partial == 1)
    {
        EPD_WriteCmd(0x32);
        for (i = 0; i < sizeof(LUT_bwy_266_fast); i++)
            EPD_WriteData(LUT_bwy_266_fast[i]);
    }
    else if (full_or_partial == 2)
    {
        EPD_WriteCmd(0x32);
        for (i = 0; i < sizeof(LUT_bwy_266_part); i++)
            EPD_WriteData(LUT_bwy_266_part[i]);
    }

    // Trigger display update
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xC7);
    EPD_WriteCmd(0x20);

    return epd_temperature;
}

_attribute_ram_code_ void EPD_BWY_266_set_sleep(void)
{
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);
}