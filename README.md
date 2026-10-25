# NIZ（宁芝）键盘 macOS 固件刷写修复

官方 2019 年的 macOS「固件升级」工具在较新的 macOS 上刷不进去，升级在第二条记录就中止：

```
IOHIDDeviceSetReport error: 0x3758097110 (OSStatus error -536870186 / 0xE00002D6)
```

原因是这个工具本身：它每次只发 3～27 字节的短报文，而键盘的 OUT 端点收的是定长 64 字节包，第一条短报文之后端点一直处于未完成状态；同时它独占打开设备且从不读取设备回复。

本仓库用 `DYLD_INSERT_LIBRARIES` 注入层修好它，**不修改官方 App 本体**，不动固件。

## 方案

`shim/shim.c` interpose 三个符号：

| 符号 | 处理 |
|---|---|
| `IOHIDDeviceOpen` | 强制 `kIOHIDOptionsTypeNone`（官方用 `SeizeDevice` 独占） |
| `IOHIDManagerOpen` | 强制 `kIOHIDOptionsTypeNone` |
| `IOHIDDeviceSetReport` | 不足 64 字节补零到 64 字节 |

另外起一个线程持有 runloop，在设备 open **之前**注册输入报告回调并轮询 `GetReport`，把设备回复及时取走，避免管道再次堵塞。

## 用法

### 0. 前提
- 固件和工具从官方渠道下载（本仓库不包含）
- Apple Silicon 需要 Rosetta：`softwareupdate --install-rosetta`
- 工具是 2019 年的 x86_64 程序，且带 App Sandbox，需要重新签名去掉沙盒

### 1. 编译
```bash
./build.sh
```

### 2. 重新签名官方工具
```bash
cp -R "/path/to/固件升级.app" /tmp/niztool.app
codesign --force --deep --sign - --entitlements entitlements.plist /tmp/niztool.app
```

### 3. 刷写
```bash
DYLD_INSERT_LIBRARIES="$PWD/shim/shim.dylib" \
  /tmp/niztool.app/Contents/MacOS/FirmwareUpgradeProject
```
界面里「选取文件」选中 `.bin`，再点「开始升级」。日志实时写在 `/tmp/nizshim.log`。

或直接跑脚本（需要终端有辅助功能权限，界面自动化比较脆，失败就手动点）：
```bash
./scripts/flash-mac.sh /tmp/niztool.app /path/to/firmware.bin
```

### 4. 校验版本
```bash
./tools/read-version
# mini84EC(S);V2.2.1;V1.0;
```

### 卡住了怎么办
工具收不到/读不掉回复时，键盘会拒收后续写入，只能**拔插一次 USB** 恢复。
另外别在工具运行期间拔插——它检测到设备会立刻发一次版本查询，又把设备堵上（此时先退出工具，再拔插）。

## 已测试设备

| 型号 | VID:PID | 硬件版本 | 原固件 | 刷入固件 | 结果 |
|---|---|---|---|---|---|
| NIZ mini84EC(S)_AW2 | 0483:5235 | V1.0 | V2.0.56 | mini84ECS_V2.2.1.bin（2024.6） | 成功，刷后回报 `V2.2.1` |

刷写过程 2428 次 `SetReport` 全部成功，刷完设备自动重启，键盘正常枚举。
（只列出实际刷过的设备，其他型号未验证。）

## 固件版本选择

- `mini84ECS_V2.2.1` — 2023 年 mini84 EC-S PRO / AW2 硬件，本项目实际使用
- `mini84ECS_V2.2.4`（2025.4）— 属于 I84ALU / Mini84 v6 新硬件，**不要**刷到 AW2 上

刷错机型固件可能变砖，刷之前先记下自己的型号和当前固件版本（用 `tools/read-version`）。

## 相关资料

- 官方固件下载：https://www.nizkeyboard.com/pages/firmware-upgrade
- NiZ 支持站下载页：https://support.niz.store/portal/ja/kb/articles/download
- 官方站点：https://www.nizkeyboard.com/

## 说明

- 本仓库不包含官方固件与工具本体
- 刷固件有风险，后果自负
