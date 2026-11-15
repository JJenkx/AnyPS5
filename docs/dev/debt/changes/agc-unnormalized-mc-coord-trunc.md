# libSceAgcDriver

## change An [unnormalized S#]

- An [unnormalized S#](../../../core/libs/prx/libSceAgcDriver/Graphics/src/GuestSamplerResource.cpp) (libSceAgcDriver) follows RDNA2 gfx1036 measurements, not gfx1013: clamp 0 samples like clamp-to-last-texel; modes 1 and 3 mirror negative texel index i to -i-1 after the offset, then clamp. Without MC their shader maps negative c to the float just below -c, so -k reads texel k-1. Unequal min/mag filters use magnification at LOD 0. Anisotropy and clamps 4, 5 and 7 throw. MC_COORD_TRUNC uses the direct coordinate path documented in the shader debt entry. Secondary LOD bias is accepted and ignored, matching explicit/implicit RDNA2 samples. High on Life (PPSA15388, #4736) uses secondary bias and clamp 0 with unequal filters.

## change [TRUNC_COORD]

- [TRUNC_COORD](../../../core/libs/prx/libSceAgcDriver/Graphics/src/GuestSamplerResource.cpp) (libSceAgcDriver) is still ignored outside the MC_COORD_TRUNC shader path; point selection and bilinear weights follow the host sampler. Native RDNA2 (RX 6700 XT) rounds scaled coordinates to 8 fractional bits, ties-even, for 0 and truncates for 1. AMD Windows driver 32.0.21045.5002 truncates for nearest and rounds for linear: a TRUNC_COORD 0 point sample within 1/512 texel below an edge can read below it where native reads above; normalized k/width computed in f32 often lands there. A TRUNC_COORD 1 bilinear weight can be 1/256 above native. Gfx1036 driver 31.0.22044.1 instead rounds the near-edge nearest case for 0 and truncates for 1, so a workaround tuned to the other driver is wrong there. Other host drivers remain unmeasured.

# shader

## add Functional

- [MC_COORD_TRUNC](../../../core/shader/recompiler/SpirvBackend/src/SpirvImageEmitter.cpp) on proven unnormalized direct 1D/2D samples follows local RX 6650 XT gfx1032 measurements, not PS5 equivalence: round texel coordinates to 8 fractional bits, ties-even, or floor with TRUNC_COORD; bilinear MC then floors to half-texel steps. Modes 0, 1, 2, 3 and 6 are supported. Negative mirrored point indices map to exact texel centers; linear mirroring uses the quantized coordinate's absolute value. Converted filtering and min/max reduction throw. Normalized MC is still ignored. Native evidence covers 2D procedural RGBA8, both axes, border and negative-integer cases; 1D, other formats, offsets, mip/LOD, comparison and exceptional coordinates lack native MC measurements. See PR #14964's independent MC follow-up for replay evidence. No title unblock is established.
