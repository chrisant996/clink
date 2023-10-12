// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

class lua_state;

HANDLE get_task_manager_event();
void task_manager_on_idle(lua_state& lua);
void task_manager_diagnostics();
extern "C" void end_task_manager();
void shutdown_task_manager();
