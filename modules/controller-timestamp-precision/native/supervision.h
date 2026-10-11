#pragma once
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
#include <stdexcept>

// Only the hold owner arms this. Linux clears PDEATHSIG in its forked guardian,
// which must survive the owner's death long enough to restore target memory.
static void armSupervisorDeath(void (*handler)(int)) {
    pid_t supervisor=getppid();
    if(supervisor<=1)throw std::runtime_error("No live module supervisor");
    struct sigaction action{};
    action.sa_handler=handler;
    sigemptyset(&action.sa_mask);
    if(sigaction(SIGTERM,&action,nullptr)||sigaction(SIGHUP,&action,nullptr)||
       sigaction(SIGINT,&action,nullptr)||prctl(PR_SET_PDEATHSIG,SIGTERM))
        throw std::runtime_error("Cannot arm supervisor-death recovery");
    if(getppid()!=supervisor)throw std::runtime_error("Module supervisor exited before activation");
}
