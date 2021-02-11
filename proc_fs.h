#ifndef _READ_PROC_PROC_FS_H_
#define _READ_PROC_PROC_FS_H_

#include <linux/proc_fs.h>
#include "monitor_path.h"

typedef enum {
    MONITOR_PROC_FS_ENTRY_ROOT     = 0,
    MONITOR_PROC_FS_ENTRY_CPU_ROOT = 1,
    MONITOR_PROC_FS_ENTRY_MEM_ROOT = 2,
    MONITOR_PROC_FS_ENTRY_MAX      = 3,
} monitor_proc_fs_type_t;

extern struct proc_dir_entry *monitor_proc_fs_entry(const monitor_proc_fs_type_t t);
extern int __init monitor_proc_fs_init(void);
extern void monitor_proc_fs_exit(void);

#endif /* _READ_PROC_PROC_FS_H_ */
