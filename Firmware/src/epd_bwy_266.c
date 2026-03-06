#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwy_266.h"
#include "drivers.h"
#include "stack/ble/ble.h"

// SSD1675/SSD1680 compatible EPD Controller - BWY 2.66" implementation
// Target panels: DEP50286YN9800F1HP-H0 / 0201DP-0351-01-10557-W (FPC7510 = DKE 2.66 BWY)

#define BWY_266_Len 50
uint8_t LUT_BWY_266_full[153] =
{
0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

BWY_266_Len, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
0x00, 0x00, 0x00,
};

#define EPD_BWY_266_test_pattern 0xA5
_attribute_ram_code_ uint8_t EPD_BWY_266_detect(void)
{
    /*
     * SSD1680 detect for 2.66" BWY panel (152x296).
     * Use LUT write/readback to verify the controller has a 153-byte LUT.
     */

    // SW Reset
    EPD_WriteCmd(0x12);
    WaitMs(10);

    // Write a test pattern into the LUT register (0x32)
    EPD_WriteCmd(0x32);
    int i;
    for (i = 0; i < 153; i++)
    {
        EPD_WriteData(EPD_BWY_266_test_pattern);
    }
    // Read back via LUT readback register (0x33)
    EPD_WriteCmd(0x33);
    for (i = 0; i < 153; i++)
    {
        if (EPD_SPI_read() != EPD_BWY_266_test_pattern)
            return 0;
    }
    return 1;
}

_attribute_ram_code_ uint8_t EPD_BWY_266_read_temp(void)
{
    uint8_t epd_temperature = 0 ;
    
    // SW Reset
    EPD_WriteCmd(0x12);

    EPD_CheckStatus_inverted(100);

    // Set Analog Block control
    EPD_WriteCmd(0x74);
    EPD_WriteData(0x54);
    // Set Digital Block control
    EPD_WriteCmd(0x7E);
    EPD_WriteData(0x3B);
    
    // ACVCOM Setting (SSD1680-style)
    EPD_WriteCmd(0x2B);
    EPD_WriteData(0x04);
    EPD_WriteData(0x63);

    // Booster soft start
    EPD_WriteCmd(0x0C);
    EPD_WriteData(0x8B);
    EPD_WriteData(0x9C);
    EPD_WriteData(0x96);
    EPD_WriteData(0x0F);

    // Driver output control: 296 lines (0x0127)
    EPD_WriteCmd(0x01);
    EPD_WriteData(0x27);    // GD[7:0]
    EPD_WriteData(0x01);    // GD[8], SM, TB
    EPD_WriteData(0x01);

    // Data entry mode setting
    EPD_WriteCmd(0x11);
    EPD_WriteData(0x01);

    // Temperature sensor control
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);

    // Set RAM X- Address Start/End (0..18 => 19 bytes => 152 pixels)
    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData(0x12);

    // Set RAM Y- Address Start/End (0..295)
    EPD_WriteCmd(0x45);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
    EPD_WriteData(0x27);
    EPD_WriteData(0x01);

    // Border waveform control
    EPD_WriteCmd(0x3C);
    EPD_WriteData(0x01);

    // Display update control (sequence to start temperature read)
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xB1);

    // Master Activation
    EPD_WriteCmd(0x20);
    
    EPD_CheckStatus_inverted(100);

    // Temperature sensor read from register
    EPD_WriteCmd(0x1B);
    epd_temperature = EPD_SPI_read();    
    EPD_SPI_read();

    WaitMs(5);

    // Display update control (restore/update)
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xB1);
    
    // Master Activation
    EPD_WriteCmd(0x20);

    EPD_CheckStatus_inverted(100);
    
    // Display update control
    EPD_WriteCmd(0x21);
    EPD_WriteData(0x03);
    
    // deep sleep
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);

    return epd_temperature;
}

_attribute_ram_code_ uint8_t EPD_BWY_266_Display(unsigned char *image, int size, uint8_t full_or_partial)
{    
    uint8_t epd_temperature = 0 ;
    
    // SW Reset
    EPD_WriteCmd(0x12);

    EPD_CheckStatus_inverted(100);

    // Set Analog Block control
    EPD_WriteCmd(0x74);
    EPD_WriteData(0x54);
    // Set Digital Block control
    EPD_WriteCmd(0x7E);
    EPD_WriteData(0x3B);
    
    // ACVCOM Setting
    EPD_WriteCmd(0x2B);
    EPD_WriteData(0x04);
    EPD_WriteData(0x63);

    // Booster soft start
    EPD_WriteCmd(0x0C);
    EPD_WriteData(0x8B);
    EPD_WriteData(0x9C);
    EPD_WriteData(0x96);
    EPD_WriteData(0x0F);

    // Driver output control: 296 lines (0x0127)
    EPD_WriteCmd(0x01);
    EPD_WriteData(0x27);    // GD[7:0]
    EPD_WriteData(0x01);    // GD[8], SM, TB
    EPD_WriteData(0x01);

    // Data entry mode setting
    EPD_WriteCmd(0x11);
    EPD_WriteData(0x01);

    // Temperature sensor control
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);

    // Set RAM X- Address Start/End (0..18 => 19 bytes => 152 pixels)
    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData(0x12);

    // Set RAM Y- Address Start/End (0..295)
    EPD_WriteCmd(0x45);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
    EPD_WriteData(0x27);
    EPD_WriteData(0x01);

    // Border waveform control
    EPD_WriteCmd(0x3C);
    EPD_WriteData(0x01);

    // Display update control
    EPD_WriteCmd(0x21);
    EPD_WriteData(0x00);
    EPD_WriteData(0x80);

    // Temperature sensor control
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
    epd_temperature = EPD_SPI_read();
    EPD_SPI_read();

    WaitMs(5);

    // Set RAM X address counter
    EPD_WriteCmd(0x4E);
    EPD_WriteData(0x00);

    // Set RAM Y address counter
    EPD_WriteCmd(0x4F);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);

    // Load BW image data into RAM (0x24)
    EPD_LoadImage(image, size, 0x24);

    // Set RAM X address counter for yellow plane
    EPD_WriteCmd(0x4E);
    EPD_WriteData(0x00);

    // Set RAM Y address counter for yellow plane
    EPD_WriteCmd(0x4F);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);

    // Clear Yellow/Red RAM (0x26) - all 0x00 means no yellow pixels
    EPD_WriteCmd(0x26);
    int i;
    for (i = 0; i < size; i++)
    {
        EPD_WriteData(0x00);
    }

    if (!full_or_partial)
    {
        EPD_WriteCmd(0x32);
        for (i = 0; i < sizeof(LUT_BWY_266_full); i++)
        {
            EPD_WriteData(LUT_BWY_266_full[i]);
        }
    }

    // Display update control
    EPD_WriteCmd(0x22);
    EPD_WriteData(0xC7);

    // Master Activation
    EPD_WriteCmd(0x20);

    return epd_temperature;
}

_attribute_ram_code_ void EPD_BWY_266_set_sleep(void)
{
    // deep sleep
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01);

}
