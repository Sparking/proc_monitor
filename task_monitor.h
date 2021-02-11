#ifndef _PROC_MONITOR_TASK_MONITOR_H_
#define _PROC_MONITOR_TASK_MONITOR_H_

#include <linux/stddef.h>
#include <linux/list.h>
#include "history.h"

extern void task_stat_update(void);
extern int __init task_stat_init(void);
extern void task_stat_exit(void);

#endif /* _PROC_MONITOR_TASK_MONITOR_H_ */
