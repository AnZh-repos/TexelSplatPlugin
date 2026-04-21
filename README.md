# TexelSplatPlugin

UE5 plugin implementing real-time 3D pixel art via texel splatting, based on Ebert 2026 ([arXiv 2603.14587](https://arxiv.org/abs/2603.14587)).

<table>
  <tr>
    <td><img src="screenshots/1-texel-off.png"/></td>
    <td><img src="screenshots/1-texel-on.png"/></td>
  </tr>
    <tr>
    <td><img src="screenshots/3-texel-off.png"/></td>
    <td><img src="screenshots/3-texel-on.png"/></td>
  </tr>
</table>

## What it does

Scene geometry is rasterized into a cubemap array from fixed world-space probe origins. Each cubemap texel is shaded in a compute pass and splatted to screen as a world-space quad. Because pixel assignment is tied to probe position rather than camera position, the result is stable under camera rotation and translation, no pixel crawl.

Pipeline:

1. Capture - custom `FMeshPassProcessor` rasterizes static geometry into 384x384 cubemap faces per probe, writing albedo, packed normals, Chebyshev depth, and entity ID into a 18-slice `Texture2DArray` (3 probes x 6 faces).
2. Shading - compute shader applies Lambert diffuse and OKLab posterization (32 bands, round-to-nearest) per cubemap texel.
3. Splat - vertex shader reconstructs world-space position from Chebyshev depth using `p = o + (d / ||r||∞) * r`, expands each texel to a screen-facing quad with grazing-angle gap fill, and writes to SceneColor via depth test.

Three-probe system (Eye / Grid / Prev) with 100 UU grid step and 4x4 Bayer crossfade handles translation stability and probe transitions. SVE hooks: `PostRenderBasePassDeferred_RenderThread` for capture, `PrePostProcessPass_RenderThread` for shading and splatting.

## Requirements

- UE5.7 source build - private engine headers (`FSceneRenderer`, `FMeshPassProcessor` internals) are required and not available in the installed build.
- Requires `r.AntiAliasingMethod 0` (TSR off) for clean output.
- Disable Nanite on any mesh you want captured. Nanite meshes bypass `PrimitiveSceneInfo->StaticMeshes`. Keep "Generate Nanite Fallback Meshes" enabled in Project Settings.
- Visual Studio 2026: two engine source files need manual edits before building. `VCEnvironment.cs` line 502: `Compiler = WindowsCompiler.VisualStudio2026;` and `DatasmithMax2017.Target.cs` line 43: `WindowsPlatform.Compiler = WindowsCompiler.Default;`

## Engineering notes

Two findings, neither documented publicly for UE5.7.

**Translated World Space.** UE5's `GetTranslatedViewProjectionMatrix()` returns `TranslatedWorldToClip`, not a standard world-to-clip matrix. It expects positions already offset by `PreViewTranslation`. Passing absolute world positions displaces every vertex by the full camera-to-origin offset. The symptom was all splats collapsing into a flat shape at a fixed world position regardless of camera movement. Fixed in `SplatVS` by adding `PreViewTranslation.xyz` before the `mul()` call. The matrix name gives no indication this is required.

**Unassigned material slots.** A static mesh with N material slots emits N `FStaticMeshBatch` entries in `PrimitiveSceneInfo->StaticMeshes` regardless of whether all slots are assigned. Unassigned slots use the engine default material (`WorldGridMaterial`). Iterating the batch list without filtering captures both the real batch and the default material batch for the same primitive. Whichever draws last wins the depth test, producing order-dependent wrong-color rendering on some meshes. Fix: skip any batch whose `MaterialRenderProxy` matches the cached default material proxy before calling `AddMeshBatch`.

## Setup

Copy the `TexelSplatPlugin` folder into your project's `Plugins/` directory and rebuild. The plugin registers via `PostConfigInit`.

No Blueprint or editor configuration needed. The SVE registers on startup and runs every frame when `r.TexelSplat.Enable 1` (default).

## Status

WIP. Pipeline end-to-end working, material capture correct across all mesh types. Probe rebuild scheduling (currently unconditional 18 passes/frame) and scene depth integration are next.

## Reference

Ebert, D. (2026). *Texel Splatting*. arXiv:2603.14587. Source: [github.com/dylanebert/texel-splatting](https://github.com/dylanebert/texel-splatting) (MIT).
