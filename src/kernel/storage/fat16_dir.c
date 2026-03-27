#include "fat16_internal.h"
#include "serial.h"
#include "video.h"

/* Keep traversal and mutation logic split out without changing the final link unit. */
#include "fat16_dir_lookup.inc"
#include "fat16_dir_mutation.inc"
