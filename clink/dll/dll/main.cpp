// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include <Windows.h>

//------------------------------------------------------------------------------
void shutdown_clink();

//------------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID unused)
{
    if (reason == DLL_PROCESS_DETACH)
        shutdown_clink();

    return TRUE;
}



//------------------------------------------------------------------------------
// This function exists purely to make sure it gets exported from the DLL.
__declspec(dllexport) int loader_main_thunk()
{
    extern int loader_main_impl();
    return loader_main_impl();
}
