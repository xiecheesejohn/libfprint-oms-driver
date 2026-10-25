# 极光指纹：独立 KDE 插件

[English](README.md)

这个可选插件使用标准 **Plasma/Wallpaper KPackage**，提供动态极光背景和
真实指纹状态反馈。不需要重新编译 KDE，也不替换 KDE 自带文件。

![预览](../docs/aurora-preview.png)

## 它如何工作

- 安装到 `/usr/local/share/plasma/wallpapers/`，拥有独立插件 ID。
- 配置为当前用户的锁屏背景，以及 Plasma Login Manager 的开机登录背景。
- 只读监听 fprintd：等待时呼吸、检测到手指时扫描、匹配时绿勾、失败时红色提示。
  没有认证请求时隐藏指纹提示。
  如果驱动不提供 `finger-present` 状态，则保持等待，直到收到验证结果，
  不会凭时间伪造手指触摸或扫描状态。
- 不自行发起认证，不占用传感器，不读取指纹模板，不接收密码或鼠标键盘输入。
- 提示图层仍在 KDE 原有窗口中，放在背景模糊效果之上，不创建额外锁屏窗口。
- 不修改 PAM、Polkit、KDE 程序或服务配置。

**通常升级 KDE 不需要重编译。** 但未来 QML 接口变化仍可能需要更新插件源码。
状态监听独立加载，即使它因不兼容无法加载，也不会把背景一起拖垮。

实测环境为 Fedora 44、Plasma 6.7.5、Qt 6.11.2。开机界面针对
**Plasma Login Manager**，安装脚本不配置 SDDM。
认证成功后 KDE 会立即关闭窗口，因此绿勾可能只短暂显示；插件不延迟解锁。

插件不能单独启用开机指纹登录。如果登录管理器仍使用密码认证，背景会正常
显示，但不会伪造“正在扫描”状态。开机指纹认证使用主 README 中的可选 PAM 配置脚本，与本插件独立。

## 安装

进入仓库后，以平时登录桌面的用户执行：

```bash
sudo python3 ui/install.py --install "$USER"
```

脚本备份原先的背景选择和已有插件。**请保存输出的回退命令。** 下次锁屏和
下次启动 Plasma Login Manager 时使用新背景。安装不会注销或重启登录管理器。
插件运行时不需要 Python 后台进程，也不需要额外守护服务。

按 **Meta + L** 查看锁屏效果，并保留密码作为登录方式。

## 预览与测试

预览可选依赖：

```bash
sudo dnf install python3-pyside6 python3-gobject dbus-daemon
python3 ui/preview/run-preview.py
```

窗口里的“等待、扫描、成功、失败”按钮只用于演示，不执行身份验证。

自动测试：

```bash
dbus-run-session -- python3 ui/tests/test-monitor.py
python3 ui/tests/test-wallpaper.py
```

已测试私有 D-Bus 上的设备发现、等待、手指属性变化、成功、失败和断开；
测试不会在真实系统总线上伪造 fprintd。原生 WallpaperItem 加载及图层位置
检查也已通过。原生锁屏测试模式无 QML 加载错误；本机无触摸验证还确认插件收到“等待 → 拒绝 → 空闲”的真实状态。完整冷启动视觉效果仍待确认。

壁纸设置提供“减少动态效果”和“显示真实指纹验证状态”开关。

## 回退

使用安装输出的真实备份目录：

```bash
sudo python3 ui/install.py --restore /var/lib/oms-aurora-ui-backups/实际备份目录
```

回退仅恢复背景选择和本插件，不覆盖其他设置，也不修改指纹或认证配置。
源码采用 LGPL-2.1-or-later，见仓库 LICENSE。
