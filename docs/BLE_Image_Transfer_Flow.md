# 手机发送图片 → 电子纸接收 → 显示 完整流程

整个流程涉及 **4 层**，从底到顶逐层说明。

---

## 第 1 层：硬件连接（SPI 驱动 EPD 屏幕）

```
TLSR8258 MCU ──SPI──→ SSD1675 EPD控制器 ──→ 2.66寸电子纸
```

`epd_spi.h` 定义了最底层的 GPIO/SPI 操作：

```c
EPD_POWER_ON()   → gpio_write(EPD_ENABLE, 0)   // 给屏幕供电
EPD_WriteCmd()   → DC引脚拉低 + SPI发一字节       // 发命令
EPD_WriteData()  → DC引脚拉高 + SPI发一字节       // 发数据
EPD_IS_BUSY()    → 读BUSY引脚                    // 屏幕是否在刷新中
```

`epd_bwy_266.c` 中 `EPD_BWY_266_Display()` 做的事：

```c
EPD_WriteCmd(0x12);          // 1. 软复位
EPD_WriteCmd(0x01);          // 2. 设置驱动输出(296行)
EPD_WriteCmd(0x44/0x45);     // 3. 设置RAM地址范围(19列×296行)
EPD_LoadImage(image, 0x24);  // 4. 往黑白RAM(0x24)写入图像数据
EPD_WriteCmd(0x26); ...      // 5. 清空黄色RAM(0x26)
EPD_WriteCmd(0x32); ...      // 6. 加载LUT波形表(控制刷新效果)
EPD_WriteCmd(0x20);          // 7. 触发刷新！屏幕开始变化
```

---

## 第 2 层：图像缓冲区管理（固件内存）

```c
// epd.h
#define epd_height 152
#define epd_width  296
#define epd_buffer_size ((epd_height/8) * epd_width)  // = 19 * 296 = 5,624 字节

// epd.c
uint8_t epd_buffer[epd_buffer_size];  // 主缓冲区 → 最终发送给EPD的数据
uint8_t epd_temp[epd_buffer_size];    // 临时缓冲区 → 用于解码/旋转
```

**内存布局**：每个字节表示 8 个像素（1-bpp），`1` = 白，`0` = 黑

```
epd_buffer 内存排列 (EPD RAM 格式):
  ┌─列0─┐ ┌─列1─┐     ┌─列295─┐
  │byte0│ │byte1│ ... │byte295│  ← 第0~7行像素
  │byte0│ │byte1│ ... │byte295│  ← 第8~15行像素
  ...
  │byte0│ │byte1│ ... │byte295│  ← 第144~151行像素
  └─────┘ └─────┘     └───────┘
  共 19 行 × 296 列 = 5,624 字节
```

---

## 第 3 层：BLE 协议与回调（固件侧）

### 3.1 ATT 属性表注册

`app_att.c` 中注册了 ATT 属性表：

```c
// EPD_BLE Service UUID: 13187B10-EBA9-A3BA-044E-83D3217D9A38
// EPD_BLE Char UUID:    4B646063-6264-F3A7-8941-E65356EA82FE

{0, ATT_PERMISSIONS_WRITE, 16, sizeof(my_EPD_BLE_Data),
 (u8*)(&my_EPD_BLEUUID),
 (&my_EPD_BLE_Data),
 (att_readwrite_callback_t) &epd_ble_handle_write},  // ← 写回调
```

### 3.2 回调处理

当手机往这个特征值写数据时，BLE栈会自动调用 `epd_ble_handle_write`：

```c
int epd_ble_handle_write(void* p)
{
    // 解析BLE数据包
    rf_packet_att_write_t* req = (rf_packet_att_write_t*)p;
    uint8_t* payload = &req->value;        // 手机发来的原始字节
    unsigned int payload_len = req->l2capLen - 3;

    switch (payload[0])   // 第一个字节是命令码
    {
    case 0x00: // 清空缓冲区
    case 0x01: // 显示到屏幕
    case 0x02: // 设置写入位置
    case 0x03: // 写入图片数据
    case 0x04: // 显示TIFF
    case 0x05: // 显示原始位图(含旋转)
    }
}
```

### 3.3 BLE 数据包格式

| payload[0] 命令码 | payload[1..N] 数据 | 说明 |
|---|---|---|
| `0x00` | `[填充值]` | 清空: `memset(epd_buffer, 填充值)` |
| `0x02` | `[高字节][低字节]` | 设置位置: `byte_pos = 高<<8 \| 低` |
| `0x03` | `[图片数据...]` | 写入: `memcpy` 到 `epd_buffer` |
| `0x01` | (无) | 显示: `EPD_Display()` |
| `0x04` | (无) | 解码并显示TIFF图像 |
| `0x05` | (无) | 旋转位图后显示 |

---

## 第 4 层：手机端发送（Image2ESL macOS App）

### 4.1 BLE 连接

`MyBLE.m` 中通过 CoreBluetooth 框架：

```objc
// 扫描名称以 "ESL" 开头的设备
if (memcmp(deviceName, "ESL", 3) == 0) {
    [self connectToPeripheral: aPeripheral];
}

// 匹配 Service + Characteristic UUID
static NSString *ESLService = @"13187b10-eba9-a3ba-044e-83d3217d9a38";
static NSString *ESLChar    = @"4b646063-6264-f3a7-8941-e65356ea82fe";
```

### 4.2 图像预处理

`ViewController.m` 中 `ditherFile` 方法：

```
原始图片 → 灰度转换 → 缩放到250×122 → Floyd-Steinberg抖动 → 1-bpp位图
```

### 4.3 发送图像

`ViewController.m` 中 `sendImage` 方法：

```objc
// ========= 第1步: 设置写入起始位置为0 =========
ucTemp[0] = 0x02;              // 命令: 设置byte_pos
ucTemp[1] = ucTemp[2] = 0;     // 位置 = 0x0000
[BLEClass writeData:ucTemp withLength:3 withResponse:NO];
sleep(0.01);

// ========= 第2步: 逐列发送旋转后的图像数据 =========
// 原始图像: 250×122 (水平×垂直)
// 需旋转90°匹配EPD内存布局
ucTemp[0] = 0x03;              // 命令: 写入数据
for (x = iWidth-1; x >= 0; x--) {    // 从最后一列到第一列
    // 将该列的像素纵向打包成字节
    // 每列 122 像素 → 122/8 ≈ 16 字节
    [BLEClass writeData:ucTemp withLength:(iPitch+1) withResponse:NO];
    sleep(0.005);  // 每包间隔5ms，防止溢出
}

// ========= 第3步: 触发显示 =========
ucTemp[0] = 0x01;              // 命令: 显示
[BLEClass writeData:ucTemp withLength:1 withResponse:NO];
```

### 4.4 图像旋转的原因

```
手机上的图片(水平):          EPD RAM 内存排列(垂直):
  ←── 250px ──→                ←── 296px ──→
  ┌───────────┐ ↑              ┌────────────┐ ↑
  │  H e l l  │ 122px          │ byte[0]    │ 152/8
  │  o        │ ↓              │ = 8个纵排  │ = 19字节
  └───────────┘                │ 像素       │ ↓
                               └────────────┘

  需要旋转90°才能正确显示！
```

---

## 完整时序图

```
    手机(BLE Central)                      电子纸(BLE Peripheral)                    EPD屏幕
         │                                         │                                     │
    ① BLE扫描 "ESL_XXXXXX"                         │                                     │
         │────── 连接请求 ────────────────────→     │                                     │
         │←───── 连接成功 ────────────────────      │                                     │
         │                              ble_connect_callback()                            │
         │                              ble_set_connection_speed(200)                     │
         │                                         │                                     │
    ② 发现Service+Char                              │                                     │
         │── discover(13187B10...) ──────────→      │                                     │
         │←── found char(4B646063...) ──────        │                                     │
         │                                         │                                     │
    ③ 设置写入位置                                    │                                     │
         │── write [0x02, 0x00, 0x00] ─────→       │                                     │
         │                              epd_ble_handle_write()                            │
         │                              → byte_pos = 0                                   │
         │                                         │                                     │
    ④ 循环发送图片数据 (约250~296次)                   │                                     │
         │── write [0x03, pixel_data...] ──→       │                                     │
         │                              → memcpy(epd_buffer+byte_pos, data)              │
         │                              → byte_pos += len                                │
         │── write [0x03, pixel_data...] ──→       │                                     │
         │                              → memcpy(epd_buffer+byte_pos, data)              │
         │                              → byte_pos += len                                │
         │   ... 重复直到5624字节全部发完 ...          │                                     │
         │                                         │                                     │
    ⑤ 触发显示                                       │                                     │
         │── write [0x01] ─────────────────→       │                                     │
         │                              EPD_Display(epd_buffer)                           │
         │                                         │──── SPI: 写入RAM ──────────────→     │
         │                                         │──── SPI: 加载LUT ──────────────→     │
         │                                         │──── SPI: 触发刷新(0x20) ─────────→   │
         │                                         │                              屏幕开始变化
         │                                         │                              (约2~15秒)
         │                                         │←─── BUSY引脚拉高 ────────────        │
         │                              epd_state_handler()                               │
         │                              → EPD_IS_BUSY() == true                           │
         │                              → epd_set_sleep()                                 │
         │                                         │──── SPI: 深度睡眠(0x10) ─────→       │
         │                                         │                              屏幕保持显示
         │                                         │                              零功耗
```

---

## 传输数据量计算

```
屏幕分辨率:  296 × 152 = 44,992 像素
1-bpp:      44,992 / 8 = 5,624 字节

BLE MTU:    250 字节 (blc_att_setRxMtuSize(250))
有效负荷:    ~244 字节/包 (去掉ATT/L2CAP头)
命令码:      1 字节 (0x03)
每包数据:    ~243 字节图像数据

总包数:      5,624 / 243 ≈ 24 包
每包间隔:    5~10ms
传输时间:    24 × 10ms ≈ 0.24秒 (理想情况)

但是 Image2ESL 代码是逐列发送(每列19字节+1字节命令码=20字节)
总包数:      296 包
每包间隔:    5ms
传输时间:    296 × 5ms ≈ 1.5秒
```

---

## 涉及的源文件清单

| 文件 | 层次 | 作用 |
|---|---|---|
| `epd_spi.h` / `epd_spi.c` | 第1层 硬件 | SPI/GPIO 底层驱动 |
| `epd_bwy_266.c` | 第1层 硬件 | 2.66寸EPD控制器命令序列 |
| `epd.h` / `epd.c` | 第2层 缓冲区 | 图像缓冲区、旋转、TIFF解码 |
| `epd_ble_service.c` | 第3层 BLE协议 | BLE写回调，命令解析 |
| `app_att.c` | 第3层 BLE协议 | GATT属性表定义，注册回调 |
| `ble.c` | 第3层 BLE协议 | BLE协议栈初始化，连接管理 |
| `app.c` | 第3层 固件主循环 | `main_loop` 中调用 `epd_state_handler()` |
| `Image2ESL/MyBLE.m` | 第4层 手机端 | CoreBluetooth 扫描/连接/写数据 |
| `Image2ESL/ViewController.m` | 第4层 手机端 | 图像预处理、抖动、旋转、发送 |

