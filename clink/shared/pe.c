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
#include "util.h"

//------------------------------------------------------------------------------
static void* rva_to_addr(void* base, unsigned rva)
{
    return (char*)(uintptr_t)rva + (uintptr_t)base;
}

//------------------------------------------------------------------------------
void* get_nt_headers(void* base)
{
    IMAGE_DOS_HEADER* dos_header;

    dos_header = base;
    return (char*)base + dos_header->e_lfanew;
}

//------------------------------------------------------------------------------
void* get_data_directory(void* base, int index, int* size)
{
    IMAGE_NT_HEADERS* nt_headers;
    IMAGE_DATA_DIRECTORY* data_dir;

    nt_headers = get_nt_headers(base);
    data_dir = nt_headers->OptionalHeader.DataDirectory + index;
    if (data_dir == NULL)
    {
        return NULL;
    }

    if (data_dir->VirtualAddress == 0)
    {
        return NULL;
    }

    if (size != NULL)
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

    iid = get_data_directory(base, 1, NULL);
    if (iid == NULL)
    {
        LOG_INFO("Failed to find import desc for base %p", base);
        return 0;
    }

    while (iid->Characteristics)
    {
        char* name;
        size_t len;

        len = (dll != NULL) ? strlen(dll) : 0;
        name = (char*)rva_to_addr(base, iid->Name);
        if (dll == NULL || _strnicmp(name, dll, len) == 0)
        {
            void** ret;

            LOG_INFO("Checking imports in '%s'", name);

            ret = callback(base, iid, param);
            if (ret != NULL)
            {
                return ret;
            }
        }

        ++iid;
    }

    return NULL;
}

//------------------------------------------------------------------------------
static void** import_by_addr(
    void* base,
    IMAGE_IMPORT_DESCRIPTOR* iid,
    const void* func_addr
)
{
    void** at = rva_to_addr(base, iid->FirstThunk);
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

    return NULL;
}

//------------------------------------------------------------------------------
static void** import_by_name(
    void* base,
    IMAGE_IMPORT_DESCRIPTOR* iid,
    const void* func_name
)
{
    void** at = rva_to_addr(base, iid->FirstThunk);
    intptr_t* nt = rva_to_addr(base, iid->OriginalFirstThunk);
    while (*at != 0 && *nt != 0)
    {
        // Check that this import is imported by name (MSB not set)
        if (*nt > 0)
        {
            unsigned rva = (unsigned)(*nt & 0x7fffffff);
            IMAGE_IMPORT_BY_NAME* iin = rva_to_addr(base, rva);

            if (_stricmp(iin->Name, func_name) == 0)
                return at;
        }

        ++at;
        ++nt;
    }

    return NULL;
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
    IMAGE_DOS_HEADER* dos_header;
    IMAGE_NT_HEADERS* nt_header;
    IMAGE_DATA_DIRECTORY* data_dir;
    IMAGE_EXPORT_DIRECTORY* ied;
    int i;
    DWORD* names;
    WORD* ordinals;
    DWORD* addresses;

    dos_header = base;
    nt_header = (IMAGE_NT_HEADERS*)((char*)base + dos_header->e_lfanew);
    data_dir = nt_header->OptionalHeader.DataDirectory;

    if (data_dir == NULL)
    {
        LOG_INFO("Failed to find export table for base %p", base);
        return NULL;
    }

    if (data_dir->VirtualAddress == 0)
    {
        LOG_INFO("No export directory found at base %p", base);
        return NULL;
    }

    ied = rva_to_addr(base, data_dir->VirtualAddress);
    names = rva_to_addr(base, ied->AddressOfNames);
    ordinals = rva_to_addr(base, ied->AddressOfNameOrdinals);
    addresses = rva_to_addr(base, ied->AddressOfFunctions);

    for (i = 0; i < (int)(ied->NumberOfNames); ++i)
    {
        const char* export_name;
        WORD ordinal;

        export_name = rva_to_addr(base, names[i]);
        if (_stricmp(export_name, func_name))
        {
            continue;
        }

        ordinal = ordinals[i];
        return rva_to_addr(base, addresses[ordinal]);
    }

    return NULL;
}


// vim: expandtab
