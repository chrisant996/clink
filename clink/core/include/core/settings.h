// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"

class setting;

//------------------------------------------------------------------------------
namespace settings
{
    setting*    first();
    setting*    find(const char* name);
    bool        load(const char* file);
    bool        save(const char* file);
};



//------------------------------------------------------------------------------
class setting
{
public:
    enum type_e {
        type_unknown,
        type_int,
        type_bool,
        type_string,
        type_enum,
    };

    virtual           ~setting();
    setting*          next() const;
    type_e            get_type() const;
    const char*       get_name() const;
    const char*       get_short_desc() const;
    const char*       get_long_desc() const;
    virtual bool      set(const char* value) = 0;
    virtual void      get(str_base& out) const = 0;

protected:
                      setting(const char* name, const char* short_desc, const char* long_desc, type_e type);
    str<32, false>    m_name;
    str<48, false>    m_short_desc;
    str<128>          m_long_desc;
    setting*          m_prev;
    setting*          m_next;
    type_e            m_type;
};

//------------------------------------------------------------------------------
template <typename TYPE>
class setting_impl
    : public setting
{
public:
                    setting_impl(const char* name, const char* short_desc, const char* long_desc, TYPE default_value);
    TYPE            get() const;
    virtual bool    set(const char* value) override;
    virtual void    get(str_base& out) const override;

protected:
    template <typename TYPE> struct store
    {
                    operator TYPE () const { return value; }
        TYPE        value;
    };

    template <> struct store<const char*>
    {
                    operator const char* () const { return value.c_str(); }
        str<64>     value;
    };

    template <typename TYPE> struct type              { enum { id = type_unknown }; };
    template <>              struct type<bool>        { enum { id = type_bool }; };
    template <>              struct type<int>         { enum { id = type_int }; };
    template <>              struct type<const char*> { enum { id = type_string }; };

    store<TYPE>     m_store;
};

//------------------------------------------------------------------------------
template <typename TYPE> setting_impl<TYPE>::setting_impl(
    const char* name,
    const char* short_desc,
    const char* long_desc,
    TYPE default_value)
: setting(name, short_desc, long_desc, type_e(type<TYPE>::id))
{
    m_store.value = default_value;
}

//------------------------------------------------------------------------------
template <typename TYPE> TYPE setting_impl<TYPE>::get() const
{
    return m_store;
}



//------------------------------------------------------------------------------
typedef setting_impl<bool>         setting_bool;
typedef setting_impl<int>          setting_int;
typedef setting_impl<const char*>  setting_str;

//------------------------------------------------------------------------------
class setting_enum
    : public setting_impl<int>
{
public:
                       setting_enum(const char* name, const char* short_desc, const char* long_desc, const char* values, int default_value);
    virtual bool       set(const char* value) override;
    virtual void       get(str_base& out) const override;
    const char*        get_options() const;

    using setting_impl<int>::get;

protected:
    static const char* next_option(const char* option);
    str<48>            m_options;
};
