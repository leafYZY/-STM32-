# 车库蓝牙助手（GarageBLE）

用手机替代 STM32 蓝牙遥控器，通过 HC-05 连接你的「车库系统」并完成全部遥控功能。

- **不动硬件、不改车库业务逻辑**：APP 发出的字节序列与 `蓝牙遥控器5` 工程里 `Serial_SendString()` 发出的完全一致。
- 车库固件只加了一条**状态查询指令** `@Q#*`（可选，打了补丁 APP 首页才能实时显示闸门/模式/空位）。

---

## 1. 通信协议（从两个工程的源码里对齐出来的）

链路：`手机 APP ──蓝牙SPP──► HC-05(从机) ──USART2 PA2/PA3──► STM32F103C8T6 车库`

串口参数 **9600 / 8 / N / 1**，帧格式 **`@` + 载荷 + `#` + `*`**。

| 功能 | APP / 遥控器发送 | 车库回复 | 对应的车库源码 |
|---|---|---|---|
| 开闸 | `@K#*` | `@KOK#*` | `main.c` `strcmp(BLE_RxPacket,"K")` |
| 关闸 | `@G#*` | `@GOK#*` | `strcmp(...,"G")` |
| 自动模式 | `@A#*` | `@AOK#*` | `strcmp(...,"A")` → `work_mode = 1` |
| 手动模式 | `@M#*` | `@MOK#*` | `strcmp(...,"M")` → `work_mode = 0` |
| 语音音量 | `@V050#*` | `@VOK#*` | `BLE_RxPacket[0]=='V'` → `atoi(+1)` |
| 车位检测器 | `@S1=1#*` | `@SOK#*` | `[1]-'0'` 取编号、`[3]-'0'` 取开关 |
| **状态查询（新增）** | `@Q#*` | `@Q1205011111#*` | 本工程新增分支 |
| 停车记录（车库主动推送） | — | `@P<车牌>\|<HHMMSS>\|<车位>#*` | 扫码开闸后 `BLE_Serial_SendString()` |

**状态回复位定义**（`@Q` 之后共 10 个字符）

| 位置 | 含义 | 取值 |
|---|---|---|
| 1 | 工作模式 | `1`=自动、`0`=手动 |
| 2 | 闸门状态 | `0`=关闸(红灯)、`1`=等待(黄灯)、`2`=开闸(绿灯) |
| 3~4 | 剩余车位数 | `00` ~ `06` |
| 5~10 | 6 路车位检测器 | 依次对应 1~6 号，`1`=启用 `0`=禁用 |

> 举例：`@Q1205011111#*` = 自动模式 + 已开闸 + 剩 5 个车位 + 6 路检测器全开。

车位编号换算（与遥控器 `GetSpaceText()` 一致）：`1~3` → **L1-1~L1-3**，`4~6` → **L2-1~L2-3**。

---

## 2. 三步跑起来

### 前置：装 Android Studio

下载 **Android Studio Ladybug (2024.2) 或更新版本**（安装包里自带 JDK 和 SDK，不用单独装 Java）。

### 方式 A（推荐）：直接打开本工程

1. Android Studio → `File` → `Open…` → 选中本文件夹（**GarageBLE**，不是里面某个子目录）。
2. 首次打开会提示同步（Sync）。它会自动下载 Gradle 8.11.1 和依赖库，等进度条走完。
   - 如果弹窗问「Trust Project」→ 选 **Trust**。
   - 如果需要 `local.properties`，AS 会自动生成，不用管。
3. 手机用 USB 连电脑（开启「开发者选项 → USB 调试」），点绿色 ▶ **Run** 直接装到手机。

### 方式 B：如果方式 A 的同步报错

在 Android Studio 里新建一个空工程（`New Project` → **Empty Views Activity**），
模板参数随便填（建议 Package name 填 `com.yeziyi.garageble`，语言选 **Kotlin**），
创建成功后把本工程的文件覆盖过去：

```
覆盖 app/src/main/java/com/yeziyi/garageble/   ← 8 个 .kt 文件
覆盖 app/src/main/res/                         ← 全部 layout / values / menu / drawable
覆盖 app/src/main/AndroidManifest.xml
覆盖 app/build.gradle.kts
覆盖 build.gradle.kts
覆盖 gradle.properties
```

然后 `File` → `Sync Project with Gradle Files`。

### 出 APK 装到手机上（不用数据线）

菜单 `Build` → `Build Bundle(s) / APK(s)` → `Build APK(s)`，
产物在 `app/build/outputs/apk/debug/app-debug.apk`，
传到手机上点击安装（需要在系统里允许「安装未知来源应用」）。

> 注意：**命令行 `./gradlew` 用不了**——本工程包里没有 `gradle-wrapper.jar`（二进制文件没法用文本方式生成）。
> 这不影响 Android Studio 的同步和 Run：AS 自己会按 `gradle-wrapper.properties` 去下载对应 Gradle。
> 只有非要用命令行时，先在项目根目录执行一次 `gradle wrapper` 把 jar 补出来。

---

## 3. APP 功能一览

| 区域 | 功能 | 说明 |
|---|---|---|
| 顶部 | 连接状态 | 红灯=未连接、绿灯=已连接，并显示设备名 |
| 设备页 | 已配对 + 扫描设备列表 | 点一下即连接；未配对的会先发起配对（PIN `123456`） |
| 状态卡 | 闸门 / 模式 / 剩余车位 | 每 2 秒自动 `@Q#*` 轮询，与车库真实状态同步 |
| 闸门卡 | 开闸 / 关闸 | 两个大按钮，发 `@K#*` / `@G#*` |
| 模式卡 | 自动 / 手动 | 分段按钮，发 `@A#*` / `@M#*`，轮询后自动回显真实模式 |
| 音量卡 | 滑条 0~100（步长 5） | 松手才发送，`@V050#*` |
| 检测器卡 | 6 路开关 + 全部启用/禁用 | `@S1=1#*`，轮询后自动回显真实开关状态 |
| 记录卡 | 停车记录列表 | 车库扫码开闸后主动推 `@P…`，APP 自动解析并本地留存（最多 100 条） |
| 底部 | 运行日志 | 收发原始帧 + 中文翻译，红色为错误，可折叠/清空 |

额外的小设计：
- 记住上次连接的设备 MAC，「重连」一键重连。
- 先试**安全通道** SPP，失败自动退回**非安全通道**（部分 HC-05 固件在 Android 10+ 上需要这样兜底）。
- 程序化回显状态时会抑制回调，避免「同步界面」被误当成「用户操作」又发一遍指令。

---

## 4. 车库固件改动（已帮你改好，需重新编译烧录）

改的是 `车库\User\main.c` 和 `车库\Hardware\BLE_Serial.c`，
原文件已备份为 `main.c.bak-20261015`、`BLE_Serial.c.bak-20261015`。

**改动 1 —— 新增 `@Q#*` 状态查询**（`main.c`，蓝牙指令分支末尾）

```c
else if (strcmp(BLE_RxPacket, "Q") == 0)	/*状态查询 @Q#*（手机 APP 用）*/
{
    char st[24];
    sprintf(st, "@Q%c%c%02d%c%c%c%c%c%c#*",
        work_mode ? '1' : '0',
        (char)('0' + (gate_state > 2 ? 0 : gate_state)),
        (int)Get_FreeSpaces(),
        sensor_enable[0] ? '1' : '0', sensor_enable[1] ? '1' : '0',
        sensor_enable[2] ? '1' : '0', sensor_enable[3] ? '1' : '0',
        sensor_enable[4] ? '1' : '0', sensor_enable[5] ? '1' : '0');
    BLE_Serial_SendString(st);
}
```

**改动 2 —— 顺带修掉一个缓冲区溢出隐患**（`BLE_Serial.c` 的 `USART2_IRQHandler`）

原来的接收状态机里 `pRxPacket++` 没有边界检查，一旦收到超过 99 字节的长帧就会越界写坏内存
（`BLE_RxPacket[100]`）。已补上：

```c
else if (pRxPacket < sizeof(BLE_RxPacket) - 1)
{
    BLE_RxPacket[pRxPacket] = RxData;
    pRxPacket++;
}
```

用 Keil 重新编译、烧录一次车库板子即可。**不烧录也能用**，只是 APP 首页的实时状态会是「—」。

---

## 5. 常见问题

**Q：手机搜不到 HC-05？**
- Android 12 以下：必须在系统设置里把「位置信息」总开关打开，否则蓝牙扫描搜不到任何设备（系统限制，不是 APP 的问题）。
- 确认已授予 APP 蓝牙权限；确认 HC-05 上电、指示灯在慢闪（未连接状态）。
- HC-05 出厂模式指示灯是**快闪**，慢闪表示已经和别的设备连着。

**Q：连不上 / 连上就断？**
- 先在**系统蓝牙设置**里和 HC-05 配对一次（密码 `123456`）。你的从机 MAC 是 `48:87:7F:3B:2E:F3`。
- **重要：HC-05 从机同一时刻只能被一个主设备连接。** 如果实体遥控器已经连上车库，
  手机就连不上。用手机 APP 时请把遥控器关掉（或断开其蓝牙），二者不能同时在线。
- 连接失败后等 2~3 秒再试；HC-05 换主设备后有时需要断电重启。

**Q：提示已连接，但按开闸没反应？**
- 看底部日志有没有「发送 开闸 → `@K#*`」。有发送、没回复，多半是遥控器还占着连接。
- 车库板上 USART2 的 PA2/PA3 和 HC-05 的 RXD/TXD 是交叉接的，原来能跑通就不用动。

**Q：状态卡一直是「—」？**
- 说明车库固件没打 `@Q#*` 补丁（没重新烧录），或者车库端收到了但没回。其余功能不受影响。

**Q：手机 APP 能替代遥控器，那遥控器的 SD 卡记录怎么办？**
- 两者的停车记录是各自独立的：遥控器写自己的 SD 卡，APP 存在手机本地。
  两边都会收到车库推送的 `@P…` 帧，数据来源相同。

---

## 6. 工程结构

```
GarageBLE/
├── settings.gradle.kts            Gradle 仓库与模块声明
├── build.gradle.kts               插件版本（AGP 8.7.3 / Kotlin 2.0.21）
├── gradle.properties
├── gradle/wrapper/gradle-wrapper.properties   Gradle 8.11.1
└── app/
    ├── build.gradle.kts           compileSdk 34 / minSdk 26 / targetSdk 34 / ViewBinding
    └── src/main/
        ├── AndroidManifest.xml    蓝牙权限（区分 API≤30 与 API≥31）
        ├── java/com/yeziyi/garageble/
        │   ├── Protocol.kt            协议封装：命令生成 + 回复解析（含 @Q 状态位）
        │   ├── FrameParser.kt         '@载荷#*' 接收状态机（与固件同构）
        │   ├── BluetoothSppManager.kt 经典蓝牙 RFCOMM 连接/收发/扫描/广播
        │   ├── ParkingRecord.kt       停车记录数据模型
        │   ├── DeviceAdapter.kt       设备列表
        │   ├── LogAdapter.kt          运行日志
        │   ├── RecordAdapter.kt       停车记录列表
        │   └── MainActivity.kt        界面与全部交互逻辑
        └── res/
            ├── layout/           activity_main + 3 个 item 布局
            ├── menu/main_menu.xml
            ├── values/           colors / strings / themes
            ├── drawable/         自适应图标（矢量，无需 PNG）
            └── mipmap-anydpi-v26/ 图标声明
```

---

## 7. 为什么不用 Web Bluetooth / 网页 APP？

HC-05 是**蓝牙 2.x 经典模块**，只提供 SPP（串口）服务；而浏览器里的 Web Bluetooth 只支持 **BLE（GATT）**。
两者不是一回事，网页 APP 原理上就连不上 HC-05。所以要么用原生 Android 走 `BluetoothSocket` + RFCOMM
（本工程的做法），要么先把模块换成 BLE 型号（HM-10 / JDY-31 / AT-09）。
