// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
#define COLOR_XS\
    COLOR_X(black)\
    COLOR_X(red)\
    COLOR_X(green)\
    COLOR_X(yellow)\
    COLOR_X(blue)\
    COLOR_X(magenta)\
    COLOR_X(cyan)\
    COLOR_X(grey)\
    COLOR_X(dark_grey)\
    COLOR_X(light_red)\
    COLOR_X(light_green)\
    COLOR_X(light_yellow)\
    COLOR_X(light_blue)\
    COLOR_X(light_magenta)\
    COLOR_X(light_cyan)\
    COLOR_X(white)

#define COLOR_X(x) color_##x,
enum : uint8
{
    COLOR_XS
    color_count,
};
#undef COLOR_X

//------------------------------------------------------------------------------
class attributes
{
public:
    struct color
    {
        union
        {
            struct
            {
                unsigned short  r : 5;
                unsigned short  g : 5;
                unsigned short  b : 5;
                unsigned short  is_rgb : 1;
            };
            unsigned short      value;
        };

        bool                    operator == (const color& rhs) const { return value == rhs.value; }
        void                    as_888(uint8 (&out)[3]) const;
    };

    template <typename T>
    struct attribute
    {
        explicit                operator bool () const  { return bool(set); }
        const T*                operator -> () const { return &value; }
        const T                 value;
        const uint8             set : 1;
        const uint8             is_default : 1;
    };

    enum default_e { defaults };

                                attributes();
                                attributes(default_e);
    bool                        operator == (const attributes rhs);
    bool                        operator != (const attributes rhs) { return !(*this == rhs); }
    static attributes           merge(const attributes first, const attributes second);
    static attributes           diff(const attributes from, const attributes to);
    void                        reset_fg();
    void                        reset_bg();
    void                        set_fg(uint8 value);
    void                        set_bg(uint8 value);
    void                        set_fg(uint8 r, uint8 g, uint8 b);
    void                        set_bg(uint8 r, uint8 g, uint8 b);
    void                        set_bold(bool state=true);
    void                        set_underline(bool state=true);
    void                        set_reverse(bool state=true);
    attribute<color>            get_fg() const;
    attribute<color>            get_bg() const;
    attribute<bool>             get_bold() const;
    attribute<bool>             get_underline() const;
    attribute<bool>             get_reverse() const;

private:
    union flags
    {
        struct
        {
            uint8               fg : 1;
            uint8               bg : 1;
            uint8               bold : 1;
            uint8               underline : 1;
            uint8               reverse : 1;
        };
        uint8                   all;
    };

    union
    {
        struct
        {
            color               m_fg;
            color               m_bg;
            unsigned short      m_bold : 1;
            unsigned short      m_underline : 1;
            unsigned short      m_reverse : 1;
            flags               m_flags;
            uint8               m_unused;
        };
        unsigned long long      m_state;
    };
};
