#include <linux/slab.h>
#include <linux/tick.h>
#include <linux/math64.h>
#include <linux/version.h>
#include <linux/seq_file.h>
#include <linux/kernel_stat.h>
#include "history.h"
#include "proc_fs.h"
#include "percpu_monitor.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,14,0)
#define SEQ_DELIM_SPACE " "
#else
#define SEQ_DELIM_SPACE ' '
#endif

#ifndef arch_irq_stat_cpu
#define arch_irq_stat_cpu(cpu) 0
#endif
#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif

struct single_cpu_stat_value {
    u64 user;
    u64 nice;
    u64 system;
    u64 idle;
    u64 iowait;
    u64 irq;
    u64 softirq;
    u64 steal;
    u64 guest;
    u64 guest_nice;
};

struct single_cpu_stat_history {
    u64 total;
    history_record_t hist;
    vsmall_ring_buffer_def(rt_5min, 300 / 5);
    u8 cur_stat;
    u8 online;
    struct single_cpu_stat_value stat[2];
};

struct cpu_stat_value {
    unsigned int ncore;
    struct single_cpu_stat_history core[0];
};

static struct cpu_stat_value *cpu_stat = NULL;

#ifdef arch_idle_time

static u64 get_idle_time(struct kernel_cpustat *kcs, int cpu)
{
    u64 idle;

    idle = kcs->cpustat[CPUTIME_IDLE];
    if (cpu_online(cpu) && !nr_iowait_cpu(cpu))
        idle += arch_idle_time(cpu);
    return idle;
}

static u64 get_iowait_time(struct kernel_cpustat *kcs, int cpu)
{
    u64 iowait;

    iowait = kcs->cpustat[CPUTIME_IOWAIT];
    if (cpu_online(cpu) && nr_iowait_cpu(cpu))
        iowait += arch_idle_time(cpu);
    return iowait;
}

#else

static u64 get_idle_time(struct kernel_cpustat *kcs, int cpu)
{
    u64 idle, idle_usecs = -1ULL;

    if (cpu_online(cpu))
        idle_usecs = get_cpu_idle_time_us(cpu, NULL);

    if (idle_usecs == -1ULL)
        /* !NO_HZ or cpu offline so we can rely on cpustat.idle */
        idle = kcs->cpustat[CPUTIME_IDLE];
    else
        idle = idle_usecs * NSEC_PER_USEC;

    return idle;
}

static u64 get_iowait_time(struct kernel_cpustat *kcs, int cpu)
{
    u64 iowait, iowait_usecs = -1ULL;

    if (cpu_online(cpu))
        iowait_usecs = get_cpu_iowait_time_us(cpu, NULL);

    if (iowait_usecs == -1ULL)
        /* !NO_HZ or cpu offline so we can rely on cpustat.iowait */
        iowait = kcs->cpustat[CPUTIME_IOWAIT];
    else
        iowait = iowait_usecs * NSEC_PER_USEC;

    return iowait;
}

#endif

static int show_hist(struct seq_file *p, void *v, vsmall_ring_buffer_t *phis)
{
    u8 c;
    u16 index;

    c = phis->rear;
    /* 瞬时值 */
    if (likely(phis->accum_times)) {
        if (likely(phis->accum_val)) {
            seq_put_decimal_ull(p, SEQ_DELIM_SPACE,
                (unsigned long long) (phis->accum_val / phis->accum_times));
        } else {
            seq_printf(p, " 0");
        }
    } else {
        seq_put_decimal_ull(p, SEQ_DELIM_SPACE, (unsigned long long) phis->buffer[c]);
    }

    /* 历史值 */
    for (index = 0; index < phis->cnt; index++) {
        seq_put_decimal_ull(p, SEQ_DELIM_SPACE, (unsigned long long) phis->buffer[c]);
        if (unlikely(c == 0)) {
            c = phis->size - 1;
        } else {
            c--;
        }
    }

    for (; index < phis->size; index++)
        seq_printf(p, " 0");
    seq_putc(p, '\n');

    return 0;
}

static int show_hist_filed(struct seq_file *p, void *v, unsigned long off)
{
    unsigned int i;
    struct single_cpu_stat_history *core;

    for (i = 0; i < cpu_stat->ncore; i++) {
        core = cpu_stat->core + i;
        seq_printf(p, "%d", i);
        show_hist(p, v, (vsmall_ring_buffer_t *) ((void *) core + off));
    }

    return 0;
}

static int show_rt_with_off(struct seq_file *p, void *v, const unsigned off)
{
    unsigned int i;
    vsmall_ring_buffer_t *phis;

    for (i = 0; i < cpu_stat->ncore; i++) {
        phis = (vsmall_ring_buffer_t *) (((char *) &cpu_stat->core[i]) + off);
        seq_printf(p, "%d", i);
        seq_put_decimal_ull(p, SEQ_DELIM_SPACE, (unsigned long long) phis->sum / phis->cnt);
        seq_putc(p, '\n');
    }

    return 0;
}

static int show_rt_1min(struct seq_file *p, void *v)
{
    return show_rt_with_off(p, v, offsetof(struct single_cpu_stat_history, hist.h_1min));
}

static int show_rt_5min(struct seq_file *p, void *v)
{
    return show_rt_with_off(p, v, offsetof(struct single_cpu_stat_history, rt_5min));
}

static int show_hist_1min(struct seq_file *p, void *v)
{
    return show_hist_filed(p, v, offsetof(struct single_cpu_stat_history, hist.h_1min));
}

static int show_hist_5min(struct seq_file *p, void *v)
{
    return show_hist_filed(p, v, offsetof(struct single_cpu_stat_history, hist.h_5min));
}

static int show_hist_1hour(struct seq_file *p, void *v)
{
    return show_hist_filed(p, v, offsetof(struct single_cpu_stat_history, hist.h_1hour));
}

static int show_hist_1day(struct seq_file *p, void *v)
{
    return show_hist_filed(p, v, offsetof(struct single_cpu_stat_history, hist.h_1day));
}

static int stat_rt_1min_open(struct inode *inode, struct file *file)
{
    return single_open_size(file, show_rt_1min, NULL, 8 * num_online_cpus());
}

static int stat_rt_5min_open(struct inode *inode, struct file *file)
{
    return single_open_size(file, show_rt_5min, NULL, 8 * num_online_cpus());
}

static int stat_hist_1min_open(struct inode *inode, struct file *file)
{
    return single_open_size(file, show_hist_1min, NULL, 128 * num_online_cpus());
}

static int stat_hist_5min_open(struct inode *inode, struct file *file)
{
    return single_open_size(file, show_hist_5min, NULL, 128 * num_online_cpus());
}

static int stat_hist_1hour_open(struct inode *inode, struct file *file)
{
    return single_open_size(file, show_hist_1hour, NULL, 128 * num_online_cpus());
}

static int stat_hist_1day_open(struct inode *inode, struct file *file)
{
    return single_open_size(file, show_hist_1day, NULL, 160 * num_online_cpus());
}

static const struct file_operations rt_1min_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = stat_rt_1min_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static const struct file_operations rt_5min_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = stat_rt_5min_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static const struct file_operations stat_hist_1min_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = stat_hist_1min_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static const struct file_operations hist_5min_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = stat_hist_5min_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static const struct file_operations hist_1hour_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = stat_hist_1hour_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static const struct file_operations hist_1day_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = stat_hist_1day_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

int cpu_stat_update(void)
{
    int i;
    u8 index;
    u8 delta_index;
    u16 res;
    u64 busy;
    struct kernel_cpustat kcpustat;
    u64 *const cpustat = kcpustat.cpustat;
    struct single_cpu_stat_history *core;
    struct single_cpu_stat_value *cur;
    struct single_cpu_stat_value *last;

    if (unlikely(!cpu_stat)) {
        printk(KERN_ERR "MONITOR: cpu stat null!");
        return -EINVAL;
    }

    for_each_online_cpu(i) {
        core = cpu_stat->core + i;
        delta_index = core->cur_stat;
        index = delta_index ^ 1;
        core->cur_stat = index;
        cur = &core->stat[index];
        last = &core->stat[delta_index];

        kcpustat = kcpustat_cpu(i);
        cur->user       = cpustat[CPUTIME_USER];
        cur->nice       = cpustat[CPUTIME_NICE];
        cur->system     = cpustat[CPUTIME_SYSTEM];
        cur->idle       = get_idle_time(&kcpustat, i);
        cur->iowait     = get_iowait_time(&kcpustat, i);
        cur->irq        = cpustat[CPUTIME_IRQ];
        cur->softirq    = cpustat[CPUTIME_SOFTIRQ];
        cur->steal      = cpustat[CPUTIME_STEAL];
        cur->guest      = cpustat[CPUTIME_GUEST];
        cur->guest_nice = cpustat[CPUTIME_GUEST_NICE];

        last->user = cur->user - last->user;
        last->nice = cur->nice - last->nice;
        last->system = cur->system - last->system;
        last->idle = cur->idle - last->idle;
        last->iowait = cur->iowait - last->iowait;
        last->irq = cur->irq - last->irq;
        last->softirq = cur->softirq - last->softirq;
        last->steal = cur->steal - last->steal;
        last->guest = cur->guest - last->guest;
        last->guest_nice = cur->guest_nice - last->guest_nice;
        busy = last->user + last->nice + last->system + last->irq + last->softirq + last->guest
            + last->guest_nice;
        core->total = busy + last->idle + last->iowait + last->steal;

        if (unlikely(core->total == 0 || busy == 0)) {
            res = 0;
        } else {
            res = (u16) div64_u64(busy * 100 * 100, core->total);
        }

        history_record_update(&core->hist, res);
        (void) vsmall_ring_buffer_add(vsmall_ring_buffer_ptr(core->rt_5min), res);
    }

    return 0;
}

int cpu_stat_init(void)
{
    unsigned int ncore;
    unsigned int size;
    struct proc_dir_entry *entry_root;
    struct proc_dir_entry *entry;

    entry_root = monitor_proc_fs_entry(MONITOR_PROC_FS_ENTRY_CPU_ROOT);
    if (unlikely(!entry_root)) {
        printk(KERN_ERR "get monitor entry of cpu root fail!");
        return -EINVAL;
    }

    ncore = num_online_cpus();
    size = ncore * sizeof(cpu_stat->core[0]) + sizeof(*cpu_stat);
    cpu_stat = kmalloc(size, GFP_KERNEL);
    if (unlikely(!cpu_stat)) {
        printk(KERN_ERR "MONITOR: kmalloc fail for cpu stat!");
        return -EINVAL;
    }

    (void) memset(cpu_stat, 0, size);
    cpu_stat->ncore = ncore;
    for (size = 0; size < ncore; size++) {
        history_record_init(&cpu_stat->core[size].hist);
        vsmall_ring_buffer_init(cpu_stat->core[size].rt_5min, 1);
    }

    entry = proc_create("stat", 0444, entry_root, &stat_hist_1min_proc_ops);
    if (unlikely(!entry)) {
        printk(KERN_ERR "create /proc/monitor/cpu/stat fail!");
        goto err;
    }

    entry = proc_create("stat_rt_1min", 0444, entry_root, &rt_1min_proc_ops);
    if (unlikely(!entry)) {
        printk(KERN_ERR "create /proc/monitor/cpu/stat fail!");
        goto err;
    }

    entry = proc_create("stat_rt_5min", 0444, entry_root, &rt_5min_proc_ops);
    if (unlikely(!entry)) {
        printk(KERN_ERR "create /proc/monitor/cpu/stat fail!");
        goto err;
    }

    entry = proc_create("stat_hist_5min", 0444, entry_root, &hist_5min_proc_ops);
    if (unlikely(!entry)) {
        printk(KERN_ERR "create /proc/monitor/cpu/stat_hist_5min fail!");
        goto err;
    }

    entry = proc_create("stat_hist_1hour", 0444, entry_root, &hist_1hour_proc_ops);
    if (unlikely(!entry)) {
        printk(KERN_ERR "create /proc/monitor/cpu/stat_hist_1hour fail!");
        goto err;
    }

    entry = proc_create("stat_hist_1day", 0444, entry_root, &hist_1day_proc_ops);
    if (unlikely(!entry)) {
        printk(KERN_ERR "create /proc/monitor/cpu/stat_hist_1day fail!");
        goto err;
    }

    return 0;
err:
    kfree(cpu_stat);
    return -EINVAL;
}

void cpu_stat_destory(void)
{
    struct proc_dir_entry *entry;

    entry = monitor_proc_fs_entry(MONITOR_PROC_FS_ENTRY_CPU_ROOT);
    if (unlikely(!entry && !cpu_stat))
        return;

    remove_proc_entry("stat", entry);
    remove_proc_entry("stat_rt_1min", entry);
    remove_proc_entry("stat_rt_5min", entry);
    remove_proc_entry("stat_hist_5min", entry);
    remove_proc_entry("stat_hist_1hour", entry);
    remove_proc_entry("stat_hist_1day", entry);
    kfree(cpu_stat);
    cpu_stat = NULL;
}
