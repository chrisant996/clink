// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include <Windows.h>

//------------------------------------------------------------------------------
void    on_dll_attach();
void    on_dll_detach();

//------------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID unused)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:    on_dll_attach();    break;
    case DLL_PROCESS_DETACH:    on_dll_detach();    break;
    }

    return TRUE;
}



//------------------------------------------------------------------------------
// This function exists purely to make sure it gets exported from the DLL.
__declspec(dllexport) int loader_main_thunk()
{
    extern int loader_main_impl();
    return loader_main_impl();
}
