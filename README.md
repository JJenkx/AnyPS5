Performance traces for the AnyPS5 pull request "fix(agc): release the sampled views of a retired depth surface".

Dusk Diver 2 (PPSA06565), Chapter 1 autosave loaded and advanced with Cross every 6 s, `APS5_TRACE_SECONDS=360:20` only, warm caches.
RX 6650 XT (RADV NAVI23), Mesa 26.2.6, Linux 7.2.14, 3840x2160 output, Release builds.

- A runs (`fbe969020bc2`): `main` 561fb7672f plus #17787 (443a0dd898), which stops the #6926 crash in this title.
- B runs (`4ffb7a00ba1b`): the same plus this fix.
- Order: A1, B2, B3, A4.

Each directory holds `aps5-summary.jsonl`, the trace (opens in ui.perfetto.dev) and the output of `tools/trace_digest.py`.
