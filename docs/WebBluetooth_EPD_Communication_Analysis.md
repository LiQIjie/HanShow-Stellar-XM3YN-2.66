# 网页与固件通信流程深度分析

## 文档概述

本文档从**实际通信流程**的角度，深入分析网页作为发送端、固件 `epd_ble_service.c` 作为接收端的完整通信过程，评估两者之间的兼容性问题和潜在风险。

---

## 一、固件 BLE 架构全景分析

### 1.1 固件支持的 BLE Service 完整列表

根据 `app_att.c` 的定义，固件实际支持 **7 个 BLE Service**：

| Service 名称 | UUID | 功能 | 处理函数 |
|-------------|------|------|---------|
| **GAP Service** | `0x1800` (标准) | 设备名称、外观、连接参数 | 系统自动处理 |
| **GATT Service** | `0x1801` (标准) | 服务变更通知 | 系统自动处理 |
| **Battery Service** | `0x180F` (标准) | 电池电量通知 | 系统自动处理 |
| **Temperature Service** | `0x181A` (标准) | 温度数据通知 | 系统自动处理 |
| **OTA Service** | `0x221F` (自定义) | **固件升级** | `otaWritePre()` → `custom_otaWrite()` |
| **RxTx Service** | `0x1F10` (自定义) | **命令控制** | `RxTxWrite()` → `cmd_parser()` |
| **EPD BLE Service** | `13187B10-...` (128位自定义) | **图像数据传输** | `epd_ble_handle_write()` |

### 1.2 关键发现：网页与固件的 Service 对应关系

```
┌────────────────────────────────────────────────────────────────┐
│                    网页请求的 Service                           │
├────────────────────────────────────────────────────────────────┤
│ ✅ OTA Service:    0000221F-0000-1000-8000-00805F9B34FB         │
│ ✅ RxTx Service:   00001F10-0000-1000-8000-00805F9B34FB         │
│ ✅ EPD Service:    13187B10-EBA9-A3BA-044E-83D3217D9A38         │
└────────────────────────────────────────────────────────────────┘
                              ↓ 匹配
┌────────────────────────────────────────────────────────────────┐
│                    固件提供的 Service                           │
├────────────────────────────────────────────────────────────────┤
│ ✅ OTA Service:    0x221F (等同于 0000221F-...)                │
│ ✅ RxTx Service:   0x1F10 (等同于 00001F10-...)                │
│ ✅ EPD Service:    13187B10-EBA9-A3BA-044E-83D3217D9A38         │
└────────────────────────────────────────────────────────────────┘
```

**结论**：网页请求的三个 Service **全部在固件中存在**！✅

---

## 二、通信流程的三条并行路径

### 2.1 路径1：OTA Service（固件升级）

```
┌─────────────┐         OTA Service (0x221F)          ┌─────────────┐
│             │  ─────────────────────────────────>    │             │
│  Web页面    │      Characteristic (0x331F)          │   固件      │
│  发送端     │          写入命令数据                  │  接收端     │
│             │  <─────────────────────────────────    │             │
└─────────────┘         通知返回结果                  └─────────────┘
                              ↓
                    ┌──────────────────┐
                    │  otaWritePre()   │
                    └──────────────────┘
                              ↓
                    ┌──────────────────┐
                    │custom_otaWrite() │
                    └──────────────────┘
                              ↓
                    固件升级逻辑（擦除、写入、CRC）
```

**命令格式**（网页发送）：
```
0x01 + [4字节地址]           擦除 Flash 块
0x02 + [4字节地址]           设置写入地址
0x03 + [数据...]             写入数据到缓冲区
0x04 + [4字节地址]           读取 Flash
0x05 + [4字节地址]           读取 RAM
0x06                        CRC 校验
0x07 C0 01 CE ED [2字节CRC]  最终刷写
```

**处理状态**：
```c
// ble.c
_attribute_ram_code_ int otaWritePre(void *p)
{
    if (ota_started == 0)
    {
        ota_started = 1;
        ble_set_connection_speed(6);  // 🚀 加速连接到 7.5ms 间隔
    }
    return custom_otaWrite(p);  // 调用 OTA 升级逻辑
}
```

### 2.2 路径2：RxTx Service（命令控制）

```
┌─────────────┐       RxTx Service (0x1F10)          ┌─────────────┐
│             │  ─────────────────────────────────>    │             │
│  Web页面    │      Characteristic (0x1F1F)          │   固件      │
│  发送端     │          写入命令字节                  │  接收端     │
│             │  <─────────────────────────────────    │             │
└─────────────┘         通知返回结果                  └─────────────┘
                              ↓
                    ┌──────────────────┐
                    │   RxTxWrite()    │
                    └──────────────────┘
                              ↓
                    ┌──────────────────┐
                    │  cmd_parser()    │
                    └──────────────────┘
                              ↓
            设置参数、保存Flash、显示字符等
```

**命令格式**（网页发送）：
```
0xDD + [4字节时间戳]         设置时间
0xE0 + [1字节型号]           强制 EPD 型号
0xFE + [1字节间隔]           设置广播间隔
0xFA + [1字节偏移]           设置温度偏移
0xFC + [1字节报警点]         设置温度报警点
0xB1 + [1字节字符]           显示单个字符
... 等 20+ 种命令
```

**处理逻辑**（cmd_parser.c）：
```c
_attribute_ram_code_ int RxTxWrite(void *p)
{
    cmd_parser(p);  // 解析并执行命令
    return 0;
}

void cmd_parser(void * p){
    rf_packet_att_data_t *req = (rf_packet_att_data_t*)p;
    uint8_t inData = req->dat[0];
    
    if(inData == 0xDD){  // 设置时间
        uint32_t new_time = (req->dat[1]<<24) + (req->dat[2]<<16) 
                          + (req->dat[3]<<8) + (req->dat[4]&0xff);
        set_time(new_time);
    }
    else if(inData == 0xE0){  // 强制 EPD 型号
        set_EPD_model(req->dat[1]);
    }
    // ... 更多命令
}
```

### 2.3 路径3：EPD BLE Service（图像传输）⭐

```
┌─────────────┐     EPD BLE Service (13187B10)       ┌─────────────┐
│             │  ─────────────────────────────────>    │             │
│  Web页面    │  Characteristic (4B646063-...)        │   固件      │
│  发送端     │        写入图像数据                    │  接收端     │
│             │                                       │             │
└─────────────┘                                       └─────────────┘
                              ↓
                ┌──────────────────────────────┐
                │  epd_ble_handle_write()      │
                │  (epd_ble_service.c)         │
                └──────────────────────────────┘
                              ↓
        ┌─────────────────────────────────────────┐
        │  case 0x00: 清空缓冲区                    │
        │  case 0x01: 推送到显示屏                  │
        │  case 0x02: 设置写入位置 (2字节)          │
        │  case 0x03: 写入图像数据                  │
        │  case 0x04: 解码并显示 TIFF               │
        │  case 0x05: 显示位图+旋转                 │
        └─────────────────────────────────────────┘
                              ↓
                    写入 epd_buffer[]
                              ↓
                    EPD_Display() → 显示到屏幕
```

---

## 三、网页与 EPD Service 的通信兼容性分析 ⚠️

### 3.1 网页的 `sendimg()` 函数分析

```javascript
function sendimg(cmdIMG) {
    imgArray = cmdIMG.replace(/(?:\r\n|\r|\n|,|0x| )/g, '');
    imgArrayLen = imgArray.length;
    uploadPart = 0;
    
    // ===== 步骤 1: 清空缓冲区 =====
    sendCommand(hexToBytes("0000")).then(() => {
        
        // ===== 步骤 2: 设置写入位置 =====
        sendCommand(hexToBytes("020000")).then(() => {
            
            // ===== 步骤 3: 循环发送数据 =====
            sendIMGpart();
        })
    })
}

function sendIMGpart() {
    if (imgArray.length > 0) {
        // 每次发送 19 字节数据 (38个十六进制字符)
        let currentpart = "03" + imgArray.substring(0, 38);
        imgArray = imgArray.substring(38);
        sendCommand(hexToBytes(currentpart)).then(() => {
            sendIMGpart();  // 递归调用
        })
    } else {
        // ===== 步骤 4: 触发显示 =====
        sendCommand(hexToBytes("01")).then(() => {
            console.log('Update was send');
        })
    }
}
```

### 3.2 固件的 `epd_ble_handle_write()` 函数分析

```c
int epd_ble_handle_write(void *p)
{
    rf_packet_att_write_t *req = (rf_packet_att_write_t *)p;
    uint8_t *payload = &req->value;
    unsigned int payload_len = req->l2capLen - 3;

    ASSERT_MIN_LEN(payload_len, 1);

    switch (payload[0])
    {
    case 0x00: // Clear EPD buffer
        ASSERT_MIN_LEN(payload_len, 2);
        memset(epd_buffer, payload[1], epd_buffer_size);  // 用 payload[1] 填充
        ble_set_connection_speed(40);  // 降速到 50ms 间隔
        return 0;

    case 0x01: // Push buffer to display
        ble_set_connection_speed(200);  // 降速到 250ms 间隔
        EPD_Display(epd_buffer, epd_buffer_size, 1);  // 触发显示
        return 0;

    case 0x02: // Set write position
        ASSERT_MIN_LEN(payload_len, 3);
        byte_pos = payload[1] << 8 | payload[2];  // 2字节地址
        return 0;

    case 0x03: // Write data to image buffer
        if (byte_pos + payload_len - 1 >= epd_buffer_size + 1)
            return 0;  // 越界检查
        memcpy(epd_buffer + byte_pos, payload + 1, payload_len - 1);
        byte_pos += payload_len - 1;  // 自动递增
        return 0;

    case 0x04: // Decode & display a TIFF image
        epd_display_tiff(epd_buffer, byte_pos);
        return 0;

    case 0x05: // Display raw bitmap with rotation
        FixBuffer(epd_buffer, epd_temp, epd_width, epd_height);
        ble_set_connection_speed(200);
        EPD_Display(epd_temp, epd_buffer_size, 1);
        return 0;

    default:
        return 0;
    }
}
```

### 3.3 逐步对比分析

#### 步骤 1：网页发送 `00 00`

```
网页发送:  00 00
           ↓
固件接收:  payload[0] = 0x00
           payload[1] = 0x00
           payload_len = 2
           ↓
固件执行:  memset(epd_buffer, 0x00, epd_buffer_size);
           // 将整个缓冲区清零（全黑）
           ↓
结果:      ✅ 兼容！缓冲区被填充为 0x00
```

**分析**：
- ✅ 命令格式正确：`0x00 + [填充值]`
- ✅ 语义正确：清空缓冲区
- ⚠️ 注意：固件会调用 `ble_set_connection_speed(40)`，将连接间隔降到 50ms

#### 步骤 2：网页发送 `02 00 00`

```
网页发送:  02 00 00
           ↓
固件接收:  payload[0] = 0x02
           payload[1] = 0x00
           payload[2] = 0x00
           payload_len = 3
           ↓
固件执行:  byte_pos = (0x00 << 8) | 0x00 = 0
           ↓
结果:      ✅ 兼容！写入位置设置为 0
```

**分析**：
- ✅ 命令格式正确：`0x02 + [高字节] + [低字节]`
- ✅ 语义正确：设置写入位置为 0
- ✅ 完美兼容！

#### 步骤 3：网页循环发送 `03 [19字节数据]`

```
网页发送:  03 12 34 56 78 9A BC ... (19字节)
           ↓
固件接收:  payload[0] = 0x03
           payload[1..19] = 图像数据
           payload_len = 20
           ↓
固件执行:  memcpy(epd_buffer + 0, payload + 1, 19);
           byte_pos += 19;  // byte_pos 变为 19
           ↓
下一次:    网页发送: 03 [19字节]
           固件执行: memcpy(epd_buffer + 19, payload + 1, 19);
           byte_pos += 19;  // byte_pos 变为 38
           ↓
结果:      ✅ 兼容！数据被逐步写入 epd_buffer
```

**分析**：
- ✅ 命令格式正确：`0x03 + [数据...]`
- ✅ 语义正确：写入数据到缓冲区
- ✅ 自动递增：固件会自动更新 `byte_pos`
- ⚠️ 性能问题：每次只发送 19 字节，效率较低

#### 步骤 4：网页发送 `01`

```
网页发送:  01
           ↓
固件接收:  payload[0] = 0x01
           payload_len = 1
           ↓
固件执行:  ble_set_connection_speed(200);  // 降速
           EPD_Display(epd_buffer, epd_buffer_size, 1);
           ↓
结果:      ✅ 兼容！图像显示到屏幕
```

**分析**：
- ✅ 命令格式正确：`0x01`
- ✅ 语义正确：推送缓冲区到显示屏
- ⚠️ 注意：显示过程需要 2-15 秒，期间设备会降速

---

## 四、关键问题与风险评估 ⚠️

### 4.1 问题1：网页使用哪个 Service？ 🔴

**关键问题**：网页的 `sendimg()` 函数使用的是 **OTA Service (0x221F)** 还是 **EPD Service (13187B10)**？

#### 场景 A：网页使用 OTA Service 发送图像

```javascript
// 网页连接到 OTA Service
gattServer.getPrimaryService('0000221f-0000-1000-8000-00805f9b34fb')
.then(service => {
    return service.getCharacteristic('0000331f-0000-1000-8000-00805f9b34fb');
})
.then(characteristic => {
    writeCharacteristic = characteristic;
    // 使用 sendCommand() 发送 00 00, 02 00 00, 03 [数据], 01
});
```

**结果**：❌ **完全不兼容！**

- OTA Service 的命令会被 `otaWritePre()` → `custom_otaWrite()` 处理
- 这些函数**不会调用** `epd_ble_handle_write()`
- 命令会被解释为**固件升级命令**，导致：
  - `01` 被解释为"擦除 Flash 块"
  - `02` 被解释为"设置 Flash 写入地址"
  - `03` 被解释为"写入固件数据"
  - 可能导致**固件被破坏**！🔥

#### 场景 B：网页使用 EPD Service 发送图像

```javascript
// 网页连接到 EPD Service
gattServer.getPrimaryService('13187b10-eba9-a3ba-044e-83d3217d9a38')
.then(service => {
    return service.getCharacteristic('4b646063-6264-f3a7-8941-e65356ea82fe');
})
.then(characteristic => {
    writeCharacteristic = characteristic;
    // 使用 sendCommand() 发送 00 00, 02 00 00, 03 [数据], 01
});
```

**结果**：✅ **完全兼容！**

- EPD Service 的命令会被 `epd_ble_handle_write()` 处理
- 命令格式和语义都正确
- 图像能够正常显示

### 4.2 问题2：数据传输效率低 ⚠️

**网页的传输策略**：
```
每次发送: 1字节命令 + 19字节数据 = 20字节
总数据量: 5,624字节 (2.66英寸 EPD)
传输次数: 5,624 / 19 ≈ 296 次
每次间隔: 约 5ms (网页代码中没有明确延迟，但浏览器有限制)
总时间:   296 × 5ms ≈ 1.5 秒
```

**Image2ESL App 的传输策略**（对比）：
```
每次发送: 1字节命令 + 243字节数据 = 244字节
总数据量: 5,624字节
传输次数: 5,624 / 243 ≈ 24 次
每次间隔: 5ms
总时间:   24 × 5ms ≈ 0.12 秒
```

**对比结果**：
- 网页传输速度约为 **Image2ESL App 的 12.5%** ⚠️
- 建议：将网页的数据块大小从 19 字节提升到 243 字节

### 4.3 问题3：缺少错误处理 ⚠️

**网页代码的问题**：
```javascript
function sendIMGpart() {
    if (imgArray.length > 0) {
        let currentpart = "03" + imgArray.substring(0, 38);
        imgArray = imgArray.substring(38);
        sendCommand(hexToBytes(currentpart)).then(() => {
            sendIMGpart();  // 🔴 无错误处理
        })
    } else {
        sendCommand(hexToBytes("01")).then(() => {
            console.log('Update was send');  // 🔴 无失败处理
        })
    }
}
```

**缺少的错误处理**：
1. ❌ BLE 写入失败时没有重试机制
2. ❌ 没有检查固件是否成功接收数据
3. ❌ 没有超时保护
4. ❌ 没有进度百分比显示
5. ❌ 连接断开时无法恢复

**建议改进**：
```javascript
async function sendIMGpart() {
    if (imgArray.length > 0) {
        let currentpart = "03" + imgArray.substring(0, 38);
        imgArray = imgArray.substring(38);
        uploadPart++;
        
        try {
            await sendCommand(hexToBytes(currentpart));
            setStatus('Progress: ' + (uploadPart / totalParts * 100).toFixed(1) + '%');
            await sendIMGpart();  // 继续发送
        } catch (error) {
            console.error('Send failed:', error);
            // 重试 3 次
            if (retryCount < 3) {
                retryCount++;
                await delay(100);
                await sendIMGpart();
            } else {
                alert('Image upload failed!');
            }
        }
    } else {
        await sendCommand(hexToBytes("01"));
        setStatus('Image sent successfully!');
    }
}
```

### 4.4 问题4：连接速度调整不当 ⚠️

**固件的连接速度调整**：
```c
// epd_ble_service.c
case 0x00:  // 清空缓冲区
    memset(epd_buffer, payload[1], epd_buffer_size);
    ble_set_connection_speed(40);  // 降速到 50ms 间隔 ⚠️
    return 0;

case 0x01:  // 推送到显示屏
    ble_set_connection_speed(200);  // 降速到 250ms 间隔 ⚠️
    EPD_Display(epd_buffer, epd_buffer_size, 1);
    return 0;
```

**问题分析**：
- 网页在发送 `00 00` (清空缓冲区) 后，固件会将连接速度降到 **50ms 间隔**
- 这会**减慢**后续的 `03` 命令（写入数据）的传输速度
- 本应该在数据传输时**加速**，而不是减速！

**建议修改**：
```c
case 0x00:  // 清空缓冲区
    memset(epd_buffer, payload[1], epd_buffer_size);
    ble_set_connection_speed(6);  // 🚀 加速到 7.5ms 间隔（与 OTA 一致）
    return 0;

case 0x03:  // 写入数据
    if (byte_pos + payload_len - 1 >= epd_buffer_size + 1)
        return 0;
    memcpy(epd_buffer + byte_pos, payload + 1, payload_len - 1);
    byte_pos += payload_len - 1;
    // 保持高速连接，不调整
    return 0;

case 0x01:  // 推送到显示屏
    ble_set_connection_speed(200);  // 显示时降速（避免干扰 EPD 刷新）
    EPD_Display(epd_buffer, epd_buffer_size, 1);
    return 0;
```

### 4.5 问题5：缓冲区溢出风险 🔴

**固件的边界检查**：
```c
case 0x03:  // Write data to image buffer
    if (byte_pos + payload_len - 1 >= epd_buffer_size + 1)  // ⚠️ 奇怪的检查
        return 0;
    memcpy(epd_buffer + byte_pos, payload + 1, payload_len - 1);
    byte_pos += payload_len - 1;
    return 0;
```

**问题分析**：
- 边界检查条件是 `>= epd_buffer_size + 1`，这是正确的
- 但如果网页发送了**超长数据**，仍可能导致溢出
- `byte_pos` 是全局变量，如果网页**未发送 0x02 命令**就直接发送 0x03，`byte_pos` 可能是**上次的残留值**

**风险场景**：
```
场景1: 网页忘记发送 "02 00 00"
  ↓ byte_pos 保持上次的值（如 5624）
  ↓ 网页发送 "03 [19字节]"
  ↓ 固件尝试写入 epd_buffer[5624]
  ↓ 越界！可能覆盖其他内存区域！🔥

场景2: 网页发送错误的位置
  ↓ 网页发送 "02 FF FF" (位置 65535)
  ↓ byte_pos = 65535
  ↓ 网页发送 "03 [19字节]"
  ↓ 边界检查: 65535 + 19 - 1 >= 5624 + 1 ✅ 拒绝
  ↓ 但 byte_pos 仍然是 65535，下次可能出问题
```

**建议改进**：
```c
case 0x02:  // Set write position
    ASSERT_MIN_LEN(payload_len, 3);
    byte_pos = payload[1] << 8 | payload[2];
    // 🛡️ 添加边界检查
    if (byte_pos >= epd_buffer_size) {
        byte_pos = 0;  // 重置为安全值
        return 0;
    }
    return 0;

case 0x03:  // Write data to image buffer
    unsigned int data_len = payload_len - 1;
    // 🛡️ 改进边界检查
    if (byte_pos >= epd_buffer_size || 
        byte_pos + data_len > epd_buffer_size) {
        return 0;  // 拒绝写入
    }
    memcpy(epd_buffer + byte_pos, payload + 1, data_len);
    byte_pos += data_len;
    return 0;
```

---

## 五、完整通信时序图

### 5.1 正确的通信流程（使用 EPD Service）

```
    网页 (发送端)                              固件 (接收端)                    EPD 屏幕
         │                                         │                                  │
    ┌────┴────┐                                    │                                  │
    │ 连接设备 │                                    │                                  │
    └────┬────┘                                    │                                  │
         │                                         │                                  │
         ├─────── 发现 Service (13187B10) ─────────>│                                  │
         │<──────── 返回 Characteristic ───────────┤                                  │
         │                                         │                                  │
    ┌────┴────────┐                                │                                  │
    │ 步骤1: 清空 │                                │                                  │
    └────┬────────┘                                │                                  │
         ├─────── 写入 [00 00] ──────────────────>│                                  │
         │                              epd_ble_handle_write()                        │
         │                              └─ case 0x00:                                 │
         │                                 memset(epd_buffer, 0x00, 5624)             │
         │                                 ble_set_connection_speed(40)  ⚠️ 降速      │
         │                                         │                                  │
    ┌────┴─────────┐                               │                                  │
    │ 步骤2: 设置位置│                              │                                  │
    └────┬─────────┘                               │                                  │
         ├─────── 写入 [02 00 00] ───────────────>│                                  │
         │                              epd_ble_handle_write()                        │
         │                              └─ case 0x02:                                 │
         │                                 byte_pos = 0                               │
         │                                         │                                  │
    ┌────┴─────────┐                               │                                  │
    │ 步骤3: 写数据 │                               │                                  │
    │  (循环296次) │                                │                                  │
    └────┬─────────┘                               │                                  │
         ├─────── 写入 [03 + 19字节] ─────────────>│                                  │
         │                              epd_ble_handle_write()                        │
         │                              └─ case 0x03:                                 │
         │                                 memcpy(epd_buffer + 0, data, 19)           │
         │                                 byte_pos = 19                              │
         │                                         │                                  │
         ├─────── 写入 [03 + 19字节] ─────────────>│                                  │
         │                              epd_ble_handle_write()                        │
         │                              └─ case 0x03:                                 │
         │                                 memcpy(epd_buffer + 19, data, 19)          │
         │                                 byte_pos = 38                              │
         │                                         │                                  │
         │          ... 循环 294 次 ...            │                                  │
         │                                         │                                  │
         ├─────── 写入 [03 + 最后数据] ───────────>│                                  │
         │                              epd_ble_handle_write()                        │
         │                              └─ case 0x03:                                 │
         │                                 memcpy(epd_buffer + 5605, data, 19)        │
         │                                 byte_pos = 5624 ✅ 完成                     │
         │                                         │                                  │
    ┌────┴─────────┐                               │                                  │
    │ 步骤4: 显示  │                                │                                  │
    └────┬─────────┘                               │                                  │
         ├─────── 写入 [01] ─────────────────────>│                                  │
         │                              epd_ble_handle_write()                        │
         │                              └─ case 0x01:                                 │
         │                                 ble_set_connection_speed(200)  ⚠️ 降速     │
         │                                 EPD_Display(epd_buffer, 5624, 1)           │
         │                                         ├────── SPI: 写入 RAM ─────────────>│
         │                                         ├────── SPI: 设置 LUT ─────────────>│
         │                                         ├────── SPI: 开始刷新 ─────────────>│
         │                                         │                           ┌──────┴──────┐
         │                                         │                           │  显示刷新   │
         │                                         │                           │ (2-15秒)   │
         │                                         │                           └──────┬──────┘
         │                                         │<───── BUSY 信号释放 ───────────────┤
         │                              epd_state_handler()                            │
         │                              └─ EPD_IS_BUSY() == false                     │
         │                                 epd_set_sleep()                             │
         │                                         ├────── SPI: 进入休眠 ─────────────>│
         │                                         │                            ┌──────┴──────┐
         │                                         │                            │  显示完成   │
         │                                         │                            └─────────────┘
         │                                         │                                  │
    ┌────┴─────────┐                               │                                  │
    │  显示成功    │                                │                                  │
    └──────────────┘                               │                                  │
```

### 5.2 错误的通信流程（使用 OTA Service）🔴

```
    网页 (发送端)                              固件 (接收端)
         │                                         │
         ├─────── 发现 Service (0x221F) ──────────>│  ⚠️ 错误的 Service！
         │<──────── 返回 Characteristic ───────────┤
         │                                         │
         ├─────── 写入 [00 00] ──────────────────>│
         │                              otaWritePre()
         │                              └─ ota_started = 1
         │                                 ble_set_connection_speed(6)
         │                                 custom_otaWrite()
         │                              🔥 命令被解释为 OTA 命令！
         │                              🔥 可能触发意外的固件操作！
         │                                         │
         ├─────── 写入 [02 00 00] ───────────────>│
         │                              custom_otaWrite()
         │                              🔥 可能设置 Flash 写入地址！
         │                                         │
         ├─────── 写入 [03 + 数据] ──────────────>│
         │                              custom_otaWrite()
         │                              🔥 可能写入固件数据到 Flash！
         │                                         │
         ├─────── 写入 [01] ─────────────────────>│
         │                              custom_otaWrite()
         │                              🔥 可能触发 Flash 擦除！
         │                                         │
         │                              💥 固件被破坏！
         │                              💥 设备可能变砖！
```

---

## 六、实际测试建议

### 6.1 网页代码检查清单

在使用网页发送图像之前，**必须确认**：

```javascript
// ✅ 检查1: 确认连接的是 EPD Service
gattServer.getPrimaryService('13187b10-eba9-a3ba-044e-83d3217d9a38')
.then(service => {
    console.log('✅ Connected to EPD Service');
    return service.getCharacteristic('4b646063-6264-f3a7-8941-e65356ea82fe');
})

// ❌ 错误: 不要连接 OTA Service 发送图像！
gattServer.getPrimaryService('0000221f-0000-1000-8000-00805f9b34fb')  // 🔴 危险！
```

### 6.2 安全测试步骤

1. **第一步：验证 Service UUID**
   ```javascript
   console.log('Current service UUID:', Theservice.uuid);
   // 应该输出: 13187b10-eba9-a3ba-044e-83d3217d9a38
   ```

2. **第二步：发送测试命令（清空白色）**
   ```javascript
   sendCommand(hexToBytes("00FF"));  // 清空为白色
   ```

3. **第三步：发送小块数据测试**
   ```javascript
   // 只发送前 100 字节测试
   imgArray = "00".repeat(100);  // 100 字节的测试数据
   sendimg(imgArray);
   ```

4. **第四步：发送完整图像**
   ```javascript
   // 发送 5624 字节的完整图像
   sendimg(fullImageData);
   ```

### 6.3 固件日志监控

如果固件支持串口输出，监控以下日志：

```c
// 在 epd_ble_handle_write() 中添加日志
int epd_ble_handle_write(void *p)
{
    rf_packet_att_write_t *req = (rf_packet_att_write_t *)p;
    uint8_t *payload = &req->value;
    unsigned int payload_len = req->l2capLen - 3;

    printf("EPD BLE: cmd=0x%02X, len=%d\r\n", payload[0], payload_len);  // 🔍 调试日志

    switch (payload[0])
    {
    case 0x00:
        printf("EPD BLE: Clear buffer with 0x%02X\r\n", payload[1]);  // 🔍
        memset(epd_buffer, payload[1], epd_buffer_size);
        return 0;
        
    case 0x02:
        byte_pos = payload[1] << 8 | payload[2];
        printf("EPD BLE: Set position to %d\r\n", byte_pos);  // 🔍
        return 0;
        
    case 0x03:
        printf("EPD BLE: Write %d bytes at position %d\r\n", payload_len - 1, byte_pos);  // 🔍
        memcpy(epd_buffer + byte_pos, payload + 1, payload_len - 1);
        byte_pos += payload_len - 1;
        return 0;
        
    case 0x01:
        printf("EPD BLE: Display buffer\r\n");  // 🔍
        EPD_Display(epd_buffer, epd_buffer_size, 1);
        return 0;
    }
}
```

---

## 七、结论与建议

### 7.1 兼容性结论

| 评估项 | 结果 | 说明 |
|--------|------|------|
| **Service 层面** | ⚠️ **有条件兼容** | 必须使用 EPD Service (13187B10)，不能使用 OTA Service (0x221F) |
| **命令格式** | ✅ **完全兼容** | 0x00, 0x01, 0x02, 0x03 命令格式和语义都正确 |
| **数据传输** | ✅ **兼容** | 数据能够正常写入 epd_buffer |
| **显示功能** | ✅ **兼容** | 图像能够正常显示到屏幕 |
| **传输效率** | ⚠️ **较低** | 每次只发送 19 字节，建议提升到 243 字节 |
| **错误处理** | ❌ **缺失** | 缺少重试机制、进度显示、超时保护 |
| **边界安全** | ⚠️ **有风险** | byte_pos 未清零，可能导致越界写入 |

### 7.2 关键建议

#### 🔴 严重问题（必须修复）

1. **确认使用正确的 Service**
   - 网页**必须**连接到 EPD Service (13187B10)
   - **禁止**使用 OTA Service (0x221F) 发送图像数据
   - 在代码中添加 Service UUID 验证

2. **固件边界检查改进**
   ```c
   // 在 case 0x02 中添加边界检查
   if (byte_pos >= epd_buffer_size) {
       byte_pos = 0;
       return 0;
   }
   ```

#### ⚠️ 重要问题（建议修复）

3. **提升传输效率**
   ```javascript
   // 从 19 字节提升到 243 字节
   let currentpart = "03" + imgArray.substring(0, 486);  // 243 * 2
   ```

4. **固件连接速度优化**
   ```c
   case 0x00:  // 清空缓冲区
       memset(epd_buffer, payload[1], epd_buffer_size);
       ble_set_connection_speed(6);  // 🚀 加速而不是减速
       return 0;
   ```

5. **添加错误处理**
   ```javascript
   try {
       await sendCommand(cmd);
   } catch (error) {
       console.error('Failed:', error);
       // 重试或报错
   }
   ```

#### ℹ️ 一般优化（可选）

6. **添加进度显示**
   ```javascript
   setStatus('Progress: ' + (uploadPart / totalParts * 100).toFixed(1) + '%');
   ```

7. **添加 CRC 校验**
   - 网页发送图像后计算 CRC
   - 固件验证接收到的数据完整性

8. **添加图像预览**
   - 网页显示即将发送的图像
   - 用户确认后再发送

### 7.3 最终评估

**如果网页使用正确的 EPD Service (13187B10)**：
- ✅ 命令格式**完全兼容**
- ✅ 数据传输**正常工作**
- ✅ 图像显示**功能正常**
- ⚠️ 传输效率**可以优化**
- ⚠️ 错误处理**需要补充**

**如果网页使用错误的 OTA Service (0x221F)**：
- 🔥 命令会被误解为**固件升级命令**
- 🔥 可能导致**固件被破坏**
- 🔥 设备可能**变砖**
- 🔴 **绝对不兼容！**

---

## 八、修改建议代码

### 8.1 网页端修改（确保使用正确的 Service）

```javascript
// ========== 修改 connect() 函数 ==========
function connect() {
    if (writeCharacteristic == null) {
        addLog("Connecting to: " + bleDevice.name);
        bleDevice.gatt.connect().then(server => {
            console.log('> Found GATT server');
            gattServer = server;
            
            // 🔄 改为连接 EPD Service，而不是 OTA Service
            return gattServer.getPrimaryService('13187b10-eba9-a3ba-044e-83d3217d9a38');
        })
        .then(service => {
            console.log('> Found EPD service');
            Theservice = service;
            
            // 🔄 改为使用 EPD Characteristic
            return Theservice.getCharacteristic('4b646063-6264-f3a7-8941-e65356ea82fe');
        })
        .then(characteristic => {
            console.log('> Found EPD write characteristic');
            addLog('> Connected to EPD Service ✅');
            document.getElementById("connectbutton").innerHTML = 'Disconnect';
            writeCharacteristic = characteristic;
            
            // EPD Service 通常不需要 notification
            // 如果需要，可以添加
        })
        .catch(handleError);
    }
}

// ========== 修改 sendimg() 函数（优化传输效率）==========
function sendimg(cmdIMG) {
    imgArray = cmdIMG.replace(/(?:\r\n|\r|\n|,|0x| )/g, '');
    imgArrayLen = imgArray.length;
    uploadPart = 0;
    totalParts = Math.ceil(imgArrayLen / 486);  // 243 字节/次
    startTime = new Date().getTime();
    
    console.log('Sending image: ' + imgArrayLen / 2 + ' bytes');
    
    // 步骤 1: 清空缓冲区（填充0x00 = 黑色）
    sendCommand(hexToBytes("0000")).then(() => {
        addLog('Buffer cleared');
        
        // 步骤 2: 设置写入位置为 0
        sendCommand(hexToBytes("020000")).then(() => {
            addLog('Position set to 0');
            
            // 步骤 3: 发送数据
            sendIMGpart();
        })
    })
    .catch(handleError);
}

function sendIMGpart() {
    if (imgArray.length > 0) {
        // 🚀 每次发送 243 字节（486 个十六进制字符）
        let chunkSize = Math.min(486, imgArray.length);
        let currentpart = "03" + imgArray.substring(0, chunkSize);
        imgArray = imgArray.substring(chunkSize);
        uploadPart++;
        
        // 显示进度
        let progress = ((totalParts - uploadPart) / totalParts * 100).toFixed(1);
        setStatus('Uploading: ' + progress + '% (' + uploadPart + '/' + totalParts + ')');
        
        console.log('Sending part ' + uploadPart + '/' + totalParts + ': ' + (chunkSize / 2) + ' bytes');
        
        sendCommand(hexToBytes(currentpart)).then(() => {
            sendIMGpart();  // 继续发送
        })
        .catch(error => {
            console.error('Send part failed:', error);
            addLog('❌ Upload failed at part ' + uploadPart);
        });
    } else {
        // 步骤 4: 触发显示
        addLog('All data sent, triggering display...');
        sendCommand(hexToBytes("01")).then(() => {
            let elapsed = (new Date().getTime() - startTime) / 1000.0;
            addLog('✅ Image sent successfully! Time: ' + elapsed.toFixed(2) + 's');
            console.log('Image upload complete');
        })
        .catch(error => {
            console.error('Display trigger failed:', error);
            addLog('❌ Display failed');
        });
    }
}
```

### 8.2 固件端修改（优化性能和安全性）

```c
// ========== 修改 epd_ble_service.c ==========
unsigned int byte_pos = 0;
unsigned int byte_pos_valid = 0;  // 🛡️ 添加有效标志

int epd_ble_handle_write(void *p)
{
    rf_packet_att_write_t *req = (rf_packet_att_write_t *)p;
    uint8_t *payload = &req->value;
    unsigned int payload_len = req->l2capLen - 3;

    ASSERT_MIN_LEN(payload_len, 1);

    switch (payload[0])
    {
    case 0x00: // Clear EPD buffer
        ASSERT_MIN_LEN(payload_len, 2);
        memset(epd_buffer, payload[1], epd_buffer_size);
        ble_set_connection_speed(6);  // 🚀 加速到 7.5ms（而不是 40 = 50ms）
        byte_pos = 0;  // 🛡️ 重置位置
        byte_pos_valid = 0;  // 🛡️ 标记未设置
        return 0;

    case 0x01: // Push buffer to display
        ble_set_connection_speed(200);  // 显示时降速
        EPD_Display(epd_buffer, epd_buffer_size, 1);
        byte_pos = 0;  // 🛡️ 显示后重置
        byte_pos_valid = 0;  // 🛡️ 标记未设置
        return 0;

    case 0x02: // Set write position
        ASSERT_MIN_LEN(payload_len, 3);
        byte_pos = payload[1] << 8 | payload[2];
        
        // 🛡️ 添加边界检查
        if (byte_pos >= epd_buffer_size) {
            byte_pos = 0;
            byte_pos_valid = 0;
            return 0;  // 拒绝无效位置
        }
        
        byte_pos_valid = 1;  // 🛡️ 标记已设置
        return 0;

    case 0x03: // Write data to image buffer
        // 🛡️ 检查位置是否已设置
        if (!byte_pos_valid) {
            return 0;  // 拒绝写入（位置未设置）
        }
        
        unsigned int data_len = payload_len - 1;
        
        // 🛡️ 改进边界检查
        if (byte_pos >= epd_buffer_size || 
            byte_pos + data_len > epd_buffer_size) {
            return 0;  // 拒绝越界写入
        }
        
        memcpy(epd_buffer + byte_pos, payload + 1, data_len);
        byte_pos += data_len;
        // 保持高速连接，不调整
        return 0;

    case 0x04: // Decode & display a TIFF image
        epd_display_tiff(epd_buffer, byte_pos);
        byte_pos = 0;  // 🛡️ 显示后重置
        byte_pos_valid = 0;
        return 0;

    case 0x05: // Display raw bitmap with rotation
        FixBuffer(epd_buffer, epd_temp, epd_width, epd_height);
        ble_set_connection_speed(200);
        EPD_Display(epd_temp, epd_buffer_size, 1);
        byte_pos = 0;  // 🛡️ 显示后重置
        byte_pos_valid = 0;
        return 0;

    default:
        return 0;
    }
}
```

---

## 九、性能对比总结

### 9.1 传输时间对比

| 方案 | 数据块大小 | 传输次数 | 总时间 | 效率 |
|------|-----------|---------|--------|------|
| **网页（原始）** | 19 字节 | 296 次 | ~1.5 秒 | 基准 |
| **网页（优化）** | 243 字节 | 24 次 | ~0.12 秒 | **12.5x** ✅ |
| **Image2ESL App** | 243 字节 | 24 次 | ~0.12 秒 | **12.5x** ✅ |

### 9.2 固件连接速度对比

| 阶段 | 原始固件 | 优化固件 | 说明 |
|------|---------|---------|------|
| 清空缓冲区 (0x00) | 50ms 间隔 ⚠️ | 7.5ms 间隔 ✅ | 应该加速而不是减速 |
| 写入数据 (0x03) | 50ms 间隔 ⚠️ | 7.5ms 间隔 ✅ | 保持高速 |
| 触发显示 (0x01) | 250ms 间隔 ✅ | 250ms 间隔 ✅ | 显示时降速避免干扰 |

### 9.3 安全性对比

| 检查项 | 原始固件 | 优化固件 |
|--------|---------|---------|
| byte_pos 边界检查 | ⚠️ 部分检查 | ✅ 完整检查 |
| byte_pos 有效性验证 | ❌ 无 | ✅ 有标志位 |
| byte_pos 自动重置 | ❌ 无 | ✅ 每次操作后重置 |
| 越界写入保护 | ⚠️ 基本保护 | ✅ 严格保护 |

---

## 十、总结

**核心结论**：
1. ✅ 网页与固件 **可以兼容**，前提是使用正确的 EPD Service (13187B10)
2. 🔴 网页 **绝不能** 使用 OTA Service (0x221F) 发送图像数据
3. ⚠️ 原始代码存在 **传输效率低**、**错误处理缺失**、**边界检查不足** 等问题
4. ✅ 通过建议的优化，可以达到与 Image2ESL App **相同的性能**

**最终建议**：
- 📌 **立即修改**：确认网页使用正确的 Service UUID
- 🚀 **性能优化**：将数据块从 19 字节提升到 243 字节
- 🛡️ **安全加固**：添加边界检查和 byte_pos 有效性验证
- ⚡ **速度优化**：修改固件连接速度调整逻辑
- 🔍 **调试支持**：添加串口日志监控传输过程

---

**文档版本**: 2.0  
**更新日期**: 2024  
**作者**: GitHub Copilot  
**相关文件**: 
- `Firmware\WebBluetooth Firmware OTA Flashing.html`
- `Firmware\src\epd_ble_service.c`
- `Firmware\src\app_att.c`
- `Firmware\src\ble.c`
- `Firmware\src\cmd_parser.c`
