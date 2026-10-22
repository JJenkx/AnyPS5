**frames 560:100** from present 560: 100 presents, frame p50 1034.0 / p90 1190.2 / p99 1658.5 / max 1658.5 ms, GPU busy 429.1 ms per present
AMD Radeon RX 6650 XT (RADV NAVI23), radv Mesa 26.2.4-arch1.1, Linux 7.2.9-arch1-1, 3840x2160, commit a526b9bb47ca, dropped: yes

```json
{"commit":"a526b9bb47ca","dirty":false,"env":{"APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_DIR":"<path>","APS5_TRACE_FRAMES":"560:100"},"gpu":"AMD Radeon RX 6650 XT (RADV NAVI23)","driver":"radv Mesa 26.2.4-arch1.1","os":"Linux 7.2.9-arch1-1","cpu":"AMD Ryzen 9 7950X 16-Core Processor","cpu_threads":32,"memory_mib":127901,"title_id":"PPSA25636","title":"Echoes of the End","content_version":"01.012.000","resolution":"3840x2160","window":{"spec":"frames 560:100","first_present":560,"ms":103536.928},"presents":100,"frame_ms":{"p50":1033.962,"p90":1190.235,"p99":1658.498,"max":1658.498},"gpu_busy_ms_per_present":429.131,"worker_busy_pct":{"queue worker 0x0":99.764,"queue worker 0x20":97.312},"top_phases_ms_per_frame":{"Driver.Packet":2002.157,"Driver.Packet.dispatch":1030.803,"Driver.Dispatch":972.049,"Driver.Packet.memory_wait":663.697,"Vulkan.Dispatch":612.851,"Vulkan.Dispatch.shader_resources":602.088,"Resources.Complete":591.123,"Recorder.Submit":154.274},"trace_dropped":true,"trace_dropped_threads":{"queue worker 0x0":{"events_full_after_s":36.981,"events_lost":517833,"totals_full":false}},"trace":"aps5-trace-a526b9bb47ca-3289446.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 574: 1658.5 ms at 15413 ms: Driver.Packet (queue worker 0x0) 1636.1, Driver.Packet (queue worker 0x20) 1611.7, Driver.Packet.dispatch (queue worker 0x0) 853.2, Driver.Dispatch (queue worker 0x0) 839.6
- present 583: 1598.6 ms at 24748 ms: Driver.Packet (queue worker 0x0) 1578.1, Driver.Packet (queue worker 0x20) 1553.8, Driver.Packet.dispatch (queue worker 0x0) 951.7, Driver.Dispatch (queue worker 0x0) 899.3
- present 566: 1567.9 ms at 5884 ms: Driver.Packet (queue worker 0x0) 1547.5, Driver.Packet (queue worker 0x20) 1524.8, Driver.Packet.memory_wait (queue worker 0x20) 1149.0, Resources.Complete (queue worker 0x0) 906.7
- present 571: 1524.9 ms at 12078 ms: Driver.Packet (queue worker 0x0) 1503.8, Driver.Packet (queue worker 0x20) 1485.1, Driver.Packet.dispatch (queue worker 0x0) 1032.8, Driver.Dispatch (queue worker 0x0) 1021.2
- present 659: 1383.8 ms at 102153 ms: Driver.Packet (queue worker 0x20) 1340.2, Driver.Packet.memory_wait (queue worker 0x20) 963.8, batch (gpu) 498.7, Driver.Packet.dispatch (queue worker 0x20) 344.4

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.1 (0 %) | 1035.2 | 0.0 |  |
| shader prepare | 0.2 (0 %) | 0.0 (0 %) | 1035.2 | 0.0 | Shader.PrepareArtifact 0.2 |
| shader prepare | 0.2 (0 %) | 0.0 (0 %) | 1035.2 | 0.0 | Shader.PrepareArtifact 0.2 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 1035.4 | 0.0 | Vulkan.GraphicsPipelinePrebuild 0.0 |
| queue worker 0x0 | 272.6 (26 %) | 92.4 (9 %) | 670.3 | 31.6 | Driver.Packet 361.8, Driver.EndSubmission 3.2 |
| draw committer | 128.8 (12 %) | 32.9 (3 %) | 873.6 | 3.3 | Recorder.Submit 60.5, Graphics.Draw 56.9 |
| queue worker 0x20 | 261.2 (25 %) | 770.6 (74 %) | 3.6 | 24.8 | Driver.Packet 973.9, Driver.EndSubmission 30.3 |
| title | 0.0 (0 %) | 0.1 (0 %) | 1035.2 | 0.0 |  |
| presenter | 1.5 (0 %) | 5.1 (0 %) | 1028.7 | 0.1 | VideoOut.Flip 6.4, Driver.Present 0.2 |
| title | 0.0 (0 %) | 0.2 (0 %) | 1035.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 3.0 (0 %) | 0.0 (0 %) | 1032.3 | 0.0 | Shader.ResolveAbi 2.9, Shader.ResolveGraphicsStagesAbi 0.1 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.4 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.4 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 2.6 (0 %) | 0.1 (0 %) | 1032.6 | 0.0 | Shader.ResolveAbi 2.5, Shader.ResolveGraphicsStagesAbi 0.1 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.4 | 0.0 | Shader.Register 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.2 (0 %) | 1035.2 | 0.0 |  |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.4 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.4 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.3 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.4 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.4 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.3 | 0.0 | Shader.Register 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 1035.4 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 1035.3 | 0.0 |  |
| gpu | 429.3 (41 %) | 0.0 (0 %) | 606.1 | 0.0 | batch 429.3 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.Packet 1335.8, Driver.Packet.memory_wait 643.8, Driver.Packet.dispatch 578.9, Driver.Dispatch 528.7, batch 429.3, Vulkan.Dispatch 345.4, Resources.Complete 341.4, Vulkan.Dispatch.shader_resources 339.9, Recorder.Submit 114.3, GpuMutex.wait.indirect 70.2

**Longest waits**

- Driver.Packet.memory_wait (queue worker 0x20) 975.1 ms at 6454 ms, present 566
- Driver.Packet.memory_wait (queue worker 0x20) 832.3 ms at 102682 ms, present 659
- Driver.Packet.memory_wait (queue worker 0x20) 791.5 ms at 12788 ms, present 571
- Driver.Packet.memory_wait (queue worker 0x20) 647.5 ms at 48559 ms, present 607
- Driver.Packet.memory_wait (queue worker 0x20) 629.8 ms at 100246 ms, present 657
- Driver.Packet.memory_wait (queue worker 0x20) 607.7 ms at 64509 ms, present 623
- Driver.Packet.memory_wait (queue worker 0x20) 607.4 ms at 68014 ms, present 626
- Driver.Packet.memory_wait (queue worker 0x20) 605.6 ms at 47225 ms, present 606
- Driver.Packet.memory_wait (queue worker 0x20) 605.4 ms at 93857 ms, present 651
- Driver.Packet.memory_wait (queue worker 0x20) 597.9 ms at 53084 ms, present 611

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 4628: 42.0 ms at 26256 ms, present 583: GPU no ranges; recorded by Driver.Packet (queue worker 0x0) 241.2, Driver.Packet.dispatch (queue worker 0x0) 241.2, Driver.Dispatch (queue worker 0x0) 241.2, Vulkan.Dispatch (queue worker 0x0) 240.7
- batch 3834: 42.0 ms at 21505 ms, present 579: GPU no ranges; recorded by Driver.Packet (queue worker 0x0) 223.2, Driver.Packet.dispatch (queue worker 0x0) 223.2, Driver.Dispatch (queue worker 0x0) 223.2, Vulkan.Dispatch (queue worker 0x0) 222.7
- batch 16381: 41.0 ms at 86111 ms, present 643: GPU no ranges; recorded by no CPU slices
- batch 10639: 40.4 ms at 57531 ms, present 615: GPU no ranges; recorded by no CPU slices
- batch 9075: 40.3 ms at 49150 ms, present 607: GPU no ranges; recorded by no CPU slices
