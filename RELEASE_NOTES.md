# EX_AM_Exclusive v0.2.0

基于 [AntiOblivionis/AMExclusive](https://github.com/AntiOblivionis/AMExclusive) 的首个改版发布，保留原项目 MIT 许可与署名。

## 新增与修复

- 软件内选择 WASAPI 输出设备或跟随系统默认，选择在重启和升级后保留。
- ASIO 直接输出：64 位驱动选择、左右通道配置、驱动控制面板、采样率及硬件缓冲协商。
- 修复延迟手动切歌通知清除新歌曲 PCM，以及慢速选曲确认提前失效导致的无声问题。
- 检测驱动缺失、重置、回调或时钟停滞，明确报告失败，不自动回退到其他后端。
- 安装器更名为 `EX_AM_Exclusive.exe`，原有安装位置及内部组件名保留，支持覆盖升级。

## 使用

下载 `EX_AM_Exclusive.exe`，运行后选择 `1` 安装/升级。在 Apple Music 的“设置 → 播放 → 独占输出设备”中选择设备并点击“确定”，下次打开歌曲时生效。ASIO 需另行安装厂商的 64 位驱动；默认左右通道为 `1 / 2`。关闭独占模式后可打开 ASIO 控制面板。卸载时运行同一安装器并选择 `2`。

## 验证

Windows x64 Release 编译、8 项运行时测试、9 项 ASIO 测试和 11 个安装器内嵌资源校验通过。使用者反馈全部测试通过；未提供具体硬件与驱动版本，不代表所有设备组合均兼容。ASIO 是否独占硬件取决于驱动。请整体升级所有组件（IPC 3.1）。

---

First EX_AM_Exclusive fork release: in-app WASAPI device selection, direct ASIO output with channel routing and driver control panel, and fixes for silent playback after manual track changes. Install or upgrade with `EX_AM_Exclusive.exe`; existing component names and installation paths are retained. A separate 64-bit vendor driver is required for ASIO. All 17 automated cases and installer resource checks pass; the requester also reports successful testing on their setup. Hardware compatibility and multi-client access depend on the driver.
