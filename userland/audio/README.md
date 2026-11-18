# I9100 iOS6 physical audio adapter

This is a default-off `.pdev` for the original iOS6 CoreAudio physical-device
loader. It connects to `IOS6I9100AudioPCM`, then the real owned PCM workloop.
It is an output-only development adapter, not a replacement for VirtualAudio,
IOAudio2, audio routing, microphone capture or the complete media service.

The original CoreAudio ARM call sites were checked against compiler-produced
callees: 15 exports, byte-sized Boolean output, property length pointers,
two by-value doubles and the seventh stream-buffer argument. See the project
research `AUDIO-PDEV-INTEGRATION-20261118.md` for evidence and limits.

An explicit kernel board profile `use_gated_dma=1` now selects the guarded
PL330 loop through the same worker/user-client/pdev ABI. Its default remains
zero. Userspace cannot select that mode or grant output permission. The gated
path currently polls with its GIC sources masked; `use_irq=1` is rejected.
See project research `AUDIO-GATED-PIPELINE-20261119.md` for the current complete
link, source audit and real-device acceptance steps.

Build in WSL, under the project CPU envelope:

```sh
python3 -B userland/audio/build.py
python3 -B port-tests/check_i9100_audio_pdev.py
python3 -B port-tests/check_i9100_pdev_abi.py
python3 -B port-tests/check_i9100_audio_gated_pdev.py
```

Each tool applies the CPU envelope and writes retained evidence under repo
`out/`. `build.py` audits imports against the original iOS6 cache and creates
symbol-only linker interfaces. Those interfaces are not runtime libraries and
must never be copied onto the phone. Only `I9100Audio.pdev/` is the bundle.
The output is unsigned and not installed; signing and real CoreAudio loading
remain unverified. No rootfs/SD/flash or USB operation is performed by these
commands.

`Info.plist` has `I9100AudioPDevEnabled=false`. The kernel must also explicitly
create the service from an independently verified board/clock profile. Initial
validation keeps `approved_low_output=0`, so PCM runs with outputs muted/off.
The kernel permission is immutable from userspace; allowing low output still
requires an actual completed zero period and native qualification before
digital −30dB / analog −36dB. A not-ready activation fails and closes the
session; no PCM buffer or failed command is automatically replayed.

Acquire returns a private CPU buffer. Write copies a bounded signed16 block
into the real user client and workloop; no user DMA address is exposed.
GetZeroTimeStamp uses confirmed FIFO completion frames and native clock
observations, not an elapsed-time completion estimate. These timestamps do
not establish DAC or acoustic position, and finite jobs may still underrun.
The gated path confirms a single hardware fence, feeds data or zeros, then
releases one period. Real workloop/FIFO latency and continuity remain unproven.

Publication callbacks run outside the pdev mutex; in-flight callbacks and
teardown are guarded. A late buffer release is allowed after shutdown starts,
while new writes/initialization are refused. Driver/RPC errors close once and
preserve a failed kernel owner for explicit cleanup. Full hardware clock,
power, gain direction, pop/stop waveforms and real HAL acceptance are still
required before calling this a working sound device.
