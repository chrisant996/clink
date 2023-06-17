// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include <cstdint>

//------------------------------------------------------------------------------
// This function exists purely to make sure it gets exported from the DLL.
__declspec(dllexport) int32_t loader_main_thunk()
{
    extern int32_t loader_main_impl();
    return loader_main_impl();
}
