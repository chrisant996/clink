// Copyright (c) 2016 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

//------------------------------------------------------------------------------
#define COLOUR_XS\
    COLOUR_X(black)\
    COLOUR_X(red)\
    COLOUR_X(green)\
    COLOUR_X(yellow)\
    COLOUR_X(blue)\
    COLOUR_X(magenta)\
    COLOUR_X(cyan)\
    COLOUR_X(grey)\
    COLOUR_X(dark_grey)\
    COLOUR_X(light_red)\
    COLOUR_X(light_green)\
    COLOUR_X(light_yellow)\
    COLOUR_X(light_blue)\
    COLOUR_X(light_magenta)\
    COLOUR_X(light_cyan)\
    COLOUR_X(white)

#define COLOUR_X(x) colour_##x,
enum : unsigned char
{
    COLOUR_XS
    colour_count,
};
#undef COLOUR_X

//------------------------------------------------------------------------------
class attributes
{
public:
    struct colour
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

        bool                    operator == (const colour& rhs) const { return value == rhs.value; }
        void                    as_888(unsigned char (&out)[3]) const;
    };

    template <typename T>
    struct attribute
    {
        explicit                operator bool () const  { return bool(set); }
        const T*                operator -> () const { return &value; }
        const T                 value;
        const unsigned char     set : 1;
        const unsigned char     is_default : 1;
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
    void                        set_fg(unsigned char value);
    void                        set_bg(unsigned char value);
    void                        set_fg(unsigned char r, unsigned char g, unsigned char b);
    void                        set_bg(unsigned char r, unsigned char g, unsigned char b);
    void                        set_bold(bool state=true);
    void                        set_underline(bool state=true);
    attribute<colour>           get_fg() const;
    attribute<colour>           get_bg() const;
    attribute<bool>             get_bold() const;
    attribute<bool>             get_underline() const;

private:
    union flags
    {
        struct
        {
            unsigned char       fg : 1;
            unsigned char       bg : 1;
            unsigned char       bold : 1;
            unsigned char       underline : 1;
        };
        unsigned char           all;
    };

    union
    {
        struct
        {
            colour              m_fg;
            colour              m_bg;
            unsigned short      m_bold : 1;
            unsigned short      m_underline : 1;
            flags               m_flags;
            unsigned char       m_unused;
        };
        unsigned long long      m_state;
    };
};
