// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "history_timeformatter.h"

#include <core/settings.h>
#include <terminal/ecma48_iter.h>

#include <new>
extern "C" {
#include <readline/history.h>
}

//------------------------------------------------------------------------------
static setting_str g_history_timeformat(
    "history.time_format",
    "Format for showing history times",
    "This specifies a format string to override the default string (\"%F %T  \")\n"
    "for showing timestamps for history items.  Timestamps are shown when the\n"
    "'history.show_time' setting is enabled.  This can be overridden by flags in\n"
    "the 'history' command.\n"
    "\n"
    "The format string may contain regular characters and special format\n"
    "specifiers.  Format specifiers begin with a percent sign (%), and are expanded\n"
    "to their corresponding values.  For a list of possible format specifiers,\n"
    "refer to the C++ strftime() documentation.\n"
    "\n"
    "Some common format specifiers are:\n"
    "  %a    Abbreviated weekday name for the locale (e.g. Thu).\n"
    "  %b    Abbreviated month name for the locale (e.g. Aug).\n"
    "  %c    Date and time representation for the locale.\n"
    "  %D    Short MM/DD/YY date (e.g. 08/23/01).\n"
    "  %F    Short YYYY/MM/DD date (e.g. 2001-08-23).\n"
    "  %H    Hour in 24-hour format (00 - 23).\n"
    "  %I    Hour in 12-hour format (01 - 12).\n"
    "  %m    Month (01 - 12).\n"
    "  %M    Minutes (00 - 59).\n"
    "  %p    AM or PM indicator for the locale.\n"
    "  %r    12-hour clock time for the locale (e.g. 02:55:41 pm).\n"
    "  %R    24-hour clock time (e.g. 14:55).\n"
    "  %S    Seconds (00 - 59).\n"
    "  %T    ISO 8601 time format HH:MM:SS (e.g. 14:55:41).\n"
    "  %x    Date representation for the locale.\n"
    "  %X    Time representation for the locale.\n"
    "  %y    Year without century (00 - 99).\n"
    "  %Y    Year with century (e.g. 2001).\n"
    "  %%    A % sign.",
    "%F %T  ");

//------------------------------------------------------------------------------
history_timeformatter::history_timeformatter(bool plaintext)
: m_plaintext(plaintext)
{
}

//------------------------------------------------------------------------------
history_timeformatter::~history_timeformatter()
{
}

//------------------------------------------------------------------------------
void history_timeformatter::set_timeformat(const char* timeformat, bool for_popup)
{
    if (m_plaintext)
        ecma48_processor(timeformat, &m_timeformat, nullptr, ecma48_processor_flags::plaintext);
    else
        m_timeformat = timeformat;
    m_max_timelen = 0;
    m_for_popup = for_popup;
}

//------------------------------------------------------------------------------
uint32 history_timeformatter::max_timelen()
{
    if (!m_max_timelen)
        ensure_timeformat();

    return m_max_timelen;
}

//------------------------------------------------------------------------------
void history_timeformatter::format(time_t time, str_base& out)
{
    struct tm tm = {};
    if (localtime_s(&tm, &time) == 0)
    {
        if (!m_max_timelen)
            ensure_timeformat();

        char timebuf[128];
        strftime(timebuf, sizeof_array(timebuf), m_timeformat.c_str(), &tm);
        out = timebuf;
        return;
    }
    out.clear();
}

//------------------------------------------------------------------------------
void history_timeformatter::ensure_timeformat()
{
    if (m_timeformat.empty())
        g_history_timeformat.get(m_timeformat);
    if (m_timeformat.empty())
        m_timeformat = "%F %T  ";

    if (m_plaintext && !m_timeformat.empty())
    {
        str_moveable tmp;
        ecma48_processor(m_timeformat.c_str(), &tmp, nullptr, ecma48_processor_flags::plaintext);
        m_timeformat = std::move(tmp);
    }

    if (m_for_popup)
    {
        str<> tmp;
        for (uint32 i = 0; i < m_timeformat.length(); ++i)
        {
            const char c = m_timeformat.c_str()[i];
            if (c != '\t')
                tmp.concat(&c, 1);
        }
        tmp.trim();
        m_timeformat = tmp.c_str();
    }

    struct tm tm = {};
    tm.tm_year = 2001;
    tm.tm_mon = 11;
    tm.tm_mday = 15;
    tm.tm_hour = 23;
    tm.tm_min = 30;
    tm.tm_sec = 30;

    char timebuf[128];
    timebuf[0] = '\0';
    strftime(timebuf, sizeof_array(timebuf), m_timeformat.c_str(), &tm);
    m_max_timelen = cell_count(timebuf);
}
