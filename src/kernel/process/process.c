#include "process.h"

const char* process_state_name(process_state_t state) {
    switch (state) {
        case PROCESS_UNUSED:
            return "unused";
        case PROCESS_READY:
            return "ready";
        case PROCESS_RUNNING:
            return "running";
        case PROCESS_TERMINATED:
            return "terminated";
        default:
            return "invalid";
    }
}
