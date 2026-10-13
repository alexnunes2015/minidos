## Why

`WIN95UI` currently spends most of its drawing time crossing the user/kernel boundary for small rectangles and rasterizing transparency and clipped text in userland. The video subsystem already has a deferred backbuffer and partial present path, so the next useful step is to make those existing paths accept UI-sized batches and surfaces efficiently and safely.

## What Changes

- Add a bounded rectangle-batch graphics syscall so a UI frame can submit many solid spans under one validation and video lock.
- Extend surface blits with a native packed XRGB8888 format, ARGB8888 source-over transparency, strict descriptor validation, and a direct row-copy fast path for unscaled opaque copies.
- Add a clipped text descriptor with optional transparent background and render each request under one video lock.
- Coalesce overlapping or adjacent kernel damage rectangles before present instead of recording every primitive independently.
- Route generic clipped text through the new text syscall, and route `WIN95UI` icons, cursors, pixel-identical transparent glyph runs, and other small primitives through the appropriate bulk operations while preserving fallbacks.
- Add host-side ABI/runtime regressions plus QEMU graphics stress and deterministic performance counters for the new paths.

### Non-goals

- GPU hardware acceleration, SSE/AVX requirements, or a new VBE mode-selection flow.
- Per-process window surfaces, a multi-application compositor, runtime display-mode switching, or triple buffering.
- Moving widget layout, window z-order, or input event policy into the kernel.
- Replacing the existing graphics syscalls; they remain backward compatible.

## Capabilities

### New Capabilities

- `app-graphics-command-batch`: External apps can submit bounded arrays of solid rectangles with one syscall and one atomic render operation.
- `app-surface-compositing`: External apps can safely blit packed XRGB8888 or ARGB8888 surfaces with clipping, scaling, transparency, and an optimized opaque copy path.
- `clipped-text-rendering`: External apps can render opaque or transparent 8x8 text within an explicit clip rectangle without per-pixel syscalls.
- `video-damage-coalescing`: The kernel merges compatible damage rectangles so presentation work tracks changed screen regions rather than primitive count.

### Modified Capabilities

None; there is no promoted baseline spec tree yet, and all existing application graphics calls retain their current behavior.

## Impact

- Primary subsystem owner: `video`.
- Kernel: `src/kernel/video/`, graphics syscall dispatch and user-range validation in `src/kernel/shell/shell_apps.c`.
- User ABI/runtime: `external_apps/runtime/minidos_app.h` and `external_apps/runtime/ui/`.
- Reference app: targeted drawing helpers in `external_apps/apps/win95_demo/`, preserving unrelated local work.
- Tests: `tests/test_ui_runtime.py`, `tests/test_graphics_perf.py`, and focused video/unit coverage as needed.
- Documentation: `docs/GRAPHICS_API.md`, `docs/DESIGN.md`, `docs/DEBUGGING.md`, `docs/TEST_SCRIPTS.md`, `docs/contracts.md`, and the subsystem-owner list in `docs/DEVELOPMENT_PROTOCOL.md`.
- Validation tier: Tier 3 (`make ci`), plus the focused `make test-ui-runtime`, `make test-graphics`, and `make test-mouse` suites for the graphics/UI integration.
