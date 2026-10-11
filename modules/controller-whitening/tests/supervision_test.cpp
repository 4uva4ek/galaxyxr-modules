// Runs only private test processes; no controller/library/module access.
#include "../native/supervision.h"
#include <sys/wait.h>
#include <poll.h>
#include <time.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
static volatile sig_atomic_t stopped=0;
static void stop(int){stopped=1;}
static void check(bool ok,const char* reason){if(!ok){perror(reason);_exit(2);}}
int main() {
    alarm(10);int ready[2],report[2];check(pipe(ready)==0&&pipe(report)==0,"pipe");
    pid_t supervisor=fork();check(supervisor>=0,"fork supervisor");
    if(!supervisor){
        close(ready[0]);close(report[0]);
        pid_t owner=fork();check(owner>=0,"fork owner");
        if(!owner){
            alarm(6);armSupervisorDeath(stop);
            pid_t guardian=fork();check(guardian>=0,"fork guardian");
            if(!guardian){
                int death=-1;check(prctl(PR_GET_PDEATHSIG,&death)==0,"get pdeath");
                char result=death==0?'G':'E';check(write(report[1],&result,1)==1,"guardian report");_exit(death==0?0:2);
            }
            int code=0;check(waitpid(guardian,&code,0)==guardian&&WIFEXITED(code)&&WEXITSTATUS(code)==0,"guardian wait");
            char armed='A';check(write(ready[1],&armed,1)==1,"ready");
            for(int i=0;!stopped&&i<250;++i)usleep(20000);
            char result=stopped?'S':'E';check(write(report[1],&result,1)==1,"owner report");_exit(stopped?0:2);
        }
        close(ready[1]);close(report[1]);for(;;)pause();
    }
    close(ready[1]);close(report[1]);char armed=0;check(read(ready[0],&armed,1)==1&&armed=='A',"armed");close(ready[0]);
    check(kill(supervisor,SIGKILL)==0,"kill own supervisor");int code=0;check(waitpid(supervisor,&code,0)==supervisor&&WIFSIGNALED(code),"supervisor wait");
    bool guardianClear=false,ownerStopped=false;
    for(int i=0;i<2;++i){pollfd p{report[0],POLLIN,0};check(poll(&p,1,3000)>0,"report timeout");char c=0;check(read(report[0],&c,1)==1,"report");guardianClear|=c=='G';ownerStopped|=c=='S';}
    check(guardianClear&&ownerStopped,"supervision assertions");close(report[0]);
    puts("PASS: forked guardian PDEATHSIG=0; owner receives SIGTERM after supervisor SIGKILL.");
    return 0;
}
