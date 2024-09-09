// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#ifdef _MSC_VER

#define MAX_ADDRESS_LEN         (2 + 16)
#define MAX_MODULE_LEN          (24)
#define MAX_SYMBOL_LEN          (256)

//                                   MODULE      "! "     SYMBOL     " + "   0xOFFSET       "\r\n" or " / "
#define MAX_FRAME_LEN           (MAX_MODULE_LEN + 2 + MAX_SYMBOL_LEN + 3 + MAX_ADDRESS_LEN + 3)

#define DEFAULT_CALLSTACK_LEN   (MAX_FRAME_LEN * 20)

#ifdef __cplusplus
#define CALLSTACK_EXTERN_C      extern "C"
#endif

// Formats buffer (capacity is size of buffer) with up to total_frames, skipping
// the first skip_frames.  The frames are delimited with newlines.
CALLSTACK_EXTERN_C size_t format_callstack(int32 skip_frames, int32 total_frames, char* buffer, size_t capacity, int32 newlines);

// Copies stack frame pointers.  They can can formatted later with
// format_frames().
CALLSTACK_EXTERN_C int32 get_callstack_frames(int32 skip_frames, int32 total_frames, void** frames, DWORD* hash);

// Formats buffer (capacity is size of buffer) with up to total_frames.  The
// frames are delimited with slashes or newlines.
CALLSTACK_EXTERN_C size_t format_frames(int32 total_frames, void* const* frames, DWORD hash, char* buffer, size_t capacity, int32 newlines);

#endif // _MSC_VER