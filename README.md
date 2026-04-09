# 充电器升级上位机

这是一个用于替代 SecureCRT 手工升级流程的 Windows 上位机工具。用户只需要连接串口、点击下载并选择固件文件，然后给充电器上 AC 电，上位机会自动发送 `update` 并通过 YMODEM 协议发送固件。

## 交付目标

- 运行端不需要安装 Python、.NET、pyserial 或 SecureCRT。
- 编译输出为 32 位 Windows exe，优先兼容老 Windows。
- 串口参数固定为 `115200 8N1`，避免用户手工配置错误。
- 串口日志按 GB2312/CP936 解码显示，兼容原 SecureCRT 使用方式。
- 点击“下载”后进入等待状态，检测到充电器上电串口输出后自动发送 `update`。
- 等待充电器进入 YMODEM 接收状态后自动发送 `.bin` 固件。
- 界面只保留串口连接和下载相关操作。

## 直接运行

当前仓库已生成可直接运行的 32 位 Windows 程序：

```text
release\ChargerUpdater.exe
```

最终用户只需要这个 exe，不需要安装 Python、.NET、pyserial 或 SecureCRT。

## 重新生成 exe

当前仓库同时保留原生 Win32 C++ 源码和构建脚本，便于后续维护。

在开发机上构建：

```bat
build_mingw32.bat
```

脚本会优先使用 `C:\qp\qtools\MinGW32\bin\g++.exe`，如果不存在则使用 `PATH` 中的 `g++`。输出文件：

```text
build\ChargerUpdater.exe
```

## 使用步骤

1. 将 USB 转串口线连接电脑和充电器升级串口。
2. 打开 `ChargerUpdater.exe`，选择对应 COM 口，点击“连接”。
3. 点击“下载”，选择充电器固件 `.bin` 文件。
4. 工具显示“等待充电器上电”后，给充电器接入 AC 电源。
5. 工具检测到充电器启动输出后会自动发送 `update`，随后自动执行 YMODEM 固件发送。
6. 进度到 100% 且状态显示升级完成后，再断开或重启设备。

## 注意事项

- 下载过程中不要关闭工具、拔掉串口线或切断充电器电源。
- 如果长时间没有进度，请确认选择的 COM 口正确，并重新点击“断开”后再“连接”。
- 如果充电器固件较大，YMODEM 发送会持续一段时间，期间日志可能不会持续滚动，这是正常现象。
- 当前实现会在检测到任意充电器上电串口输出后发送 `update`。如果后续明确了 bootloader 的固定提示文本，可以在 `src/main.cpp` 中收窄触发条件。
