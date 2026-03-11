#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwy_266.h"   // 只保留 2.66 BWY
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
// support for 2.66" display
#include "epd_bwy_266.h"

// ------------------ 固定：仅 2.66" BWY（SSD1680, FPC7510） ------------------
#define EPD_MODEL_BWY266   7
#define EPD_MODEL_NAME     "BWY266"

// 你的工程已有的全局/缓冲
RAM uint8_t epd_model = EPD_MODEL_BWY266;     // 直接固定为 2.66
RAM uint8_t epd_model = 7; // 0 = Undetected, 1 = BW213, 2 = BWR213, 3 = BWR154, 4 = BW213ICE, 5 = BWR350, 6 = BWY350, 7 = BWY266
const char *epd_model_string[] = {"NC", "BW213", "BWR213", "BWR154", "213ICE", "BWR350", "BWY350", "BWY266"};
RAM uint8_t epd_update_state = 0;

const char* BLE_conn_string[] = { "", "B" };
RAM uint8_t epd_temperature_is_read = 0;
RAM uint8_t epd_temperature = 0;

uint8_t epd_buffer[epd_buffer_size];
uint8_t epd_temp[epd_buffer_size]; // for OneBitDisplay to draw into
OBDISP obd;                        // virtual display structure
TIFFIMAGE tiff;

// 可选：保留该接口（与上层兼容）。仅允许设置为 2.66。
void set_EPD_model(uint8_t model_nr)
{
    (void)model_nr;
    epd_model = EPD_MODEL_BWY266;
}

// 仅检测 2.66 BWY（若探测失败也强制按照 2.66 处理）
_attribute_ram_code_ void EPD_detect_model(void)
{
    EPD_init();
    EPD_POWER_ON();

    WaitMs(10);
    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    if (EPD_BWY_266_detect())
        epd_model = EPD_MODEL_BWY266;
    // Here we neeed to detect it
    if (EPD_BWR_213_detect())
    {
        epd_model = 2;
    }
    else if (EPD_BWR_154_detect())// Right now this will never trigger, the 154 is same to 213BWR right now.
    {
        epd_model = 3;
    }
    else if (EPD_BW_213_ice_detect())
    {
        epd_model = 4;
    }
    else if (EPD_BWY_266_detect())
    {
        epd_model = 7;
    }
    else
        epd_model = EPD_MODEL_BWY266; // 强制固定（只保留 2.66）

    EPD_POWER_OFF();
}

_attribute_ram_code_ uint8_t EPD_read_temp(void)
{
    if (epd_temperature_is_read) return epd_temperature;

    if (!epd_model) EPD_detect_model();

    EPD_init();
    EPD_POWER_ON();
    WaitMs(5);

    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    epd_temperature = EPD_BWY_266_read_temp();
    if (epd_model == 1)
        epd_temperature = EPD_BW_213_read_temp();
    else if (epd_model == 2)
        epd_temperature = EPD_BWR_213_read_temp();
    else if (epd_model == 3)
        epd_temperature = EPD_BWR_154_read_temp();
    else if (epd_model == 4)
        epd_temperature = EPD_BW_213_ice_read_temp();
    else if (epd_model == 5)
        epd_temperature = EPD_BWR_350_read_temp();
    else if (epd_model == 6)
        epd_temperature = EPD_BWY_350_read_temp();
    else if (epd_model == 7)
        epd_temperature = EPD_BWY_266_read_temp();

    EPD_POWER_OFF();
    epd_temperature_is_read = 1;
    return epd_temperature;
}

_attribute_ram_code_ void EPD_Display(unsigned char* image, int size, uint8_t full_or_partial)
{
    if (!epd_model) EPD_detect_model();

    EPD_init();
    EPD_POWER_ON();
    WaitMs(5);

    gpio_write(EPD_RESET, 0);
    WaitMs(10);
    gpio_write(EPD_RESET, 1);
    WaitMs(10);

    // 仅 2.66 BWY（双平面：BW + Yellow）
    epd_temperature = EPD_BWY_266_Display(image, size, full_or_partial);
    if (epd_model == 1)
        epd_temperature = EPD_BW_213_Display(image, size, full_or_partial);
    else if (epd_model == 2)
        epd_temperature = EPD_BWR_213_Display(image, size, full_or_partial);
    else if (epd_model == 3)
        epd_temperature = EPD_BWR_154_Display(image, size, full_or_partial);
    else if (epd_model == 4)
        epd_temperature = EPD_BW_213_ice_Display(image, size, full_or_partial);
    else if (epd_model == 5)
        epd_temperature = EPD_BWR_350_Display(image, size, full_or_partial);
    else if (epd_model == 6)
        epd_temperature = EPD_BWY_350_Display(image, size, full_or_partial);
    else if (epd_model == 7)
        epd_temperature = EPD_BWY_266_Display(image, size, full_or_partial);

    epd_temperature_is_read = 1;
    epd_update_state = 1;
}

_attribute_ram_code_ void epd_set_sleep(void)
{
    if (!epd_model) EPD_detect_model();

    EPD_BWY_266_set_sleep();
    if (epd_model == 1)
        EPD_BW_213_set_sleep();
    else if (epd_model == 2)
        EPD_BWR_213_set_sleep();
    else if (epd_model == 3)
        EPD_BWR_154_set_sleep();
    else if (epd_model == 4)
        EPD_BW_213_ice_set_sleep();
    else if (epd_model == 5)
        EPD_BWR_350_set_sleep();
    else if (epd_model == 6)
        EPD_BWY_350_set_sleep();
    else if (epd_model == 7)
        EPD_BWY_266_set_sleep();

    EPD_POWER_OFF();
    epd_update_state = 0;
}

// 刷新完成后的状态处理（保持你原工程在“非 BW213 分支”时的逻辑）
_attribute_ram_code_ uint8_t epd_state_handler(void)
{
    switch (epd_update_state)
    {
    case 0:
        break;
    case 1:
        // 在你原工程中：对于非 BW213 的型号，使用 if (EPD_IS_BUSY()) 完成休眠
        // 这里保留相同行为，避免引入刷新时序行为变化
        if (EPD_IS_BUSY())
            epd_set_sleep();
        break;
    }
    return epd_update_state;
}

// 旋转/搬运显示缓冲（保持不变）
_attribute_ram_code_ void FixBuffer(uint8_t* pSrc, uint8_t* pDst, uint16_t width, uint16_t height)
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

// TIFF 解码回调（保持不变）
_attribute_ram_code_ void TIFFDraw(TIFFDRAW* pDraw)
{
    uint8_t uc = 0, ucSrcMask, ucDstMask, * s, * d;
    int x, y;

    s = pDraw->pPixels;
    y = pDraw->y;
    d = &epd_buffer[(249 * 16) + (y / 8)]; // rotated 90 deg clockwise
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
            d[-(x * 16)] &= ~ucDstMask;
        }
        ucSrcMask >>= 1;
    }
}

_attribute_ram_code_ void epd_display_tiff(uint8_t* pData, int iSize)
{
    memset(epd_buffer, 0xff, epd_buffer_size);
    TIFF_openRAW(&tiff, 250, 122, BITDIR_MSB_FIRST, pData, iSize, TIFFDraw);
    TIFF_setDrawParameters(&tiff, 65536, TIFF_PIXEL_1BPP, 0, 0, 250, 122, NULL);
    TIFF_decode(&tiff);
    TIFF_close(&tiff);
    EPD_Display(epd_buffer, epd_buffer_size, 1);
}

extern uint8_t mac_public[6];

_attribute_ram_code_ void epd_display(uint32_t time_is, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial)
{
    if (epd_update_state) return;

    if (!epd_model) EPD_detect_model();

    // 仅 2.66"：296 x 152（注意：显示库用横×纵）
    uint16_t resolution_w = 296;
    uint16_t resolution_h = 152;
    if (!epd_model)
    {
        EPD_detect_model();
    }
    uint16_t resolution_w = 250;
    uint16_t resolution_h = 128; // 122 real pixel, but needed to have a full byte
    if (epd_model == 1)
    {
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 2)
    {
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 3)
    {
        resolution_w = 200;
        resolution_h = 200;
    }
    else if (epd_model == 4)
    {
        resolution_w = 212;
        resolution_h = 104;
    }
    else if (epd_model == 5)
    {// Just as placeholder right now, needs a complete different driving because of RAM limits
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 6)
    {// Just as placeholder right now, needs a complete different driving because of RAM limits
        resolution_w = 250;
        resolution_h = 128; // 122 real pixel, but needed to have a full byte
    }
    else if (epd_model == 7)
    {
        // 2.66" display (rotated/oriented as in driver)
        resolution_w = 152;
        resolution_h = 296;
    }

    obdCreateVirtualDisplay(&obd, resolution_w, resolution_h, epd_temp);
    obdFill(&obd, 0, 0); // white

    char buff[100];
    // 直接使用固定型号名，避免 epd_model_string 下标风险
    sprintf(buff, "ESL_%02X%02X%02X %s", mac_public[2], mac_public[1], mac_public[0], EPD_MODEL_NAME);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 1, 17, (char*)buff, 1);

    sprintf(buff, "%s", BLE_conn_string[ble_get_connected()]);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 232, 20, (char*)buff, 1);

    sprintf(buff, "%02d:%02d", ((time_is / 60) / 60) % 24, (time_is / 60) % 60);
    obdWriteStringCustom(&obd, (GFXfont*)&DSEG14_Classic_Mini_Regular_40, 50, 65, (char*)buff, 1);

    sprintf(buff, "%d'C", EPD_read_temp());
    obdWriteStringCustom(&obd, (GFXfont*)&Special_Elite_Regular_30, 10, 95, (char*)buff, 1);

    sprintf(buff, "Battery %dmV", battery_mv);
    obdWriteStringCustom(&obd, (GFXfont*)&Dialog_plain_16, 10, 120, (char*)buff, 1);

    FixBuffer(epd_temp, epd_buffer, resolution_w, resolution_h);
    EPD_Display(epd_buffer, resolution_w * resolution_h / 8, full_or_partial);
}

_attribute_ram_code_ void epd_display_char(uint8_t data)
{
    // 保持原有行为
    for (int i = 0; i < epd_buffer_size; i++)
        epd_buffer[i] = data;
    EPD_Display(epd_buffer, epd_buffer_size, 1);
}

_attribute_ram_code_ void epd_clear(void)
{
    memset(epd_buffer, 0x00, epd_buffer_size);
}