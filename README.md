<p align="center">
  <img src="assets/ame-logo.svg" alt="AME." width="480">
</p>

# EX_AM_Exclusive

### Apple Music for Windows with WASAPI device selection and ASIO output

[中文](README.zh-CN.md)

[Download EX_AM_Exclusive.exe](https://github.com/Lauk1z/EX_AM_Exclusive/releases/latest/download/EX_AM_Exclusive.exe) · [Releases](https://github.com/Lauk1z/EX_AM_Exclusive/releases) · [Build workflow](https://github.com/Lauk1z/EX_AM_Exclusive/actions/workflows/build-setup.yml)

[Upstream AMExclusive](https://github.com/AntiOblivionis/AMExclusive) · [Fork changes](CHANGES.zh-CN.md) · [Upstream documentation](https://amexclusive.tgwgroup.ltd/en/docs/)

## Description

EX_AM_Exclusive is a fork of [AntiOblivionis/AMExclusive](https://github.com/AntiOblivionis/AMExclusive), based on upstream commit `4e89a00`. It retains the original WASAPI exclusive output and highest-quality audio policy, and adds selectable output devices, an ASIO backend, and fixes for silent playback after manual track changes. The original project and its contributors remain credited under the MIT license.

## Added in This Fork

- **In-app output selection:** choose a WASAPI endpoint or follow the system default without changing Windows settings. Device preferences survive restarts and upgrades.
- **ASIO output:** select a registered 64-bit driver, route left/right output channels, open its control panel, and negotiate supported sample rates and buffer sizes. ASIO access by other applications depends on the driver.
- **Manual track-change fixes:** preserve the successor's queued PCM when skip notifications arrive late, and keep confirmed selection valid for the old track's generation during slow transitions.
- **Explicit failure handling:** missing drivers/devices, reset notifications and stalled callbacks report errors instead of silently switching backends.
- **Validation:** 8 runtime tests and 9 ASIO sink tests pass, along with installer payload verification. The requester reports all their tests passed; hardware model, driver version and test coverage were not supplied, so this is not a universal compatibility claim.

The installer is named **`EX_AM_Exclusive.exe`**. Internal component names and the existing `AMExclusive` installation directory are retained for upgrade compatibility. Output changes apply when the next song opens.

## Development and Test Environment

- Windows 11 x64
- Apple Music for Windows file version `1.6.4.90`
- Verified package/framework build `1.1540.23042.0` x64
- Apple Music for Windows file version `1.7.0.155`
- Verified package/framework build `1.1792.39682.0` x64

The versions above are the upstream's documented test environment. This fork was built and its settings UI checked with Apple Music `1.7.0.156` / package `1.1792.39941.0` on Windows 11 x64. The requester subsequently reported all tests passed on their setup. Other combinations remain unverified.

## Features

1. **WASAPI exclusive output**  
   Sends audio directly to the selected Windows output device in exclusive mode, avoiding the normal shared-mode Windows audio mixing path.

2. **Forced highest audio quality**  
   Removes Apple Music's native low-bandwidth quality downgrade behavior and keeps waiting for the highest available lossless stream instead of automatically falling back to a lower audio specification.

3. **Bit-perfect output**  
   Preserves the source sample format through the supported playback path and submits integer PCM directly to WASAPI, allowing bit-perfect playback when the source, device, and supported output format match.

4. **Optional sample-rate and bit-depth conversion for device compatibility**

   The optional compatibility switch first tries lossless output at the original rate, then permits up/downsampling, bit-depth conversion and float32 output while retaining WASAPI exclusive output and the existing quality lock. For example, both 44.1 kHz/24-bit and 96 kHz/24-bit sources can use a 48 kHz/16-bit-only endpoint. Each output format must pass the driver's exclusive support check and initialization; shared-mode output is never a fallback. The probe includes common integer containers, float32, alternate WAVEFORMATEX/EXTENSIBLE declarations and the device's mix rate as a candidate only.

   Among rates at or above 44.1 kHz (or the source rate if lower), source precision is preferred, then float32/24-bit, then 16-bit. Within each precision group, try the original rate, the nearest higher rates, then descending lower rates. Lower-bandwidth outputs are the last resort. Upsampling uses a band-limited interpolation filter to suppress spectral images; downsampling suppresses aliasing. Depth reduction uses TPDF dither and saturating rounding. Compatibility mode also accepts local int32 audio through Apple's decoded float32 stream without claiming original int32 bit-perfect recovery. Converted output is labelled explicitly. With the switch off, the existing lossless format policy and PCM path are unchanged.

5. **ASIO direct output**

   Select an installed 64-bit ASIO driver and two output channels in the device list. The backend reuses PCM capture, track transitions and optional conversion, and writes directly to ASIO double buffers. ASIO has no universal exclusive-mode switch: access by other applications depends on the driver. AMExclusive permits only one of its own instances to own a driver at a time.

## Recommended Settings

In Apple Music, open **Settings > Playback** and use the following settings:

- Turn **Crossfade** off.
- Turn **Sound Enhancer** off.
- Turn **Sound Check** off.
- Turn **Lossless Audio** on.
- Set both audio-quality selectors below Lossless Audio to **Hi-Res Lossless (ALAC)**.
- Turn **Dolby Atmos** off.
- Turn **Exclusive Mode** on.
- Enter an integer from `1` through `100` milliseconds in **Hardware buffer** field and select **Apply**. The setting is used when exclusive output opens for the next song. The default is `20` ms; try a higher value if encountering playback problems.
- Enable **Allow compatible format conversion for unsupported formats** if your device cannot play the original format. The setting applies when the next song opens.
- Choose **Exclusive output device**, click **Apply**, and check the **Saved** label. The selection persists and applies when the next song opens. Opening the list refreshes connected devices. **WASAPI · Follow system default** resolves the Windows default each time output opens. An unavailable explicit device reports a failure instead of silently switching outputs. Upgrade all components together (IPC protocol 3.1).
- For ASIO, install the manufacturer's **64-bit driver**, select **ASIO · driver name**, and set distinct left/right output channels (one-based, default `1 / 2`). Turn exclusive mode off to open the driver's control panel. Drivers are not bundled. ASIO uses the nearest supported buffer size, or the driver's fixed preferred size.
- Turn **Spatial sound** off.

With a separate DAC and amplifier, keep the Apple Music app volume, Windows output volume, and DAC output volume at maximum, and adjust listening volume only on the amplifier. With an integrated DAC/amp, keep the Apple Music app volume and Windows output volume at maximum, and adjust listening volume using the DAC/amp hardware control. Before applying these settings for the first time, lower the amplifier or DAC/amp hardware volume to avoid an unexpectedly high sound level.

## Build and Install

ASIO currently supports stereo PCM with 16/24/32-bit integer, 16/24-bit integer in a 32-bit container, and float32/float64 buffers, in either byte order. DSD and 18/20-bit aligned formats are rejected. The displayed precision is the retained PCM precision (24 for float32, up to 32 for float64). Strict mode rejects sample-rate changes or precision loss; compatibility mode permits resampling and dithered depth reduction. Every track opens a new driver stream.

ASIO callbacks output silence during pause or source starvation without consuming missing media. Reset/resync/latency notifications, external sample-rate changes, and stopped/backward clocks fail the current output explicitly. Selecting/retrying a track opens a fresh stream; there is no automatic backend fallback. Automated tests use a simulated driver; the requester also reports their tests passed. Validate your own driver and hardware combination.

### Build with GitHub Actions

1. Open this fork's **Actions → Build EX_AM_Exclusive**. Enable Actions in your fork if GitHub prompts you to do so.
2. Select **Run workflow**, choose the branch, and run it.
3. When the run succeeds, download **EX_AM_Exclusive-x64** from **Artifacts** and unzip it to obtain `EX_AM_Exclusive.exe`.

The workflow runs only when manually triggered. It builds Windows x64 Release, runs the runtime and ASIO regressions, and checks the embedded installer payload; it does not publish a GitHub Release. No Apple Music installation is needed on the runner: it downloads pinned Microsoft Windows App SDK / WinUI metadata packages from NuGet and verifies `Microsoft.UI.Xaml.winmd` against a pinned SHA-256 before building. Apple Music is still required on the PC where you install EX_AM_Exclusive. Artifacts are retained for 14 days; downloading them requires signing in to GitHub.

### Build locally

To build from source, install Visual Studio 2022 or newer MSVC Build Tools, a Windows 11 SDK, CMake 3.24 or newer, and PowerShell. By default, the build script reads the required WinUI metadata from the locally installed Apple Music package:

```powershell
cd mod
.\scripts\Build-Mod.ps1 -Configuration Release
```

Apple Music does not need to be installed on the build machine if you provide standalone metadata instead:

```powershell
cd mod
.\scripts\Get-WinUIMetadata.ps1 -OutputDirectory .\build\winui-metadata
.\scripts\Build-Mod.ps1 -Configuration Release -WinUIMetadataDirectory .\build\winui-metadata
```

The installer will be generated at:

```text
mod\build\dist\EX_AM_Exclusive.exe
```

To install or upgrade, run `EX_AM_Exclusive.exe` and choose `1`. Then open Apple Music, go to **Settings > Playback**, and enable **Exclusive mode**.

To uninstall, run the same setup and choose `2`, or uninstall **AMExclusive** from **Windows Settings > Apps > Installed apps**.

## Acknowledgements

Thanks to the [LINUX DO community](https://linux.do/) for the discussion, support, and feedback provided during the development and testing of this project.

This project uses the following open-source projects:

- [MinHook](https://github.com/TsudaKageyu/minhook) — provides the Windows x64/x86 API hooking foundation.
- [rigtorp/SPSCQueue](https://github.com/rigtorp/SPSCQueue) — provides the bounded single-producer, single-consumer queue used on the real-time audio path.

Thanks to these projects and their contributors. Complete version, copyright, and license information is available in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License and Disclaimer

Project-authored source code is released under the [MIT License](LICENSE). Third-party components retain their respective upstream licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

EX_AM_Exclusive is an independent fork of the unofficial AMExclusive project. Neither project is affiliated with, endorsed by, or licensed by Apple Inc. Apple Music is a trademark of Apple Inc.

This project is not guaranteed to operate correctly across different software, hardware, driver, or audio-device configurations. Its use may cause unforeseen consequences, and users assume the associated risks.

This project does not distribute Apple software, Apple binaries, decrypted media, or DRM-free reference files, and it does not provide functionality intended to bypass DRM protection.
