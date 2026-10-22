**frames 560:100** from present 560: 100 presents, frame p50 630.0 / p90 1299.8 / p99 2373.5 / max 2373.5 ms, GPU busy 400.7 ms per present
AMD Radeon RX 6650 XT (RADV NAVI23), radv Mesa 26.2.4-arch1.1, Linux 7.2.9-arch1-1, 3840x2160, commit 0560946d6da2, dropped: yes

```json
{"commit":"0560946d6da2","dirty":false,"env":{"APS5_PAD_INPUT_FILE":"<path>","APS5_TRACE_DIR":"<path>","APS5_TRACE_FRAMES":"560:100"},"gpu":"AMD Radeon RX 6650 XT (RADV NAVI23)","driver":"radv Mesa 26.2.4-arch1.1","os":"Linux 7.2.9-arch1-1","cpu":"AMD Ryzen 9 7950X 16-Core Processor","cpu_threads":32,"memory_mib":127901,"title_id":"PPSA25636","title":"Echoes of the End","content_version":"01.012.000","resolution":"3840x2160","window":{"spec":"frames 560:100","first_present":560,"ms":78426.489},"presents":100,"frame_ms":{"p50":630.016,"p90":1299.811,"p99":2373.513,"max":2373.513},"gpu_busy_ms_per_present":400.723,"worker_busy_pct":{"queue worker 0x0":99.763,"queue worker 0x20":96.499},"top_phases_ms_per_frame":{"Driver.Packet":1521.026,"Driver.Packet.dispatch":823.749,"Driver.Dispatch":745.761,"Driver.Packet.memory_wait":418.504,"Vulkan.Dispatch":410.589,"Vulkan.Dispatch.shader_resources":402.112,"Resources.Complete":387.016,"GpuMutex.wait.indirect":111.581},"trace_dropped":true,"trace_dropped_threads":{"queue worker 0x0":{"events_full_after_s":25.439,"events_lost":428881,"totals_full":false}},"trace":"aps5-trace-0560946d6da2-163082.json.gz"}
```

**Slowest frames** (largest slices inside each, inclusive)

- present 651: 2373.5 ms at 65852 ms: Driver.Packet (queue worker 0x20) 2289.4, Driver.Packet.dispatch (queue worker 0x20) 1231.7, Driver.Dispatch (queue worker 0x20) 1155.2, Vulkan.Dispatch (queue worker 0x20) 1078.2
- present 649: 1771.5 ms at 63138 ms: Driver.Packet (queue worker 0x20) 1723.4, Driver.Packet.dispatch (queue worker 0x20) 963.6, Driver.Dispatch (queue worker 0x20) 922.5, Vulkan.Dispatch (queue worker 0x20) 794.6
- present 647: 1720.0 ms at 60537 ms: Driver.Packet (queue worker 0x20) 1667.3, Driver.Packet.dispatch (queue worker 0x20) 887.9, Driver.Dispatch (queue worker 0x20) 842.9, Vulkan.Dispatch (queue worker 0x20) 780.6
- present 646: 1693.1 ms at 58844 ms: Driver.Packet (queue worker 0x20) 1651.5, Driver.Packet.dispatch (queue worker 0x20) 966.6, Driver.Dispatch (queue worker 0x20) 923.6, Vulkan.Dispatch (queue worker 0x20) 800.2
- present 643: 1600.2 ms at 54646 ms: Driver.Packet (queue worker 0x20) 1489.6, Driver.Packet.memory_wait (queue worker 0x20) 739.7, Driver.Packet.dispatch (queue worker 0x20) 699.2, Driver.Dispatch (queue worker 0x20) 627.9

**Threads** (ms per frame)

| thread | busy | wait | untraced | in slices < 20 us | largest |
|---|---|---|---|---|---|
| title | 0.0 (0 %) | 0.6 (0 %) | 783.7 | 0.0 |  |
| shader prepare | 0.1 (0 %) | 0.0 (0 %) | 784.2 | 0.0 | Shader.PrepareArtifact 0.1 |
| shader prepare | 0.1 (0 %) | 0.0 (0 %) | 784.2 | 0.0 | Shader.PrepareArtifact 0.1 |
| shader prepare | 0.3 (0 %) | 0.0 (0 %) | 784.0 | 0.0 | Shader.PrepareArtifact 0.3 |
| shader prepare | 1.0 (0 %) | 0.0 (0 %) | 783.2 | 0.0 | Shader.PrepareArtifact 1.0 |
| shader prepare | 1.2 (0 %) | 0.0 (0 %) | 783.1 | 0.0 | Shader.PrepareArtifact 1.2 |
| shader prepare | 0.1 (0 %) | 0.0 (0 %) | 784.1 | 0.0 | Shader.PrepareArtifact 0.1 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 784.2 | 0.0 | Shader.PrepareArtifact 0.0 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 784.2 | 0.0 | Shader.PrepareArtifact 0.0 |
| shader prepare | 0.0 (0 %) | 0.0 (0 %) | 784.2 | 0.0 | Shader.PrepareArtifact 0.0 |
| shader prepare | 0.7 (0 %) | 0.0 (0 %) | 783.6 | 0.0 | Shader.PrepareArtifact 0.7 |
| shader prepare | 1.3 (0 %) | 0.0 (0 %) | 782.9 | 0.0 | Vulkan.GraphicsPipelinePrebuild 1.3 |
| queue worker 0x0 | 182.5 (23 %) | 67.0 (9 %) | 534.8 | 33.9 | Driver.Packet 247.1, Driver.EndSubmission 2.4 |
| draw committer | 104.6 (13 %) | 52.9 (7 %) | 626.8 | 3.1 | Graphics.Draw 58.6, Recorder.Submit 37.8 |
| queue worker 0x20 | 268.2 (34 %) | 512.8 (65 %) | 3.2 | 24.4 | Driver.Packet 741.3, Driver.EndSubmission 12.5 |
| title | 0.0 (0 %) | 0.0 (0 %) | 784.2 | 0.0 |  |
| presenter | 1.0 (0 %) | 3.8 (0 %) | 779.4 | 0.1 | VideoOut.Flip 4.8, Driver.Present 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 784.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| title | 1.4 (0 %) | 0.2 (0 %) | 782.7 | 0.0 | Shader.ResolveAbi 1.3, Shader.ResolveGraphicsStagesAbi 0.1 |
| title | 1.4 (0 %) | 0.1 (0 %) | 782.8 | 0.0 | Shader.ResolveAbi 1.4, Shader.ResolveGraphicsStagesAbi 0.1 |
| title | 0.0 (0 %) | 0.0 (0 %) | 784.3 | 0.0 | Shader.Register 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 784.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 784.2 | 0.0 | Shader.Register 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.4 (0 %) | 783.9 | 0.0 | Shader.Register 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 784.3 | 0.0 | Shader.ResolveAbi 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 784.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 784.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 784.1 | 0.0 | Shader.ResolveAbi 0.0, Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.1 (0 %) | 784.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.5 (0 %) | 0.0 (0 %) | 783.8 | 0.0 | Shader.ResolveAbi 0.2, Shader.ResolveGraphicsStagesAbi 0.2 |
| title | 0.0 (0 %) | 0.1 (0 %) | 784.2 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 784.3 | 0.0 | Shader.ResolveGraphicsStagesAbi 0.0, Shader.ResolveAbi 0.0 |
| title | 0.0 (0 %) | 0.0 (0 %) | 784.2 | 0.0 |  |
| gpu | 400.8 (51 %) | 0.0 (0 %) | 383.5 | 0.0 | batch 400.8 |

**Largest slices** (ms per frame, inclusive, all threads): Driver.Packet 988.3, Driver.Packet.dispatch 497.7, Driver.Dispatch 442.1, batch 400.8, Driver.Packet.memory_wait 397.8, Vulkan.Dispatch 276.6, Vulkan.Dispatch.shader_resources 272.4, Resources.Complete 266.2, Recorder.Submit 72.2, Graphics.Draw 61.4

**Longest waits**

- Driver.Packet.memory_wait (queue worker 0x20) 782.9 ms at 67397 ms, present 651
- Driver.Packet.memory_wait (queue worker 0x20) 692.9 ms at 52078 ms, present 640
- Driver.Packet.memory_wait (queue worker 0x20) 590.4 ms at 55606 ms, present 643
- Driver.Packet.memory_wait (queue worker 0x20) 527.3 ms at 68917 ms, present 652
- Driver.Packet.memory_wait (queue worker 0x20) 509.8 ms at 73701 ms, present 656
- Driver.Packet.memory_wait (queue worker 0x20) 485.6 ms at 56806 ms, present 644
- Driver.Packet.memory_wait (queue worker 0x20) 482.5 ms at 12990 ms, present 580
- Driver.Packet.memory_wait (queue worker 0x20) 481.1 ms at 47626 ms, present 635
- Driver.Packet.memory_wait (queue worker 0x20) 477.6 ms at 51131 ms, present 639
- Driver.Packet.memory_wait (queue worker 0x20) 472.1 ms at 53222 ms, present 641

**Largest GPU batches** (GPU ranges inside, and the CPU slices that recorded each, inclusive)

- batch 1827: 43.9 ms at 6988 ms, present 570: GPU no ranges; recorded by Driver.Packet (queue worker 0x0) 3.7, Driver.Packet.dispatch (queue worker 0x0) 3.7, Driver.Dispatch (queue worker 0x0) 3.7, Vulkan.Dispatch (queue worker 0x0) 2.9
- batch 1161: 41.9 ms at 4229 ms, present 566: GPU no ranges; recorded by Driver.Packet (queue worker 0x0) 4.5, Driver.Packet.dispatch (queue worker 0x0) 4.5, Driver.Dispatch (queue worker 0x0) 4.4, Vulkan.Dispatch (queue worker 0x0) 3.6
- batch 8627: 41.8 ms at 32393 ms, present 610: GPU no ranges; recorded by no CPU slices
- batch 8993: 41.7 ms at 33560 ms, present 612: GPU no ranges; recorded by no CPU slices
- batch 11945: 41.7 ms at 44408 ms, present 630: GPU no ranges; recorded by no CPU slices
