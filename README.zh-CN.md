# OMS/OFilm 指纹驱动

[English](README.md)

可选功能：[独立登录／锁屏动画插件](ui/README.zh-CN.md)，无需重编译 KDE。

可选功能：[sudo 等终端命令的指纹状态面板](terminal/README.zh-CN.md)。

为 USB **`33a7:2388`** 指纹传感器编写的实验性 libfprint 驱动，支持多指纹
槽位分配、fprintd 验证和 KDE 指纹管理。不是厂商或 libfprint 官方版本。

## 支持哪些设备

| 项目 | 当前实测 |
| --- | --- |
| 指纹模组 | OMS/OFilm 芯片内匹配传感器，USB **`33a7:2388`** |
| 电脑 | **机械革命无界 14 Pro（WUJIE 14 Pro）** |
| CPU | 测试者称 **8845S**；本机 `lscpu` 检测为 **AMD Ryzen 7 8845HS** |
| 系统 | Fedora 44 KDE，x86_64 |
| 桌面 | KDE Plasma 6.7.5 |
| libfprint | **1.94.100** |

**以传感器 USB ID 为准，不是所有无界 14 Pro 或 8845S 电脑都必然适用。**
不同批次可能更换模组。先运行：

```bash
lsusb -d 33a7:2388
```

没有输出就不能据此认为支持。未验证其他 OMS/OFilm 型号，也不支持 Goodix、
ELAN、FPC、Realtek、PixelAuth PXAT 等其他模组。
本项目构建只启用 OMS 驱动，替换系统库后，该库中的其他指纹驱动不可用。

## 安装

目前安装脚本仅面向 **Fedora x86_64、系统 libfprint 1.94.100**。
其他发行版或不同库版本需要移植，不要直接覆盖不同版本。

安装依赖：

```bash
sudo dnf install git curl patch gcc gcc-c++ meson ninja-build pkgconf-pkg-config \
  glib2-devel libgusb-devel gobject-introspection-devel systemd-devel \
  libfprint fprintd fprintd-pam usbutils
```

用平时登录桌面的用户下载并构建：

```bash
git clone https://github.com/xiecheesejohn/libfprint-oms-driver.git
cd libfprint-oms-driver
bash scripts/build.sh
```

构建脚本下载并校验固定版本的 libfprint 源码，再应用补丁、编译和运行测试。
不需要 Windows 驱动、厂商固件或反编译文件。

关闭正在录入或验证指纹的窗口，然后安装：

```bash
sudo bash scripts/install-fedora.sh --install
```

安装会备份现有库和原有 USB 电源规则，再安装并重启 fprintd。
**请保存输出的备份目录。** 不修改 PAM，不删除现有指纹，也不重启桌面。

## 录入和使用

以下命令用正常桌面用户执行，**不要加 sudo**：

```bash
fprintd-list "$USER"
kcmshell6 kcm_users
```

在“**系统设置 → 用户 → 当前用户 → 指纹**”中添加手指，按提示反复触摸和移开。
每根手指分别录入。驱动会查询芯片占用表并选择空槽，不再把所有手指写入槽 0。

也可以使用终端：

```bash
fprintd-enroll -f right-index-finger "$USER"
fprintd-enroll -f right-middle-finger "$USER"
fprintd-verify -f any "$USER"
```

先测试：正确手指应通过、未登记手指应拒绝、不触摸应超时拒绝。
之后按 **Meta + L** 测试锁屏解锁，保留密码作为登录方式。

如果 Fedora 的 authselect 尚未启用指纹功能：

```bash
authselect current
sudo authselect enable-feature with-fingerprint
authselect check
```

这要求所用 profile 支持该功能。**锁屏解锁和开机首次登录不是同一套认证入口。**
实测机的 Plasma Login Manager 使用不含指纹模块的 `password-auth`；本项目
不自动修改该配置，因此不能把锁屏成功当成开机登录已支持指纹。

## 已验证和未验证

- KDE 中录入两根手指，分别使用槽 0、槽 1。
- 正确手指验证成功；错误手指收到芯片明确的不匹配结果。
- 不触摸不会通过；KDE 锁屏解锁已确认可用。
- 验证限定指定槽；`any` 识别只接受当前用户登记列表中的槽。
- **待验证：**完整冷启动、休眠唤醒、真实芯片删除、多用户和其他固件版本。
- 占用表为 256 位，**不代表实际支持 256 枚指纹**，硬件容量尚未确认。
- 兼容旧原型的槽 0 记录；旧记录没有以密码学方式绑定芯片模板的所有者。

这是认证相关的实验性代码，单台电脑测试成功不等于完成安全审计。

## 重启、更新和回退

库、登记记录和 USB 防自动休眠规则会保留。fprintd 通过 D-Bus 按需启动，
显示 `static` 或空闲时退出属于正常情况，不需要 enable。
完整冷启动仍待实测；重启后运行 `fprintd-list "$USER"` 和
`fprintd-verify -f any "$USER"` 检查。

**系统更新或重装 libfprint 可能覆盖自定义库。** 本项目还不是 Fedora RPM。
不要把本项目旧版库强装到新版系统包上，应先适配对应版本。

回退时使用安装输出的实际备份路径：

```bash
sudo bash scripts/restore-fedora.sh /var/lib/libfprint-oms-backup.XXXXXXXX
```

回退恢复库和电源规则，不删除指纹；也无法恢复已经从芯片删除的模板。
删除接口已实现但尚未做真实删除测试，不要通过删指纹来排查其他问题。

## 排错

```bash
systemctl status fprintd --no-pager
sudo journalctl -u fprintd -b -n 100 --no-pager
```

- `Not Authorized`：从正常桌面会话打开设置，不要从 root 终端仅切换用户名
  启动 GUI；不需要放宽所有 Polkit 权限。
- `Already claimed`：关闭其他指纹程序；确认无录入操作后可重启 fprintd。
- 空间不足：新版选择空槽；仍失败需检查占用表和芯片返回值，不自动冒认旧模板。
- 不匹配：多手指测试使用 `-f any`，附上脱敏日志和 USB ID 反馈。

不要在公开 issue 上传原始抓包、指纹数据、账户信息或设备序列号。
协议说明见 [docs/protocol.md](docs/protocol.md)，测试范围见
[docs/testing.md](docs/testing.md)。项目采用 **LGPL-2.1-or-later**，保留上游署名；
发布二进制时须按许可证提供对应源码和构建材料。见 [LICENSE](LICENSE)、[NOTICE](NOTICE)。
