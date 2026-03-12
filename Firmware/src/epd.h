#pragma once

#define epd_height 152
#define epd_width 296
#define epd_buffer_size ((epd_height / 8) * epd_width)  // 19 * 296 = 5624 bytes

extern uint8_t epd_buffer[epd_buffer_size];
extern uint8_t epd_temp[epd_buffer_size];

uint8_t EPD_read_temp(void);
void EPD_Display(unsigned char *image, int size, uint8_t full_or_partial);
void epd_display_tiff(uint8_t *pData, int iSize);
void epd_display(uint32_t time_is, uint16_t battery_mv, int16_t temperature, uint8_t full_or_partial);
void epd_display_default_meeting(uint8_t full_or_partial);
void epd_set_sleep(void);
uint8_t epd_state_handler(void);
void epd_display_char(uint8_t data);
void epd_clear(void);
void set_EPD_model(uint8_t model_nr);
void FixBuffer(uint8_t *pSrc, uint8_t *pDst, uint16_t width, uint16_t height);