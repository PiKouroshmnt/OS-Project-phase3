#include "types.h"

#define MAX_REPORT_BUFFER_SIZE 20

struct report {
    char pname[16];
    int pid;
    uint64 scause;
    uint64 sepc;
    uint64 stval;
};

struct global_data{
    struct report reports[MAX_REPORT_BUFFER_SIZE];
    int numberOfReports;
    int writeIndex;
};

struct report_traps {
    struct report reports[MAX_REPORT_BUFFER_SIZE];
    int count;
};

extern struct global_data internal_report_list;
