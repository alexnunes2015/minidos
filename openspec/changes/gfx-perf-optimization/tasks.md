## 1. Infrastructure Setup

- [x] 1.1 Add dirty-rect structure to video_internal.h and initialize at boot
- [x] 1.2 Create video_mark_dirty() helper function for region accumulation
- [x] 1.3 Add BACKBUFFER_CACHELINE_SIZE and BACKBUFFER_PITCH macros to video_internal.h
- [x] 1.4 Create tests/test_graphics_perf.py skeleton for regression testing

## 2. Reader-Writer Lock Implementation

- [x] 2.1 Implement video_rwlock_t structure with readers/writer/wait_writer fields in video_internal.h
- [x] 2.2 Implement video_read_lock() using __sync_fetch_and_add() for atomic reads
- [x] 2.3 Implement video_read_unlock() to decrement reader count
- [x] 2.4 Implement video_write_lock() with busy-wait for exclusive access
- [x] 2.5 Implement video_write_unlock() to release writer slot
- [x] 2.6 Refactor existing video_lock()/video_unlock() as wrappers to write_lock/write_unlock
- [x] 2.7 Update all existing video_lock() calls to remain functional (no API change required)
- [x] 2.8 Validate with make test-serial that no regressions occur

## 3. Cacheline-Aligned Backbuffer Allocation

- [x] 3.1 Update backbuffer allocation to calculate stride with 64-byte alignment
- [x] 3.2 Update video_fb_pitch() to return aligned stride instead of logical width
- [x] 3.3 Update all pixel address calculations (write_frontbuffer_pixel, fill routines) to use aligned stride
- [x] 3.4 Verify backbuffer allocation at 0x00B00000 still works with new stride
- [x] 3.5 Test with make test-serial that rendering output is unchanged
- [x] 3.6 Validate stride alignment with serial marker (e.g., "STRIDE_ALIGN_OK")

## 4. Dirty Rectangle Accumulation

- [x] 4.1 Integrate video_mark_dirty() calls into fill_frontbuffer_rect_rgb_locked()
- [x] 4.2 Integrate video_mark_dirty() calls into video_draw_text_at()
- [x] 4.3 Integrate video_mark_dirty() calls into video_blit_surface_desc()
- [x] 4.4 Integrate video_mark_dirty() calls into boot splash and cursor operations
- [x] 4.5 Add dirty-rect reset logic at start of frame
- [x] 4.6 Test with make test-serial that regions are marked correctly
- [x] 4.7 Add optional debug output for dirty-rect bounds when DIRTY_RECT_DEBUG is set

## 5. Optimized Fill and Clear

- [x] 5.1 Refactor fill_frontbuffer_rect_rgb_locked() to use word-aligned writes for 32-bit framebuffer
- [x] 5.2 Add fallback to per-pixel loop for 16-bit and 24-bit framebuffers
- [x] 5.3 Optimize video_clear_color() to use fastest path (aligned loop or memset)
- [x] 5.4 Verify no data corruption with make test-serial (text rendering, boot splash)
- [x] 5.5 Benchmark clear time with serial timer: target <1ms for 640x480x32
- [x] 5.6 Add regression test for color accuracy across all supported depths

## 6. Dirty Rectangle Present Integration

- [x] 6.1 Modify video_present_pending() to clip dirty-rect to framebuffer bounds
- [x] 6.1 Integrate dirty-rect bounds into asm_fast_present_rect_rgb() call
- [x] 6.2 Update fallback C path to flush only dirty rectangle to VESA LFB
- [x] 6.3 Reset dirty-rect state after each present
- [x] 6.4 Handle edge case: empty dirty-rect (no modifications since last present)
- [x] 6.5 Test with make test-serial that frame output is correct
- [x] 6.6 Validate dirty-rect is only flushed once per present cycle

## 7. Multi-threaded Stress Testing

- [x] 7.1 Create test_graphics_perf.py with concurrent render workload (multiple scheduler threads drawing)
- [x] 7.2 Add test case: simultaneous fills on different regions (no contention)
- [x] 7.3 Add test case: read-heavy queries while writer is active (rwlock behavior)
- [x] 7.4 Add test case: rapid dirty-rect boundary updates (correctness)
- [x] 7.5 Run stress test with make test-graphics and verify no deadlock/starvation
- [x] 7.6 Validate with GDB: inspect rwlock state under contention

## 8. Documentation and Validation

- [x] 8.1 Update docs/DESIGN.md with new graphics subsystem architecture (dirty-rect, rwlock, alignment)
- [x] 8.2 Add new section in docs/TEST_SCRIPTS.md for graphics performance validation
- [x] 8.3 Document new macros and locks in src/kernel/video/video_internal.h header comments
- [x] 8.4 Update README.md with new test target: make test-graphics
- [x] 8.5 Run full Tier 2 validation: make verify-image + make test-graphics
- [x] 8.6 Ensure make ci still passes with no regressions

## 9. Performance Validation

- [x] 9.1 Create micro-benchmark: measure clear time before/after optimization
- [x] 9.2 Create micro-benchmark: measure present time (full-frame vs dirty-rect)
- [x] 9.3 Create micro-benchmark: measure rwlock contention under multi-threaded workload
- [x] 9.4 Log results to serial output with markers (e.g., "PERF_CLEAR_MS: 0.5")
- [x] 9.5 Document expected performance gains in design doc
- [x] 9.6 Verify no regressions in boot time or shell responsiveness

## 10. Final Integration

- [x] 10.1 Run make clean && make to ensure build is reproducible
- [x] 10.2 Run make phase0-check for baseline regression
- [x] 10.3 Run make test-serial for graphics regression
- [x] 10.4 Run make test-graphics for new multi-threaded stress tests
- [x] 10.5 Run make verify-image for full integration validation
- [ ] 10.6 Commit all changes with reference to gfx-perf-optimization change