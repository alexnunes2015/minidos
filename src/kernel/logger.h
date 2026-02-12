#ifndef LOGGER_H
#define LOGGER_H

#include "video.h"
#include "serial.h"

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3,
    LOG_LEVEL_TRACE = 4
} LogLevel;

typedef enum {
    LOG_DEST_SCREEN = 1,
    LOG_DEST_SERIAL = 2,
    LOG_DEST_BOTH = 3
} LogDest;

#ifndef MIN_LOG_LEVEL
#define MIN_LOG_LEVEL LOG_LEVEL_DEBUG
#endif

void log_init();
void log_write(LogLevel level, const char* module, const char* msg, LogDest dest);

void log_screen(const char* msg);
void log_serial_raw(const char* msg);
void log_both(const char* msg);

void log_dump_buffer(LogDest dest);

#endif
