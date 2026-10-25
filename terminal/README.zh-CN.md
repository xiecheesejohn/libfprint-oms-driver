# sudo 等终端命令的指纹状态面板

[English](README.md)

这是独立的用户态包装器，在 `sudo -i` 等命令的原生指纹提示旁显示状态：

```text
Place your finger on the fingerprint reader

╭─ 指纹读取器 ────────────────────
│ ○ 请触摸指纹传感器 · ·
│ ✓ 指纹匹配
╰─────────────────────────────────
```

等待时追加进度点；驱动提供手指接触状态时显示读取；匹配和失败时显示结果。
只读监听 fprintd，不读取 stdin、不代理密码、不创建 PTY、不修改终端模式、
不清除行。命令继承原终端，状态仅写 stderr；密码输入关闭回显时抑制动画。
原来的 PAM 提示和 sudo 密码输入流程保留。

## 安装与使用

面向 Fedora 的 Bash 桌面用户。进入仓库后执行：

```bash
sudo dnf install python3-gobject
sudo python3 terminal/install.py --install "$USER"
```

请在正常桌面用户终端执行，不要在 root 登录后直接使用 `$USER`。
安装器沿用 Fedora 已有的 `~/.bashrc.d` 加载方式，安装：

- `~/.local/bin/fingerprint-tui`
- `~/.bashrc.d/90-fingerprint-tui.sh`

原文件若存在会先备份。不修改 PAM、sudoers、sudo 程序或 root 配置。

**新开 Bash 终端即可生效。** 当前普通用户终端可以执行：

```bash
source ~/.bashrc.d/90-fingerprint-tui.sh
sudo -i
```

如果 sudo 的认证缓存仍有效，不会发起指纹认证，也就不会出现面板。
包装器不会为显示动画而主动清除认证缓存。

## 演示和其他命令

```bash
FPRINT_TUI_LANG=zh fingerprint-tui --demo
fingerprint-tui -- sudo -i
fingerprint-tui -- su -
```

演示不执行认证。其他命令只有在其原认证配置使用 fprintd 时才会显示真实状态。
sudo 的 Bash 包装默认中文；`FPRINT_TUI_LANG=en` 可切换英文。

脚本／非 TTY 调用，以及 `sudo -S`、`-n`、`-A` 会直接调用原 sudo。
参数和退出码保留。临时关闭使用 `FPRINT_TUI=0`，或以 `command sudo ...`
绕过 Bash 函数。

## 限制与回退

- 只装饰经包装器启动的命令，不替换所有系统 PAM 提示。
- 面板显示读取器状态，不是命令授权凭据。fprintd 广播不标明发起客户端；
  同时进行的其他认证也可能产生状态。匹配后或 45 秒后停止监听，命令照常运行。
- 驱动不提供手指接触属性时保持等待，直到结果；不伪造扫描事件。
- 支持 `NO_COLOR`，不使用全屏控制或光标定位。

使用安装输出的真实备份路径回退：

```bash
sudo python3 terminal/install.py --restore /var/lib/oms-fingerprint-tui-backups/实际备份目录
unset -f sudo
```

回退只恢复两个用户文件。新开终端以加载原配置。

测试不调用真实 sudo，也不执行身份验证：

```bash
dbus-run-session -- python3 terminal/tests/test-terminal.py
```

覆盖输入输出透传、命令退出码、信号退出、命令不存在，以及私有总线上的
重试和匹配状态。源码采用 LGPL-2.1-or-later。
