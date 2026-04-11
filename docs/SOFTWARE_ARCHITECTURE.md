# 充电器升级上位机软件架构说明

## 1. 文档目的

本文档用于说明当前充电器升级上位机的核心逻辑、软件架构、技术选型、编译方式和后续可扩展方向，方便后续维护、学习和二次开发。

## 2. 项目目标

该上位机用于批量给充电器进行固件升级，设计目标如下：

- 操作简单：尽量减少人工步骤，避免误操作。
- 稳定优先：确保在 bootloader 7 秒升级窗口内稳定发送 `update`。
- 低依赖：运行端不依赖 Python、.NET、SecureCRT 或其他第三方运行环境。
- 兼容旧系统：优先兼容老 Windows 环境。
- 适配批量场景：同一固件批次只选择一次，后续设备可连续上电自动升级。

## 3. 技术选型

### 3.1 编程语言

- 语言：C++
- 风格：偏 C 风格过程式实现

选择原因：

- 易于直接调用 Win32 API
- 部署简单
- 可控性高，便于处理串口和协议时序
- 对老 Windows 的兼容性更好

### 3.2 图形界面

- 原生 Win32 API
- 不依赖 Qt、MFC、.NET、Electron 等框架

### 3.3 串口通信

- 使用 Windows 串口 API
- 通过 `CreateFileW` 打开 `COM` 口
- 使用 `DCB` 配置 `115200 8N1`
- 使用 `COMMTIMEOUTS` 控制读写超时

### 3.4 协议层

- 升级协议入口：bootloader 串口文本提示
- 文件传输协议：YMODEM
- YMODEM 为项目内自实现，不依赖外部库

## 4. 编译与运行环境

### 4.1 编译器

- 默认编译器：`C:\qp\qtools\MinGW32\bin\g++.exe`
- 备用编译器：环境变量 `PATH` 中的 `g++`

### 4.2 构建脚本

构建脚本文件：

- [build_mingw32.bat](/E:/codex/a009 ymodem/build_mingw32.bat)

该脚本会同时生成两个版本：

- `release\ChargerUpdater_debug.exe`
- `release\ChargerUpdater_user.exe`

### 4.3 关键编译参数

- `-std=gnu++98`
- `-Os`
- `-Wall -Wextra`
- `-D_WIN32_WINNT=0x0501`
- `-DUNICODE -D_UNICODE`
- `-mwindows`
- `-static -static-libgcc -static-libstdc++`

含义说明：

- `gnu++98`：降低语言特性依赖，兼容老编译环境
- `-Os`：优先减小体积
- `_WIN32_WINNT=0x0501`：以 Windows XP 级别 API 为目标
- `-mwindows`：构建 GUI 程序而非控制台程序
- `-static`：尽量减少外部运行库依赖

### 4.4 当前产物特性

- 可执行文件格式：`pei-i386`
- 架构：32 位
- 依赖 DLL：
  - `COMCTL32.DLL`
  - `COMDLG32.DLL`
  - `GDI32.DLL`
  - `KERNEL32.DLL`
  - `msvcrt.dll`
  - `USER32.DLL`

## 5. 目录结构

```text
src/
  main.cpp           界面、串口、状态机、批量升级主流程
  ymodem.cpp         YMODEM 协议发送实现
  ymodem.h           YMODEM 接口声明

docs/
  UPGRADE_FLOW.md            升级流程说明
  SOFTWARE_ARCHITECTURE.md   软件架构说明

release/
  ChargerUpdater_debug.exe   调试版
  ChargerUpdater_user.exe    用户版

build_mingw32.bat            构建脚本
README.md                    项目使用说明
```

## 6. 软件总体架构

整体可以划分为四层：

### 6.1 表现层

位于：

- [main.cpp](/E:/codex/a009 ymodem/src/main.cpp)

职责：

- 创建窗口和控件
- 响应按钮点击
- 展示升级状态
- 展示进度
- 用户版与调试版 UI 分支切换

当前通过宏 `SIMPLE_UI` 区分两套界面：

- 调试版：保留详细日志
- 用户版：保留大状态提示、进度和成功台数

### 6.2 串口接入层

位于：

- [main.cpp](/E:/codex/a009 ymodem/src/main.cpp)

职责：

- 扫描 COM 口
- 打开/关闭串口
- 设置波特率和超时
- 读取 bootloader 文本输出

典型入口函数：

- `open_serial_port`
- `connect_or_disconnect`

### 6.3 升级流程控制层

位于：

- [main.cpp](/E:/codex/a009 ymodem/src/main.cpp)

职责：

- 批量待机
- 等待设备上电
- 识别 bootloader 升级提示
- 在 7 秒内发送 `update`
- 等待“等待文件发送……”提示
- 调用 YMODEM 发送文件
- 升级成功后清尾并重新待机

典型函数：

- `wait_for_boot_prompt`
- `wait_for_file_send_prompt`
- `drain_post_upgrade_output`
- `upgrade_thread_proc`

### 6.4 YMODEM 协议层

位于：

- [ymodem.h](/E:/codex/a009 ymodem/src/ymodem.h)
- [ymodem.cpp](/E:/codex/a009 ymodem/src/ymodem.cpp)

职责：

- 等待接收端握手字符
- 构造头包
- 发送 128 字节或 1024 字节数据包
- 计算 CRC16
- ACK/NAK 重试
- EOT 收尾
- 空头包结束

核心对外接口：

```c
BOOL ymodem_send_file(
    HANDLE serial,
    const WCHAR *firmware_path,
    volatile LONG *stop_flag,
    YmodemLogFn log_fn,
    YmodemProgressFn progress_fn,
    void *user,
    WCHAR *error,
    size_t error_count);
```

## 7. 线程模型

当前为双线程模型：

### 7.1 UI 主线程

职责：

- 处理窗口消息
- 绘制界面
- 更新状态
- 响应用户操作

### 7.2 升级工作线程

职责：

- 等待设备上电
- 识别 bootloader 输出
- 发送 `update`
- 进入 YMODEM 发送
- 完成后重新进入待机

线程入口：

- `upgrade_thread_proc`

线程间通信方式：

- 工作线程通过 `PostMessageW` 向主线程发送：
  - 日志消息
  - 状态消息
  - 进度消息
  - 成功消息
  - 失败消息

这种方式的好处是：

- UI 不会因串口或文件传输阻塞
- 串口状态机更集中
- 避免 UI 线程直接执行耗时 I/O

## 8. 核心升级状态机

当前升级流程的核心状态可以概括为：

1. `未连接`
2. `已连接`
3. `选择固件`
4. `批量待机`
5. `识别 bootloader 升级提示`
6. `发送 update`
7. `等待文件接收提示`
8. `YMODEM 发送`
9. `升级成功`
10. `清尾日志并等待串口静默`
11. `重新进入批量待机`

### 8.1 关键行为

#### 行为一：不是看到任意串口输出就发 `update`

程序会先识别 bootloader 提示文本中包含的升级关键词，例如：

- `update`
- `开始升级程序`

这样做的目的是减少误触发。

#### 行为二：发送 `update` 后，不会立刻发文件

程序会先等待设备明确进入文件接收状态，即：

- 收到“等待文件发送……”提示
- 或检测到 YMODEM 握手字符 `C`

确认设备进入接收状态后才开始发包。

#### 行为三：单台升级完成后先清尾

设备升级成功后仍可能输出：

- 下载成功
- 文件名
- 文件大小
- 开始执行新程序

如果不处理这些尾日志，下一轮设备上电时容易与上一轮尾输出混在一起，导致批量升级重入识别异常。

因此程序会先读取剩余输出，并等待串口静默，再重新进入待机。

## 9. 调试版与用户版

### 9.1 调试版

文件：

- [ChargerUpdater_debug.exe](/E:/codex/a009 ymodem/release/ChargerUpdater_debug.exe)

特点：

- 显示详细串口日志
- 打印 bootloader 输出
- 打印升级过程细节
- 适合联调和问题定位

### 9.2 用户版

文件：

- [ChargerUpdater_user.exe](/E:/codex/a009 ymodem/release/ChargerUpdater_user.exe)

特点：

- 保留核心状态提示
- 界面更简洁
- 仅强调：
  - 请先连接串口
  - 请点击下载
  - 请给充电器上电
  - 正在升级
  - 升级成功
- 增加成功升级台数统计

设计目标：

- 面向产线或普通操作员
- 降低误读和误操作概率

## 10. 为什么当前方案稳定

当前方案稳定的主要原因有：

1. 不依赖外部终端软件
2. 不依赖外部脚本环境
3. 协议时序完全在程序内部控制
4. 使用明确文本触发而不是模糊触发
5. 批量模式下增加串口尾输出清理机制
6. 使用线程隔离 UI 和 I/O
7. 编译目标保守，依赖少

## 11. 当前局限

目前仍有一些可以继续完善的点：

- bootloader 提示词仍写死在代码中
- 串口自动重连能力较弱
- 没有升级记录文件
- 没有批量升级结果导出
- 固件合法性检查还较少
- 没有加入设备序列号识别或版本比对

## 12. 后续建议

后续可考虑按以下方向优化：

### 12.1 配置化

- 把 bootloader 关键提示词提取到配置文件
- 把超时时间提取到配置文件
- 把串口默认参数提取到配置文件

### 12.2 日志增强

- 调试版支持保存日志到文件
- 用户版支持生成简洁升级结果记录

### 12.3 升级记录

- 记录每台设备升级时间
- 记录固件名称
- 记录升级结果
- 记录失败原因

### 12.4 设备识别

- 支持识别设备编号
- 支持升级前版本显示
- 支持升级后版本校验

### 12.5 结构重构

可以将 `main.cpp` 进一步拆分为：

- `ui_*`：界面相关
- `serial_*`：串口收发相关
- `upgrade_*`：升级状态机相关
- `boot_prompt_*`：文本识别相关

这样便于长期维护和单元测试。

## 13. 结论

当前这套上位机的设计核心是：

- 用最少依赖实现最稳定的升级链路
- 用 Win32 原生实现兼容老 Windows
- 用工作线程保证 UI 不阻塞
- 用明确的状态机控制 bootloader 升级时序
- 用内置 YMODEM 保证升级过程可控
- 用调试版和用户版双轨满足联调和实际批量使用

对于当前“批量升级充电器”的需求，这套架构是务实且可持续演进的。
