/* Copyright (c) 2012 Martin Ridgers
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pch.h"

#include <core/log.h>

//------------------------------------------------------------------------------
static void* rva_to_addr(void* base, unsigned rva)
{
    return (char*)(uintptr_t)rva + (uintptr_t)base;
}

//------------------------------------------------------------------------------
void* get_nt_headers(void* base)
{
    IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*)base;
    return (char*)base + dos_header->e_lfanew;
}

//------------------------------------------------------------------------------
void* get_data_directory(void* base, int index, int* size)
{
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)get_nt_headers(base);
    IMAGE_DATA_DIRECTORY* data_dir = nt->OptionalHeader.DataDirectory + index;
    if (data_dir == nullptr)
    {
        return nullptr;
    }

    if (data_dir->VirtualAddress == 0)
    {
        return nullptr;
    }

    if (size != nullptr)
    {
        *size = data_dir->Size;
    }

    return rva_to_addr(base, data_dir->VirtualAddress);
}

//------------------------------------------------------------------------------
static void** iterate_imports(
    void* base,
    const char* dll,
    const void* param,
    void** (*callback)(void*, IMAGE_IMPORT_DESCRIPTOR*, const void*)
)
{
    IMAGE_IMPORT_DESCRIPTOR* iid;
    iid = (IMAGE_IMPORT_DESCRIPTOR*)get_data_directory(base, 1, nullptr);
    if (iid == nullptr)
    {
        LOG("Failed to find import desc for base %p", base);
        return 0;
    }

    while (iid->Characteristics)
    {
        char* name;
        size_t len;

        len = (dll != nullptr) ? strlen(dll) : 0;
        name = (char*)rva_to_addr(base, iid->Name);
        if (dll == nullptr || _strnicmp(name, dll, len) == 0)
        {
            void** ret = callback(base, iid, param);

            LOG("Checking imports in '%s'", name);

            if (ret != nullptr)
                return ret;
        }

        ++iid;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
static void** import_by_addr(
    void* base,
    IMAGE_IMPORT_DESCRIPTOR* iid,
    const void* func_addr
)
{
    void** at = (void**)rva_to_addr(base, iid->FirstThunk);
    while (*at != 0)
    {
        uintptr_t addr = (uintptr_t)(*at);
        void* addr_loc = at;

        if (addr == (uintptr_t)func_addr)
        {
            return at;
        }

        ++at;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
static void** import_by_name(
    void* base,
    IMAGE_IMPORT_DESCRIPTOR* iid,
    const void* func_name
)
{
    void** at = (void**)rva_to_addr(base, iid->FirstThunk);
    intptr_t* nt = (intptr_t*)rva_to_addr(base, iid->OriginalFirstThunk);
    while (*at != 0 && *nt != 0)
    {
        // Check that this import is imported by name (MSB not set)
        if (*nt > 0)
        {
            unsigned rva = (unsigned)(*nt & 0x7fffffff);
            IMAGE_IMPORT_BY_NAME* iin;
            iin = (IMAGE_IMPORT_BY_NAME*)rva_to_addr(base, rva);

            if (_stricmp((const char*)(iin->Name), (const char*)func_name) == 0)
                return at;
        }

        ++at;
        ++nt;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
void** get_import_by_name(void* base, const char* dll, const char* func_name)
{
    return iterate_imports(base, dll, func_name, import_by_name);
}

//------------------------------------------------------------------------------
void** get_import_by_addr(void* base, const char* dll, void* func_addr)
{
    return iterate_imports(base, dll, func_addr, import_by_addr);
}

//------------------------------------------------------------------------------
void* get_export(void* base, const char* func_name)
{
    IMAGE_NT_HEADERS* nt_header;
    IMAGE_DATA_DIRECTORY* data_dir;
    IMAGE_EXPORT_DIRECTORY* ied;
    int i;
    DWORD* names;
    WORD* ordinals;
    DWORD* addresses;

    IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*)base;
    nt_header = (IMAGE_NT_HEADERS*)((char*)base + dos_header->e_lfanew);
    data_dir = nt_header->OptionalHeader.DataDirectory;

    if (data_dir == nullptr)
    {
        LOG("Failed to find export table for base %p", base);
        return nullptr;
    }

    if (data_dir->VirtualAddress == 0)
    {
        LOG("No export directory found at base %p", base);
        return nullptr;
    }

    ied = (IMAGE_EXPORT_DIRECTORY*)rva_to_addr(base, data_dir->VirtualAddress);
    names = (DWORD*)rva_to_addr(base, ied->AddressOfNames);
    ordinals = (WORD*)rva_to_addr(base, ied->AddressOfNameOrdinals);
    addresses = (DWORD*)rva_to_addr(base, ied->AddressOfFunctions);

    for (i = 0; i < (int)(ied->NumberOfNames); ++i)
    {
        const char* export_name = (const char*)rva_to_addr(base, names[i]);
        if (_stricmp(export_name, func_name))
            continue;

        WORD ordinal = ordinals[i];
        return rva_to_addr(base, addresses[ordinal]);
    }

    return nullptr;
}
