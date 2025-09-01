#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <readline/readline.h> 

pthread_t main_thread;

void *start_routine(void* arg)
{
    sleep(10);
    pthread_kill(main_thread, SIGUSR1);
}

void handler(int sig)
{
    puts("Trap signal");
    (void) sig;
}


int main(int argc, char** argv)
{
    char *readline_buffer;
    pthread_t id;
    struct sigaction sa;
    sa.sa_handler = handler;
    sa.sa_flags = 0;

    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);


    rl_catch_signals = 0; /* disable signal handling -- This doesn't work */
    main_thread = pthread_self();
    pthread_create(&id, NULL, start_routine, NULL);
    readline_buffer = readline("Enter some text: ");

    puts("main thread done"); 
    return 0;
}
