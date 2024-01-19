// Copyright (c) 2022 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
struct object
{
    virtual ~object() {}

#if defined(USE_MEMORY_TRACKING) && defined(USE_RTTI)
    void* __cdecl object::operator new(size_t size);
#endif
};

//------------------------------------------------------------------------------
#if defined(USE_MEMORY_TRACKING) && defined(USE_RTTI)
#define USE_DEBUG_OBJECT
#endif
