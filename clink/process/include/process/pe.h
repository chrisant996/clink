// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
class pe_info
{
public:
                        pe_info(void* base);
    void**              get_import_by_name(const char* dll, const char* func_name) const;
    void**              get_import_by_addr(const char* dll, void* func_addr) const;
    void*               get_export(const char* func_name) const;

private:
    typedef void**      (pe_info::*import_iter_t)(IMAGE_IMPORT_DESCRIPTOR*, const void*) const;

    IMAGE_NT_HEADERS*   get_nt_headers() const;
    void*               get_data_directory(int index, int* size=nullptr) const;
    void*               rva_to_addr(unsigned int rva) const;
    void**              import_by_addr(IMAGE_IMPORT_DESCRIPTOR* iid, const void* func_addr) const;
    void**              import_by_name(IMAGE_IMPORT_DESCRIPTOR* iid, const void* func_name) const;
    void**              iterate_imports(const char* dll, const void* param, import_iter_t iter_func) const;
    void*               m_base;
};
