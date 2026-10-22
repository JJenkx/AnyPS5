# Performance comparison

| Capture | Commit | Trace | Presents | Window (ms) |
| --- | --- | --- | ---: | ---: |
| baseline | 4ff898faefa3 | aps5-trace-4ff898faefa3-3124014.json.gz | 100 | 105019.366 |
| candidate | a526b9bb47ca | aps5-trace-a526b9bb47ca-3289446.json.gz | 100 | 103536.928 |

- Compare captures from the same machine, title and scene; summaries do not record the CPU, title or scene.
- Changes are candidate minus baseline. A negative frame-time change means a shorter frame interval.
- A missing top phase is unreported, not zero; its change is unavailable.
- baseline: trace events were dropped; the event trace omits some of the work included in phase totals.
- candidate: trace events were dropped; the event trace omits some of the work included in phase totals.

## Frame and GPU times (ms)

| Metric | Baseline | Candidate | Change | Change (%) |
| --- | ---: | ---: | ---: | ---: |
| frame_ms.p50 | 958.000 | 1033.962 | +75.962 | +7.929 |
| frame_ms.p90 | 1456.922 | 1190.235 | -266.687 | -18.305 |
| frame_ms.p99 | 3292.388 | 1658.498 | -1633.890 | -49.626 |
| frame_ms.max | 3292.388 | 1658.498 | -1633.890 | -49.626 |
| mean_frame_ms | 1050.194 | 1035.369 | -14.824 | -1.412 |
| gpu_busy_ms_per_present | 434.116 | 429.131 | -4.985 | -1.148 |

## Top phases (ms per frame)

| Metric | Baseline | Candidate | Change | Change (%) |
| --- | ---: | ---: | ---: | ---: |
| Driver.Dispatch | 992.721 | 972.049 | -20.672 | -2.082 |
| Driver.Packet | 2048.790 | 2002.157 | -46.633 | -2.276 |
| Driver.Packet.dispatch | 1056.775 | 1030.803 | -25.972 | -2.458 |
| Driver.Packet.memory_wait | 677.391 | 663.697 | -13.694 | -2.022 |
| GpuMutex.wait.indirect | 147.167 | n/a | n/a | n/a |
| Recorder.Submit | n/a | 154.274 | n/a | n/a |
| Resources.Complete | 615.461 | 591.123 | -24.338 | -3.954 |
| Vulkan.Dispatch | 628.122 | 612.851 | -15.271 | -2.431 |
| Vulkan.Dispatch.shader_resources | 616.824 | 602.088 | -14.736 | -2.389 |
