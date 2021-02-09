#ifndef _PROC_MONITOR_TASK_MONITOR_H_
#define _PROC_MONITOR_TASK_MONITOR_H_

#include <linux/stddef.h>
#include <linux/list.h>
#include "history.h"

#define READ_PROC_TIME_INTERVAL_SEC     (5 * HZ)

#define MONITOR_TASK_TYPE_PROCESS       0
#define MONITOR_TASK_TYPE_THREAD        1

struct monitor_task_stat {
    u8 type;
    pid_t id;
    char task_spec_data[0];
};

struct monitor_process_stat {
    struct list_head threads;
    history_record_t mem;
    int nthreads;
    int core_mask;
};

struct monitor_thread_stat {
    struct list_head process;
    history_record_t cpu;
    short nice;
    short prio;
    u8 processor;
};

extern void task_stat_update(void);

#endif /* _PROC_MONITOR_TASK_MONITOR_H_ */
