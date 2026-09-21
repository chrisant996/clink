#pragma once

#include <bldopts.h>
#include <core/base.h>
#include <core/debugheap.h>

#define __wcswidth clink_wcswidth
extern "C" uint32 clink_wcswidth(const char* s, uint32 len);
