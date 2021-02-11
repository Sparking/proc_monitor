#include <linux/slab.h>
#include <linux/tick.h>
#include <linux/math64.h>
#include <linux/rwlock.h>
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
    rwlock_t rwlock;
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

void cpu_stat_update(void)
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

    /* XXX: 删除代码优化，可以保证cpu_stat不会为空 */
#if 0
    if (unlikely(!cpu_stat)) {
        printk(KERN_ERR "MONITOR: cpu stat null!");
        return;
    }
#endif

    write_lock(&cpu_stat->rwlock);
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

        res = 0;
        if (likely(core->total && busy))
            res = (u16) div64_u64(busy * 100 * 100, core->total);

        history_record_update(&core->hist, res);
        (void) vsmall_ring_buffer_add(vsmall_ring_buffer_ptr(core->rt_5min), res);
    }
    write_unlock(&cpu_stat->rwlock);
}

static int show_rt(struct seq_file *p, void *v)
{
    unsigned int i;
    unsigned long off;
    vsmall_ring_buffer_t *phis;

    off = (unsigned long) p->private;
    read_lock(&cpu_stat->rwlock);
    for_each_online_cpu(i) {
        phis = (vsmall_ring_buffer_t *) ((void *) &cpu_stat->core[i] + off);
        seq_printf(p, "%d", i);
        seq_put_decimal_ull(p, SEQ_DELIM_SPACE, (unsigned long long) phis->sum / phis->cnt);
        seq_putc(p, '\n');
    }
    read_unlock(&cpu_stat->rwlock);

    return 0;
}

static int do_show_his(struct seq_file *__restrict p, const vsmall_ring_buffer_t *__restrict phis)
{
    u8 c;
    u16 index;

    c = phis->rear;
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

    for (index = 0; index < phis->cnt; index++) {
        seq_put_decimal_ull(p, SEQ_DELIM_SPACE, (unsigned long long) phis->buffer[c]);
        if (unlikely(c == 0))
            c = phis->size;
        c--;
    }

    for (; index < phis->size; index++)
        seq_printf(p, " 0");
    seq_putc(p, '\n');

    return 0;
}

static int show_hist(struct seq_file *p, void *v)
{
    unsigned int i;
    unsigned long off;

    off = (unsigned long) p->private;
    read_lock(&cpu_stat->rwlock);
    for_each_online_cpu(i) {
        seq_printf(p, "%d", i);
        do_show_his(p, (vsmall_ring_buffer_t *) ((void *) &cpu_stat->core[i] + off));
    }
    read_unlock(&cpu_stat->rwlock);

    return 0;
}

static int rt_open(struct inode *inode, struct file *file)
{
    return single_open_size(file, show_rt, PDE_DATA(inode), 8 * num_online_cpus());
}

static int hist_open(struct inode *inode, struct file *file)
{
    return single_open_size(file, show_hist, PDE_DATA(inode), 160 * num_online_cpus());
}

static const struct file_operations rt_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = rt_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

static const struct file_operations hist_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = hist_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

int cpu_stat_init(void)
{
    unsigned int ncore;
    unsigned int size;
    const char *path;
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

    cpu_stat->ncore = ncore;
    rwlock_init(&cpu_stat->rwlock);
    for (size = 0; size < ncore; size++) {
        history_record_init(&cpu_stat->core[size].hist);
        vsmall_ring_buffer_init(cpu_stat->core[size].rt_5min, 1);
    }

    entry = proc_create_data(MONITOR_PROC_RT_1MIN, 0444, entry_root, &rt_proc_ops,
        (void *) offsetof(struct single_cpu_stat_history, hist.h_1min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_RT_1MIN_PERCPU;
        goto err;
    }

    entry = proc_create_data(MONITOR_PROC_RT_5MIN, 0444, entry_root, &rt_proc_ops,
        (void *) offsetof(struct single_cpu_stat_history, rt_5min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_RT_5MIN_PERCPU;
        goto err;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1MIN, 0444, entry_root, &hist_proc_ops,
        (void *) offsetof(struct single_cpu_stat_history, hist.h_1min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_HIST_1MIN_PERCPU;
        goto err;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_5MIN, 0444, entry_root, &hist_proc_ops,
        (void *) offsetof(struct single_cpu_stat_history, hist.h_5min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_HIST_5MIN_PERCPU;
        goto err;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1HOUR, 0444, entry_root, &hist_proc_ops,
        (void *) offsetof(struct single_cpu_stat_history, hist.h_1hour));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_HIST_1HOUR_PERCPU;
        goto err;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1DAY, 0444, entry_root, &hist_proc_ops,
        (void *) offsetof(struct single_cpu_stat_history, hist.h_1day));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_HIST_1DAY_PERCPU;
        goto err;
    }

    return 0;
err:
    printk(KERN_ERR "MONITOR: create %s fail!", path);
    cpu_stat_destory();
    return -EINVAL;
}

void cpu_stat_destory(void)
{
    struct proc_dir_entry *entry;

    entry = monitor_proc_fs_entry(MONITOR_PROC_FS_ENTRY_CPU_ROOT);
    if (unlikely(!entry && !cpu_stat))
        return;

    remove_proc_entry(MONITOR_PROC_HIST_1MIN, entry);
    remove_proc_entry(MONITOR_PROC_RT_1MIN, entry);
    remove_proc_entry(MONITOR_PROC_RT_5MIN, entry);
    remove_proc_entry(MONITOR_PROC_HIST_5MIN, entry);
    remove_proc_entry(MONITOR_PROC_HIST_1HOUR, entry);
    remove_proc_entry(MONITOR_PROC_HIST_1DAY, entry);
    kfree(cpu_stat);
    cpu_stat = NULL;
}
