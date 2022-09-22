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
#define _DBGOBJECT      : public object
#define DBGOBJECT_      public object,
#else
#define _DBGOBJECT
#define DBGOBJECT_
#endif
