# MiniDOS Runtime Contracts

This document is the authoritative contract for subsystem invariants, failure classes, marker IDs, ownership boundaries, and storage-policy statements that other docs and tests must consume.

## Failure Classes

- `Hard-fail`: the kernel stops the current path immediately, emits a deterministic marker, and does not invent substitute runtime state.
- `Degraded mode`: the kernel keeps booting or returns control to the shell, but it emits a deterministic marker plus an operator-visible reason for the reduced behavior.
- `Decorative fallback`: non-critical UX paths may fall back without blocking boot or the shell, but the fallback behavior still has to be documented.

The rule for critical paths is strict: no silent skip and no synthetic critical resource outside isolated test fixtures.

## Initial Marker Catalog

Only deterministic IDs in this section are contractual. Human-readable prose logs remain useful for diagnosis, but tests and operator docs should prefer these IDs.

### Boot and startup markers

- `BOOT100`: kernel early boot reached before runtime drive probing.
- `BOOT110`: runtime logo assets loaded and the splash window can begin.
- `BOOT190`: splash window finished and the shell handoff is about to happen.
- `BOOT300`: documented degraded mode for startup-script processing when the required storage path is unavailable.

### Scheduler and runtime markers

- `SCHED100`: scheduler runtime bootstrap started.
- `SCHED110`: scheduler bootstrap thread and idle thread are initialized.
- `SCHED120`: scheduler timer/preemption path is active.
- `SCHED190`: positive scheduler self-test or ready path completed.
- `SCHED900`: negative scheduler guard-page path triggered.
- `STOP 0x00000006`: scheduler runtime bootstrap hard-failed before the shell started.
- `STOP 0x00000007`: scheduler phase-5 self-test hard-failed before the shell started.

### Shell and app markers

- `APPIN001`: app input surface is ready to accept keyboard or mouse injection.
- `APPRET001`: app returned control to the shell.
- `APPFLT900`: a user-mode fault was contained to the offending app or app group.
- `PTBIG100`: large-ELF regression app started after being mapped into userland.
- `PTBIG190`: large-ELF regression app touched memory beyond the old 1 MiB slot and returned successfully.
- `PTBIG900` / `PTBIG901`: large-ELF regression app detected an internal mapped-memory failure.
- `SHELL100`: emitted immediately after `Entering main loop` to signal that the interactive command loop is ready for deterministic injection.

### Storage markers

- `DISK021`: no validated boot volume or partition set was detected; boot continues without inventing a valid `A:` entry.

### Input markers

- `KBD200`: the keyboard consumed input through the direct polling compatibility fallback. It is a degraded mode, emitted at most once per boot; IRQ1 remains the preferred path.
- `MOUSE201`: PS/2 controller was busy during mouse initialization; mouse input is disabled.
- `MOUSE202`: PS/2 command-byte update failed; mouse input is disabled.
- `MOUSE203`: mouse default-settings command did not receive an ACK; mouse input is disabled.
- `MOUSE204`: mouse data-reporting command did not receive an ACK; mouse input is disabled.

### Video markers

- `PTVIDEO100 start workers=<w> iterations=<i> started=<s>`: the `videostress` request was accepted; decimal fields report requested workers, requested iterations per worker, and workers actually started.
- `PTVIDEO110 limited by scheduler slots`: fewer stress workers started than requested because scheduler capacity was unavailable.
- `PTVIDEO200 worker=0x........ ticks=0x........ iterations=0x........`: one completed stress worker. Each field is an uppercase, zero-padded 32-bit hexadecimal value; `ticks` is that worker's elapsed kernel-tick count and `iterations` is its completed render-loop count.

## Critical Subsystem Contracts

### `kernel/core`

**Primary owner:** `boot`

**Invariants**
- Protected-mode entry must complete before the kernel advertises shell readiness.
- Boot flow must emit deterministic startup markers in order.
- Startup policy must not live in build scripts or tests; the kernel must make the decision and log it.

**Invalid input / timeout / hardware-fault policy**
- Corrupt or missing boot metadata is a `Hard-fail`.
- Optional startup assets may use a `Decorative fallback` if the shell path remains valid.
- Missing storage required for `AUTOEXEC.AUT` processing is a `Degraded mode` (`BOOT300`), not a silent skip.

### `scheduler`

**Primary owner:** `scheduler`

**Invariants**
- The bootstrap thread, idle thread, and IRQ0-driven time base must be initialized before foreground/background user tasks rely on them.
- Kernel/user fault containment must emit deterministic markers before the shell regains control.

**Invalid input / timeout / hardware-fault policy**
- Guard-page hits, corrupted saved context, or fatal bootstrap invariants are `Hard-fail` unless the offending context is provably user-owned and containable.
- Fatal scheduler bootstrap failures must emit `STOP 0x00000006` or `STOP 0x00000007` on serial before the BSOD path takes over.
- User faults that stay inside the offending app group are `Degraded mode` for that app, not a kernel panic, and must emit `APPFLT900` before control returns through `APPRET001`.
- A terminated user task and every terminated member of its app group must become reusable on the next launch; the cleanup invariant covers the scheduler PID slot, user stack/runtime mapping, page-directory storage, and physical app arena span.

### `paging/interrupts`

**Primary owner:** `paging/interrupts`

**Invariants**
- Keyboard input is delivered through the IRQ1 handler when the interrupt path is enabled.
- The direct PS/2 polling path is compatibility-only and must not be required by the normal IRQ keyboard regression.
- Mouse input is exposed only after the PS/2 auxiliary device has completed initialization and IRQ12 is enabled.

**Invalid input / timeout / hardware-fault policy**
- If the compatibility polling path consumes a key, the kernel remains in degraded mode and emits `KBD200` so the loss of IRQ delivery is observable.
- If mouse initialization fails, the kernel remains in degraded mode without advertising mouse presence and emits exactly one of `MOUSE201`–`MOUSE204`.

### `disk/FAT`

**Primary owner:** `disk/FAT`

**Invariants**
- Drive enumeration reflects real media only.
- The boot volume may be BIOS-backed, but it must still represent an actual validated medium.
- FAT12/floppy compatibility claims must match the code path actually present in the repository.

**Invalid input / timeout / hardware-fault policy**
- Missing or invalid boot media is a reported storage failure (`DISK021`), not a synthetic `A:` drive.
- ATA/FAT errors that preserve kernel control must enter `Degraded mode` with deterministic storage markers.
- Corruption that invalidates the storage topology contract is a `Hard-fail`.

### `shell` startup

**Primary owner:** `shell`

**Invariants**
- The shell becomes injectable only after the runtime has entered the main loop and published its readiness marker.
- Startup-script policy, foreground return, and background app return paths must be observable from serial logs.

**Invalid input / timeout / hardware-fault policy**
- Missing optional user content may use a `Decorative fallback`.
- Missing startup-script storage is a `Degraded mode` with the stable marker `BOOT300`.
- A failed shell handoff after boot markers is a `Hard-fail`.

### `video`

**Primary owner:** `video`

**Invariants**
- Graphics syscalls are additive: syscall 35 is the bounded rectangle batch and syscall 36 is clipped text; existing single-rectangle, text, and surface calls keep their behavior.
- A rectangle batch contains 1 through 64 entries, is fully copied and validated before drawing, and is rendered in submission order under one video operation.
- Surface format 0 remains the legacy byte sequence `[X,R,G,B]`. Format 1 is a little-endian packed word with numeric value `0x00RRGGBB`; format 2 is straight-alpha `0xAARRGGBB` and applies source-over independently per RGB component as `(src * alpha + dst * (255 - alpha) + 127) / 255`.
- Formats 1 and 2 require runtime/kernel lockstep. Runtime run fallbacks activate only when the surface syscall explicitly returns failure and cannot detect a kernel that reports success while ignoring the format field; format 0 is the preserved earlier-ABI byte stream.
- Dirty-region coalescing may remove containment or merge touching/overlapping rectangles with the same complete horizontal or vertical span, but it must remain conservative; capacity overflow retains the bounding-union fallback.

**Invalid input / timeout / hardware-fault policy**
- Invalid batch entries, counts, flags, formats, geometry, stride, source-span arithmetic, or user ranges are rejected before the renderer changes pixels or records damage.
- A valid request clipped entirely outside the framebuffer succeeds without modifying the frame.
- Unsupported framebuffer layouts use the existing generic rendering/present paths; they are not silently reinterpreted as a packed format.

### `userland`

**Primary owner:** `userland`

**Invariants**
- Syscall arguments are validated against the calling app's mapped user range before the kernel dereferences user pointers or mutates filesystem/video state.
- ELF and `.COM` apps share the same syscall return-value contract and return to the shell after contained invalid-argument cases.

**Invalid input / timeout / hardware-fault policy**
- Invalid pointers, ranges, flags, formats, geometry, and unknown syscall numbers return the syscall's documented failure value; they do not panic the kernel or leave a scheduler task unreleased.
- A direct user page fault remains an app/group fault (`APPFLT900`), distinct from a rejected syscall argument.

## Subsystem Owner Map

- `boot`: `src/boot/`, stage2 handoff, early `kernel/core` orchestration, boot-time serial markers.
- `paging/interrupts`: GDT/IDT/ISR/PIC/PIT paths under `src/kernel/core/`, plus the authoritative PS/2 keyboard/mouse IRQ plumbing under `src/kernel/input/` and the shared `src/kernel/input/ps2_controller.h` helpers they consume.
- `disk/FAT`: `src/kernel/storage/`, storage enumeration, FAT12/floppy compatibility policy, ATA/FAT runtime behavior.
- `shell`: shell loop, built-in command routing, shell-visible filesystem behavior, shell readiness contract.
- `scheduler`: `src/kernel/process/`, task lifecycle, guard pages, ring3 return-to-kernel behavior.
- `video`: `src/kernel/video/`, graphics rendering/presentation and damage tracking, plus the graphics ABI validation portion of `src/kernel/shell/shell_apps.c` and its matching declarations/helpers in `external_apps/runtime/`.
- `userland`: external app ABI, loader/runtime bridge, `src/kernel/shell/shell_apps.c`, `external_apps/`.
- `docs/tooling`: docs, build/test contracts, marker catalog, `scripts/`, and harness-level validation behavior.

## Oversized File Split Targets

### `src/kernel/core/kernel.c`

- **Current owner bucket:** `boot`
- **Keep in file:** early orchestration, subsystem init ordering, deterministic boot/degraded-mode markers.
- **Move out:** storage-policy decisions, startup-script policy helpers, and any shell/runtime behavior that is not strictly boot orchestration.

### `src/kernel/shell/shell_apps.c`

- **Current owner bucket:** `userland`
- **Keep in file:** app-launch contract and shell-facing dispatch glue only.
- **Move out:** loader format details, background lifecycle helpers, and syscall/runtime bridge code that can live behind narrower interfaces.

### `src/kernel/shell/shell_builtin.c`

- **Current owner bucket:** `shell`
- **Keep in file:** builtin dispatch tables and shell-only command glue.
- **Move out:** process-introspection commands, UI launch commands, and other grouped builtin families into dedicated modules.

### `src/kernel/storage/fat16_dir.c`

- **Current owner bucket:** `disk/FAT`
- **Keep in file:** directory traversal contract and entry lifecycle boundaries.
- **Move out:** path parsing helpers, mutation helpers, and enumeration/reporting helpers once they can be tested independently.

## Shared Helper Rule

Port I/O wrappers, physical-memory access helpers, and similar low-level primitives must converge on shared infrastructure instead of remaining duplicated across unrelated files. Keyboard cleanup is the first required audit point for this rule.

## FAT12 / Floppy Policy

The current repository contract is `compatibility-only`:

- The build image is still a FAT12 floppy because BIOS boot compatibility requires it.
- The boot floppy is exposed at runtime through the BIOS thunk as a validated boot volume only.
- ATA-backed FAT16 remains the first-class runtime storage path.
- There is no standalone FAT12 runtime driver and no native FDC driver in the current contract.

If the project later promotes FAT12/floppy to a first-class storage capability, this document must name the owning module, the required marker IDs, and the dedicated regression coverage before the roadmap or product strings are updated.
