// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "maybe_windows.h"
#include "tib_base.h"
#include "tib_bindings.h"
#include "tib_terminal.h"
#include "test_util.h"
#include <assert.h>

static tib::terminal_in* new_test_terminal_in(tib::pushed_input& pushed)
{
    return new test_terminal_in(pushed);
}

static tib::terminal_out* new_test_terminal_out()
{
    return new test_terminal_out;
}

test_terminal_in* test_terminal_in::s_instance = nullptr;
test_terminal_out* test_terminal_out::s_instance = nullptr;

test_terminal_in::test_terminal_in(tib::pushed_input& pushed)
{
    assert(!s_instance);
    s_instance = this;
}

test_terminal_in::~test_terminal_in()
{
    assert(s_instance == this);
    s_instance = nullptr;
}

int32_t test_terminal_in::read() noexcept
{
    if (empty())
        return -1;
    return uint8_t(m_input.c_str()[m_index++]);
}

bool test_terminal_in::avail(uint32_t timeout) noexcept
{
    return !empty();
}

void test_terminal_in::set_input(const char* input, size_t len)
{
    assert(empty());
    m_input.set(input, len);
    m_index = 0;
}

void test_terminal_in::clear() noexcept
{
    m_input.clear();
    m_index = 0;
}

test_terminal_in* test_terminal_in::get() noexcept
{
    return s_instance;
}

test_input_stream::test_input_stream(const char* input, size_t len)
{
    m_terminal = test_terminal_in::get();
    assert(m_terminal);
    m_terminal->set_input(input, len);
}

test_input_stream::~test_input_stream()
{
    assert(m_terminal == test_terminal_in::get());
    m_terminal->clear();
}

bool test_input_stream::empty() const noexcept
{
    return m_terminal->empty();
}

test_terminal_out::test_terminal_out()
{
    assert(!s_instance);
    s_instance = this;
}

test_terminal_out::~test_terminal_out()
{
    assert(s_instance == this);
    s_instance = nullptr;
}

void test_terminal_out::write(const char* s, size_t len) noexcept
{
    if (m_output)
        m_output->append(s, len);
}

void test_terminal_out::ding() noexcept
{
    ++m_ding_count;
}

test_terminal_out* test_terminal_out::get() noexcept
{
    return s_instance;
}

test_output_stream::test_output_stream(tib::cstring& output)
{
    m_terminal = test_terminal_out::get();
    assert(m_terminal);
    assert(!m_terminal->m_output);
    m_terminal->m_output = &output;
    m_terminal->m_ding_count = 0;
}

test_output_stream::~test_output_stream()
{
    assert(m_terminal == test_terminal_out::get());
    assert(m_terminal->m_output);
    m_terminal->m_output = nullptr;
}

uint32_t test_output_stream::get_ding_count() const noexcept
{
    return m_terminal->m_ding_count;
}

void install_test_terminal()
{
    assert(!tib::hook_new_terminal_in);
    assert(!tib::hook_new_terminal_out);
    tib::hook_new_terminal_in = new_test_terminal_in;
    tib::hook_new_terminal_out = new_test_terminal_out;
}

bool add_binding(tib::key_table& table, const char* sequence, const char* name)
{
    return table.add(sequence, tib::binding_target_func(name));
}
