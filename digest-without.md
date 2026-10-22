**frames 560:100** from present 560: 100 presents, frame p50 958.0 / p90 1456.9 / p99 3292.4 / max 3292.4 ms, GPU busy 434.1 ms per present
AMD Radeon RX 6650 XT (RADV NAVI23), radv Mesa 26.2.4-arch1.1, Linux 7.2.9-arch1-1, 3840x2160, commit 4ff898faefa3, dropped: yes

```json
{"commit":"4ff898faefa3","dirty":false,"env":{"APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_DIR":"<path>","APS5_TRACE_FRAMES":"560:100"},"gpu":"AMD Radeon RX 6650 XT (RADV NAVI23)","driver":"radv Mesa 26.2.4-arch1.1","os":"Linux 7.2.9-arch1-1","cpu":"AMD Ryzen 9 7950X 16-Core Processor","cpu_threads":32,"memory_mib":127901,"title_id":"PPSA25636","title":"Echoes of the End","content_version":"01.012.000","resolution":"3840x2160","window":{"spec":"frames 560:100","first_present":560,"ms":105019.366},"presents":100,"frame_ms":{"p50":958.0,"p90":1456.922,"p99":3292.388,"max":3292.388},"gpu_busy_ms_per_present":434.116,"worker_busy_pct":{"queue worker 0x0":99.794,"queue worker 0x20":97.524},"top_phases_ms_per_frame":{"Driver.Packet":2048.79,"Driver.Packet.dispatch":1056.775,"Driver.Dispatch":992.721,"Driver.Packet.memory_wait":677.391,"Vulkan.Dispatch":628.122,"Vulkan.Dispatch.shader_resources":616.824,"Resources.Complete":615.461,"GpuMutex.wait.indirect":147.167},"trace_dropped":true,"trace_dropped_threads":{"queue worker 0x0":{"events_full_after_s":41.83,"events_lost":484625,"totals_full":false}},"trace":"aps5-trace-4ff898faefa3-3124014.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 602: 3292.4 ms at 49530 ms: Driver.Packet (queue worker 0x20) 3256.6, Driver.Packet.dispatch (queue worker 0x20) 2461.1, Driver.Dispatch (queue worker 0x20) 2420.4, Vulkan.Dispatch (queue worker 0x20) 2351.2
- present 569: 2500.2 ms at 10973 ms: Driver.Packet (queue worker 0x0) 2478.4, Driver.Packet (queue worker 0x20) 2425.1, Driver.Packet.dispatch (queue worker 0x0) 1209.8, Driver.Packet.dispatch (queue worker 0x20) 1203.2
- present 570: 2090.1 ms at 13473 ms: Driver.Packet (queue worker 0x0) 2067.6, Driver.Packet (queue worker 0x20) 1993.0, Driver.Packet.memory_wait (queue worker 0x20) 1130.1, Resources.Complete (queue worker 0x0) 928.7
- present 563: 1791.4 ms at 3477 ms: Driver.Packet (queue worker 0x0) 1769.8, Driver.Packet (queue worker 0x20) 1701.2, Driver.Packet.dispatch (queue worker 0x20) 864.2, Driver.Packet.memory_wait (queue worker 0x20) 817.0
- present 575: 1718.5 ms at 19989 ms: Driver.Packet (queue worker 0x0) 1697.7, Driver.Packet (queue worker 0x20) 1617.1, Driver.Packet.dispatch (queue worker 0x20) 835.4, Driver.Packet.dispatch (queue worker 0x0) 775.6

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.3 (0 %) | 1049.9 | 0.0 |  |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Vulkan.GraphicsPipelinePrebuild 0.0 |
| queue worker 0x0 | 292.0 (28 %) | 121.6 (12 %) | 636.6 | 31.1 | Driver.Packet 411.1, Driver.EndSubmission 2.5 |
| draw committer | 138.4 (13 %) | 33.3 (3 %) | 878.5 | 3.2 | Graphics.Draw 70.7, Recorder.Submit 51.9 |
| queue worker 0x20 | 312.4 (30 %) | 734.4 (70 %) | 3.4 | 24.4 | Driver.Packet 1004.9, Driver.EndSubmission 16.1 |
| title | 0.0 (0 %) | 0.1 (0 %) | 1050.1 | 0.0 |  |
| presenter | 1.3 (0 %) | 7.2 (1 %) | 1041.7 | 0.1 | VideoOut.Flip 8.4, Driver.Present 0.1 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.3 (0 %) | 1049.9 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.3 (0 %) | 1049.8 | 0.0 |  |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 1050.1 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1050.2 | 0.0 |  |
| gpu | 437.7 (42 %) | 0.0 (0 %) | 612.4 | 0.0 | batch 437.7 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.Packet 1416.0, Driver.Packet.memory_wait 638.9, Driver.Packet.dispatch 637.7, Driver.Dispatch 580.2, batch 437.7, Resources.Complete 397.3, Vulkan.Dispatch 393.9, Vulkan.Dispatch.shader_resources 387.2, Recorder.Submit 98.0, Graphics.Draw 90.0

**Longest waits**

- Driver.Packet.memory_wait (queue worker 0x20) 1038.9 ms at 33480 ms, present 587
- Driver.Packet.memory_wait (queue worker 0x20) 1037.1 ms at 14498 ms, present 570
- Driver.Packet.memory_wait (queue worker 0x20) 759.6 ms at 4448 ms, present 563
- Driver.Packet.memory_wait (queue worker 0x20) 737.8 ms at 12676 ms, present 569
- Driver.Packet.memory_wait (queue worker 0x20) 680.1 ms at 20970 ms, present 575
- Driver.Packet.memory_wait (queue worker 0x20) 611.0 ms at 39923 ms, present 592
- Driver.Packet.memory_wait (queue worker 0x20) 607.1 ms at 85645 ms, present 639
- Driver.Packet.memory_wait (queue worker 0x20) 596.3 ms at 43242 ms, present 595
- Driver.Packet.memory_wait (queue worker 0x20) 589.3 ms at 41152 ms, present 593
- Driver.Packet.memory_wait (queue worker 0x20) 583.0 ms at 98907 ms, present 653

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 753: 46.0 ms at 5155 ms, present 563: GPU no ranges; recorded by Recorder.Submit (queue worker 0x0) 1.3
- batch 347: 39.4 ms at 2361 ms, present 561: GPU no ranges; recorded by Driver.Packet (queue worker 0x0) 270.1, Driver.Packet.dispatch (queue worker 0x0) 270.1, Driver.Dispatch (queue worker 0x0) 270.1, Vulkan.Dispatch (queue worker 0x0) 269.5
- batch 11730: 39.0 ms at 69914 ms, present 621: GPU no ranges; recorded by no CPU slices
- batch 5839: 38.7 ms at 37017 ms, present 589: GPU no ranges; recorded by Driver.Packet (queue worker 0x0) 290.9, Driver.Packet.dispatch (queue worker 0x0) 290.9, Driver.Dispatch (queue worker 0x0) 290.9, Vulkan.Dispatch (queue worker 0x0) 290.4
- batch 16517: 37.9 ms at 93702 ms, present 647: GPU no ranges; recorded by no CPU slices
