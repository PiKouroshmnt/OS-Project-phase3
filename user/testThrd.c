#include "user.h"
#include "../kernel/types.h"
#include "../kernel/stat.h"

int main() {
    int highUse = 0;
    int pid = fork();
    if(pid == 0){
        pid = fork();
        if(pid == 0){
            highUse = 1;
            for(int i = 0;i < 3;i++){
                fork();
            }
            for(int i = 0;i <= 200000000;i++);
            set_cpu_quota(getpid(),1);
            sleep(2);
        }else{
            for(int i = 0;i < 3;i++){
                fork();
            }
            for(int i = 0;i <= 200000000;i++);
            sleep(2);
        }
    }else{
        while (1);
    }
    sleep(2);
    if(highUse){
        printf("1\n");
    }else{
        printf("2\n");
    }
    while (1);
}