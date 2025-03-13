// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "pe.h"

#include <core/log.h>
#include <core/str.h>

#include <vector>

//------------------------------------------------------------------------------
pe_info::pe_info(void* base)
: m_base(base)
{
}

//------------------------------------------------------------------------------
void* pe_info::rva_to_addr(uint32 rva) const
{
    return (char*)(uintptr_t)rva + (uintptr_t)m_base;
}

//------------------------------------------------------------------------------
IMAGE_NT_HEADERS* pe_info::get_nt_headers() const
{
    IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*)m_base;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;

    return (IMAGE_NT_HEADERS*)((char*)m_base + dos_header->e_lfanew);
}

//------------------------------------------------------------------------------
void* pe_info::get_data_directory(int32 index, int32* size) const
{
    IMAGE_NT_HEADERS* nt = get_nt_headers();
    if (nt == nullptr)
        return nullptr;

    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;

    IMAGE_DATA_DIRECTORY* data_dir = nt->OptionalHeader.DataDirectory + index;
    if (data_dir == nullptr)
        return nullptr;

    if (data_dir->VirtualAddress == 0)
        return nullptr;

    if (size != nullptr)
        *size = data_dir->Size;

    return rva_to_addr(data_dir->VirtualAddress);
}

//------------------------------------------------------------------------------
pe_info::funcptr_t* pe_info::import_by_addr(IMAGE_IMPORT_DESCRIPTOR* iid, const void* func_addr) const
{
    void** at = (void**)rva_to_addr(iid->FirstThunk);
    while (*at != 0)
    {
        uintptr_t addr = (uintptr_t)(*at);
        void* addr_loc = at;

        if (addr == (uintptr_t)func_addr)
            return (funcptr_t*)at;

        ++at;
    }

    return nullptr;
}

//------------------------------------------------------------------------------
pe_info::funcptr_t* pe_info::import_by_name(IMAGE_IMPORT_DESCRIPTOR* iid, const void* func_name) const
{
    void** at = (void**)rva_to_addr(iid->FirstThunk);
    intptr_t* nt = (intptr_t*)rva_to_addr(iid->OriginalFirstThunk);
    funcptr_t* ret = nullptr;

    // Validation to avoid crashing; e.g. ANSICON begins with an invalid RVA.
    size_t entry_index = 0;
    size_t num_badrva = 0;
    IMAGE_NT_HEADERS* headers = get_nt_headers();
    if (headers == nullptr)
        return nullptr;
    const uintptr_t sizeOfImage = headers->OptionalHeader.SizeOfImage;

    while (*at != 0 && *nt != 0)
    {
        // Check that this import is imported by name (MSB not set)
        if (*nt > 0)
        {
            if (uintptr_t(*nt) >= sizeOfImage)
            {
                if (!num_badrva)
                    LOG("Skipping bad RVA 0x%p in import entry %zu; exceeds sizeOfImage 0x%p for '%s'.", *nt, entry_index, sizeOfImage, m_image ? m_image : "<unknown>");
                ++num_badrva;
            }
            else
            {
                unsigned rva = (unsigned)(*nt & 0x7fffffff);
                IMAGE_IMPORT_BY_NAME* iin;
                iin = (IMAGE_IMPORT_BY_NAME*)rva_to_addr(rva);

                if (_stricmp((const char*)(iin->Name), (const char*)func_name) == 0)
                {
                    ret = (funcptr_t*)at;
                    break;
                }
            }
        }

        ++at;
        ++nt;
        ++entry_index;
    }

    if (num_badrva > 1)
        LOG("... (%zu bad RVAs total)", num_badrva);

    return ret;
}

//------------------------------------------------------------------------------
pe_info::funcptr_t* pe_info::get_import_by_name(const char* dll, const char* func_name, str_base* found_in_module) const
{
    return iterate_imports(dll, func_name, &pe_info::import_by_name, found_in_module);
}

//------------------------------------------------------------------------------
pe_info::funcptr_t* pe_info::get_import_by_addr(const char* dll, funcptr_t func_addr, str_base* found_in_module) const
{
    return iterate_imports(dll, (const void*)func_addr, &pe_info::import_by_addr, found_in_module);
}

//------------------------------------------------------------------------------
pe_info::funcptr_t pe_info::get_export(const char* func_name) const
{
    if (m_base == nullptr)
        return nullptr;

    IMAGE_EXPORT_DIRECTORY* ied = (IMAGE_EXPORT_DIRECTORY*)get_data_directory(0);
    if (ied == nullptr)
    {
        LOG("No export directory found at base %p", m_base);
        return nullptr;
    }

    DWORD* names = (DWORD*)rva_to_addr(ied->AddressOfNames);
    WORD* ordinals = (WORD*)rva_to_addr(ied->AddressOfNameOrdinals);
    DWORD* addresses = (DWORD*)rva_to_addr(ied->AddressOfFunctions);

    for (int32 i = 0; i < int32(ied->NumberOfNames); ++i)
    {
        const char* export_name = (const char*)rva_to_addr(names[i]);
        if (_stricmp(export_name, func_name))
            continue;

        WORD ordinal = ordinals[i];
        return funcptr_t(rva_to_addr(addresses[ordinal]));
    }

    return nullptr;
}

//------------------------------------------------------------------------------
pe_info::funcptr_t* pe_info::iterate_imports(
    const char* dll,
    const void* param,
    import_iter_t iter_func,
    str_base* found_in_module) const
{
    IMAGE_IMPORT_DESCRIPTOR* iid;
    iid = (IMAGE_IMPORT_DESCRIPTOR*)get_data_directory(1, nullptr);
    if (iid == nullptr)
    {
        LOG("Failed to find import desc for base %p", m_base);
        return 0;
    }

    std::vector<str_moveable> checked_imports;
    while (iid->Name)
    {
        char* name;
        size_t len;

        len = (dll != nullptr) ? strlen(dll) : 0;
        name = (char*)rva_to_addr(iid->Name);
        if (dll == nullptr || _strnicmp(name, dll, len) == 0)
        {
            m_image = name;
            if (funcptr_t* ret = (this->*iter_func)(iid, param))
            {
                LOG("Found import in '%s'", name);
                if (found_in_module)
                    found_in_module->copy(name);
                m_image = nullptr;
                return ret;
            }
            checked_imports.emplace_back(name);
            m_image = nullptr;
        }

        ++iid;
    }

    for (auto const& name : checked_imports)
        LOG("Not found in '%s'", name.c_str());

    return nullptr;
}
