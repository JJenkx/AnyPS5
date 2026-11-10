Same-scene performance traces for AnyPS5 PR #12827, "perf(agc): store a resident heap mirror's write-back from its readback buffer".

Mortal Shell II (PPSA34008), title key art, the same scripted input in every run (pad input file; `APS5_PLAYGO_UNLISTED_CHUNKS=installed`). The only trace switch is `APS5_TRACE_SECONDS=80:20`. Caches were warmed by one untraced run per build.
RX 6650 XT (RADV NAVI23), Mesa 26.2.6, Linux 7.2.14, Release.

- A (`98afc0b8cf88`): `main` 98afc0b8cf.
- B (`ac133e612c33`): the same plus #12827 head c12a20f45d (clean merge).
- Order: A1, B2, B3, A4.

Each directory holds `aps5-summary.jsonl`, the trace (opens in ui.perfetto.dev) and the output of `tools/trace_digest.py`.
