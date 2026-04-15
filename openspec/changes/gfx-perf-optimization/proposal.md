## Why

MiniDOS currently renders graphics pixel-by-pixel without batching, dirty-region tracking, or cacheline-aware memory access patterns. This causes unnecessary memory traffic to the VESA framebuffer and redundant writes to static regions. For educational GUI apps and visual demos, rendering throughput is a limiting factor for interactive frame rates (target: 30+ fps on QEMU). Optimizing the graphics path directly improves user experience and demonstrates kernel-level performance engineering to students.

## What Changes

- **Dirty Rectangle Tracking**: Kernel accumulates rendering operations and tracks bounding boxes of modified regions; only flushes changed rectangles to VESA LFB on present.
- **Optimized Backbuffer Fill**: Replace pixel-per-pixel loops with word-aligned memset (32/64-bit writes) for fill/clear operations in the software backbuffer.
- **Lock-Free Framebuffer Reads**: Enable read-heavy graphics operations (compositing, UI queries) without global lock contention when multiple threads access framebuffer metadata.
- **Cacheline-Aligned Backbuffer Stride**: Ensure backbuffer scanline stride aligns to 64-byte cacheline to avoid false-sharing in multi-threaded render scenarios.
- **Scheduler Priority Boost for Render Paths** (optional): Give graphics-bound threads priority to reduce context-switch latency during frame budgets.

## Capabilities

### New Capabilities
- `dirty-rect-tracking`: Accumulate rendering region updates and track bounding box; only present changed rectangles to VESA LFB.
- `optimized-memset-fill`: Fast 32/64-bit memset for backbuffer fill/clear in software scratch region.
- `lock-free-framebuffer-reads`: Reader-writer synchronization for concurrent framebuffer reads without blocking writers.
- `cacheline-aligned-backbuffer`: Ensure backbuffer scanline pitch is 64-byte aligned and metadata layout avoids false-sharing.

### Modified Capabilities
- `video-rendering`: Backbuffer layout and present flow modified to support dirty-rect and alignment invariants.
- `framebuffer-locking`: Lock semantics change from simple mutex to reader-writer pattern for better concurrency.

## Impact

**Affected Subsystems**:
- `src/kernel/video/video.h` — new API for dirty-rect accumulation, reader-writer lock
- `src/kernel/video/video_internal.h` — backbuffer metadata and allocation layout
- `src/kernel/video/video_render.c` — fill/clear routines and present logic
- `src/kernel/video/video_present.c` — deferred present logic updated for dirty rects
- `src/kernel/process/scheduler.c` — optional priority boost for render threads

**API Changes**:
- New: `video_accumulate_dirty_rect(int x, int y, int w, int h)`
- New: `video_read_lock()`, `video_read_unlock()` for concurrent reads
- Modified: `video_present_pending()` to flush only dirty rectangles
- Modified: Backbuffer allocation to ensure 64-byte stride alignment

**Validation Tier**: Tier 2 (focused regression suite for graphics + multi-threaded stress test)

**Non-goals**:
- GPU hardware acceleration (still direct VESA LFB writes)
- Tile-based rendering engine
- Font glyph caching (deferred to later phase)
- VESA mode negotiation changes (reuse existing 640x480/800x600 defaults)
