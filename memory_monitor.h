#ifndef _PROC_MONITOR_MEMORY_MONITOR_H_
#define _PROC_MONITOR_MEMORY_MONITOR_H_

#include <linux/types.h>

extern int __init memory_stat_init(void);
extern void memory_stat_update(void);
extern void memory_stat_exit(void);
extern __kernel_ulong_t memory_total_size_kb(void);

#endif /* _PROC_MONITOR_MEMORY_MONITOR_H_ */
