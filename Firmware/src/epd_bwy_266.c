#include <stdint.h>
#include "tl_common.h"
#include "main.h"
#include "epd.h"
#include "epd_spi.h"
#include "epd_bwy_266.h"
#include "drivers.h"
#include "stack/ble/ble.h"

// 面板参数（152x296）
#define EPD_W           152
#define EPD_H           296
#define EPD_X_BYTES     ((EPD_W + 7) / 8)     // 每行字节数 = 19
#define EPD_PLANE_BYTES (EPD_X_BYTES * EPD_H) // 每个平面的总字节数 = 5624

// ====== LUT（沿用你给的 153B 全刷 LUT；如需我给三色更强对比的 LUT，可再换）======
#define BWY_266_Len 50
static uint8_t LUT_BWY_266_full[153] =
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

#define EPD_BWY_266_TEST_PATTERN 0xA5

// ----------------- 小工具：统一设置整屏窗口与地址计数器 -----------------
static inline void epd_set_window_full(void)
{
    // X: 0 .. 18
    EPD_WriteCmd(0x44);
    EPD_WriteData(0x00);
    EPD_WriteData(EPD_X_BYTES - 1); // 0x12

    // Y: 0 .. 295 (low, high)
    EPD_WriteCmd(0x45);
    EPD_WriteData(0x00);
    EPD_WriteData(0x00);
    EPD_WriteData((EPD_H - 1) & 0xFF);       // 0x27
    EPD_WriteData(((EPD_H - 1) >> 8) & 0xFF);// 0x01

    // 复位地址计数器
    EPD_WriteCmd(0x4E); EPD_WriteData(0x00); // XCount
    EPD_WriteCmd(0x4F); EPD_WriteData(0x00); EPD_WriteData(0x00); // YCount
}

static inline void epd_powerup_basic(void)
{
    // SW Reset
    EPD_WriteCmd(0x12);
    EPD_CheckStatus_inverted(1000); // 等待 BUSY=高（空闲）

    // 模拟/数字块控制（常见安全值）
    EPD_WriteCmd(0x74); EPD_WriteData(0x54);
    EPD_WriteCmd(0x7E); EPD_WriteData(0x3B);

    // ACVCOM（可按需要微调）
    EPD_WriteCmd(0x2B);
    EPD_WriteData(0x04);
    EPD_WriteData(0x63);

    // Booster soft-start
    EPD_WriteCmd(0x0C);
    EPD_WriteData(0x8B);
    EPD_WriteData(0x9C);
    EPD_WriteData(0x96);
    EPD_WriteData(0x0F);

    // Driver output control = 296 行 (0x0127)
    EPD_WriteCmd(0x01);
    EPD_WriteData(0x27); // GD[7:0]
    EPD_WriteData(0x01); // GD[8], SM, TB
    EPD_WriteData(0x01);

    // 数据入口方向：X++，Y++（0x01 / 0x03 取决于排版，这里沿用 0x01）
    EPD_WriteCmd(0x11);
    EPD_WriteData(0x01);

    // 边框波形（可试 0x05/0x07）
    EPD_WriteCmd(0x3C);
    EPD_WriteData(0x05);

    // 温度传感器：内部
    EPD_WriteCmd(0x18);
    EPD_WriteData(0x80);

    // 完整窗口
    epd_set_window_full();
}

// ----------------- 1) 探测：LUT 回读确认控制器在位 -----------------
_attribute_ram_code_ uint8_t EPD_BWY_266_detect(void)
{
    epd_powerup_basic();

    // 写入 153B 测试模式到 LUT（0x32），再从 0x33 回读校验
    EPD_WriteCmd(0x32);
    for (int i = 0; i < 153; ++i) {
        EPD_WriteData(EPD_BWY_266_TEST_PATTERN);
    }

    EPD_WriteCmd(0x33);
    for (int i = 0; i < 153; ++i) {
        if (EPD_SPI_read() != EPD_BWY_266_TEST_PATTERN) {
            return 0; // 回读不匹配，判定失败
        }
    }
    return 1;
}

// ----------------- 2) 读温度（可选；若 SPI 读有效则返回寄存器值） -----------------
_attribute_ram_code_ uint8_t EPD_BWY_266_read_temp(void)
{
    epd_powerup_basic();

    // 触发一次更新时序以稳定内部温感采样（常见做法）
    EPD_WriteCmd(0x22); EPD_WriteData(0xB1);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(500);

    // 读温度寄存器（部分 SSD1680 有效）
    EPD_WriteCmd(0x1B);
    uint8_t t = EPD_SPI_read();
    (void)EPD_SPI_read(); // 有些实现需要丢弃一个 dummy

    // 可选：再次触发轻更新恢复
    EPD_WriteCmd(0x22); EPD_WriteData(0xB1);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(500);

    // 深睡（按需）
    EPD_WriteCmd(0x10); EPD_WriteData(0x01);

    return t; // 若底层不支持 SPI 读，这里可能为随机/固定值
}

// ----------------- 3) 显示（双平面 BW+Y，支持全刷/预留局刷） -----------------
// image: [BW 平面 | Y 平面]，每平面 5624 字节，总 11248 字节
// full_or_partial: 0=全刷；非 0（预留，当前仍按全刷）
_attribute_ram_code_ uint8_t EPD_BWY_266_Display(unsigned char* image, int size, uint8_t full_or_partial)
{
    if (!image || size < (int)EPD_PLANE_BYTES) {
        return 0; // 缓冲不足
    }

    epd_powerup_basic();

    // （可选）加载/刷新 LUT（全刷时）
    if (!full_or_partial) {
        EPD_WriteCmd(0x32);
        for (int i = 0; i < (int)sizeof(LUT_BWY_266_full); ++i) {
            EPD_WriteData(LUT_BWY_266_full[i]);
        }
    }

    // 设置地址计数器到 (0,0)
    EPD_WriteCmd(0x4E); EPD_WriteData(0x00);
    EPD_WriteCmd(0x4F); EPD_WriteData(0x00); EPD_WriteData(0x00);

    // ---- 写入 BW 层 (0x24) ----
    unsigned char* bw = image;
    EPD_LoadImage(bw, EPD_PLANE_BYTES, 0x24);

    // ---- 写入黄层 (0x26) ----
    if (size >= (int)(2 * EPD_PLANE_BYTES)) {
        unsigned char* y = image + EPD_PLANE_BYTES;

        // 重置地址计数器
        EPD_WriteCmd(0x4E); EPD_WriteData(0x00);
        EPD_WriteCmd(0x4F); EPD_WriteData(0x00); EPD_WriteData(0x00);

        EPD_LoadImage(y, EPD_PLANE_BYTES, 0x26);
    }
    else {
        // 若没有黄层数据，则清空黄层（全部 0x00 => 无黄）
        EPD_WriteCmd(0x4E); EPD_WriteData(0x00);
        EPD_WriteCmd(0x4F); EPD_WriteData(0x00); EPD_WriteData(0x00);

        EPD_WriteCmd(0x26);
        for (int i = 0; i < (int)EPD_PLANE_BYTES; ++i) {
            EPD_WriteData(0x00);
        }
    }

    // ---- 触发刷新 ----
    // 全刷常用：0xC7（若残影/闪烁异常，可试 0xF7）
    EPD_WriteCmd(0x22); EPD_WriteData(0xC7);
    EPD_WriteCmd(0x20);
    EPD_CheckStatus_inverted(3000); // 等待 BUSY=高（空闲）

    // 返回一个温度或 0（按需可去掉温度读取流程）
    return 0;
}

// ----------------- 4) 深睡 -----------------
_attribute_ram_code_ void EPD_BWY_266_set_sleep(void)
{
    EPD_WriteCmd(0x10);
    EPD_WriteData(0x01); // Deep Sleep
}