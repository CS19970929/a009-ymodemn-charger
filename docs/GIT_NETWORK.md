# Git 网络自动切换方案

当前环境里的 Git 全局配置把代理固定成了 `127.0.0.1:7897`。当这个代理没有启动时，所有走 HTTPS 的 Git 操作都会直接失败，报错通常类似：

```text
Failed to connect to 127.0.0.1 port 7897: Connection refused
```

这不是 Git 本身坏了，而是“代理可用”被当成了唯一前提。只要代理断掉，Git 就失去出口。

## 解决思路

仓库里提供了一个自动切换脚本：

- `scripts/git-net.ps1`
- `scripts/git-net.cmd`

它会先检测本地代理 `127.0.0.1:7897` 是否可达，再决定：

- 代理可用时，按代理模式执行 Git
- 代理不可用时，自动切换到直连模式，避免被一个失效代理卡死

## 推荐用法

### 查看状态

```powershell
.\scripts\git-net.ps1 --proxy-status
```

### 恢复代理配置

```powershell
.\scripts\git-net.ps1 --proxy-on
```

### 清空全局代理

```powershell
.\scripts\git-net.ps1 --proxy-off
```

### 让 `git` 命令自动切换

```powershell
.\scripts\git-net.ps1 --install-profile
```

安装后，PowerShell 会在个人配置里挂一个 `git` 包装函数。以后直接输入 `git pull`、`git fetch`、`git clone`，脚本会自动判断代理是否可用并切换。

## 说明

这里做的是“避免单点代理故障”，不是凭空创造网络。也就是说：

- 代理坏了，Git 仍然能尝试直连
- 代理好了，Git 可以继续走代理
- 但如果你当前网络环境本身就无法直连 GitHub，又没有可用代理，那么任何工具都无法凭空完成访问

## 为什么要修正代理地址

Git 的代理配置一般建议写成：

```text
http://127.0.0.1:7897
```

而不是 `https://127.0.0.1:7897`。后者容易造成额外的协议混淆，没必要。

