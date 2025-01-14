#include "user.h"
#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "top.h"

const char * get_enum_value(enum procstate inp) {
    switch (inp) {
        case UNUSED:
            return "UNUSED  ";
        case USED:
            return "USED    ";
        case SLEEPING:
            return "SLEEPING";
        case RUNNABLE:
            return "RUNNABLE";
        case RUNNING:
            return "RUNNING ";
        case ZOMBIE:
            return "ZOMBIE  ";
        default:
            return "UNKNOWN ";
    }
}

int main(){
    int pid = fork();
    if(pid == 0){
        int t = getpid();
        pid = fork();
        if(pid != 0){
            for(int i = 0;i < 3;i++){
                fork();
            }
            for(int i = 0;i < 300000000;i++);
            for(int i = 0;i < 300000000;i++);
            for(int i = 0;i < 300000000;i++);
            for(int i = 0;i < 300000000;i++);
            if(t == getpid()){
                sleep(30);
                exit(0);
            }
            sleep(100000);
        }else{
            for(int i = 0;i < 3;i++){
                fork();
            }
            for(int i = 0;i < 300000000;i++);
            sleep(100000);
        }
    }else{
        wait(&pid);
        struct top *top1 = malloc(sizeof(struct top));
        top(top1);
        printf("number of active processes: %d\n",top1->count);
        printf("NAME\t\tPID\t\tPPID\t\tSTATE\t\tSTART\t\tUSAGE\n");
        for(int i = 0;i < top1->count;i++){
            printf("%s\t\t%d\t\t%d\t\t%s\t%u\t\t%u\n",
                   top1->processes[i].name,top1->processes[i].pid,top1->processes[i].ppid,
                   get_enum_value(top1->processes[i].state),top1->processes[i].usage.start_tick,
                   top1->processes[i].usage.sum_of_ticks);
        }
    }
}