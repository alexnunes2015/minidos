## 1. Graphics ABI and kernel entry points

- [x] 1.1 Add additive rectangle-batch, clipped-text, packed-XRGB, and packed-ARGB definitions and wrappers to the application ABI; verify with `make verify-image`.
- [x] 1.2 Validate and copy rectangle batches and surface descriptors atomically in the syscall boundary, including checked stride/span arithmetic; verify with `make verify-image`.
- [x] 1.3 Add kernel video interfaces for batched rectangles, clipped text, and typed surface descriptors while preserving existing entry points; verify with `make verify-image`.

## 2. Video rendering fast paths

- [x] 2.1 Render rectangle batches under one video lock and one auto-present decision while preserving submission order; verify with `make test-graphics`.
- [x] 2.2 Implement packed-XRGB row-copy and packed-ARGB source-over paths, retain legacy format-0 conversion, and remove inner-loop scaling divisions; verify with `make test-ui-runtime && make test-graphics`.
- [x] 2.3 Render clipped opaque or transparent text under one video lock and delegate the legacy text call to it; verify with `make test-ui-runtime && make test-graphics`.
- [x] 2.4 Coalesce contained and same-span touching dirty rectangles without changing overflow fallback behavior; verify with `make test-graphics`.

## 3. UI runtime and Win95Ui integration

- [x] 3.1 Queue up to 64 runtime rectangle fills and flush them before order-sensitive graphics operations and presentation; verify with `make test-ui-runtime`.
- [x] 3.2 Migrate runtime-generated surfaces to explicit packed formats with clipped color-run fallbacks, and route generic clipped text through the new syscall; verify byte order, fallback, and clipping with `make test-ui-runtime`.
- [x] 3.3 Route Win95Ui icons through the ARGB surface compositor with a run fallback, and keep its transparent labels pixel-identical through batched `ui_font_8x8` runs; verify with `make test-ui-runtime`.

## 4. Regression coverage and observability

- [x] 4.1 Extend host runtime tests for batch ordering/fallback, packed byte order, alpha, and single-call clipped text; verify with `make test-ui-runtime`.
- [x] 4.2 Make the existing `PTVIDEO200` stress marker report deterministic ticks and iteration fields and assert its schema; verify with `make test-graphics`.

## 5. Documentation and validation

- [x] 5.1 Document the additive ABI, byte-order compatibility, batching rules, alpha semantics, performance marker, and `video` owner boundary in graphics, design, debugging, test, protocol, and contract documentation; verify with `git diff --check` and strict OpenSpec validation.
- [x] 5.2 Run Tier 3 validation with `make ci`, plus the focused `make test-ui-runtime`, `make test-graphics`, and `make test-mouse` suites.
