#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include "proc_fs.h"
#include "percpu_monitor.h"
#include "task_monitor.h"

/* TODO: 打桩，后续实现内存信息的收集 */
#define mem_stat_update() do {} while (0)

static struct delayed_work monitor_sample_work;

static void monitor_sample_work_func(struct work_struct *work)
{
    schedule_delayed_work(&monitor_sample_work, 5 * HZ);
    cpu_stat_update();
    mem_stat_update();
    task_stat_update();
}

static int __init this_module_init(void)
{
    int ret;

    ret = monitor_proc_fs_init();
    if (ret < 0) {
        return ret;
    }

    ret = cpu_stat_init();
    if (ret < 0) {
        monitor_proc_fs_exit();
        return ret;
    }

    ret = task_stat_init();
    if (ret < 0) {
        cpu_stat_exit();
        monitor_proc_fs_exit();
        return ret;
    }

    INIT_DELAYED_WORK(&monitor_sample_work, monitor_sample_work_func);
    schedule_delayed_work(&monitor_sample_work, HZ);

    return 0;
}

static void __exit this_module_exit(void)
{
    cancel_delayed_work(&monitor_sample_work);
    task_stat_exit();
    cpu_stat_exit();
    monitor_proc_fs_exit();
}

module_init(this_module_init);
module_exit(this_module_exit);
MODULE_LICENSE("GPL");
