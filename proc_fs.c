#include "proc_fs.h"
#include "monitor_percpu.h"

static struct proc_dir_entry *fs_entry[MONITOR_PROC_FS_ENTRY_MAX] = {NULL};

struct proc_dir_entry *monitor_proc_fs_entry(const monitor_proc_fs_type_t t)
{
    return fs_entry[t];
}

int __init monitor_proc_fs_init(void)
{
    const char *path;
    struct proc_dir_entry *fs_entry_root;

    fs_entry[MONITOR_PROC_FS_ENTRY_ROOT] = proc_mkdir(MONITORY_FS_ENTRY_ROOT_NAME, NULL);
    fs_entry_root =  fs_entry[MONITOR_PROC_FS_ENTRY_ROOT];
    if (unlikely(!fs_entry_root)) {
        path = MONITOR_PROC_ROOT_PATH;
        return -EINVAL;
    }

    fs_entry[MONITOR_PROC_FS_ENTRY_CPU_ROOT] = proc_mkdir(MONITORY_FS_ENTRY_CPU_ROOT_NAME,
        fs_entry_root);
    if (unlikely(!fs_entry[MONITOR_PROC_FS_ENTRY_ROOT])) {
        path = MONITOR_PROC_CPU_ROOT_PATH;
        goto destory_entry;
    }

    fs_entry[MONITOR_PROC_FS_ENTRY_MEM_ROOT] = proc_mkdir(MONITORY_FS_ENTRY_MEM_ROOT_NAME,
        fs_entry_root);
    if (unlikely(!fs_entry[MONITOR_PROC_FS_ENTRY_MEM_ROOT])) {
        path = MONITOR_PROC_MEM_ROOT_PATH;
        goto destory_entry;
    }

    fs_entry[MONITOR_PROC_FS_ENTRY_TASK_ROOT] = proc_mkdir(MONITORY_FS_ENTRY_TSK_ROOT_NAME,
        fs_entry_root);
    if (unlikely(!fs_entry[MONITOR_PROC_FS_ENTRY_TASK_ROOT])) {
        path = MONITOR_PROC_TASK_ROOT_PATH;
        goto destory_entry;
    }

    fs_entry[MONITOR_PROC_FS_ENTRY_PROCESS_ROOT] = proc_mkdir(MONITOR_PROC_TASK_RPOCESS_NAME,
        fs_entry[MONITOR_PROC_FS_ENTRY_TASK_ROOT]);
    if (unlikely(!fs_entry[MONITOR_PROC_FS_ENTRY_PROCESS_ROOT])) {
        path = MONITOR_PROC_TASK_RPOCESS_ROOT;
        goto destory_entry;
    }

    fs_entry[MONITOR_PROC_FS_ENTRY_THREAD_ROOT] = proc_mkdir(MONITOR_PROC_TASK_THREAD_NAME,
        fs_entry[MONITOR_PROC_FS_ENTRY_TASK_ROOT]);
    if (unlikely(!fs_entry[MONITOR_PROC_FS_ENTRY_THREAD_ROOT])) {
        path = MONITOR_PROC_TASK_THREAD_ROOT;
        goto destory_entry;
    }

    return 0;
destory_entry:
    printk(KERN_ERR "proc monitor: fail to create proc entry %s.\n", path);
    monitor_proc_fs_exit();

    return -EINVAL;
}

void monitor_proc_fs_exit(void)
{
    struct proc_dir_entry *fs_entry_root;
    struct proc_dir_entry *task_entry_root;

    fs_entry_root =  fs_entry[MONITOR_PROC_FS_ENTRY_ROOT];
    if (likely(fs_entry_root)) {
        task_entry_root = fs_entry[MONITOR_PROC_FS_ENTRY_TASK_ROOT];
        if (task_entry_root) {
            remove_proc_entry(MONITOR_PROC_TASK_THREAD_NAME, task_entry_root);
            remove_proc_entry(MONITOR_PROC_TASK_RPOCESS_NAME, task_entry_root);
        }

        remove_proc_entry(MONITORY_FS_ENTRY_TSK_ROOT_NAME, fs_entry_root);
        remove_proc_entry(MONITORY_FS_ENTRY_MEM_ROOT_NAME, fs_entry_root);
        remove_proc_entry(MONITORY_FS_ENTRY_CPU_ROOT_NAME, fs_entry_root);
        remove_proc_entry(MONITORY_FS_ENTRY_ROOT_NAME, NULL);
        memzero_explicit(&fs_entry, sizeof(fs_entry));
    }
}
