# 结构 — niz-mac-flash

## 1. 组成

```
shim/shim.c            x86_64 动态库，注入官方刷写工具
tools/read-version.c   原生程序，读键盘固件版本
scripts/flash-mac.sh   用 UI 自动化驱动官方工具刷写
build.sh               编译上面两个程序
entitlements.plist     重签名官方工具时使用
```

| 文件 | 产物 | 作用 |
|---|---|---|
| `shim/shim.c` | `shim/shim.dylib` | interpose 三个 IOKit HID 符号 |
| `tools/read-version.c` | `tools/read-version` | 向 vendor 接口发 `0xf9`，打印版本串 |
| `scripts/flash-mac.sh` | — | 启动工具、选文件、点「开始升级」、盯日志 |

依赖：系统自带 clang、IOKit、CoreFoundation。没有第三方库。

## 2. shim 拦截的符号

| 符号 | 处理 |
|---|---|
| `IOHIDDeviceOpen` | 忽略调用方的 options，用 `kIOHIDOptionsTypeNone` 打开 |
| `IOHIDManagerOpen` | 同上 |
| `IOHIDDeviceSetReport` | 不足 64 字节的报文补零到 64 字节 |

`IOHIDDeviceOpen` 被替换后还会：启动一个持有 runloop 的线程，在打开设备之前注册输入报告回调并调度到该 runloop；另一个线程每 20ms 调 `IOHIDDeviceGetReport` 取走排队回复。日志写到 `/tmp/nizshim.log`。

注入方式：`DYLD_INSERT_LIBRARIES=shim/shim.dylib`。

## 3. 刷写流程

```
build.sh
  → shim/shim.dylib（x86_64）+ tools/read-version（本机架构）

重签名官方工具
  → codesign --force --deep --sign - --entitlements entitlements.plist

启动工具（带 shim）
  → 选取文件 → 开始升级 → 工具按 64 字节报文写入 → 设备回报 → 设备重启
```

`scripts/flash-mac.sh` 用 osascript 完成「选文件 + 开始升级」，轮询 `/tmp/niztool.log` 里的「结束固件升级」/「upgrade error」。

## 4. 能做什么

- 让 2019 年的官方 macOS 刷写工具在较新 macOS 上完成刷写（短报文补齐、非独占打开、取走回复）
- 读键盘当前固件版本（`mini84EC(S);V2.2.1;V1.0;` 格式）
- 在不修改官方 App 本体、不改固件的前提下注入
- 用脚本自动完成选文件和开始升级

## 5. 不能做什么

- 不包含官方固件或官方工具，需要自行下载
- 不绕过刷写风险：刷错机型固件仍可能变砖
- 不支持 Apple Silicon 原生运行官方工具，需要 Rosetta（工具本身是 x86_64）
- `tools/read-version` 只匹配 VID `0x0483` / PID `0x5235`，其他型号需改源码
- 不保证 UI 自动化长期可用；官方工具改版后需要手动点「选取文件」和「开始升级」
- 不能从「设备被堵住」的状态恢复，只能拔插一次 USB
- 只在 macOS 27.2 / Apple M6 与 NIZ mini84EC(S)_AW2 上验证过
