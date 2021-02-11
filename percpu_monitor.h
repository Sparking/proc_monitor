#ifndef _PROC_MONITOR_PERCPU_MONITOR_H_
#define _PROC_MONITOR_PERCPU_MONITOR_H_

extern void cpu_stat_update(void);
extern int __init cpu_stat_init(void);
extern void cpu_stat_exit(void);

#endif /* _PROC_MONITOR_PERCPU_MONITOR_H_ */
