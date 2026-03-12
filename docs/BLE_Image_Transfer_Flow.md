# 手机?送?片 → ?子??接收 → ?示 完整流程

整个流程?及 **4 ?**，从底到?逐??明。

---

## 第 1 ?：硬件?接（SPI ?? EPD 屏幕）

```
TLSR8258 MCU ──SPI──? SSD1675 EPD控制器 ──? 2.66寸?子?
```

`epd_spi.h` 定?了最底?的 GPIO/SPI 操作：

```c
EPD_POWER_ON()   → gpio_write(EPD_ENABLE, 0)   // ?屏幕供?
EPD_WriteCmd()   → DC引脚拉低 + SPI?一字?       // ?命令
EPD_WriteData()  → DC引脚拉高 + SPI?一字?       // ?数据
EPD_IS_BUSY()    → ?BUSY引脚                    // 屏幕是否在刷新中
```

`epd_bwy_266.c` 中 `EPD_BWY_266_Display()` 做的事：

```c
EPD_WriteCmd(0x12);          // 1. ??位
EPD_WriteCmd(0x01);          // 2. ?置???出(296行)
EPD_WriteCmd(0x44/0x45);     // 3. ?置RAM地址范?(19列×296行)
EPD_LoadImage(image, 0x24);  // 4. 往黑白RAM(0x24)写入?像数据
EPD_WriteCmd(0x26); ...      // 5. 清空黄色RAM(0x26)
EPD_WriteCmd(0x32); ...      // 6. 加?LUT波形表(控制刷新效果)
EPD_WriteCmd(0x20);          // 7. 触?刷新！屏幕?始?化
```

---

## 第 2 ?：?像?冲区管理（固件内存）

```c
// epd.h
#define epd_height 152
#define epd_width  296
#define epd_buffer_size ((epd_height/8) * epd_width)  // = 19 * 296 = 5,624 字?

// epd.c
uint8_t epd_buffer[epd_buffer_size];  // 主?冲区 ? 最?送?EPD的数据
uint8_t epd_temp[epd_buffer_size];    // ???冲区 ? 用于??/旋?
```

**内存布局**：?个字?表示 8 个像素（1-bpp），`1` = 白，`0` = 黑

```
epd_buffer 内存排列 (EPD RAM 格式):
  ┌─列0─┐ ┌─列1─┐     ┌─列295─┐
  │byte0│ │byte1│ ... │byte295│  ← 第0~7行像素
  │byte0│ │byte1│ ... │byte295│  ← 第8~15行像素
  ...
  │byte0│ │byte1│ ... │byte295│  ← 第144~151行像素
  └─────┘ └─────┘     └───────┘
  共 19 行 × 296 列 = 5,624 字?
```

---

## 第 3 ?：BLE ??与回?（固件?）

### 3.1 ATT 属性表注册

`app_att.c` 中注册了 ATT 属性表：

```c
// EPD_BLE Service UUID: 13187B10-EBA9-A3BA-044E-83D3217D9A38
// EPD_BLE Char UUID:    4B646063-6264-F3A7-8941-E65356EA82FE

{0, ATT_PERMISSIONS_WRITE, 16, sizeof(my_EPD_BLE_Data),
 (u8*)(&my_EPD_BLEUUID),
 (&my_EPD_BLE_Data),
 (att_readwrite_callback_t) &epd_ble_handle_write},  // ← 写回?
```

### 3.2 回??理

当手机往?个特征?写数据?，BLE???自??用 `epd_ble_handle_write`：

```c
int epd_ble_handle_write(void* p)
{
    // 解析BLE数据包
    rf_packet_att_write_t* req = (rf_packet_att_write_t*)p;
    uint8_t* payload = &req->value;        // 手机?来的原始字?
    unsigned int payload_len = req->l2capLen - 3;

    switch (payload[0])   // 第一个字?是命令?
    {
    case 0x00: // 清空?冲区
    case 0x01: // ?示到屏幕
    case 0x02: // ?置写入位置
    case 0x03: // 写入?片数据
    case 0x04: // ?示TIFF
    case 0x05: // ?示原始位?(?旋?)
    }
}
```

### 3.3 BLE 数据包格式

| payload[0] 命令? | payload[1..N] 数据 | ?明 |
|---|---|---|
| `0x00` | `[填充?]` | 清空: `memset(epd_buffer, 填充?)` |
| `0x02` | `[高字?][低字?]` | ?置位置: `byte_pos = 高<<8 \| 低` |
| `0x03` | `[?片数据...]` | 写入: `memcpy` 到 `epd_buffer` |
| `0x01` | (无) | ?示: `EPD_Display()` |
| `0x04` | (无) | 解?并?示TIFF?像 |
| `0x05` | (无) | 旋?位?后?示 |

---

## 第 4 ?：手机端?送（Image2ESL macOS App）

### 4.1 BLE ?接

`MyBLE.m` 中通? CoreBluetooth 框架：

```objc
// ?描名称以 "ESL" ??的??
if (memcmp(deviceName, "ESL", 3) == 0) {
    [self connectToPeripheral: aPeripheral];
}

// 匹配 Service + Characteristic UUID
static NSString *ESLService = @"13187b10-eba9-a3ba-044e-83d3217d9a38";
static NSString *ESLChar    = @"4b646063-6264-f3a7-8941-e65356ea82fe";
```

### 4.2 ?像??理

`ViewController.m` 中 `ditherFile` 方法：

```
原始?片 → 灰度?? → ?放到250×122 → Floyd-Steinberg抖? → 1-bpp位?
```

### 4.3 ?送?像

`ViewController.m` 中 `sendImage` 方法：

```objc
// ========= 第1?: ?置写入起始位置?0 =========
ucTemp[0] = 0x02;              // 命令: ?置byte_pos
ucTemp[1] = ucTemp[2] = 0;     // 位置 = 0x0000
[BLEClass writeData:ucTemp withLength:3 withResponse:NO];
sleep(0.01);

// ========= 第2?: 逐列?送旋?后的?像数据 =========
// 原始?像: 250×122 (水平×垂直)
// 需旋?90°?配EPD内存布局
ucTemp[0] = 0x03;              // 命令: 写入数据
for (x = iWidth-1; x >= 0; x--) {    // 从最后一列到第一列
    // 将?列的像素?向打包成字?
    // ?列 122 像素 → 122/8 ? 16 字?
    [BLEClass writeData:ucTemp withLength:(iPitch+1) withResponse:NO];
    sleep(0.005);  // ?包?隔5ms，防止溢出
}

// ========= 第3?: 触??示 =========
ucTemp[0] = 0x01;              // 命令: ?示
[BLEClass writeData:ucTemp withLength:1 withResponse:NO];
```

### 4.4 ?像旋?的原因

```
手机上的?片(水平):          EPD RAM 内存排列(?直):
  ←── 250px ──→                ←── 296px ──→
  ┌───────────┐ ↑              ┌────────────┐ ↑
  │  H e l l  │ 122px          │ byte[0]    │ 152/8
  │  o        │ ↓              │ = 8个?排  │ = 19字?
  └───────────┘                │ 像素       │ ↓
                               └────────────┘

  需要旋?90°才能正??示！
```

---

## 完整?序?

```
    手机(BLE Central)                      ?子??(BLE Peripheral)                    EPD屏幕
         │                                         │                                     │
    ① BLE?描 "ESL_XXXXXX"                         │                                     │
         │────── ?接?求 ────────────────────?     │                                     │
         │?───── ?接成功 ────────────────────      │                                     │
         │                              ble_connect_callback()                            │
         │                              ble_set_connection_speed(200)                     │
         │                                         │                                     │
    ② ??Service+Char                              │                                     │
         │── discover(13187B10...) ──────────?      │                                     │
         │?── found char(4B646063...) ──────        │                                     │
         │                                         │                                     │
    ③ ?置写入位置                                    │                                     │
         │── write [0x02, 0x00, 0x00] ─────?       │                                     │
         │                              epd_ble_handle_write()                            │
         │                              → byte_pos = 0                                   │
         │                                         │                                     │
    ④ 循??送?片数据 (?250~296次)                   │                                     │
         │── write [0x03, pixel_data...] ──?       │                                     │
         │                              → memcpy(epd_buffer+byte_pos, data)              │
         │                              → byte_pos += len                                │
         │── write [0x03, pixel_data...] ──?       │                                     │
         │                              → memcpy(epd_buffer+byte_pos, data)              │
         │                              → byte_pos += len                                │
         │   ... 重?直到5624字?全部?完 ...          │                                     │
         │                                         │                                     │
    ⑤ 触??示                                       │                                     │
         │── write [0x01] ─────────────────?       │                                     │
         │                              EPD_Display(epd_buffer)                           │
         │                                         │──── SPI: 写入RAM ──────────────?     │
         │                                         │──── SPI: 加?LUT ──────────────?     │
         │                                         │──── SPI: 触?刷新(0x20) ─────────?   │
         │                                         │                              屏幕?始?化
         │                                         │                              (?2~15秒)
         │                                         │?─── BUSY引脚?高 ────────────        │
         │                              epd_state_handler()                               │
         │                              → EPD_IS_BUSY() == true                           │
         │                              → epd_set_sleep()                                 │
         │                                         │──── SPI: 深度睡眠(0x10) ─────?       │
         │                                         │                              屏幕保持?示
         │                                         │                              零功耗
```

---

## ??数据量?算

```
屏幕分辨率:  296 × 152 = 44,992 像素
1-bpp:      44,992 / 8 = 5,624 字?

BLE MTU:    250 字? (blc_att_setRxMtuSize(250))
有效?荷:    ~244 字?/包 (去掉ATT/L2CAP?)
命令?:      1 字? (0x03)
?包数据:    ~243 字??像数据

?包数:      5,624 / 243 ? 24 包
?包?隔:    5~10ms
????:    24 × 10ms ? 0.24秒 (理想情况)

?? Image2ESL 代?是逐列?送(?列19字?+1字?命令?=20字?)
?包数:      296 包
?包?隔:    5ms
????:    296 × 5ms ? 1.5秒
```

---

## ?及的源文件清?

| 文件 | ?? | 作用 |
|---|---|---|
| `epd_spi.h` / `epd_spi.c` | 第1? 硬件 | SPI/GPIO 底??? |
| `epd_bwy_266.c` | 第1? 硬件 | 2.66寸EPD控制器命令序列 |
| `epd.h` / `epd.c` | 第2? ?冲区 | ?像?冲区、旋?、TIFF解? |
| `epd_ble_service.c` | 第3? BLE?? | BLE写回?，命令解析 |
| `app_att.c` | 第3? BLE?? | GATT属性表定?，注册回? |
| `ble.c` | 第3? BLE?? | BLE???初始化，?接管理 |
| `app.c` | 第3? 固件主循? | `main_loop` 中?用 `epd_state_handler()` |
| `Image2ESL/MyBLE.m` | 第4? 手机端 | CoreBluetooth ?描/?接/写数据 |
| `Image2ESL/ViewController.m` | 第4? 手机端 | ?像??理、抖?、旋?、?送 |
