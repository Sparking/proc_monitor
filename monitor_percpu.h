#ifndef _PROC_MONITOR_PERCPU_MONITOR_H_
#define _PROC_MONITOR_PERCPU_MONITOR_H_

#include <linux/stddef.h>

extern void cpu_stat_update(void);
extern int __init cpu_stat_init(void);
extern u64 cpu_delta_tick(const unsigned int cpu);
extern void cpu_stat_exit(void);

#endif /* _PROC_MONITOR_PERCPU_MONITOR_H_ */
