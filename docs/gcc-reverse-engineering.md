# GIGABYTE CONTROL CENTER 逆向笔记

对 `dist/` 里 GCC 安装包的静态逆向记录。目的有两个：核对我们的报文实现，以及
搞清楚"从 Windows 重启到 Linux 后灯光不受控"到底是什么机制。

复现方法：

```sh
7z x GCC_..._Setup_....exe                       # 取出 GBT_rgbMotherboard_UC_*.exe
7z x GBT_rgbMotherboard_UC_*.exe -orgbmb         # RgbMotherboard.dll 等
7z x "GIGABYTE Control Center_*.exe" -omain      # RgbCommon.dll / GHidApi.dll / SMBCtrl.dll
ilspycmd -o cs rgbmb/RgbMotherboard.dll          # .NET 程序集反编译成 C#
```

`RgbMotherboard.dll`、`RgbCommon.dll`、`LedIoControl.dll`、`Dynamic_Lighting_Fun_Lib.dll`
都是 .NET；`GHidApi.dll`、`SMBCtrl.dll` 是原生 x86-64，用 radare2。

## 命令表（`MCU_8297`）

| 命令 | GCC 方法 | 报文 |
| --- | --- | --- |
| `0x60` | `GetMcuInfo()` | `CC 60`，读回 64 字节 `IT8297_Info` |
| `0x20+i` | 区域效果 / `ClearIT8297Parameter()` | `CC 2i` + `DataFormat_8297` |
| `0x28` | `Apply(mask)` | `CC 28 <mask>`，IT5711+ 还写 byte 3 |
| `0x31` | `Beat_Enable()` | 音频律动 |
| `0x47` | `SaveLedParameter(en)` | KeepLedSetting 开关，受 `SuppCmdFlag` bit0 门控 |
| `0x48` | `get/set_MSDLSwState()` | MSDL 开关，受 `SuppCmdFlag` bit1 门控 |
| `0x90..0x92` | `ClearIT8297Parameter()` | 仅 IT5711+ 的扩展区域寄存器 |

`SuppCmdFlag` 的两个位在 `GetMcuInfo()` 里被读成 `bSuppKeepLedSetting`（bit0）和
`bSuppMSDLSwitch`（bit1）。B760M AORUS ELITE 上读到 `0x03`，两个都支持。

### `DataFormat_8297`

字段顺序和我们 `buildZoneReport()` 构造的完全一致，偏移从报文的 byte 2 起算：

```
Zone_Sel0 u32 | Zone_Sel1 u32 | Reserve0 u8 | Mode_Sel u8
MaxBrightness u8 | MinBrightness u8 | dwColor0 u32 | dwColor1 u32
wTime_base0..3 u16 x4 | CtrlVal0 u8 | CtrlVal1 u8 | CtrlOem0 u8 | CtrlOem1 u8
```

### `Apply` 的掩码宽度

```csharp
array[1] = 40;                                   // 0x28
if (iApplyBit == -1) { array[2] = 0xFF;
    if (LedCtrlBy >= LED_CTRL_BY.IT5711) array[3] = 7; }
else { array[2] = (byte)iApplyBit;
    if (LedCtrlBy >= LED_CTRL_BY.IT5711) array[3] = (byte)(iApplyBit >> 8); }
```

`LED_CTRL_BY.IT5711 = 1007` 是枚举里最大的一项，而 `GetMcuInfo()` 只在 PID `0x5711`
时走到它 —— 所以宽掩码等价于"PID == 0x5711"。老芯片上 byte 3 必须保持 0。

### 夺回控制权的时序

`Collection_8297.EasySetMode()` 的顺序是 `ClearParameter(0)` → 停止主机侧效果线程
→ `SetLedEffect(..., bApplyNow: true)`。而 `ClearIT8297Parameter(from)` 本身是：

```
for i in from..7:  写 CC (0x20+i) 全零
IT5711+:           写 CC 0x90..0x92 全零
Apply(-1)
SpinWait 100 ms
```

注意它**不发 `0x31` / `0x48`** —— 那两条只在 UI 显式操作时才走（`Apply()` 的
case 10/11）。我们在 `takeOverZones()` 里顺手发它们，A/B 实测无害，但确实不是
GCC 的做法。

## HID 传输层（`GHidApi.dll`）

`fcn.1800038e0(handle, mode, buf, len)` 用 `mode` 选 API：

```
0 = HidD_GetFeature   1 = HidD_SetFeature   2 = HidD_GetInputReport
3 = HidD_SetOutputReport   4 = ReadFile   5 = WriteFile
```

`ReadDataFromDevice` 传 mode 0，`WriteDataToDevice` 传 mode 1。也就是说
`HIDIOCGFEATURE` / `HIDIOCSFEATURE` 和 GCC 是一一对应的。

**`get_MSDLSwState()` 是坏的**，别照抄：

```csharp
array[0] = 204; array[1] = 72;                    // CC 48
result = InvkGHidApi.ReadDataFromDevice(...);     // 纯 GetFeature
if (result == 1) result = array[1];
```

`HidD_GetFeature` 只把报文 ID 送上线，`array[1] = 0x48` 根本不出去，读回来的是设备
当前的响应缓冲（即 INFO 报文），取 `array[1]` 得到的是 `Product` 字段。实测在
"正常"和"MSDL 锁死"两种状态下都返回 `0x01`，**没有区分能力**。

## BIOS / SMI 那条路：不要走

`SMBCtrl.dll` 里有一组 `*ToBios` 导出，看着像是另一条控制通道，实际上：

- `SetLedModeToBios(v)` 只是往内存里的 Setup 结构写值并置脏标记
  （失败日志：`pAzaliaSetupAttr is NULL`），**不产生任何总线操作**。
- `SaveToBios()` 才落盘，方式是触发 **SMI 命令 `0xB265`**
  （日志：`SMI Command 0xb265 done.`），由 BIOS 的 SMM 处理程序完成写入。
- 前提是加载 ring-0 驱动 **`gdrv3`**；SMBus 侧另有互斥量
  `Global\Access_SMBUS.HTP.Method`。

三个理由不在 Linux 上复刻：敲 `0xB2` 端口需要自写内核模块；SMI 处理程序的参数
约定未公开且随 BIOS 版本变化，传错不是"无效果"而是让 SMM 拿错误输入执行，有损坏
NVRAM 的风险；没有安全的验证手段。

**而且它多半也不解决问题**：这批导出管的是电源状态下的灯光行为
（`SetLedPwrStateToBios` / `SetLedPwrOnStateToBios` / `GetLedModeSetupData`，即
BIOS Setup 里"睡眠/关机时 LED 亮不亮"那类选项），而 MSDL 锁死是运行时"谁占着 LED
输出所有权"，状态在 MCU 里而不在 BIOS NVRAM 里。

同理，`IT8295_Block_RW` / `I2C_Block_RW` 是给 **SMBus 挂载的 IT8295** 用的；
B760M AORUS ELITE 上的 IT5701 走 USB，不在那条总线上。

## GCC 怎么在 Windows 上"恢复"灯光

用户观察到 Windows→Linux→Windows 之后 GCC 能正常控灯，一度像是"存在某条我们没找到
的设备命令"。不是。`RgbMotherboard.dll` 里 `SetFwMSDLSwitchState` 只被 UI 调度器
（`Apply()` 的 case 11）调用，常规灯效流程从不碰 `0x48`；`GetMcuInfo()` 读回来的
`iMsdlSw` 只有 getter，**不参与任何判断**。

真正的动作在 `Dynamic_Lighting_Fun_Lib.dll`，而且是操作系统层面的：

```csharp
// 写 HKCU\Software\Microsoft\Lighting\Devices\<USB_ID>\AmbientLightingEnabled
registryKey2.SetValue(Key_name, Status_value, RegistryValueKind.DWord);
```

GCC 把某设备的 `AmbientLightingEnabled` 置 0，Windows 灯光服务随之放开 LampArray，
设备回到自主模式。Linux 上没有这个服务，也就没有对应的"放开"动作 —— 这解释了为什么
遍历设备命令找不到出路。

### 由此引出的待验证假设

把 `AutonomousMode=1` 发下去时，板子**回到了 GCC 的效果**，说明 LampArray 侧确实已经
释放、设备已回自主模式。那么挡住 Fusion 写入的就是别的东西，最可能是 `0x47`
KeepLedSetting 被留在了开启状态 —— 症状（全 ACK、保持既有效果、写入无效）完全吻合。

之前唯一一次发 `0x47` 的顺序是：stage → apply → `0x47 0` → `AutonomousMode=1`，
**暂存发生在 KeepLedSetting 还开着的时候**，之后再没重新暂存过。"先关 `0x47`，再
stage，再 apply"这个顺序从未测过，而它正是现在 `takeOverZones()` 的顺序。

下次从 Windows 回来时用当前版本跑一次 `--restore` 即可证伪或证实。

## 尚未解决

从 Windows 热重启后控制器停在 MSDL 模式：Fusion 报文全部被 ACK 但不执行，只有标准
LampArray 接口还能驱动 LED，且该状态能扛过普通关机（芯片吃 +5VSB），只有切断市电
能清除。已排除：`0x31`、`0x48`（含各种参数与翻转）、`0x47`、重发 `0x60`、逐区
apply 掩码、LampArray `AutonomousMode` 翻转。设备侧也找不到可区分两种状态的可读位，
所以程序无法自检，`lampFallback` 只能做成手动开关。
