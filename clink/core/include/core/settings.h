// Copyright (c) 2012 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include "str.h"
#include "str_map.h"

#include <map>
#include <vector>

class setting;

//------------------------------------------------------------------------------
typedef str_map_caseless<setting*>::type setting_map;

//------------------------------------------------------------------------------
class setting_iter
{
public:
                            setting_iter(setting_map& map);
    setting*                next();
private:
    setting_map&            m_map;
    setting_map::iterator   m_iter;
};

//------------------------------------------------------------------------------
namespace settings
{

const uint32 c_max_len_name = 32;
const uint32 c_max_len_short_desc = 48;

setting_iter        first();
setting*            find(const char* name);
bool                load(const char* file, const char* default_file=nullptr);
bool                save(const char* file);

void                get_settings_file(str_base& out);
bool                sandboxed_set_setting(const char* name, const char* value);

struct setting_name_value
{
    setting_name_value(const char* name, const char* value)
    : name(name)
    , value(value)
    {
    }

    str_moveable    name;
    str_moveable    value;
};

bool                migrate_setting(const char* name, const char* value, std::vector<setting_name_value>& out);

#ifdef DEBUG
bool                get_ever_loaded();
void                TEST_set_ever_loaded();
#endif

};



//------------------------------------------------------------------------------
class setting
{
    friend void load_custom_defaults(const char* file);

public:
    enum type_e : uint8 {
        type_unknown,
        type_int,
        type_bool,
        type_string,
        type_enum,
        type_color,
        type_max
    };

    virtual         ~setting();
    type_e          get_type() const;
    const char*     get_name() const;
    const char*     get_short_desc() const;
    const char*     get_long_desc() const;
    virtual bool    is_default() const = 0;
    virtual bool    is_saveable() const = 0;
    virtual void    set() = 0;
    virtual bool    set(const char* value) = 0;
    virtual void    get(str_base& out) const = 0;
    virtual void    get_descriptive(str_base& out) const { get(out); }

    void            set_source(char const* source);
    const char*     get_source() const;

protected:
                    setting(const char* name, const char* short_desc, const char* long_desc, type_e type);
    const char*     get_custom_default() const;
    str<settings::c_max_len_name + 1, false> m_name;
    str<settings::c_max_len_short_desc + 1, false> m_short_desc;
    str<128>        m_long_desc;
    str_moveable    m_source;
    type_e          m_type;

    template <typename T>
    struct store
    {
        explicit    operator T () const                  { return value; }
        bool        operator == (const store& rhs) const { return value == rhs.value; }
        T           value;
    };

    static const char* get_loaded_value(const char* name);
};

//------------------------------------------------------------------------------
template <> struct setting::store<const char*>
{
    explicit        operator const char* () const        { return value.c_str(); }
    bool            operator == (const store& rhs) const { return value.equals(rhs.value.c_str()); }
    str<64>         value;
};

//------------------------------------------------------------------------------
template <typename T>
class setting_impl
    : public setting
{
public:
                    setting_impl(const char* name, const char* short_desc, T default_value);
                    setting_impl(const char* name, const char* short_desc, const char* long_desc, T default_value);
    T               get() const;
    virtual bool    is_default() const override;
    virtual bool    is_saveable() const override;
    virtual void    set() override;
    virtual bool    set(const char* value) override;
    virtual void    get(str_base& out) const override;
    void            deferred_load();

protected:
    virtual bool    parse(const char* value, store<T>& out);
    struct          type;
    store<T>        m_store;
    store<T>        m_default;
    bool            m_save = true;
};

//------------------------------------------------------------------------------
template <typename T> setting_impl<T>::setting_impl(
    const char* name,
    const char* short_desc,
    T default_value)
: setting_impl<T>(name, short_desc, nullptr, default_value)
{
}

//------------------------------------------------------------------------------
template <typename T> setting_impl<T>::setting_impl(
    const char* name,
    const char* short_desc,
    const char* long_desc,
    T default_value)
: setting(name, short_desc, long_desc, type_e(type::id))
{
    m_default.value = default_value;
    set();
}

//------------------------------------------------------------------------------
template <typename T> void setting_impl<T>::set()
{
    const char* custom_default = get_custom_default();
    if (!custom_default || !parse(custom_default, m_store))
        m_store.value = T(m_default);
    m_save = !is_default();
}

//------------------------------------------------------------------------------
template <typename T> bool setting_impl<T>::set(const char* value)
{
    if (!parse(value, m_store))
        return false;
    m_save = true;
    return true;
}

//------------------------------------------------------------------------------
template <typename T> bool setting_impl<T>::is_default() const
{
    return m_store == m_default;
}

//------------------------------------------------------------------------------
template <typename T> bool setting_impl<T>::is_saveable() const
{
    return m_save;
}

//------------------------------------------------------------------------------
template <typename T> T setting_impl<T>::get() const
{
    return T(m_store);
}

//------------------------------------------------------------------------------
template <typename T> void setting_impl<T>::deferred_load()
{
    const char* value = get_loaded_value(m_name.c_str());
    if (value)
        set(value);
}



//------------------------------------------------------------------------------
template <> struct setting_impl<bool>::type        { enum { id = setting::type_bool }; };
template <> struct setting_impl<int32>::type       { enum { id = setting::type_int }; };
template <> struct setting_impl<const char*>::type { enum { id = setting::type_string }; };

//------------------------------------------------------------------------------
typedef setting_impl<bool>         setting_bool;
typedef setting_impl<int32>        setting_int;
typedef setting_impl<const char*>  setting_str;

//------------------------------------------------------------------------------
class setting_enum
    : public setting_impl<int32>
{
public:
                       setting_enum(const char* name, const char* short_desc, const char* values, int32 default_value);
                       setting_enum(const char* name, const char* short_desc, const char* long_desc, const char* values, int32 default_value);
    virtual void       get(str_base& out) const override;
    const char*        get_options() const;

    using setting_impl<int32>::get;

protected:
    virtual bool       parse(const char* value, store<int32>& out) override;
    static const char* next_option(const char* option);
    str<48>            m_options;
};

//------------------------------------------------------------------------------
class setting_color
    : public setting_str
{
public:
                       setting_color(const char* name, const char* short_desc, const char* default_value);
                       setting_color(const char* name, const char* short_desc, const char* long_desc, const char* default_value);
    virtual void       set() override;
    virtual bool       set(const char* value) override { return setting_str::set(value); }
    virtual void       get_descriptive(str_base& out) const override;
protected:
    virtual bool       parse(const char* value, store<const char*>& out) override;
};
