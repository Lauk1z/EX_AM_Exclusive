<p align="center">
  <img src="assets/ame-logo.svg" alt="AME." width="480">
</p>

# EX_AM_Exclusive

### 为 Windows Apple Music 增加设备选择、ASIO 输出与切歌修复

[English](README.md)

[下载安装包 EX_AM_Exclusive.exe](https://github.com/Lauk1z/EX_AM_Exclusive/releases/latest/download/EX_AM_Exclusive.exe) · [版本发布](https://github.com/Lauk1z/EX_AM_Exclusive/releases) · [在线构建](https://github.com/Lauk1z/EX_AM_Exclusive/actions/workflows/build-setup.yml)

[原项目 AMExclusive](https://github.com/AntiOblivionis/AMExclusive) · [改版说明](CHANGES.zh-CN.md) · [原项目使用文档](https://amexclusive.tgwgroup.ltd/zh/docs/)

## 描述

EX_AM_Exclusive 是基于 [AntiOblivionis/AMExclusive](https://github.com/AntiOblivionis/AMExclusive) 上游提交 `4e89a00` 的改版。在保留 WASAPI 独占输出、强制最高音质等原有能力的基础上，加入软件内输出设备选择、ASIO 后端，并修复手动切歌后播放进度正常但没有声音的两处状态竞态。保留原项目及贡献者署名，沿用 MIT 许可证。

## 本改版新增内容

- **软件内选择输出设备**：选择指定 WASAPI 端点或跟随系统默认，无需更改 Windows 输出设置；重启和升级后保留选择。
- **ASIO 直接输出**：选择已安装的 64 位 ASIO 驱动、配置左右输出通道、打开厂商控制面板，支持采样率与缓冲区协商。设备是否排斥其他应用由驱动决定。
- **手动切歌无声修复**：延迟切歌通知不再清除已建立的新歌曲及其 PCM 缓存；加载较慢时，已确认的选曲操作不会因短时间窗口到期而丢失。
- **输出异常明确反馈**：驱动缺失、设备不可用、驱动重置、回调或时钟停滞时报告错误，不悄悄回退到其他输出后端。
- **验证结果**：8 项运行时测试、9 项 ASIO 测试及安装器资源校验通过。使用者反馈“测试全部通过”；尚未提供硬件型号、驱动版本和具体覆盖范围，因此不代表所有设备组合均已验证。

安装包统一命名为 **`EX_AM_Exclusive.exe`**。为兼容原有安装和升级，内部组件名称及 `AMExclusive` 安装目录保留。输出设置在下次打开歌曲时生效。

## 开发与测试环境

- Windows 11 x64
- Windows 版 Apple Music 文件版本 `1.6.4.90`
- 已验证包/框架版本 `1.1540.23042.0` x64
- Windows 版 Apple Music 文件版本 `1.7.0.155`
- 已验证包/框架版本 `1.1792.39682.0` x64

以上为原项目记录的测试环境。本改版在 Windows 11 x64、Apple Music `1.7.0.156`（包 `1.1792.39941.0`）上完成编译及设置界面验证，随后收到使用者全部测试通过的反馈。其他版本与硬件组合仍需自行验证。

## 特性

1. **WASAPI 独占输出**  
   以独占模式直接向所选输出设备提交音频，绕过常规共享模式下的 Windows 音频混音路径。在软件内选择设备，无需修改 Windows 默认输出。

2. **强制最高音频规格**  
   去除 Apple Music 原生“低带宽自动降低音频规格”的逻辑，在最高可用无损规格尚未就绪时继续加载等待，而不是自动回退到更低音频规格。

3. **Bit-perfect 输出**  
   在受支持的播放路径中保持源音频规格，并将整数 PCM 直接提交给 WASAPI；当音源、输出设备与目标格式匹配时，可实现 bit-perfect 播放。

4. **可选采样率与位深转换以兼容设备**

   开启兼容开关后，先尝试原采样率的无损输出；设备不支持时，允许升／降采样、位深转换及 float32 输出，保留 WASAPI 独占和现有音质锁定。例如，44.1 kHz/24-bit 和 96 kHz/24-bit 音源都可转换至仅支持 48 kHz/16-bit 的设备。每个输出格式都必须通过驱动的独占支持检查与初始化，不回退到共享模式。探测覆盖常见整数容器、float32、WAVEFORMATEX／EXTENSIBLE 格式声明；设备的混音采样率也会作为候选，不能直接当作独占支持的证据。

   在 44.1 kHz 及以上（音源更低时以原采样率为准），优先保留音源精度，再尝试 float32／24-bit、16-bit；同一精度组先原采样率，再从最近的更高采样率尝试，最后从高到低尝试更低采样率。更低带宽的输出作为最后选择。升采样使用带限插值抑制频谱镜像，降采样抑制混叠；降低位深使用 TPDF 抖动和饱和舍入。兼容模式也允许本地 int32 音频通过 Apple 解码后的 float32 数据播放，不宣称恢复原始 int32 的 bit-perfect。转换后的输出会明确标注。关闭开关时保留现有无损格式策略与 PCM 路径。

5. **ASIO 直接输出**

   可在输出设备列表中选择已安装的 64 位 ASIO 驱动，并指定左右输出通道。复用 PCM 捕获、切歌管理和可选格式转换，通过驱动的双缓冲回调输出；不经过 WASAPI sink。ASIO 没有统一的“独占开关”，是否允许其他程序同时使用硬件由驱动决定。本软件对同一 ASIO 驱动只允许一个 AMExclusive 输出实例。

## 推荐设置

在 Apple Music 的 **设置 > 播放** 中：

- 关闭 **交叉渐入渐出**。
- 关闭 **声音增强器**。
- 关闭 **音量平衡**。
- 开启 **无损音频**。
- 将无损音频下方的两项音频规格均设为 **高解析度无损（ALAC）**。
- 关闭 **杜比全景声**。
- 开启 **独占模式**。
- 在 **独占输出设备** 中选择设备并点击 **确定**，查看“已保存”确认结果。选择会保留到下次启动，并在下次打开歌曲时生效；展开列表会刷新已连接设备。选择 **跟随系统默认** 可在每次打开输出时使用 Windows 默认设备。指定设备不可用时会报错，不会自动切换到其他设备。
- 使用 ASIO：先安装声卡/DAC 厂商的 **64 位 ASIO 驱动**，再选择列表中的 **ASIO · 驱动名称**。左右通道从 `1` 开始，默认 `1 / 2`，必须对应驱动实际提供的两个不同输出通道。关闭独占模式后可点击 **打开 ASIO 控制面板**。驱动不随本项目分发。
- 在 **硬件缓冲区** 中输入 `1`–`100` 毫秒的整数并点击 **确定**。该数值会在下一首歌曲打开独占输出时应用。默认值为 `20` 毫秒，遇到播放问题时可尝试增加数值。
- ASIO 将目标缓冲时长换算为驱动支持的最接近帧数；固定缓冲驱动使用其首选值。更改采样率或切换歌曲时会释放并重新打开驱动。
- 设备无法播放原规格时，可开启 **设备不支持原规格时允许兼容格式转换**，下次打开歌曲时生效。
- 关闭 **空间音效**。

使用独立 DAC 和放大器时，将 Apple Music 应用内音量、Windows 输出音量和 DAC 输出音量保持在最大值，只通过放大器调整实际播放音量。使用 DAC/AMP 一体机时，将 Apple Music 应用内音量和 Windows 输出音量保持在最大值，通过 DAC/AMP 一体机的硬件音量控制调整实际播放音量。首次应用这些设置前，请先降低放大器或 DAC/AMP 一体机的硬件音量，避免突然出现过高声压。

## 编译与安装

### 使用 GitHub Actions 编译

1. 打开本 fork 的 **Actions → Build EX_AM_Exclusive**。如 GitHub 提示，请先在自己的仓库中启用 Actions。
2. 点击 **Run workflow**，选择分支并运行。
3. 成功后，在该次运行的 **Artifacts** 中下载 **EX_AM_Exclusive-x64**，解压得到 `EX_AM_Exclusive.exe`。

工作流仅手动触发，编译 Windows x64 Release、运行运行时与 ASIO 回归测试并检查安装器内嵌文件，不自动发布 Release。构建机不需要安装 Apple Music：工作流会从 NuGet 下载固定版本的 Microsoft Windows App SDK / WinUI 元数据包，并在编译前使用固定 SHA-256 校验 `Microsoft.UI.Xaml.winmd`。安装 EX_AM_Exclusive 的电脑仍需安装 Apple Music。构建产物保留 14 天，下载需登录 GitHub。

### 本地编译

从源码编译需要 Visual Studio 2022 或更新版本的 MSVC Build Tools、Windows 11 SDK、CMake 3.24 或更新版本和 PowerShell。默认情况下，构建脚本会从本机安装的 Windows 版 Apple Music 中读取所需 WinUI 元数据：

```powershell
cd mod
.\scripts\Build-Mod.ps1 -Configuration Release
```

构建机没有安装 Apple Music 时，也可以先准备独立元数据目录再编译：

```powershell
cd mod
.\scripts\Get-WinUIMetadata.ps1 -OutputDirectory .\build\winui-metadata
.\scripts\Build-Mod.ps1 -Configuration Release -WinUIMetadataDirectory .\build\winui-metadata
```

安装器输出位置：

```text
mod\build\dist\EX_AM_Exclusive.exe
```

安装或升级时，运行 `EX_AM_Exclusive.exe` 并选择 `1`。随后打开 Apple Music，进入 **设置 > 播放**，开启 **独占模式**。

卸载时，运行同一个 Setup 并选择 `2`，或在 **Windows 设置 > 应用 > 已安装的应用** 中卸载 **AMExclusive**。

### 切歌修复与验证

手动切歌时，旧歌曲可能已停止，新歌曲的音频图与 PCM 已就绪，但确认切歌的 IPC 消息尚未送达。原有 Previous/Next 处理会清除等待接管的新歌曲及其缓存，导致无法继续绑定。现在只退出旧歌曲，保留身份核验过的新歌曲并触发绑定。

列表选曲的已确认操作原先也受 750 毫秒窗口限制。加载较慢或 IPC 延迟时，旧歌曲停止会被误判为自然结束并等待解码 EOF。现在已确认操作跟随旧歌曲代次有效，代次变化后自动失效；未确认的单击仍不会取消自然播放。

`mod/tests/RuntimeTransitionTests.cpp` 覆盖延迟切歌、慢速选曲、过期消息、自然结束保护、设备选择及 ASIO 缺失后的恢复。`mod/tests/AsioSinkTests.cpp` 使用模拟 ASIO 驱动覆盖回调、格式打包、暂停、等待、时钟排空及驱动错误。测试不访问音频硬件；实际 Apple Music 与 DAC 的听音验证仍需单独执行。所有组件须一起升级，ASIO 功能要求 IPC 协议 3.1。

ASIO 当前支持双声道 PCM：16/24/32 位整数、32 位容器中的 16/24 位整数、float32/float64，以及这些格式的大端/小端布局。不支持 DSD、18/20 位容器有效精度或多声道音源。严格模式拒绝降位深或改变采样率；开启兼容转换后可按驱动能力重采样和抖动降位深。状态栏显示的位深是输出所能保留的 PCM 精度，float32 为 24 位精度，float64 可保留这里的 32 位 PCM。

ASIO 驱动时钟在暂停和源数据不足时继续运行，回调填充静音但不消费缺失的源帧，数据到达后继续。驱动重置、外部采样率变化、时钟倒退或停止回调会停止本次输出并报告错误；重新选择/播放歌曲会创建新输出，不自动将当前歌曲倒回或回退到其他后端。自动测试使用模拟驱动；使用者已反馈测试通过，具体硬件与驱动组合仍需自行验证。

## 致谢

感谢 [LINUX DO 社区](https://linux.do/) 在项目开发与测试过程中提供的交流、支持与反馈。

本项目使用了以下开源项目：

- [MinHook](https://github.com/TsudaKageyu/minhook) — 提供 Windows x64/x86 API Hook 基础能力。
- [rigtorp/SPSCQueue](https://github.com/rigtorp/SPSCQueue) — 提供音频实时路径使用的单生产者、单消费者有界队列。

感谢上述项目及其贡献者。完整版本、版权和许可证信息见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 协议与声明

项目自行编写的源码使用 [MIT License](LICENSE)。第三方组件继续遵循其各自的上游许可证，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

EX_AM_Exclusive 是非官方 AMExclusive 项目的独立 fork。两个项目均与 Apple Inc. 没有任何关联，也未获得其许可或背书。Apple Music 是 Apple Inc. 的商标。

本项目不保证在不同软件、硬件、驱动程序或音频设备条件下能够正常运行。使用本项目可能造成不可预知的后果，使用者应自行承担相关风险。

本项目不分发 Apple 软件、Apple 二进制文件、解密后的媒体或 DRM-free 参考文件，也不提供用于绕过 DRM 保护的功能。
