#include "user.h"
#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../kernel/report_traps.h"

int main() {
    struct report_traps *rprt = malloc(sizeof(struct report_traps));
    memset(rprt,0,sizeof(struct report_traps));

    for(int i = 0;i < 5;i++){
        int pid = fork();
        if(pid == 0){
            int x = 0;
            int y = 1 / x;
            return y;
        }
    }
    sleep(3);

    int err = report(rprt);
    if(err != 0){
        printf("an Error has occurred :(\n");
        return -1;
    }

    printf("number of exceptions: %d\n",rprt->count);
    printf("PID\t\tPNAME\t\tscause\t\tsepc\t\tstval\n");
    for(int i = 0; i <rprt->count; i++){
        printf("%d\t\t%s\t\t%lu\t\t%lu\t\t%lu\n",rprt->reports[i].pid,rprt->reports[i].pname, rprt->reports[i].scause,rprt->reports[i].sepc,rprt->reports[i].stval);
    }
    free(rprt);
    return err;
}