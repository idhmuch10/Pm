#include "common.h"

#ifdef PORT
// PORT: the IS-Viewer 64 debug channel does not exist. Route the game's debug
// output to stderr (which the Android build forwards to logcat) and leave the C
// library's printf alone: the original file defines a void printf() that would
// override libc's for the whole program and conflicts with bionic's <stdio.h>.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void is_debug_init(void) {
}

void osSyncPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void rmonPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void is_debug_panic(const char* message, char* file, s32 line) {
    fprintf(stderr, "File:%s Line:%d  %s \n", file, line, message);
    abort();
}

#else // PORT

#include "stdlib/stdarg.h"
#include "nu/nusys.h"

typedef struct {
    /* 0x00 */ u32 magic;
    /* 0x04 */ u32 get;
    /* 0x08 */ char unk_08[0x14-0x08];
    /* 0x14 */ u32 put;
    /* 0x18 */ char unk_18[0x20-0x18];
    /* 0x20 */ char data[0x10000-0x20];
} ISVDbg;

#define gISVDbgPrnAdrs ((ISVDbg*) 0xb3ff0000)

char* is_debug_print(char* arg0, const char* str, size_t count);

#if !VERSION_PAL
void is_debug_init(void) {
    osEPiWriteIo(nuPiCartHandle, (u32) &gISVDbgPrnAdrs->put, 0);
    osEPiWriteIo(nuPiCartHandle, (u32) &gISVDbgPrnAdrs->get, 0);
    osEPiWriteIo(nuPiCartHandle, (u32) &gISVDbgPrnAdrs->magic, ASCII_TO_U32('I', 'S', '6', '4'));
}
#endif

void printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    _Printf(is_debug_print, nullptr, fmt, args);
}

void osSyncPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    _Printf(is_debug_print, nullptr, fmt, args);
}

void rmonPrintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    _Printf(is_debug_print, nullptr, fmt, args);
}

char* is_debug_print(char* arg0, const char* str, size_t count) {
    u32 data;
    s32 pos;
    s32 start;
    s32 end;

    osEPiReadIo(nuPiCartHandle, (u32) &gISVDbgPrnAdrs->magic, &data);
    if (data != ASCII_TO_U32('I', 'S', '6', '4')) {
        return (char*) 1;
    }
    osEPiReadIo(nuPiCartHandle, (u32) &gISVDbgPrnAdrs->get, &data);
    pos = data;
    osEPiReadIo(nuPiCartHandle, (u32) &gISVDbgPrnAdrs->put, &data);
    start = data;
    end = start + count;
    if (end >= 0xffe0) {
        end -= 0xffe0;
        if (pos < end || start < pos) {
            return (char*) 1;
        }
    } else {
        if (start < pos && pos < end) {
            return (char*) 1;
        }
    }
    while (count) {
        u8 c = *str;

        if (c != 0) {
            u32 addr = (u32) &gISVDbgPrnAdrs->data + (start & 0xffffffc);
            s32 shift = ((3 - (start & 3)) * 8);

            osEPiReadIo(nuPiCartHandle, addr, &data);
            osEPiWriteIo(nuPiCartHandle, addr, (data & ~(0xff << shift)) | ((u8)*str << shift));

            start++;
            if (start >= 0xffe0) {
                start -= 0xffe0;
            }
        }
        count--;
        str++;
    }
    osEPiWriteIo(nuPiCartHandle, (u32)&gISVDbgPrnAdrs->put, start);
    return (char*) 1;
}

void is_debug_panic(const char* message, char* file, s32 line) {
    osSyncPrintf("File:%s Line:%d  %s \n", file, line, message);
    do {} while (true);
}

#endif // PORT
