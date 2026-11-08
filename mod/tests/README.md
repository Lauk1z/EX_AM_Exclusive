# WASAPI drain hardware regression probe

## Runtime transition regressions (no audio hardware)

Normal builds include `am_exclusive_runtime_tests` when `BUILD_TESTING` is enabled.
Run `ctest --test-dir <build-directory> -C Release --output-on-failure`.
The runtime and coordinator are production code; only the WASAPI sink is replaced
by `FakeWasapiSink.cpp`, which never opens a device or plays audio.

Cases cover a delayed skip after successor staging, confirmed selection with a
slow graph transition, delayed selection confirmation, stale skip delivery,
unconfirmed selection preserving natural drain, selected/default device changes
at generation boundaries, and malformed device commands. These tests verify
control flow and capture preservation, not real-device playback quality.

## ASIO regressions

`am_exclusive_asio_tests` runs the real ASIO sink against an injected driver
implementing the Windows ASIO ABI. No registry registration, external driver,
or audio hardware is needed. It covers integer/float packing and byte order,
channel selection, buffer constraints, same-thread driver lifetime, pause/resume,
source starvation, full/partial/empty EOF, clock-certified drain, frozen clocks,
reset/resync/latency/rate-change notifications, failure cleanup, callback timeout,
and single-owner leases. The runtime tests also verify a missing ASIO driver
does not fall back to WASAPI and that a subsequent WASAPI track can recover.

For real ASIO acceptance, use the in-app device selector with a manufacturer's
64-bit driver. Verify output channel routing, several supported sample rates,
pause/resume, repeated manual skips, natural transitions, and device disconnect.
The simulator cannot establish vendor-driver or hardware compatibility.

## WASAPI hardware probe

Build explicitly (the target is excluded from normal builds):

```powershell
cmake --build <build-directory> --config Release --target am_exclusive_drain_probe
<build-directory>\Release\am_exclusive_drain_probe.exe --run-on-default-device
```

This acquires the current default render endpoint in exclusive mode and sends
only silence. Stop other playback first. The device must support stereo packed
24-bit and 16-bit PCM at 192, 96 and 44.1 kHz. Unsupported formats are reported
as failures, not skipped. It is deliberately not registered as an unattended
CTest.

The 36 cases cover empty, partial and full terminal packets, source starvation,
pause/resume during drain, initially empty sources, and producer notifications
as well as polling. The source rejects any request after its terminal packet.
Each case requires clock-certified completion without underruns or errors.

Motivation: on a C-Media-based USB endpoint (Fosi Audio DA2120C), end-of-stream
drains that followed a source starvation wait timed out intermittently (3-4 of
the 36 cases per run, always `wait=1` cases). The device loses its event/clock
cycle when a packet is released into a stopped, previously run exclusive
stream: IAudioClock parks below the committed end-of-stream position and the
drain can only fault. Restarting the preserved endpoint queue before releasing
the next packet (with a readiness check first, so the restarted stream is
filled on the same wake) made all 36 cases pass repeatedly on the same
hardware.

These are transport/control tests. They do not establish audible correctness
or Apple Music lifecycle behavior.
