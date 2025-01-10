#include "user.h"
#include "../kernel/types.h"
#include "../kernel/stat.h"

volatile int a = 0, b = 0, c = 0;

void *my_thread(void *arg);

int main() {
    int ta = create_thread(my_thread, (void *) &a);
    int tb = create_thread(my_thread, (void *) &b);
    int tc = create_thread(my_thread, (void *) &c);
    printf("CJ\n");
    join_thread(ta);
    join_thread(tb);
    join_thread(tc);
    printf("ta: %d\ttb: %d\ttc: %d\n",ta,tb,tc);
    exit(0);
}

void *my_thread(void *arg) {
    int *number = (int *) arg;
    for (int i = 0;i < 50; ++i) {
        *number = *number + 1;
        if (number == &a) {
            printf("a: %d\n", *number);
        } else if (number == &b) {
            printf("b: %d\n", *number);
        } else {
            printf("c: %d\n", *number);
        }
    }
    return (void *) number;
}