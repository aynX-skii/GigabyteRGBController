# GigabyteRGBController

Gigabyte 主板灯光控制，Linux / Qt6 (Qt Quick) 实现。

技嘉官方的 GIGABYTE CONTROL CENTER (GCC) 只有 Windows 版本。本项目通过逆向
其 RGB 控制模块与真实硬件的 HID 接口，在 Linux 上提供等价的灯光控制。

**依赖只有 Qt6 和 CMake。** 不需要 hidapi、libusb 或任何内核模块——直接通过
`ioctl()` 与 `/dev/hidrawN` 通信。

## 支持的硬件

ITE Tech. (VID `048d`) RGB 控制器：

| PID    | 芯片      | 典型主板 |
| ------ | --------- | -------- |
| `5702` | IT5702    | B760M AORUS ELITE、Z490 Vision D 等 |
| `8297` | IT8297    | X570 AORUS ELITE 等 |
| `5711` | IT5711    | 较新的 AORUS 主板 |
| `7900` | IT8297FN  | |
| `8950` | IT82950   | GIGABYTE 官方客户端也探测这颗 |

开发与验证平台：**Gigabyte B760M AORUS ELITE**，控制器 `048d:5702`
（芯片自报 `IT5701-GIGABYTE V3.5.15.0`，`ChipId = 0x57010100`）。

已在该平台真机验证：设备识别、信息报文解析、静态/呼吸效果、分区控制、区域映射。

## 两套控制通道

同一颗 IT5702 暴露**两个独立的 hidraw 节点**，本项目都支持：

| 通道 | 接口 | 特点 |
| ---- | ---- | ---- |
| **RGB Fusion 2.0** | 厂商自定义 Usage Page `0xFF89` / Usage `0xCC` | 8 个区域，**板载硬件效果**（呼吸/闪烁/循环），设置后脱离主机独立运行 |
| **HID LampArray** | 标准 Usage Page `0x59`（Lighting And Illumination） | 标准协议，主机实时驱动，最小刷新间隔 300 µs |

LampArray 就是 Windows 11 "动态照明" 所用的标准接口——GCC 安装包里的
`Dynamic_Lighting_Fun_Lib.dll` 正是通过 `Windows.Devices.Lights` 走这条路。

**注意粒度**：LampArray 协议本身支持逐灯寻址与微米级坐标，但 B760M AORUS ELITE
上的这颗芯片实测 `LampCount = 1`——整块板子只暴露**一个**逻辑灯
（类型 `机箱`，用途 `氛围点缀`，外框 10000×0×0 µm）。也就是说在本平台上
LampArray 比厂商协议的 8 区域**更粗**，它的价值在于协议标准、便于做主机侧
实时同步；要分区控制仍应使用 RGB Fusion 通道。不同型号主板的 `LampCount`
可能不同，请以 `--probe` 实测为准。

## 构建

需要 Qt 6.5 以上，模块 `Core` `DBus` `Gui` `Qml` `Quick` `QuickControls2`
`QuickLayouts`（Debian/Ubuntu：`qt6-base-dev` `qt6-declarative-dev`）。
`DBus` 只用来听 logind 的睡眠/唤醒信号，它随 qtbase 一起来，不是额外的包。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 安装

`cmake --install` 的参数是**构建目录**，不是安装目录；装到哪由
`CMAKE_INSTALL_PREFIX` 决定，默认 `/usr/local`：

```bash
sudo cmake --install build
```

会装五样东西，落点都在系统默认会扫描的路径下（`/usr/local/lib/udev/rules.d`、
`/usr/local/lib/systemd/{system,user}`、`XDG_DATA_DIRS` 里的 `/usr/local/share`
——可以用 `systemd-analyze unit-paths` 和 `man udev` 自行核对）：

| 文件 | 落点 | 作用 |
| ---- | ---- | ---- |
| 程序 | `bin/` | 也是两个 systemd 单元里 `ExecStart` 写死的路径 |
| `99-rgbfusion2.rules` | `lib/udev/rules.d/` | hidraw 权限 |
| `GigabyteRGBController.desktop` | `share/applications/` | 应用菜单 |
| `GigabyteRGBController.svg` | `share/icons/hicolor/scalable/apps/` | 图标 |
| 两个 `.service` | `lib/systemd/{user,system}/` | 登录 / 唤醒后恢复，**装而不启用** |

**文件到位不等于生效**，三样各自要激活一次：

```bash
sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=hidraw
systemctl --user daemon-reload && systemctl --user enable --now GigabyteRGBController-restore.service
sudo systemctl daemon-reload   && sudo systemctl enable GigabyteRGBController-resume@$USER.service
```

图标那一步不能省：Wayland 靠应用 ID（取自 `QGuiApplication::setDesktopFileName()`）
反查 desktop 入口，再由入口的 `Icon=` 反查图标——少一环任务栏就只能显示一个通用
方块。

## 测试

协议的全部价值就是精确的字节偏移，一个偏移写错只会表现为"灯看起来怪怪的"。
因此报文构造与解析被拆成了纯函数（`RgbFusion2::buildZoneReport` 等），
可以脱离硬件断言每一个字节：

```bash
ctest --test-dir build --output-on-failure
```

两个套件共 56 项，不需要硬件也不需要显示服务器。协议部分 36 项，其中
`parsesRealInfoReport` 用的是从本机 IT5701 抓下来的真实 64 字节应答，
是唯一独立于实现的判据；配置部分 20 项，覆盖 `zones.ini` 的往返、方案的
增删改与命名边界、自定义色板与窗口几何的互不干扰，以及切换灯效时的亮度上限
收敛（`ZoneSetting::setMode`）。配置测试跑在 `QStandardPaths` 的测试模式下，
写不到真实的 `~/.config`——第一条断言就在盯这件事。

测试写完后做过变异验证——故意引入 6 个错误（颜色字节序改成 RGB、
`MinBrightness` 偏移错位、时序块错位、双闪次数写成 1、`ProductString`
字段宽度写成 32、灯带头命令对调），全部被对应用例拦截。

## 权限设置

控制器的 hidraw 节点默认只有 root 可访问。安装 udev 规则后即可以普通用户运行
（跑过 `sudo cmake --install build` 的话规则已经在 `/usr/local/lib/udev/rules.d/`
了，只需要下面后两条重新加载）：

```bash
sudo cp udev/99-rgbfusion2.rules /etc/udev/rules.d/   # 未安装时手动放一份
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

规则使用 `TAG+="uaccess"`，权限授予当前本地登录会话的用户，比固定 group 更安全。

没装规则时程序不会只甩一句 `Permission denied`——识别到 `EACCES` 会把上面这三条
命令直接写进**设备栏**（红色那行，连接指示灯变红时你会看的地方）、状态栏和协议
日志。太长显示不下的部分悬停可见。

## 使用

```bash
./build/GigabyteRGBController                  # 图形界面
./build/GigabyteRGBController --probe          # 无界面自检
./build/GigabyteRGBController --set ...        # 无界面设置灯效
./build/GigabyteRGBController --profile 夜间   # 无界面套用某个方案
./build/GigabyteRGBController --list-profiles  # 列出方案
./build/GigabyteRGBController --version        # 版本号
```

**这个程序不需要常驻。** 灯效设好之后是写进控制器的，程序退出灯照样亮——所以它
没有托盘图标，也不该留在后台。真正需要它的是几个"关键时刻"：登录后、休眠唤醒后、
以及你想换个灯效的时候。前两个交给下面的 systemd 单元，第三个用 `--profile`
绑个快捷键就够了。

### 图形界面

Qt Quick 实现，深色 + 橙色强调，分三页：**硬件效果**、**标准接口 (LampArray)**、
**协议日志**。

**效果一栏是数据驱动的。** `Controller::effects()` 把 `RgbFusion2` 已有的
`modeName()` / `modeUsesColor()` / `modeUsesSpeed()` / `maxBrightness()`
一并发布给 QML，左侧图标栏和右侧设置面板都从这份数据渲染——因此
**不同效果的设置项不同**，且不可能出现"界面给了一个控制器会忽略的选项"：

| 效果 | 颜色 | 亮度 | 最小亮度 | 速度 |
| --- | --- | --- | --- | --- |
| 静态 | ✅ | ≤90 | — | — |
| 呼吸 | ✅ | ≤90 | ✅ | ✅ |
| 闪烁 | ✅ | ≤100 | ✅ | ✅ |
| 双闪 | ✅ | ≤100 | ✅ | ✅ |
| 色彩循环 | —（改为显示色相条） | ≤100 | ✅ | ✅ |
| 关闭 | — | — | — | — |

亮度上限随效果切换自动收敛（静态封顶 90，闪烁 100），是 `Mode_Sel` 表里的值，
不是随手定的。

其余几点：

- **窗口边框自绘**：窗口带 `Qt.FramelessWindowHint`，标题栏、最小化/最大化/关闭
  按钮、四边四角的缩放热区都是自己画的，桌面环境不再往这套扁平暗色界面上盖一条
  自己主题的标题栏。拖动与缩放都交给 `startSystemMove()` / `startSystemResize()`
  由合成器执行——这是 Wayland 下唯一可行的做法，也顺带白拿了贴边分屏。
  窗口大小与最大化状态记在配置文件里：边框既然归自己画，就没有会话管理器
  替我们记了（Wayland 不允许应用自己定位窗口，所以坐标只在 X11 下生效）。
- **方案**：页面顶部一排芯片，每个是八个区域的完整快照，一键切换；详见
  下面的「配置持久化」。
- **快捷键**：`Ctrl+1/2/3` 切页、`Ctrl+R` 重新扫描、`Ctrl+Enter` 应用、
  `F11` 最大化、`Ctrl+Q`/`Ctrl+W` 退出、`Esc` 关闭对话框
  （探测向导进行中按 `Esc` 等于「停止」，会把灯效恢复回去）。
- **断线自愈**：每 3 秒检查一次 hidraw 节点还在不在。拔插或休眠唤醒后
  `/dev/hidrawN` 编号会变，旧的 fd 只会开始报错——此时连接指示灯会如实变红，
  设备重新出现后自动重连；开着「自动应用」就顺带把保存的灯效推回去，
  因为重新枚举的控制器里什么都没剩下。休眠唤醒另有一条 logind 信号兜底，
  见下面的「休眠唤醒」。
- **单实例**：第二次启动不会再开一个窗口，而是通过 `/tmp` 下的本地套接字
  请求已在运行的那个显示出来——两份进程会抢同一颗控制器和同一个配置文件。
- **颜色**：HSV 色环（色相=角度，饱和度=半径）+ R/G/B/十六进制输入框 + 预设色板。
  色环故意不带明度轴——在这颗控制器上"多亮"是 `MaxBrightness` 字段，
  再放一个明度轴就会和亮度滑杆打架。
- **自定义色板**：点空位保存当前颜色，右键清除，存在配置文件的 `[custom]` 段。
- **区域**：八个区域各有三种状态——未探测（暗边框）、已探测无灯（斜杠）、
  有灯（显示当前颜色）。"未探测"和"确认没灯"分开画，否则没法知道探测跑没跑过。
  选择是一个位掩码：点击选中单个，**Ctrl+点击**加选/减选，「全部」回到八个全选。
  选中多个时按钮会说「应用到3个区域」。橙色选中环只在**部分选中**时画——
  八个全选时整排都套环等于什么都没说，那个状态由左边的「全部」芯片表示。
- **探测向导**：不再是一串模态框，而是一次一问的浮层，答题时主板始终可见。
- **协议日志**：可按关键字过滤、可只看错误，右上角能把**当前筛选结果**复制到
  剪贴板或导出成文本——贴进 issue 的应该正是屏幕上看到的那些行。
- **自动应用**：默认开，改动经 120 ms 防抖后直接下发，拖滑杆能实时看到效果；
  关掉则回到手动点"应用"。配置落盘另有一层 1 秒防抖：拖一次滑杆没必要把整个
  ini 重写几十遍，退出时会把没写完的那次补上。
- **应用按钮带状态**：有改动没下发时是橙色可点，下发成功后变灰显示"已应用"，
  再改动又亮起——所以它的颜色回答的是"还有没有东西没发出去"。
  刚启动时如果配置里有已管理的区域，它是亮的：完整断电会清空控制器，而控制器
  又没有读回当前效果的命令，此时正是最需要点它的时候。

`--screenshot <文件>` 可把界面直接渲染成 PNG，配合 `-platform offscreen`
不需要显示服务器（会自动切到 Qt Quick 的软件渲染后端）：

```bash
QT_QPA_PLATFORM=offscreen ./build/GigabyteRGBController --screenshot ui.png
```

### 命令行

```bash
GigabyteRGBController --set static --color '#00FF00'
GigabyteRGBController --set breathing --color '#0080FF' --speed 1
GigabyteRGBController --set cycle --speed 3
GigabyteRGBController --set static --color '#FF0000' --zone 2   # 只设区域 2
GigabyteRGBController --set off
```

| 参数 | 说明 |
| ---- | ---- |
| `--color #RRGGBB`    | 颜色，默认 `#FF0000` |
| `--zone N`           | 区域 0–7，默认全部 |
| `--brightness N`     | 亮度 0–100，默认取该模式上限 |
| `--min-brightness N` | 渐变下限亮度（`MinBrightness` 字段），默认 0 |
| `--speed N`          | 速度 0–5，默认 2 |

模式：`off` `static` `breathing` `flash` `dflash` `cycle`。

## 配置持久化与开机恢复

控制器在温重启后会保留设置，但完整断电就丢了，而 Linux 下没有厂商服务把它写回去。

图形界面每次"应用"（开着自动应用时则是每次下发）、以及每次 `--set`，都会把状态
写入 `~/.config/GigabyteRGBController/zones.ini`（遵循 `XDG_CONFIG_HOME`）。`--restore` 重新套用：

```bash
GigabyteRGBController --restore
```

只有**实际设置过**的区域才会被恢复。配置里每个区域带一个 `managed` 标记，
默认 `false`；控制器没有提供读回区域当前效果的命令，所以恢复一个从没写过的
区域只会用编造的默认值覆盖它——因此这些区域会被跳过。

恢复不是简单地把效果写下去就完事。冷启动后控制器还在跑 BIOS 自己的灯效，直接
往上面叠一层效果不一定顶得掉它（在 B760M AORUS ELITE 上的表现是：芯片组那颗灯
一直循环着开机彩虹，其它区域都正常）。所以 `--restore` 会先**夺回控制权**——关掉
硬件节拍模式（`0x31`）和 LampArray 动态照明模式（`0x48`，仅在 `SuppCmdFlag`
置了 `0x02` 时发），把要写的那几个区域的寄存器清零并提交，然后才写真正的效果；
没被 `managed` 标记的区域不参与清零，免得把用户没交给我们的区域弄黑。

效果本身会**连写两遍**。控制器忙的时候偶尔会悄悄丢掉一条 feature report，而协议
没有读回命令、无从确认，开机那一刻正是它最忙的时候；恢复一次开机只跑一遍，
多发一轮是白送的保险。

同一个文件里还有一个 `[custom]` 段，存图形界面的自定义色板。它和区域状态分开存，
因为 `--set` 只重写效果字段，色板必须活下来。

三个全局选项对所有子命令都生效：

| 选项 | 用途 |
| ---- | ---- |
| `--config <文件>` | 指定配置文件 |
| `--user <用户名>` | 改用该用户的配置（`getpwnam` 解析家目录）；唤醒恢复的 root 单元靠它 |
| `--wait <秒>` | 控制器还没枚举出来时轮询等待，到点放弃 |

### 方案（多套灯效）

`[profileN]` 段里存的是**八个区域的整份快照**，界面上方一排芯片就是它们：
点一下切换，「保存」把当前设置写回选中的方案，「另存为」新建，右键芯片删除
（有确认框）。切换方案**不会**自动保存被切走的那一份——方案是你存下来的样子，
不是你后来调成的样子。

方案名存成段内的 `name=` 键而不是段名，所以名字里带 `/`、`=`、空格、方括号
都不会破坏文件（测试里专门有一条盯着这个）。

方案也能脱离界面用——这才是它对"不常驻"这种用法的真正价值：

```bash
GigabyteRGBController --list-profiles      # * 标记当前方案
GigabyteRGBController --profile 夜间       # 套用并记为当前
```

绑到快捷键上，换灯效就是按一下，界面完全不用开。

> 目录名早先是 `gcc-linux`，现已随项目改名为 `GigabyteRGBController`。程序**不会**
> 读旧路径，所以在改名前跑过探测向导的话，那份区域探测结果不会自动带过来。手动迁移：
> `mv ~/.config/gcc-linux ~/.config/GigabyteRGBController`，否则就重跑一遍向导。

登录后自动恢复（`cmake --install` 已经把单元放好了，见上面的「安装」）：

```bash
systemctl --user enable --now GigabyteRGBController-restore.service
```

没装的话，把 `systemd/GigabyteRGBController-restore.service` 拷到
`~/.config/systemd/user/` 再 `systemctl --user daemon-reload` 也一样。

单元里用的是 `--restore --wait 20`：控制器冷启动后要一会儿才枚举出来，写死
`sleep 3` 既可能白等也可能不够，`--wait` 是轮询到设备出现就返回、到点还没有就
放弃（不会把会话卡住）。

用的是 **user unit** 而非 system unit：udev 规则通过 `TAG+="uaccess"` 把
hidraw 节点授权给当前本地会话的用户，root 的 system service 反而拿不到
这份 ACL 语义，跟着图形会话走更自然。

### 休眠唤醒

界面开着的时候不需要额外配置：程序订阅了 logind 的
`org.freedesktop.login1.Manager.PrepareForSleep`，唤醒后等 2 秒把保存的灯效重新
下发（控制器可能在挂起期间断电，而它没有读回当前效果的命令，所以只能重推）。
订阅失败会在协议日志里说一句，不会静悄悄地不工作。

但按上面说的，这个程序本来就不该常驻，所以真正管用的是那个 systemd 单元。它必须
是 **system unit**——`suspend.target` 这些只存在于系统 manager，用户 manager 里
没有（systemd 259 实测如此）。做成了模板单元，不用改文件，用户名写在 enable
命令里：

```bash
sudo systemctl enable GigabyteRGBController-resume@$USER.service
```

它以 root 运行，靠 `--user <名字>` 找到你的配置——通过 `getpwnam` 解析家目录，
而不是假设它一定在 `/home/<名字>`。root 能直接打开 hidraw 节点，不受 udev 那条
`uaccess` ACL 的影响。

`--probe` 是排查问题的第一步，它会列出所有 hidraw 节点并标出两个接口各在哪：

```
/dev/hidraw4     048d:5702  ITE Tech. Inc. GIGABYTE Device     <= HID LampArray 接口 (0x59)
/dev/hidraw5     048d:5702  ITE Tech. Inc. GIGABYTE Device     <= RGB Fusion 接口 (0xFF89/0xCC)
```

hidraw 编号重启后会变，因此程序按**报告描述符内容**匹配，不写死路径。

---

# 协议文档

以下内容有两个独立来源相互印证：

1. **实机**：本机 `/sys/class/hidraw/hidrawN/device/report_descriptor` 的真实 HID 报告描述符。
2. **静态逆向**：GCC 安装包 `GBT_rgbMotherboard_UC_26.03.25.01.exe` 内
   `RgbMotherboard.dll` 的反编译结果（该 DLL 为 .NET 程序集）。

## RGB Fusion 2.0（Usage Page 0xFF89）

### 接口识别

实机报告描述符：

```
06 89 FF   Usage Page (Vendor-Defined 0xFF89)
09 CC      Usage (0xCC)
A1 01      Collection (Application)
85 CC        Report ID (0xCC)
75 08        Report Size (8 bits)
95 3F        Report Count (63)
B1 00        Feature (Data,Var,Abs)
C0         End Collection
```

即 Feature report、Report ID `0xCC`、63 字节数据（含 report ID 共 **64 字节**）。

GCC 内部用同一组四元组识别设备。`RgbMotherboard.dll` 中：

```csharp
DevsCount = connect_to_mcu(1165, 22274, 65417, 204);
//                        VID    PID     UsagePage Usage
//                        0x048D 0x5702  0xFF89    0xCC
```

`RgbMotherboard.IT8297_5702.Gen2ARGB_Object` 类还有
`private const ushort uFeatureLength = 64;`，与描述符完全一致。

### 设备信息报文

发送 `CC 60`，读回 64 字节。GCC 的 `IT8297_Info` 结构体给出完整布局：

| 偏移 | 类型 | 字段 |
| ---- | ---- | ---- |
| 0     | u8      | `ReportId`（`0xCC`；部分 8297 控制器错误地返回 `0x00`） |
| 1     | u8      | `Product` |
| 2     | u8      | `DeviceNum` |
| 3     | u8      | `StripDetect` — 可寻址灯带检测位掩码 |
| 4–7   | u32     | `FW_Ver` |
| 8–9   | u16     | `Strip_Ctrl_Length0` |
| 10    | u8      | `Strip_Ctrl_Length1` |
| 11    | u8      | `SuppCmdFlag` — 支持的命令位掩码 |
| 12–39 | char[28]| `ProductString`（NUL 结尾） |
| 40–55 | u32 ×4  | `CalStrip3` / `CalStrip0` / `CalStrip1` / `CalStrip2` |
| 56–59 | u32     | `ChipId` |
| 60–63 | u32     | `CalStrip4` |

> 注：公开资料常把偏移 3 标为 "总 LED 数"，但 GCC 自己的字段名是
> `StripDetect`；偏移 8–11 也不是填充，而是灯带控制长度与命令支持标志。

### 区域效果报文

缓冲区布局：`[0]` = `0xCC`，`[1]` = 命令字节，`[2]` 起是 GCC 的
`DataFormat_8297` 结构体（32 字节）。

| 缓冲区偏移 | 类型 | 字段 | 说明 |
| ---------- | ---- | ---- | ---- |
| 0     | u8  | —              | Report ID `0xCC` |
| 1     | u8  | —              | 命令：`0x20 + 区域号`（区域 0–7） |
| 2–5   | u32 | `Zone_Sel0`    | 低字节为区域位掩码 `1 << 区域号` |
| 6–9   | u32 | `Zone_Sel1`    | 第二区域选择器 |
| 10    | u8  | `Reserve0`     | |
| 11    | u8  | `Mode_Sel`     | 效果值 |
| 12    | u8  | `MaxBrightness`| 最大亮度 |
| 13    | u8  | `MinBrightness`| 最小亮度（渐变下限） |
| 14–17 | u32 | `dwColor0`     | 小端，故线序为 **B G R 00** |
| 18–21 | u32 | `dwColor1`     | 第二颜色 |
| 22–23 | u16 | `wTime_base0`  | 亮起时长 |
| 24–25 | u16 | `wTime_base1`  | 间隔 |
| 26–27 | u16 | `wTime_base2`  | |
| 28–29 | u16 | `wTime_base3`  | |
| 30    | u8  | `CtrlVal0`     | 色彩循环步数 |
| 31    | u8  | `CtrlVal1`     | 脉冲标志 |
| 32    | u8  | `CtrlOem0`     | 闪烁次数 |
| 33    | u8  | `CtrlOem1`     | |

构造函数签名印证了时序语义：

```csharp
DataFormat_8297(byte mode, byte maxBri, uint uColor,
                ushort tOn, ushort tIntev, ushort ct,
                byte cv0, byte cv1, byte coem0)
```

**提交**：发送 `CC 28 FF`，控制器锁存此前暂存的所有区域设置。

### 效果值

| 模式 | `Mode_Sel` | 脉冲 | 闪烁次数 | 循环步数 | 最大亮度 | 用颜色 | 可调速 |
| ---- | ------ | ---- | -------- | -------- | -------- | ------ | ------ |
| 关闭 | `0x01` | 否 | 0 | 0 | 0 | 否 | 否 |
| 静态 | `0x01` | 否 | 0 | 0 | 90 | 是 | 否 |
| 呼吸 | `0x02` | 是 | 0 | 0 | 90 | 是 | 是 |
| 闪烁 | `0x03` | 是 | 1 | 0 | 100 | 是 | 是 |
| 双闪 | `0x03` | 是 | 2 | 0 | 100 | 是 | 是 |
| 色彩循环 | `0x04` | 否 | 0 | 7 | 100 | 否 | 是 |

"关闭"与"静态"共用 `0x01`，区别只是亮度为 0；"闪烁"与"双闪"共用 `0x03`，
区别只是 `CtrlOem0`（闪烁次数）。

### 关于速度表

本项目采用 liquidctl 的六档速度表（经长期实机验证）。GCC 自己用的是**五档**且
数值不同，例如呼吸效果（`PluseTiming`，GCC 拼写如此）：

```csharp
{ 1600, 1600, 200 }, { 1400, 1400, 150 }, { 1200, 1200, 100 },
{ 1000, 1000,  50 }, {  800,  800,   0 }
```

前两个分量与 liquidctl 一致，第三个分量差异明显。两套都能工作，属调校差异。

GCC 还含有本项目未实现的可寻址灯带效果时序表：`WaveTiming`、`StarSlideTiming`、
`MeteorTiming`、`ShakingTiming`、`ExplodeTiming`、`RecoverTiming`——均为
Gen2 ARGB 灯带专用，走另一条数据通道。

### 区域映射

协议固定提供八个区域（`0x20`–`0x27`），但实际接了灯的区域数因主板型号而异。
未接灯的区域号仍会接受报文并返回成功，只是没有任何可见效果。

**B760M AORUS ELITE 实测**：只有 3 个区域有灯。

| 区域 | 命令 | 本机是否有灯 |
| ---- | ---- | ------------ |
| 0 | `0x20` | 无 |
| 1 | `0x21` | 无 |
| 2 | `0x22` | **有** |
| 3 | `0x23` | 无 |
| 4 | `0x24` | 无 |
| 5 | `0x25` | **有** |
| 6 | `0x26` | **有** |
| 7 | `0x27` | 无 |

> 有灯区域数恰好为 3，而信息报文里 `StripDetect = 0x07` 也是 3 个置位，但这是
> **巧合**，两者无关。GCC 里该字段经 `Gen2StripDetectSupport()` 暴露，调用处一律
> 作 `!= 0` / `== 1` 的能力判断，含义是"哪些 ARGB 灯带头支持自动检测"，
> 不是区域数量。判断区域是否有灯只能靠实测。

换一块主板时，用以下方法可在几秒内测出映射：

```bash
for z in 0 1 2 3 4 5 6 7; do
    ./build/GigabyteRGBController --set static --color '#FFFFFF' --zone $z
    read -p "区域 $z 亮了吗？按回车继续"
    ./build/GigabyteRGBController --set off --zone $z
done
```

或直接在图形界面里点"探测有灯区域"，它会把上面这段循环做成一次一问的向导，
并把结果写进配置。

## ARGB 可寻址灯带（Gen2 ARGB）

**协议已完整逆向，但只实现了检测部分。** 开发平台四个灯带头都没接灯带
（`--probe` 全部报 `numStrip = 0`），数据流部分无法验证，因此没有写进程序——
不发布未经测试的代码。以下记录供后续实现参考。

### 灯带头检测（已实现并验证）

每个头有一对命令：先发扫描命令触发总线枚举，等约 700 ms，再发读取命令并读回。

| 灯带头 | 扫描 | 读取 |
| ------ | ---- | ---- |
| 0 | `0x3C` | `0x3E` |
| 1 | `0x3D` | `0x3F` |
| 2 | `0x38` | `0x3A` |
| 3 | `0x39` | `0x3B` |

应答是 `gen2_strip_info`：

| 偏移 | 类型 | 字段 |
| ---- | ---- | ---- |
| 0     | u8      | `ReportId` |
| 1     | u8      | `numStrip` — 该头上检测到几段灯带 |
| 2–31  | u16 ×15 | `LedCountOfStrip0` … `LedCountOfStrip14` |

`--probe` 会输出每个头的检测结果。

### 灯数据传输（未实现）

六个可寻址头，命令字节来自 `LED_Strip` 枚举：

| 灯带 | 命令 | | 灯带 | 命令 |
| ---- | ---- |-| ---- | ---- |
| `Strip_0` | `0x58` | | `Strip_3` | `0x63` |
| `Strip_1` | `0x59` | | `Strip_4` | `0x64` |
| `Strip_2` | `0x62` | | `Strip_5` | `0x65` |

颜色数据按 64 字节报文分块推送：

| 偏移 | 内容 |
| ---- | ---- |
| 0     | `0xCC` |
| 1     | 灯带命令字节 |
| 2–3   | 16 位小端**字节偏移**（本包数据在整条灯带缓冲区中的位置） |
| 4     | 本包数据字节数，满包为 **57**（= 19 颗灯 × 3 字节） |
| 5–61  | 颜色数据 |

发送循环（对应 GCC 的 `DStrip0_Out_LED` / `show_effect`）：满包 57 字节循环推送，
最后一包长度为剩余字节数。

**重要**：`Wave`、`Meteor`、`Explode`、`StarSlide`、`Shaking`、`Recover` 这些
不是硬件效果。GCC 的 `show_eff_task` 每 **25 ms**（约 40 FPS）重算一帧并整条重推——
效果完全在主机侧计算，程序退出效果就停。这与 8 区域的板载效果有本质区别。

尚未确认的是灯数据的颜色字节序（灯带常见为 GRB），需要接上灯带实测。

## HID LampArray（Usage Page 0x59）

来自 HID Usage Tables 1.4。实机描述符解析出六个 Feature 报文：

| 报文 ID | 名称 | 方向 | 字段 |
| ------- | ---- | ---- | ---- |
| 1 | `LampArrayAttributesReport` | 读 | `LampCount` u16；`BoundingBoxWidth/Height/DepthInMicrometers`、`LampArrayKind`、`MinUpdateIntervalInMicroseconds` 各 u32 |
| 2 | `LampAttributesRequestReport` | 写 | `LampId` u16 |
| 3 | `LampAttributesResponseReport` | 读 | `LampId` u16；`PositionX/Y/ZInMicrometers`、`UpdateLatencyInMicroseconds`、`LampPurposes` 各 u32；`Red/Green/Blue/IntensityLevelCount`、`IsProgrammable`、`InputBinding` 各 u8 |
| 4 | `LampMultiUpdateReport` | 写 | `LampCount` u8、`LampUpdateFlags` u8、`LampId[8]` u16、`(R,G,B,I)[8]` u8 |
| 5 | `LampRangeUpdateReport` | 写 | `LampUpdateFlags` u8、`LampIdStart` u16、`LampIdEnd` u16、`R,G,B,I` u8 |
| 6 | `LampArrayControlReport` | 写 | `AutonomousMode` u8 |

报文长度（含 report ID 字节）：23 / 3 / 29 / 51 / 10 / 2。

**关键点**：必须先用报文 6 把 `AutonomousMode` 设为 0，主机的灯光更新才会生效；
设回 1 则交还板载效果控制。

## 逆向工具链

项目根目录的 `.mcp.json` 配置了 radare2 MCP，供后续分析原生模块使用：

```json
{ "mcpServers": { "radare2": { "command": ".../r2mcp", ... } } }
```

.NET 程序集用 ILSpy（`ilspycmd`）反编译。GCC 安装包为 NSIS 自解压，7-Zip 可直接解开：

```bash
7zz l GIGABYTE\ Control\ Center_..._Setup_26.08.03.01.exe
7zz e -oOUT <installer> GBT_rgbMotherboard_UC_26.03.25.01.exe
7zz e -oOUT2 GBT_rgbMotherboard_UC_26.03.25.01.exe "*.dll"
ilspycmd -o decomp RgbMotherboard.dll
```

分析中还发现两条泄露的 PDB 路径，可佐证模块归属：

```
C:\AmbientLED\LedIoControl1\LedIoControl\obj\Release\LedIoControl.pdb
D:\0_WORK\Dynamic_Lighting_Fun_Lib\obj\Release\Dynamic_Lighting_Fun_Lib.pdb
```

另注：`LedIoControl.dll` 走的是完全不同的通道——ITE **IT86xx Super-I/O 寄存器**
（`LED1PD1R`、`LED1FE1R`、`LED_BLINKING_CONTROL_REGISTER`）与 BIOS
（`dllexp_SetLedModeToBios`），用于不带 USB RGB 控制器的老主板，本项目未实现。

## 致谢与许可

协议字段偏移最初参考了 [liquidctl](https://github.com/liquidctl/liquidctl) 的
`rgb_fusion2` 驱动（作者 CaseySJ、Jonas Malaco 等），后经 GCC 静态逆向逐字段
核对与补全。相应地，本项目以 **GPL-3.0-or-later** 发布。
