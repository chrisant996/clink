// Copyright (c) 2026 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include "tib_base.h"
#include "tib_bindings.h"
#include "tib_terminal.h"

class test_terminal_in final : public tib::terminal_in
{
public:
                        ~test_terminal_in() override;
                        test_terminal_in(tib::pushed_input& pushed);

    int32_t             read() noexcept override;
    bool                avail(uint32_t timeout=0) noexcept override;

private:
    friend class test_input_stream;

    void                set_input(const char* input, size_t len);
    void                clear() noexcept;
    bool                empty() const noexcept { return m_index >= m_input.length(); }
    static test_terminal_in* get() noexcept;

    static test_terminal_in* s_instance;
    tib::cstring        m_input;
    size_t              m_index = 0;
};

class test_terminal_out final : public tib::terminal_out
{
public:
                        ~test_terminal_out() override;
                        test_terminal_out();

    void                write(const char* s, size_t len) noexcept override;
    void                ding() noexcept override;

private:
    friend class test_output_stream;

    static test_terminal_out* get() noexcept;
    static test_terminal_out* s_instance;
    tib::cstring*       m_output = nullptr;
    uint32_t            m_ding_count = 0;
};

class test_input_stream
{
public:
                        test_input_stream(const char* input, size_t len=tib::c_auto_length);
                        ~test_input_stream();

                        test_input_stream(const test_input_stream&) = delete;
    test_input_stream&  operator=(const test_input_stream&) = delete;

    bool                empty() const noexcept;

private:
    test_terminal_in*   m_terminal = nullptr;
};

class test_output_stream
{
public:
                        test_output_stream(tib::cstring& output);
                        ~test_output_stream();

                        test_output_stream(const test_output_stream&) = delete;
    test_output_stream& operator=(const test_output_stream&) = delete;

    uint32_t            get_ding_count() const noexcept;

private:
    test_terminal_out*  m_terminal = nullptr;
};

void install_test_terminal();

class dispatcher_tester : public tib::dispatcher_target
{
public:
                        ~dispatcher_tester() = default;
                        dispatcher_tester() = default;
    int32_t             dispatch(const tib::cstring &sequence, int32_t key, const tib::binding_target* binding, const tib::binding_params* params) noexcept { ++m_dispatch_count; return -1; }
    uint32_t            get_dispatch_count() const noexcept { return m_dispatch_count; }

private:
    uint32_t            m_dispatch_count = 0;
};

bool add_binding(tib::key_table& table, const char* sequence, const char* name);
