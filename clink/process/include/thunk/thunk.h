// Copyright (c) 2025 Christopher Antos
// License: http://opensource.org/licenses/MIT

#if defined(_MSC_VER)
# pragma warning(push)
# pragma warning(disable : 4200)
#endif
struct zzz_thunk_data
{
    void*   (WINAPI* func)(void*);
    void*   out;
    char    in[];
};
#if defined(_MSC_VER)
# pragma warning(pop)
#endif

extern "C" DWORD WINAPI zzz_stdcall_thunk(zzz_thunk_data& data);
