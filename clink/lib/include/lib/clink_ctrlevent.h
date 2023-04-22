// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

void clink_install_ctrlevent();
void clink_shutdown_ctrlevent();

int clink_is_signaled();

void clink_set_signaled(int sig);   // SIGINT, etc.
