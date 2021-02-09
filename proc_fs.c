#include "proc_fs.h"
#include "percpu_monitor.h"

static struct proc_dir_entry *fs_entry[MONITOR_PROC_FS_ENTRY_MAX] = {NULL};

struct proc_dir_entry *monitor_proc_fs_entry(const monitor_proc_fs_type_t t)
{
    return fs_entry[t];
}

int monitor_proc_fs_init(void)
{
    struct proc_dir_entry *fs_entry_root;

    fs_entry[MONITOR_PROC_FS_ENTRY_ROOT] = proc_mkdir(MONITORY_FS_ENTRY_ROOT_NAME, NULL);
    fs_entry_root =  fs_entry[MONITOR_PROC_FS_ENTRY_ROOT];
    if (unlikely(!fs_entry_root)) {
        printk(KERN_ERR "create /proc/monitor fail!");
        return -EINVAL;
    }

    fs_entry[MONITOR_PROC_FS_ENTRY_CPU_ROOT] = proc_mkdir(MONITORY_FS_ENTRY_CPU_ROOT_NAME,
        fs_entry_root);
    if (unlikely(!fs_entry[MONITOR_PROC_FS_ENTRY_ROOT])) {
        printk(KERN_ERR "create /proc/monitor/cpu fail!");
        goto destory_entry;
    }

    fs_entry[MONITOR_PROC_FS_ENTRY_MEM_ROOT] = proc_mkdir(MONITORY_FS_ENTRY_MEM_ROOT_NAME,
        fs_entry_root);
    if (unlikely(!fs_entry[MONITOR_PROC_FS_ENTRY_MEM_ROOT])) {
        printk(KERN_ERR "create /proc/monitor/memory fail!");
        goto destory_entry;
    }

    return 0;
destory_entry:
    monitor_proc_fs_destory();

    return -EINVAL;
}

void monitor_proc_fs_destory(void)
{
    struct proc_dir_entry *fs_entry_root;

    fs_entry_root =  fs_entry[MONITOR_PROC_FS_ENTRY_ROOT];
    if (likely(fs_entry_root)) {
        remove_proc_entry(MONITORY_FS_ENTRY_MEM_ROOT_NAME, fs_entry_root);
        remove_proc_entry(MONITORY_FS_ENTRY_CPU_ROOT_NAME, fs_entry_root);
        remove_proc_entry(MONITORY_FS_ENTRY_ROOT_NAME, NULL);
        (void) memset(&fs_entry, 0, sizeof(fs_entry));
    }
}
