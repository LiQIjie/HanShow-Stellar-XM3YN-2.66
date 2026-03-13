# Web Bluetooth 功能分析与接口兼容性报告

## 文档概述

本文档分析了 `WebBluetooth Firmware OTA Flashing.html` 网页的功能实现，并与 `epd_ble_service.c` 的 BLE 接口进行对比，评估命令格式的兼容性。

---

## 一、网页功能概述

### 1.1 主要功能模块

该网页是一个基于 **Web Bluetooth API** 的 Telink TLSR8258 系列设备的固件升级和控制工具，具有以下功能：

| 功能模块 | 说明 |
|---------|------|
| **BLE 连接管理** | 连接/断开/重连 BLE 设备 |
| **固件 OTA 升级** | 通过 BLE 上传新固件到 Flash |
| **时间同步** | 设置设备的 Unix 时间戳 |
| **EPD 型号强制设置** | 强制设备使用特定的 EPD 型号 |
| **Flash/RAM 读取** | 读取设备的 Flash 或 RAM 数据 |
| **自定义命令发送** | 手动发送十六进制命令 |
| **图像数据上传** | 上传图像数据到 EPD 缓冲区 |

### 1.2 支持的 BLE Service

```javascript
// 网页连接时请求的 BLE Service UUID
'0000221f-0000-1000-8000-00805f9b34fb'  // OTA Service (主要使用)
'00001f10-0000-1000-8000-00805f9b34fb'  // Custom Main Service
'13187b10-eba9-a3ba-044e-83d3217d9a38'  // EPD BLE Service
```

**注意**: 网页主要使用 **OTA Service (0000221f)** 进行通信，这与 `epd_ble_service.c` 中定义的 **EPD BLE Service (13187b10)** **不同**。

---

## 二、网页命令格式详解

### 2.1 OTA 固件升级命令（通过 0000221f Service）

网页主要实现的是 **固件 OTA 升级** 功能，使用的命令格式如下：

#### 命令 0x01: 擦除 Flash 块

```
格式: 01 [4字节地址]
示例: 01 00 00 02 00  // 擦除地址 0x20000 的块
```

**实现代码**:
```javascript
await sendCommand(hexToBytes("01" + hex_address));
```

**功能**: 擦除指定地址的 Flash 块（通常为 4KB）

#### 命令 0x02: 设置写入位置

```
格式: 02 [4字节地址]
示例: 02 00 00 02 00  // 设置位置为 0x20000
```

**功能**: 设置后续数据写入的 Flash 地址

#### 命令 0x03: 写入数据

```
格式: 03 [数据字节...]
示例: 03 12 34 56 78 9A BC...  // 写入数据到缓冲区
```

**实现代码**:
```javascript
// 每次发送最多 480 字节数据
await sendCommand(hexToBytes("03" + data_part));
```

**功能**: 将数据写入内部缓冲区（不直接写 Flash）

#### 命令 0x04: 读取 Flash

```
格式: 04 [4字节地址]
示例: 04 00 00 02 00  // 读取地址 0x20000
```

**功能**: 读取指定地址的 Flash 数据

#### 命令 0x05: 读取 RAM

```
格式: 05 [4字节地址]
示例: 05 00 00 00 00  // 读取地址 0x00000000 的 RAM
```

**功能**: 读取指定地址的 RAM 数据

#### 命令 0x06: CRC 校验

```
格式: 06
示例: 06
```

**功能**: 触发固件的 CRC 校验

#### 命令 0x07: 最终刷写 Flash

```
格式: 07 [4字节标志] [2字节CRC]
示例: 07 C0 01 CE ED 5A 3F  // 刷写固件，CRC=0x3F5A
```

**实现代码**:
```javascript
await sendCommand(hexToBytes("07C001CEED" + crc));
```

**功能**: 将缓冲区数据刷写到 Flash 并验证 CRC

### 2.2 图像上传命令（通过 0000221f Service）

网页还实现了图像数据上传功能（`sendimg` 函数）：

```javascript
function sendimg(cmdIMG) {
    // 步骤 1: 清空缓冲区（填充0x00）
    sendCommand(hexToBytes("0000"));
    
    // 步骤 2: 设置写入位置为 0
    sendCommand(hexToBytes("020000"));
    
    // 步骤 3: 循环发送图像数据（每次38字节 = 19字节数据）
    let currentpart = "03" + imgArray.substring(0, 38);
    sendCommand(hexToBytes(currentpart));
    
    // 步骤 4: 触发显示
    sendCommand(hexToBytes("01"));
}
```

**注意**: 这里的命令格式与固件升级类似，但语义不同。

### 2.3 时间同步命令（通过 00001f10 Service）

```javascript
function setTime(hourOffset) {
    let unixNow = Math.round(Date.now() / 1000) + (60*60*hourOffset);
    // 格式: DD [4字节时间戳]
    rxTxSendCommand(hexToBytes("dd" + intToHex4(unixNow)));
}
```

**格式**: `DD [4字节Unix时间戳]`  
**示例**: `DD 65 4A 3C 2B`  
**Service**: `00001f10-0000-1000-8000-00805f9b34fb`

### 2.4 强制 EPD 型号命令（通过 00001f10 Service）

```javascript
function sendForceEpd(nr) {
    // 格式: E0 [型号编号+1]
    rxTxSendCommand(hexToBytes("e0" + byteToHex(nr+1)));
}
```

**格式**: `E0 [型号编号]`  
**型号映射**:
- `E0 01` = BW213
- `E0 02` = BWR213
- `E0 03` = BWR154
- `E0 04` = 213ICE
- `E0 05` = BWR350
- `E0 06` = BWY350

---

## 三、与 epd_ble_service.c 的兼容性分析

### 3.1 Service UUID 不同

| 组件 | Service UUID | Characteristic UUID |
|------|-------------|---------------------|
| **网页主要使用** | `0000221f-0000-1000-8000-00805f9b34fb` | `0000331f-0000-1000-8000-00805f9b34fb` |
| **网页次要使用** | `00001f10-0000-1000-8000-00805f9b34fb` | `00001f1f-0000-1000-8000-00805f9b34fb` |
| **epd_ble_service.c** | `13187b10-eba9-a3ba-044e-83d3217d9a38` | `4b646063-6264-f3a7-8941-e65356ea82fe` |

**结论**: 网页使用的是 **OTA Service**，而 `epd_ble_service.c` 定义的是 **EPD 图像服务**，**两者不兼容**。

### 3.2 命令格式对比

#### epd_ble_service.c 定义的命令（EPD Service）

| 命令 | 格式 | 功能 |
|------|------|------|
| `0x00` | `00 [填充值]` | 清空 EPD 缓冲区 |
| `0x01` | `01` | 推送缓冲区到显示屏 |
| `0x02` | `02 [高字节] [低字节]` | 设置写入位置 (2字节) |
| `0x03` | `03 [数据...]` | 写入数据到图像缓冲区 |
| `0x04` | `04` | 解码并显示 TIFF 图像 |
| `0x05` | `05` | 显示原始位图（含旋转） |

#### 网页使用的命令（OTA Service）

| 命令 | 格式 | 功能 |
|------|------|------|
| `0x00` | `00 00` | （在图像上传中使用，语义不明） |
| `0x01` | `01 [4字节地址]` | 擦除 Flash 块 |
| `0x02` | `02 [4字节地址]` | 设置写入位置 (4字节) |
| `0x03` | `03 [数据...]` | 写入数据 |
| `0x04` | `04 [4字节地址]` | 读取 Flash |
| `0x05` | `05 [4字节地址]` | 读取 RAM |
| `0x06` | `06` | CRC 校验 |
| `0x07` | `07 C0 01 CE ED [CRC]` | 最终刷写 Flash |

### 3.3 兼容性总结

| 命令 | 网页语义（OTA Service） | epd_ble_service.c 语义 | 兼容性 |
|------|------------------------|------------------------|--------|
| `0x00` | 未明确（图像上传时使用） | 清空 EPD 缓冲区 | ⚠️ **部分兼容** |
| `0x01` | 擦除 Flash 块 | 推送到显示屏 | ❌ **不兼容** |
| `0x02` | 设置位置（4字节地址） | 设置位置（2字节地址） | ⚠️ **格式不同** |
| `0x03` | 写入数据 | 写入数据 | ✅ **兼容** |
| `0x04` | 读取 Flash | 解码 TIFF | ❌ **不兼容** |
| `0x05` | 读取 RAM | 显示位图+旋转 | ❌ **不兼容** |
| `0x06` | CRC 校验 | （无） | ❌ **不存在** |
| `0x07` | 刷写 Flash | （无） | ❌ **不存在** |

**总体结论**: 
- **Service 层面**: 网页使用 OTA Service，epd_ble_service.c 使用 EPD Service，**完全不同**
- **命令层面**: 虽然命令编号部分重叠（0x00~0x05），但**语义完全不同**
- **数据格式**: 地址字段长度不同（4字节 vs 2字节）

---

## 四、网页图像上传流程分析

### 4.1 `sendimg()` 函数流程

```javascript
function sendimg(cmdIMG) {
    imgArray = cmdIMG.replace(/(?:\r\n|\r|\n|,|0x| )/g, '');  // 清理输入
    imgArrayLen = imgArray.length;
    uploadPart = 0;
    
    // 步骤 1: 发送 "0000" (清空？)
    sendCommand(hexToBytes("0000")).then(() => {
        
        // 步骤 2: 发送 "020000" (设置位置为0)
        sendCommand(hexToBytes("020000")).then(() => {
            
            // 步骤 3: 循环发送数据
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
        // 步骤 4: 发送 "01" (触发显示)
        sendCommand(hexToBytes("01")).then(() => {
            console.log('Update was send');
        })
    }
}
```

### 4.2 命令序列

```
发送: 00 00           // 清空缓冲区 (填充0x00?)
发送: 02 00 00        // 设置写入位置为 0x0000
发送: 03 [19字节]     // 写入第1部分数据
发送: 03 [19字节]     // 写入第2部分数据
...
发送: 03 [剩余数据]    // 写入最后部分
发送: 01              // 触发显示
```

### 4.3 与 epd_ble_service.c 的差异

**epd_ble_service.c 期望的流程** (基于 Image2ESL App):

```
发送: 02 00 00        // 设置写入位置为 0x0000 (2字节)
发送: 03 [数据]        // 写入数据
发送: 03 [数据]        // 继续写入
...
发送: 01              // 推送到显示屏
```

**关键差异**:
1. 网页在开始时发送 `00 00`（清空缓冲区），而 Image2ESL App 不发送
2. 网页的 `02` 命令后面跟 3 字节（`02 00 00`），而 epd_ble_service.c 期望 3 字节（`02 [高] [低]`）
3. 网页每次发送 19 字节数据，Image2ESL App 发送更多（约 243 字节）

**兼容性问题**:
- 如果网页使用的是 OTA Service (0000221f)，则**与 epd_ble_service.c 完全无关**
- 如果网页切换到 EPD Service (13187b10)，则：
  - `02 00 00` 会被正确解析为位置 0
  - `03 [数据]` 可以正常工作
  - `00 00` 会将缓冲区填充为 0x00
  - `01` 会触发显示

---

## 五、固件 OTA 升级流程详解

### 5.1 完整升级流程

```javascript
async function sendFile(address, data) {
    startTime = new Date().getTime();
    var inCRC = calculateCRC(data);        // 计算固件 CRC
    
    // 步骤 1: 擦除固件区域 (0x20000 ~ 0x40000)
    await eraseFwArea();
    
    // 步骤 2: 分块发送固件数据
    var part_len = 0x200;  // 512 字节/块
    var addressOffset = 0;
    
    while (data.length) {
        var data_part = data.substring(0, part_len);
        data = data.substring(cur_part_len);
        await sendPart(address + addressOffset, data_part);
        addressOffset += cur_part_len / 2;
    }
    
    // 步骤 3: 刷写 Flash 并验证
    await doFinalFlash(inCRC);
}
```

### 5.2 擦除流程

```javascript
async function eraseFwArea() {
    var fwAreaSize = 0x20000;    // 128KB
    var fwCurAddress = 0x20000;  // 起始地址
    
    // 每次擦除 4KB (0x1000)
    while (fwCurAddress < (0x20000 + fwAreaSize)) {
        await sendCommand(hexToBytes("01" + hex_address));
        fwCurAddress += 0x1000;
    }
}
```

### 5.3 数据发送流程

```javascript
async function sendPart(address, data) {
    var part_len = 480;  // 每次发送 480 字节
    
    // 分小块发送数据
    while (data.length) {
        var data_part = data.substring(0, part_len);
        data = data.substring(part_len);
        await sendCommand(hexToBytes("03" + data_part));  // 写入缓冲区
        await delay(50);
    }
    
    // 设置写入地址
    await sendCommand(hexToBytes("02" + hex_address));
}
```

### 5.4 CRC 计算

```javascript
function calculateCRC(localData) {
    var checkPosistion = 0;
    var outCRC = 0;
    
    // 计算前 256KB (0x40000) 的 CRC16
    while (checkPosistion < 0x40000) {
        if (checkPosistion < localData.length)
            outCRC += Number("0x" + localData.substring(checkPosistion, checkPosistion + 2));
        else
            outCRC += 0xff;  // 补齐 0xFF
        checkPosistion += 2;
    }
    
    return outCRC & 0xffff;  // 返回 16 位 CRC
}
```

### 5.5 最终刷写

```javascript
async function doFinalFlash(crc) {
    // 格式: 07 C0 01 CE ED [2字节CRC]
    await sendCommand(hexToBytes("07C001CEED" + crc));
}
```

**魔术字节**: `C0 01 CE ED` 可能是固件升级的确认标志。

---

## 六、时间同步功能分析

### 6.1 实现代码

```javascript
function setTime(hourOffset) {
    let unixNow = Math.round(Date.now() / 1000) + (60*60*hourOffset);
    var s = new Date(unixNow*1000).toLocaleTimeString("de-DE");
    
    addLog("Setting time : " + s + " : dd" + intToHex4(unixNow));
    rxTxSendCommand(hexToBytes("dd" + intToHex4(unixNow)));
}

function intToHex4(intIn) {
    var stringOut = ("00000000" + intIn.toString(16)).substr(-8);
    return stringOut;
}
```

### 6.2 命令格式

```
格式: DD [4字节Unix时间戳，小端序]
示例: DD 65 4A 3C 2B  // 时间戳 0x2B3C4A65 = 725000805 (1992-12-15)
```

**注意**: 
- 使用 **Custom Main Service** (`00001f10`) 的特征 (`00001f1f`)
- 时间戳为**大端序**存储
- 支持时区偏移（hourOffset）

### 6.3 与 epd_ble_service.c 的关系

`epd_ble_service.c` **不处理时间同步命令**，该命令由**其他 BLE Service 处理**（可能在 `cmd_parser.c` 或其他文件中）。

---

## 七、EPD 型号强制设置功能

### 7.1 实现代码

```javascript
function sendForceEpd(nr) {
    addLog("Forcing EPD : e0" + byteToHex(nr+1));
    rxTxSendCommand(hexToBytes("e0" + byteToHex(nr+1)));
}
```

### 7.2 命令格式

```
格式: E0 [型号编号]
支持的型号:
  E0 01 = BW213   (2.13英寸黑白)
  E0 02 = BWR213  (2.13英寸黑白红)
  E0 03 = BWR154  (1.54英寸黑白红)
  E0 04 = 213ICE  (2.13英寸特殊型号)
  E0 05 = BWR350  (3.5英寸黑白红)
  E0 06 = BWY350  (3.5英寸黑白黄)
```

### 7.3 与 epd_ble_service.c 的关系

`epd_ble_service.c` **不处理 EPD 型号设置命令**，该命令由**其他 BLE Service 处理**。

---

## 八、网页架构分析

### 8.1 核心变量

```javascript
let bleDevice;                    // BLE 设备对象
let gattServer;                   // GATT 服务器
let Theservice;                   // OTA Service (0000221f)
let ServiceMain;                  // Main Service (00001f10)
let settingsCharacteristics;      // Main Service 的特征
let writeCharacteristic;          // OTA Service 的特征
let busy = false;                 // 忙碌标志
let imgArray;                     // 图像数据数组
let imgArrayLen = 0;              // 图像数据长度
let uploadPart = 0;               // 上传进度
let startTime = 0;                // 开始时间
let reconnectTrys = 0;            // 重连次数
```

### 8.2 连接流程

```
1. preConnect()
   ├─ navigator.bluetooth.requestDevice()
   │  └─ 请求访问 BLE 设备
   │
2. connect()
   ├─ bleDevice.gatt.connect()
   ├─ getPrimaryService('0000221f...')  // OTA Service
   ├─ getCharacteristic('0000331f...')   // Write Characteristic
   ├─ startNotifications()               // 启用通知
   └─ connect_to_rxtx()
      │
3. connect_to_rxtx()
   ├─ getPrimaryService('00001f10...')  // Main Service
   └─ getCharacteristic('00001f1f...')   // Settings Characteristic
```

### 8.3 命令发送函数

```javascript
// 通过 OTA Service 发送命令
async function sendCommand(cmd) {
    if (writeCharacteristic) {
        await writeCharacteristic.writeValue(cmd);
    }
}

// 通过 Main Service 发送命令
async function rxTxSendCommand(cmd) {
    if (settingsCharacteristics) {
        await settingsCharacteristics.writeValue(cmd);
    }
}
```

### 8.4 通知处理

```javascript
function handleNotify(data) {
    addLog("Got bytes: " + bytesToHex(data.buffer));
}

// 监听通知
writeCharacteristic.addEventListener('characteristicvaluechanged', event => {
    var value = event.target.value;
    handleNotify(value);
});
```

---

## 九、总结与建议

### 9.1 功能总结

| 功能 | 使用的 Service | 命令格式 | epd_ble_service.c 兼容性 |
|------|---------------|---------|-------------------------|
| **固件 OTA 升级** | OTA Service (0000221f) | `01~07` | ❌ 不兼容 |
| **图像上传** | OTA Service (0000221f) | `00, 02, 03, 01` | ⚠️ Service 不同，命令部分相似 |
| **时间同步** | Main Service (00001f10) | `DD [时间戳]` | ❌ 不在 epd_ble_service.c 处理 |
| **EPD 型号设置** | Main Service (00001f10) | `E0 [型号]` | ❌ 不在 epd_ble_service.c 处理 |
| **Flash/RAM 读取** | OTA Service (0000221f) | `04, 05` | ❌ 不兼容 |

### 9.2 关键发现

1. **Service 隔离**: 网页主要使用 **OTA Service**，而 `epd_ble_service.c` 定义的是 **EPD 图像服务**，两者是**完全独立**的 BLE Service。

2. **命令重叠但语义不同**: 网页的 OTA 命令（`01~07`）与 `epd_ble_service.c` 的图像命令（`00~05`）编号部分重叠，但**功能完全不同**：
   - OTA 命令用于**固件升级**（操作 Flash）
   - EPD 命令用于**图像显示**（操作 EPD 缓冲区）

3. **地址格式差异**: 
   - 网页 OTA 命令使用 **4 字节地址**（Flash 地址）
   - epd_ble_service.c 使用 **2 字节地址**（缓冲区偏移）

4. **图像上传可能兼容**: 网页的 `sendimg()` 函数虽然使用 OTA Service，但如果切换到 EPD Service，命令序列**可能兼容**：
   - `02 00 00` → 设置位置
   - `03 [数据]` → 写入数据
   - `01` → 触发显示

### 9.3 兼容性建议

#### 如果需要网页与 epd_ble_service.c 兼容：

1. **修改网页连接的 Service UUID**:
   ```javascript
   // 将
   '0000221f-0000-1000-8000-00805f9b34fb'
   // 改为
   '13187b10-eba9-a3ba-044e-83d3217d9a38'
   
   // Characteristic 也需修改
   '0000331f-0000-1000-8000-00805f9b34fb'
   // 改为
   '4b646063-6264-f3a7-8941-e65356ea82fe'
   ```

2. **修改 `sendimg()` 函数的命令格式**:
   ```javascript
   // 移除 "0000" 命令（或改为 "0000" 填充0x00）
   // 保留 "020000" (设置位置)
   // 保留 "03[数据]" (写入数据)
   // 保留 "01" (显示)
   ```

3. **调整数据分块大小**: 从每次 19 字节改为更大的块（如 243 字节），以提高传输效率。

4. **添加对 EPD Service 的支持**: 在网页中添加选项，允许用户选择使用 OTA Service 还是 EPD Service。

#### 如果保持当前架构：

- 网页专注于**固件 OTA 升级**
- 使用 Image2ESL App 或其他工具进行**图像上传**
- 两者互不干扰，各司其职

### 9.4 推荐使用场景

| 工具 | 适用场景 | BLE Service |
|------|---------|------------|
| **Web Bluetooth HTML** | 固件升级、时间同步、EPD型号设置、调试 | OTA Service (0000221f) + Main Service (00001f10) |
| **Image2ESL App** | 图像上传、日常使用 | EPD Service (13187b10) |
| **epd_ble_service.c** | 处理图像数据上传和显示 | EPD Service (13187b10) |

---

## 十、附录：完整命令参考表

### 10.1 OTA Service 命令（网页使用）

| 命令 | 格式 | 参数 | 功能 | 返回值 |
|------|------|------|------|--------|
| `0x01` | `01 [地址]` | 4字节地址 | 擦除 Flash 块 | 无 |
| `0x02` | `02 [地址]` | 4字节地址 | 设置写入位置 | 无 |
| `0x03` | `03 [数据]` | 可变长度数据 | 写入数据到缓冲区 | 无 |
| `0x04` | `04 [地址]` | 4字节地址 | 读取 Flash | Flash 数据 |
| `0x05` | `05 [地址]` | 4字节地址 | 读取 RAM | RAM 数据 |
| `0x06` | `06` | 无 | CRC 校验 | CRC 结果 |
| `0x07` | `07 C0 01 CE ED [CRC]` | 2字节CRC | 最终刷写 Flash | 刷写结果 |

### 10.2 EPD Service 命令（epd_ble_service.c）

| 命令 | 格式 | 参数 | 功能 | 实现函数 |
|------|------|------|------|---------|
| `0x00` | `00 [值]` | 1字节填充值 | 清空 EPD 缓冲区 | `memset(epd_buffer, val, size)` |
| `0x01` | `01` | 无 | 推送缓冲区到显示屏 | `EPD_Display(epd_buffer, ...)` |
| `0x02` | `02 [高] [低]` | 2字节位置 | 设置写入位置 | `byte_pos = (h<<8)|l` |
| `0x03` | `03 [数据]` | 可变长度数据 | 写入数据到缓冲区 | `memcpy(epd_buffer+pos, data, len)` |
| `0x04` | `04` | 无 | 解码并显示 TIFF | `epd_display_tiff(...)` |
| `0x05` | `05` | 无 | 显示位图（含旋转） | `FixBuffer(); EPD_Display()` |

### 10.3 Main Service 命令（网页使用）

| 命令 | 格式 | 参数 | 功能 | 实现位置 |
|------|------|------|------|---------|
| `0xDD` | `DD [时间戳]` | 4字节Unix时间 | 设置时间 | 未知（可能在 cmd_parser.c） |
| `0xE0` | `E0 [型号]` | 1字节型号编号 | 强制 EPD 型号 | 未知（可能在 cmd_parser.c） |

---

## 十一、代码示例：修改网页以兼容 EPD Service

### 11.1 修改连接代码

```javascript
// 原代码
navigator.bluetooth.requestDevice({ 
    optionalServices: [
        '0000221f-0000-1000-8000-00805f9b34fb',  // OTA Service
        '00001f10-0000-1000-8000-00805f9b34fb',  // Main Service
        '13187b10-eba9-a3ba-044e-83d3217d9a38'   // EPD Service
    ], 
    acceptAllDevices: true 
});

// 修改为使用 EPD Service
bleDevice.gatt.connect().then(server => {
    gattServer = server;
    return gattServer.getPrimaryService('13187b10-eba9-a3ba-044e-83d3217d9a38');  // EPD Service
})
.then(service => {
    Theservice = service;
    return Theservice.getCharacteristic('4b646063-6264-f3a7-8941-e65356ea82fe');  // EPD Char
})
.then(characteristic => {
    writeCharacteristic = characteristic;
    // ...
});
```

### 11.2 修改 sendimg() 函数

```javascript
function sendimg(cmdIMG) {
    imgArray = cmdIMG.replace(/(?:\r\n|\r|\n|,|0x| )/g, '');
    imgArrayLen = imgArray.length;
    uploadPart = 0;
    
    // 步骤 1: 清空缓冲区（填充0x00）
    sendCommand(hexToBytes("0000")).then(() => {
        
        // 步骤 2: 设置写入位置为 0 (2字节地址)
        sendCommand(hexToBytes("020000")).then(() => {  // 这里实际发送的是 02 00 00，epd_ble_service.c 会正确解析
            
            // 步骤 3: 循环发送数据（可以增加每次发送的数据量）
            sendIMGpart();
        })
    })
}

function sendIMGpart() {
    if (imgArray.length > 0) {
        // 增加到每次 240 字节 (480个十六进制字符)
        let currentpart = "03" + imgArray.substring(0, 480);
        imgArray = imgArray.substring(480);
        setStatus('Current part: ' + uploadPart++);
        sendCommand(hexToBytes(currentpart)).then(() => {
            sendIMGpart();
        })
    } else {
        // 步骤 4: 推送到显示屏
        sendCommand(hexToBytes("01")).then(() => {
            console.log('Image was sent to display');
        })
    }
}
```

### 11.3 添加清空缓冲区功能

```javascript
function clearBuffer(fillValue = 0x00) {
    // 格式: 00 [填充值]
    let cmd = hexToBytes("00" + byteToHex(fillValue));
    sendCommand(cmd).then(() => {
        console.log('Buffer cleared with value: 0x' + byteToHex(fillValue));
    });
}

// 添加 UI 按钮
<button type="button" onclick="clearBuffer(0x00);">Clear Buffer (Black)</button>
<button type="button" onclick="clearBuffer(0xFF);">Clear Buffer (White)</button>
```

---

## 结论

1. **网页与 epd_ble_service.c 使用不同的 BLE Service，当前不兼容**
2. **网页主要用于固件 OTA 升级，epd_ble_service.c 用于图像显示**
3. **命令格式虽然部分相似，但语义完全不同**
4. **如需兼容，需修改网页的 Service UUID 和部分命令格式**
5. **建议保持当前架构，让不同工具各司其职**

---

**文档版本**: 1.0  
**生成日期**: 2024  
**作者**: GitHub Copilot  
**相关文件**: 
- `Firmware\WebBluetooth Firmware OTA Flashing.html`
- `Firmware\src\epd_ble_service.c`
- `docs\BLE_Image_Transfer_Flow.md`
