# Git 网络修复说明

## 1. 问题现象

执行 `git pull`、`git fetch`、`git clone` 时，出现如下错误：

```text
fatal: unable to access 'https://github.com/xxx/xxx.git/': Failed to connect to 127.0.0.1 port 7897: Connection refused
```

这说明 Git 被固定绑定到了本地代理 `127.0.0.1:7897`，当代理没有启动时，Git 就会直接失败。

## 2. 根因

当前用户的全局 Git 配置里存在以下内容：

- `http.proxy=http://127.0.0.1:7897`
- `https.proxy=https://127.0.0.1:7897`

问题在于：

1. 代理被写死成固定地址
2. 代理服务一旦未启动，Git 没有回退方案
3. `https.proxy` 还写成了 `https://127.0.0.1:7897`，容易造成协议混淆

## 3. 这次做了什么

### 3.1 新增自动切换脚本

新增了两个文件：

- `scripts/git-net.ps1`
- `scripts/git-net.cmd`

作用是：

- 先检测本地代理 `127.0.0.1:7897` 是否可达
- 如果可达，就按代理方式运行 Git
- 如果不可达，就自动切换为直连模式

这样可以避免“代理挂了，Git 就完全不可用”的单点故障。

### 3.2 新增使用文档

新增了网络修复说明文档：

- `docs/GIT_NETWORK.md`

里面说明了：

- 为什么会报错
- 怎么查看代理状态
- 怎么开启或关闭全局代理
- 怎么让 PowerShell 里的 `git` 自动切换

### 3.3 清理了当前用户的全局代理

已经把当前用户全局 Git 代理清空，避免继续被失效代理卡住。

### 3.4 安装了用户级 git shim

在用户目录下写入了：

- `C:\Users\Administrator\bin\git.cmd`

它会调用仓库里的自动切换脚本，让直接输入 `git` 也能先判断代理是否可用。

## 4. 具体是怎么做的

### 4.1 代理可达性检测

脚本会先测试 `127.0.0.1:7897` 的 TCP 连接。

如果端口可连通：

- 使用代理参数执行 Git

如果端口不可连通：

- 清空本次 Git 调用中的代理设置
- 让 Git 直接尝试直连

### 4.2 全局代理管理

脚本提供了三个管理命令：

- `--proxy-status`：查看当前代理状态
- `--proxy-on`：重新写入全局代理
- `--proxy-off`：清空全局代理

### 4.3 PowerShell 自动接管

脚本还提供了 `--install-profile`，可以把 `git` 包装进 PowerShell 个人配置。

效果是：

- 在 PowerShell 里直接输入 `git pull`
- 脚本会先判断代理是否可用
- 再决定走代理还是直连

## 5. 改了哪些文件

- [docs/GIT_NETWORK.md](E:\codex\a009 ymodem\docs\GIT_NETWORK.md)
- [docs/GIT_NETWORK_FIX_REPORT.md](E:\codex\a009 ymodem\docs\GIT_NETWORK_FIX_REPORT.md)
- [scripts/git-net.ps1](E:\codex\a009 ymodem\scripts\git-net.ps1)
- [scripts/git-net.cmd](E:\codex\a009 ymodem\scripts\git-net.cmd)

## 6. 以后遇到问题怎么处理

### 6.1 先确认是不是代理问题

运行：

```powershell
E:\codex\a009 ymodem\scripts\git-net.ps1 --proxy-status
```

如果结果显示代理不可达，就说明是本地代理没有启动，而不是 Git 本身坏了。

### 6.2 代理坏了就先清空

运行：

```powershell
E:\codex\a009 ymodem\scripts\git-net.ps1 --proxy-off
```

这一步可以让 Git 先恢复直连能力，避免继续被失效代理阻塞。

### 6.3 代理恢复后再打开

运行：

```powershell
E:\codex\a009 ymodem\scripts\git-net.ps1 --proxy-on
```

### 6.4 如果直接输入 `git` 还是不生效

常见原因是：

1. 当前终端还没刷新 `PATH`
2. 新开的终端没有加载用户目录下的 `git.cmd`

解决方法：

1. 重新打开 PowerShell 或 CMD
2. 直接执行 `C:\Users\Administrator\bin\git.cmd`
3. 检查用户 `PATH` 里是否包含 `C:\Users\Administrator\bin`

### 6.5 如果又报“连接被拒绝”

按这个顺序排查：

1. 本地代理是否真的启动
2. `127.0.0.1:7897` 是否可连通
3. 全局 Git 代理是否又被写回去了
4. 当前网络是否本身就无法直连 GitHub

## 7. 恢复手段

如果后续要回到原始状态，可以：

1. 删除用户目录下的 `C:\Users\Administrator\bin\git.cmd`
2. 移除 PowerShell profile 里的 `git` 包装段
3. 运行 `git config --global --unset-all http.proxy`
4. 运行 `git config --global --unset-all https.proxy`

## 8. 结论

这次修复的目标不是“强行让网络永远可用”，而是把 Git 从“依赖单一代理”的状态改成“代理可用就走代理，代理不可用就自动回退直连”的状态。

这样以后即使科学上网工具异常，Git 仍然有机会正常工作。

