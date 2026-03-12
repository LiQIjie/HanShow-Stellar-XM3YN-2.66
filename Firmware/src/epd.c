#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "battery.h"

#include "OneBitDisplay.h"
#include "TIFF_G4.h"
extern const uint8_t ucMirror[];
#include "Roboto_Black_80.h"
#include "font_60.h"
#include "font16.h"
#include "font30.h"

#include "epd_bwy_266.h"

// Global state / buffers
RAM uint8_t epd_update_state = 0;
RAM uint8_t epd_temperature_is_read = 0;
RAM uint8_t epd_temperature = 0;

uint8_t epd_buffer[epd_buffer_size];
uint8_t epd_temp[epd_buffer_size];
OBDISP obd;
TIFFIMAGE tiff;

// Common EPD power-on and hardware reset sequence
static _attribute_ram_code_ void epd_hw_power_on_reset(void)
{
    EPD_init();
    EPD_POWER_ON();
    WaitMs(5);

    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);
}

_attribute_ram_code_ uint8_t EPD_read_temp(void)
{
    if (epd_temperature_is_read) return epd_temperature;

    epd_hw_power_on_reset();
    epd_temperature = EPD_BWY_266_read_temp();
    EPD_POWER_OFF();

    epd_temperature_is_read = 1;
    return epd_temperature;
}

_attribute_ram_code_ void EPD_Display(unsigned char *image, int size, uint8_t full_or_partial)
{
    epd_hw_power_on_reset();
    epd_temperature = EPD_BWY_266_Display(image, size, full_or_partial);
    epd_temperature_is_read = 1;
    epd_update_state = 1;
}

_attribute_ram_code_ void epd_set_sleep(void)
{
    EPD_BWY_266_set_sleep();
    EPD_POWER_OFF();
    epd_update_state = 0;
}

_attribute_ram_code_ uint8_t epd_state_handler(void)
{
    if (epd_update_state == 1 && EPD_IS_BUSY())
        epd_set_sleep();
    return epd_update_state;
}

_attribute_ram_code_ void FixBuffer(uint8_t *pSrc, uint8_t *pDst, uint16_t width, uint16_t height)
{
    int x, y;
    uint8_t* s, * d;
    for (y = 0; y < (height / 8); y++)
    { // byte rows
        d = &pDst[y];
        s = &pSrc[y * width];
        for (x = 0; x < width; x++)
        {
            d[x * (height / 8)] = ~ucMirror[s[width - 1 - x]]; // invert and flip
        }
    }
}

#define EPD_BYTES_PER_COL  (epd_height / 8)  // 19

_attribute_ram_code_ void TIFFDraw(TIFFDRAW *pDraw)
{
    uint8_t uc = 0, ucSrcMask, ucDstMask, *s, *d;
    int x, y;

    s = pDraw->pPixels;
    y = pDraw->y;
    d = &epd_buffer[((epd_width - 1) * EPD_BYTES_PER_COL) + (y / 8)];
    ucDstMask = 0x80 >> (y & 7);
    ucSrcMask = 0;
    for (x = 0; x < pDraw->iWidth; x++)
    {
        if (ucSrcMask == 0)
        {
            ucSrcMask = 0x80;
            uc = *s++;
        }
        if (!(uc & ucSrcMask))
        {
            d[-(x * EPD_BYTES_PER_COL)] &= ~ucDstMask;
        }
        ucSrcMask >>= 1;
    }
}

_attribute_ram_code_ void epd_display_tiff(uint8_t *pData, int iSize)
{
    memset(epd_buffer, 0xff, epd_buffer_size);
    TIFF_openRAW(&tiff, epd_width, epd_height, BITDIR_MSB_FIRST, pData, iSize, TIFFDraw);
    TIFF_setDrawParameters(&tiff, 65536, TIFF_PIXEL_1BPP, 0, 0, epd_width, epd_height, NULL);
    TIFF_decode(&tiff);
    TIFF_close(&tiff);
    EPD_Display(epd_buffer, epd_buffer_size, 1);
}

extern uint8_t mac_public[6];

_attribute_ram_code_ void epd_display(uint32_t time_is, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial)
{
    if (epd_update_state) return;

    memset(epd_temp, 0x00, epd_buffer_size);
    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
    obdFill(&obd, 0, 0);

    char buff[64];
    sprintf(buff, "BLE_%02X%02X%02X", mac_public[2], mac_public[1], mac_public[0]);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 85, 17, buff, 1);

    sprintf(buff, "%02d:%02d", ((time_is / 60) / 60) % 24, (time_is / 60) % 60);
    obdWriteStringCustom(&obd, (GFXfont *)&DSEG14_Classic_Mini_Regular_40, 75, 65, buff, 1);

    uint8_t temp = EPD_read_temp();
    sprintf(buff, "-----%d'C-----", temp);
    obdWriteStringCustom(&obd, (GFXfont *)&Special_Elite_Regular_30, 10, 95, buff, 1);

    sprintf(buff, "Battery %dmV  %d%%", battery_mv, get_battery_level(battery_mv));
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 40, 120, buff, 1);

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);
    EPD_Display(epd_buffer, epd_buffer_size, full_or_partial);
}

_attribute_ram_code_ void epd_display_default_meeting(uint8_t full_or_partial)
{
    if (epd_update_state) return;

    memset(epd_temp, 0x00, epd_buffer_size);
    obdCreateVirtualDisplay(&obd, epd_width, epd_height, epd_temp);
    obdFill(&obd, 0, 0);

    obdRectangle(&obd, 0, 0, epd_width - 1, epd_height - 1, 1, 0);
    obdDrawLine(&obd, 12, 95, epd_width - 12, 95, 1, 0);

    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 12, 31, (char *)"Next-Generation Project", 1);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 12, 58, (char *)"Web Conference", 1);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 12, 86, (char *)"Time : 15:00 - 17:30", 1);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 173, 86, (char *)"User : Ootsubo", 1);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 12, 118, (char *)"Next : Standardization of Winding", 1);
    obdWriteStringCustom(&obd, (GFXfont *)&Dialog_plain_16, 12, 143, (char *)"Inspection Equipment", 1);

    FixBuffer(epd_temp, epd_buffer, epd_width, epd_height);
    EPD_Display(epd_buffer, epd_buffer_size, full_or_partial);
}

_attribute_ram_code_ void epd_display_char(uint8_t data)
{
    memset(epd_buffer, data, epd_buffer_size);
    EPD_Display(epd_buffer, epd_buffer_size, 1);
}

_attribute_ram_code_ void epd_clear(void)
{
    memset(epd_buffer, 0xFF, epd_buffer_size);
}

void set_EPD_model(uint8_t model_nr)
{
    (void)model_nr; // only BWY266 is supported in this build
}
