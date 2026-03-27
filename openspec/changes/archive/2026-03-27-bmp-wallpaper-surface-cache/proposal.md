## Why

`WIN95UI` perde fluidez quando o fundo BMP está ativo porque o runtime atual expande o wallpaper em milhares de `app_gfx_rect` 1x1. Precisamos manter BMP cru como formato externo para fundos do utilizador, mas mover o custo pesado para uma fase de carga/cache em vez de o pagar em cada redraw.

## What Changes

- Add a surface blit path for app graphics so GUI apps can copy predecoded pixel buffers into the video backbuffer with one logical draw operation instead of one syscall per pixel.
- Add runtime-side BMP surface caching so wallpapers loaded from user-supplied `.bmp` files are decoded once and reused across redraws.
- Update `WIN95UI` to keep the desktop background visible again by restoring dirty regions from a cached wallpaper/desktop surface instead of redrawing the BMP through rectangle primitives.
- Add validation coverage for the new surface/blit path and document the runtime contract for BMP-backed wallpapers.

## Capabilities

### New Capabilities
- `app-surface-blit`: External apps can submit a predecoded pixel surface to the graphics layer and request clipped blits into the current frame.
- `bmp-wallpaper-cache`: GUI runtimes can load a user-provided BMP once, cache its decoded pixels, and reuse that cache during full and partial redraws.

### Modified Capabilities
- None.

## Impact

- Affected code: `external_apps/runtime/minidos_app.h`, `external_apps/runtime/minidos_ui.h`, `external_apps/apps/win95_demo/win95_demo.c`, `src/kernel/shell/shell_apps.c`, `src/kernel/video/*`, UI/runtime tests, and graphics documentation.
- APIs: new app graphics syscall/ABI for blitting decoded surfaces; no change to the external requirement that wallpapers are plain `.bmp` files.
- Systems: app/runtime graphics bridge, video backbuffer path, dirty-rect restore logic, and floppy image size budget.
