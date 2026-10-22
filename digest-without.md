**frames 560:100** from present 560: 100 presents, frame p50 748.4 / p90 1091.6 / p99 6985.3 / max 6985.3 ms, GPU busy 412.5 ms per present
AMD Radeon RX 6650 XT (RADV NAVI23), radv Mesa 26.2.4-arch1.1, Linux 7.2.9-arch1-1, 3840x2160, commit 20f928c642f7, dropped: yes

```json
{"commit":"20f928c642f7","dirty":false,"env":{"APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_DIR":"<path>","APS5_TRACE_FRAMES":"560:100"},"gpu":"AMD Radeon RX 6650 XT (RADV NAVI23)","driver":"radv Mesa 26.2.4-arch1.1","os":"Linux 7.2.9-arch1-1","cpu":"AMD Ryzen 9 7950X 16-Core Processor","cpu_threads":32,"memory_mib":127901,"title_id":"PPSA25636","title":"Echoes of the End","content_version":"01.012.000","resolution":"3840x2160","window":{"spec":"frames 560:100","first_present":560,"ms":92408.448},"presents":100,"frame_ms":{"p50":748.353,"p90":1091.598,"p99":6985.317,"max":6985.317},"gpu_busy_ms_per_present":412.483,"worker_busy_pct":{"queue worker 0x0":99.794,"queue worker 0x20":96.7},"top_phases_ms_per_frame":{"Driver.Packet":1775.286,"Driver.Packet.dispatch":983.421,"Driver.Dispatch":913.871,"Vulkan.Dispatch":547.906,"Vulkan.Dispatch.shader_resources":539.003,"Resources.Complete":530.227,"Driver.Packet.memory_wait":471.162,"GpuMutex.wait.indirect":143.345},"trace_dropped":true,"trace_dropped_threads":{"queue worker 0x0":{"events_full_after_s":31.77,"events_lost":417448,"totals_full":false}},"trace":"aps5-trace-20f928c642f7-4193406.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 655: 6985.3 ms at 79180 ms: Driver.Packet (queue worker 0x20) 6914.9, Driver.Packet.dispatch (queue worker 0x20) 5462.9, Driver.Dispatch (queue worker 0x20) 5405.1, Vulkan.Dispatch (queue worker 0x20) 5276.1
- present 658: 3585.1 ms at 87752 ms: Driver.Packet (queue worker 0x20) 3498.3, Driver.Packet.dispatch (queue worker 0x20) 2530.2, Driver.Dispatch (queue worker 0x20) 2479.1, Vulkan.Dispatch (queue worker 0x20) 2359.0
- present 652: 1940.8 ms at 74097 ms: Driver.Packet (queue worker 0x20) 1850.9, Driver.Packet.dispatch (queue worker 0x20) 917.3, Driver.Packet.memory_wait (queue worker 0x20) 886.9, Driver.Dispatch (queue worker 0x20) 812.2
- present 653: 1903.0 ms at 76037 ms: Driver.Packet (queue worker 0x20) 1827.8, Driver.Packet.dispatch (queue worker 0x20) 1020.0, Driver.Dispatch (queue worker 0x20) 956.5, Driver.Packet.memory_wait (queue worker 0x20) 785.4
- present 565: 1734.1 ms at 3741 ms: Driver.Packet (queue worker 0x0) 1694.2, Driver.Packet (queue worker 0x20) 1677.6, Driver.Packet.dispatch (queue worker 0x0) 887.1, Driver.Packet.dispatch (queue worker 0x20) 864.4

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.7 (0 %) | 923.4 | 0.0 |  |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 924.1 | 0.0 | Shader.PrepareArtifact 0.0 |
| shader prepare | 1.3 (0 %) | 0.0 (0 %) | 922.8 | 0.0 | Shader.PrepareArtifact 1.3 |
| shader prepare | 0.2 (0 %) | 0.0 (0 %) | 923.9 | 0.0 | Shader.PrepareArtifact 0.2 |
| shader prepare | 0.1 (0 %) | 0.0 (0 %) | 924.0 | 0.0 | Shader.PrepareArtifact 0.1 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 924.1 | 0.0 | Shader.PrepareArtifact 0.0 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 924.1 | 0.0 | Shader.PrepareArtifact 0.0 |
| shader prepare | 0.1 (0 %) | 0.0 (0 %) | 923.9 | 0.0 | Shader.PrepareArtifact 0.1 |
| shader prepare | 0.4 (0 %) | 0.0 (0 %) | 923.7 | 0.0 | Shader.PrepareArtifact 0.4 |
| shader prepare | 0.7 (0 %) | 0.0 (0 %) | 923.3 | 0.0 | Shader.PrepareArtifact 0.7 |
| shader prepare | 1.2 (0 %) | 0.0 (0 %) | 922.8 | 0.0 | Shader.PrepareArtifact 1.2 |
| shader prepare | 1.7 (0 %) | 0.0 (0 %) | 922.4 | 0.0 | Vulkan.GraphicsPipelinePrebuild 1.7 |
| queue worker 0x0 | 226.9 (25 %) | 85.9 (9 %) | 611.3 | 32.9 | Driver.Packet 310.5, Driver.EndSubmission 2.3 |
| draw committer | 119.6 (13 %) | 93.1 (10 %) | 711.5 | 2.9 | Graphics.Draw 65.9, Recorder.Submit 39.8 |
| queue worker 0x20 | 322.6 (35 %) | 598.2 (65 %) | 3.3 | 24.3 | Driver.Packet 855.5, Driver.EndSubmission 35.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 924.0 | 0.0 |  |
| presenter | 1.1 (0 %) | 7.4 (1 %) | 915.6 | 0.1 | VideoOut.Flip 8.5, Driver.Present 0.0 |
| title | 0.1 (0 %) | 0.2 (0 %) | 923.7 | 0.0 | Shader.ResolveAbi 0.1, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 924.1 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 1.8 (0 %) | 0.1 (0 %) | 922.2 | 0.0 | Shader.ResolveAbi 1.7, Shader.ResolveGraphicsStagesAbi 0.1 |
| title | 0.0 (0 %) | 0.1 (0 %) | 924.0 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 923.9 | 0.0 | Shader.ResolveAbi 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 1.1 (0 %) | 0.0 (0 %) | 923.0 | 0.0 | Shader.ResolveAbi 0.8, Shader.ResolveGraphicsStagesAbi 0.3 |
| title | 0.0 (0 %) | 0.1 (0 %) | 924.0 | 0.0 | Shader.ResolveAbi 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 924.1 | 0.0 | Shader.Register 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.4 (0 %) | 923.7 | 0.0 | Shader.Register 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 924.1 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 924.1 | 0.0 | Shader.Register 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 924.0 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| title | 1.2 (0 %) | 0.0 (0 %) | 922.9 | 0.0 | Shader.ResolveAbi 1.1, Shader.ResolveGraphicsStagesAbi 0.1 |
| title | 0.0 (0 %) | 0.1 (0 %) | 924.0 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 924.1 | 0.0 |  |
| title | 0.0 (0 %) | 0.0 (0 %) | 924.0 | 0.0 |  |
| gpu | 413.4 (45 %) | 0.0 (0 %) | 510.7 | 0.0 | batch 413.4 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.Packet 1166.1, Driver.Packet.dispatch 609.7, Driver.Dispatch 551.3, Driver.Packet.memory_wait 456.2, batch 413.4, Vulkan.Dispatch 375.4, Vulkan.Dispatch.shader_resources 371.0, Resources.Complete 369.0, GpuMutex.wait.label 78.3, Recorder.Submit 75.3

**Longest waits**

- GpuMutex.wait.label (draw committer) 2222.6 ms at 79744 ms, present 655
- GpuMutex.wait.flush (draw committer) 1851.3 ms at 82275 ms, present 655
- Driver.Packet.memory_wait (queue worker 0x20) 955.3 ms at 85192 ms, present 655
- Driver.Packet.memory_wait (queue worker 0x20) 766.5 ms at 75235 ms, present 652
- Driver.Packet.memory_wait (queue worker 0x20) 609.8 ms at 10648 ms, present 571
- Driver.Packet.memory_wait (queue worker 0x20) 593.1 ms at 78550 ms, present 654
- Driver.Packet.memory_wait (queue worker 0x20) 580.0 ms at 9569 ms, present 570
- GpuMutex.wait.label (draw committer) 571.1 ms at 89208 ms, present 658
- Driver.Packet.memory_wait (queue worker 0x20) 563.9 ms at 91826 ms, present 659
- Driver.Packet.memory_wait (queue worker 0x20) 556.4 ms at 8540 ms, present 569

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 6857: 41.5 ms at 32485 ms, present 599: GPU no ranges; recorded by no CPU slices
- batch 1789: 39.6 ms at 9053 ms, present 569: GPU no ranges; recorded by Driver.Dispatch.decode_key_device_setup (queue worker 0x0) 21.5, Driver.Packet (queue worker 0x0) 4.8, Driver.Packet.dispatch (queue worker 0x0) 4.8, Driver.Dispatch (queue worker 0x0) 4.7
- batch 14296: 36.2 ms at 65535 ms, present 643: GPU no ranges; recorded by no CPU slices
- batch 8381: 35.1 ms at 38951 ms, present 608: GPU no ranges; recorded by no CPU slices
- batch 14861: 34.4 ms at 68492 ms, present 646: GPU no ranges; recorded by no CPU slices
