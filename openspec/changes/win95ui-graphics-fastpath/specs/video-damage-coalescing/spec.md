## ADDED Requirements

### Requirement: Duplicate and same-span damage is coalesced
The video subsystem SHALL avoid recording redundant damage when a new rectangle is contained by an existing rectangle or can be joined to an overlapping or adjacent rectangle with the same complete horizontal or vertical span.

#### Scenario: Duplicate damage is discarded
- **WHEN** a newly marked rectangle is fully contained by an already recorded rectangle
- **THEN** the dirty rectangle count does not increase

#### Scenario: Horizontal strips are joined
- **WHEN** two damage rectangles have equal `y` and `h` and their horizontal ranges overlap or touch
- **THEN** they are replaced by their horizontal union

#### Scenario: Vertical strips are joined
- **WHEN** two damage rectangles have equal `x` and `w` and their vertical ranges overlap or touch
- **THEN** they are replaced by their vertical union

### Requirement: Damage tracking remains conservative and bounded
Coalescing MUST never omit a modified pixel, and capacity exhaustion SHALL retain the existing bounding-union fallback.

#### Scenario: Incompatible shapes remain separate
- **WHEN** two rectangles cannot be joined without a materially larger bounding region
- **THEN** both rectangles remain recorded while capacity is available

#### Scenario: Capacity is exhausted
- **WHEN** non-coalescible damage exceeds `VIDEO_DIRTY_RECT_CAPACITY`
- **THEN** present copies a conservative union containing every modified pixel
