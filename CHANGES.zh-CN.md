# EX_AM_Exclusive 改版说明

基于 [AntiOblivionis/AMExclusive](https://github.com/AntiOblivionis/AMExclusive) 上游提交 `4e89a00`。本 fork 以 `EX_AM_Exclusive` 发布，安装包命名为 `EX_AM_Exclusive.exe`，保留原有内部组件、配置和安装位置以支持升级。

## 文件与调用关系

- `mod/src/UiAdapter.cpp`：注入 Apple Music 的 WinUI 设置卡片、观察播放状态、发送控制命令。
- `mod/src/Broker.cpp`：伴生进程，保存 INI 配置、校验连接身份、向音频进程传递控制命令。
- `mod/src/IpcProtocol.h`、`IpcTransport.cpp`：命名管道协议及传输。
- `mod/src/HookDll.cpp`：观察 Apple 解码器、AudioUnit 和原生音频客户端，将 PCM 与生命周期事件交给运行时。
- `mod/src/audio_v2/AudioCoreRuntime.cpp`：管理歌曲代次、预缓冲、切歌和输出接管。
- `mod/src/audio_v2/AudioCoreCoordinator.cpp`：连接 PCM 队列、格式转换和输出 sink。
- `mod/src/audio_v2/WasapiExclusiveSink.cpp`：初始化 Windows WASAPI 独占客户端并提交音频。

首次修改扩展了现有 WASAPI 独占输出。随后根据新增请求加入 ASIO 后端，详见文末。

## 设备选择

“设置 → 播放 → 独占输出设备”列出当前可用的播放端点，并提供“跟随系统默认”。展开列表会重新枚举设备；选择后点击“确定”，“已保存”显示 Broker 确认的配置。

设备 ID 写入 `am-exclusive.ini` 的 `output_device`，重启和安装升级时保留。音频运行时在下一首歌曲绑定时读取选择；当前歌曲不被打断。空 ID 让 sink 在打开时解析 Windows 默认设备，非空 ID 直接打开指定端点，不会偷偷回退到默认设备。设备不可用时可重新连接或选择其他设备。

配置字段与实际输出状态分离，避免音频进程的状态广播覆盖用户选择。IPC 升级为协议 3，须使用同一安装包整体更新组件。

## 切歌无声的两条可复现代码路径

1. 旧歌曲停止并等待解码 EOF，新歌曲已创建图并暂存 PCM，之后 Previous/Next 确认才到达。原代码直接清除旧 capture，同时清除了已核验的新歌曲绑定请求和 PCM。工作线程没有重试目标，导致新歌无法接管。修复保留该继任者及 PCM，并请求绑定。
2. 列表选曲已确认，但加载或消息传输超过 750 毫秒。原代码让确认失效，旧 Stop 被归入自然结束等待。修复让确认绑定于旧歌曲代次，而不是短暂的点击窗口；代次改变后失效。未确认的单击仍不触发取消，旧代次的延迟消息也不会取消新歌曲。

以上是代码与自动测试证实的问题，不能据此断言用户设备上的所有无声情况都来自同一原因。

## 验证结果

使用者已反馈“测试全部通过”。该反馈未提供硬件型号、驱动版本和逐项测试记录；以下开发环境验证与使用者反馈分别记录，不扩展为所有硬件均兼容的结论。

- Windows x64 Release 全量编译成功，生成 `mod/build/dist/EX_AM_Exclusive.exe`。
- 7 个运行时回归用例通过：延迟下一首、慢速列表选曲、延迟选曲确认、旧切歌消息、未确认选曲的自然结束保护、指定/默认设备切换、非法设备命令。
- 测试使用生产 runtime/coordinator 和模拟 sink，验证新歌曲 4,800 帧 PCM 没有丢失或重复，不占用硬件设备。
- 安装器 `--extract-test` 校验 11 个内嵌资源成功。
- 安装到 `C:\Users\Public\AMExclusive`，实际检查了设备枚举、INI 写入、保存提示刷新、重启读取、升级保留和恢复系统默认。
- 从 Microsoft Store 安装 Apple Music `1.7.0.156`（包 `1.1792.39941.0`），安装了 Visual Studio 2022 Build Tools 用于本地构建。界面注入、播放观察器和音频 hook 初始化成功；这不等同于完整验证该版本的播放兼容性。
- 当前仅提供远程音频端点，未登录 Apple Music，未完成订阅曲目和实体 DAC 的独占听音验证。

重跑自动验证：

```powershell
cd mod
.\scripts\Build-Mod.ps1 -Configuration Release
ctest --test-dir build -C Release --output-on-failure
.\build\dist\EX_AM_Exclusive.exe --extract-test
```

实体设备验收建议：登录后选择 DAC，按 README 设置关闭交叉渐入渐出、声音增强器、音量平衡和杜比全景声。分别验证上一首/下一首、列表双击、暂停后选另一首、连续快速切歌、不同采样率切换和自然结束。若仍无声，保留安装目录中的 `am-exclusive.log`、`am-exclusive-ui.log`、`am-exclusive-broker.log` 和可能生成的 `am-exclusive-faults.log`，结合发生时间分析。

## 新增 ASIO 后端

- `AsioDevices.h`：枚举 64 位注册驱动、解析驱动/通道选择、驱动互斥和独立控制面板进程。
- `audio_v2/AsioApi.h`：Windows x64 ASIO ABI 声明；不依赖或分发 SDK 实现。
- `audio_v2/AsioExclusiveSink.cpp`：专用 STA 线程加载、初始化、启动、停止和释放驱动；实时双缓冲回调读取已有 PCM 队列。
- `audio_v2/AsioSampleConversion.h`：整数和浮点通道格式打包，支持大小端及 32 位容器中的 16/24 位对齐格式。
- `audio_v2/AudioOutputSink.h`：按保存的设备选择分派到 WASAPI 或 ASIO，保留现有切歌管理；打开失败不回退。
- 设置页提供 ASIO 驱动、左右输出通道、控制面板和无驱动提示。沿用 `output_device` 保存配置，例如 `asio:{驱动 CLSID}/0/1`；通道内部从 0 开始，界面从 1 开始。IPC 要求 3.1。
- 暂停或缺数据时维持 ASIO 回调，写入静音并保留未消费的源帧。EOF 完成需设备采样时钟超过最后输出及驱动延迟；缺失回调和停滞时钟有超时检查。
- 驱动重置/重同步/延迟或采样率变化会报告失败并退出本次输出，下一次打开歌曲重新协商。未实现任意驱动热重置后的无缝续播。
- 实际设备是否阻止其他应用同时使用取决于驱动，ASIO 协议本身没有强制排他的标志。

新增测试使用模拟 ASIO 驱动，不注册系统驱动、不播放声音。开发环境没有厂商 ASIO 驱动；使用者随后反馈全部测试通过。其他 DAC 与驱动组合仍需验证。

本次验证：Windows x64 Release 编译通过；8 项运行时用例与 9 项 ASIO sink 用例全部通过；安装器 11 个内嵌资源校验通过。已安装到 `C:\Users\Public\AMExclusive`，实际设置页显示 WASAPI/ASIO 说明、驱动缺失提示及原有设备列表，保留“跟随系统默认”配置。厂商驱动控制面板和实际输出通道尚未做硬件验证。

## 发布名称与构建

- 对外项目名称为 `EX_AM_Exclusive`，首个改版版本为 `0.2.0`。
- 本地构建、安装器帮助、版本资源及 GitHub Actions 产物均使用 `EX_AM_Exclusive.exe`。
- GitHub Actions 除编译和安装器资源校验外，还运行运行时及 ASIO 回归测试。
- 中英文 README 明确列出相对上游的新增内容、使用方法、验证范围与上游来源。
