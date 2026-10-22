# Performance comparison

| Capture | Commit | Trace | Presents | Window (ms) |
| --- | --- | --- | ---: | ---: |
| baseline | 20f928c642f7 | aps5-trace-20f928c642f7-4193406.json.gz | 100 | 92408.448 |
| candidate | 0560946d6da2 | aps5-trace-0560946d6da2-163082.json.gz | 100 | 78426.489 |

- Compare captures from the same machine, title and scene; summaries do not record the CPU, title or scene.
- Changes are candidate minus baseline. A negative frame-time change means a shorter frame interval.
- A missing top phase is unreported, not zero; its change is unavailable.
- baseline: trace events were dropped; the event trace omits some of the work included in phase totals.
- candidate: trace events were dropped; the event trace omits some of the work included in phase totals.

## Frame and GPU times (ms)

| Metric | Baseline | Candidate | Change | Change (%) |
| --- | ---: | ---: | ---: | ---: |
| frame_ms.p50 | 748.353 | 630.016 | -118.337 | -15.813 |
| frame_ms.p90 | 1091.598 | 1299.811 | +208.213 | +19.074 |
| frame_ms.p99 | 6985.317 | 2373.513 | -4611.804 | -66.021 |
| frame_ms.max | 6985.317 | 2373.513 | -4611.804 | -66.021 |
| mean_frame_ms | 924.084 | 784.265 | -139.820 | -15.131 |
| gpu_busy_ms_per_present | 412.483 | 400.723 | -11.760 | -2.851 |

## Top phases (ms per frame)

| Metric | Baseline | Candidate | Change | Change (%) |
| --- | ---: | ---: | ---: | ---: |
| Driver.Dispatch | 913.871 | 745.761 | -168.110 | -18.395 |
| Driver.Packet | 1775.286 | 1521.026 | -254.260 | -14.322 |
| Driver.Packet.dispatch | 983.421 | 823.749 | -159.672 | -16.236 |
| Driver.Packet.memory_wait | 471.162 | 418.504 | -52.658 | -11.176 |
| GpuMutex.wait.indirect | 143.345 | 111.581 | -31.764 | -22.159 |
| Resources.Complete | 530.227 | 387.016 | -143.211 | -27.009 |
| Vulkan.Dispatch | 547.906 | 410.589 | -137.317 | -25.062 |
| Vulkan.Dispatch.shader_resources | 539.003 | 402.112 | -136.891 | -25.397 |
