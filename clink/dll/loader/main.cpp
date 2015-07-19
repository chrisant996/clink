// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include <stdint.h>
#include <Windows.h>

//------------------------------------------------------------------------------
__declspec(dllimport) int loader_main_thunk();

#if defined(_VC_NODEFAULTLIB)
#pragma runtime_checks("", off)
int mainCRTStartup(uintptr_t param) // effectively a thread entry point.
#else
int main(int argc, char** argv)
#endif
{
    return loader_main_thunk();
}
