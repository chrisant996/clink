// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

//------------------------------------------------------------------------------
// This function exists purely to make sure it gets exported from the DLL.
__declspec(dllexport) int loader_main_thunk()
{
    extern int loader_main_impl();
    return loader_main_impl();
}
