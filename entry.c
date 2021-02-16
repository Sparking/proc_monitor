#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include "proc_fs.h"
#include "monitor_task.h"
#include "monitor_memory.h"
#include "monitor_percpu.h"

static struct delayed_work monitor_sample_work;

static void monitor_sample_work_func(struct work_struct *work)
{
    schedule_delayed_work(&monitor_sample_work, 5 * HZ);
    memory_stat_update();
    cpu_stat_update();
    task_stat_update();
}

static int __init this_module_init(void)
{
    int ret;

    ret = monitor_proc_fs_init();
    if (ret < 0)
        return ret;

    ret = cpu_stat_init();
    if (ret < 0)
        goto exit_procfs;

    ret = memory_stat_init();
    if (ret < 0)
        goto exit_cpu_stat;

    ret = task_stat_init();
    if (ret < 0)
        goto exit_memory_stat;

    INIT_DELAYED_WORK(&monitor_sample_work, monitor_sample_work_func);
    schedule_delayed_work(&monitor_sample_work, HZ);
    return 0;

exit_memory_stat:
    memory_stat_exit();
exit_cpu_stat:
    cpu_stat_exit();
exit_procfs:
    monitor_proc_fs_exit();
    return ret;
}

static void __exit this_module_exit(void)
{
    printk(KERN_DEBUG "MONITOR: cancel delayed work ...\n");
    cancel_delayed_work(&monitor_sample_work);
    printk(KERN_DEBUG "MONITOR: clean task stat ...\n");
    task_stat_exit();
    printk(KERN_DEBUG "MONITOR: clean memory stat ...\n");
    memory_stat_exit();
    printk(KERN_DEBUG "MONITOR: clean cpu stat ...\n");
    cpu_stat_exit();
    printk(KERN_DEBUG "MONITOR: clean proc root entries ...\n");
    monitor_proc_fs_exit();
    printk(KERN_DEBUG "MONITOR: exit!\n");
}

module_init(this_module_init);
module_exit(this_module_exit);
MODULE_LICENSE("GPL");
