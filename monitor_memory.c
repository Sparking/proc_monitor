#include <linux/mm.h>
#include <linux/init.h>
#include <linux/math64.h>
#include <linux/vmstat.h>
#include <linux/spinlock.h>
#include "history.h"
#include "proc_fs.h"
#include "monitor_memory.h"

struct memory_stat_history {
    __kernel_ulong_t total;
    rwlock_t rwlock;
    history_record_t hist;
};

static struct memory_stat_history mem_stat;

__kernel_ulong_t memory_total_size_kb(void)
{
    return mem_stat.total;
}

static int show_hist(struct seq_file *p, void *v)
{
    read_lock(&mem_stat.rwlock);
    show_history_record(p, (vsmall_ring_buffer_t *) ((void *) &mem_stat + (unsigned long) p->private));
    read_unlock(&mem_stat.rwlock);

    return 0;
}

static int hist_open(struct inode *inode, struct file *file)
{
    return single_open_size(file, show_hist, PDE_DATA(inode), 160);
}

static const struct file_operations hist_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = hist_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

void memory_stat_update(void)
{
    u16 rate;
    struct sysinfo i;
    long cached;
    /* long available; */
    unsigned long used;
    unsigned long sreclaimable;

    si_meminfo(&i);
    /* si_swapinfo(&i); */
    /* available = si_mem_available(); */
	cached = global_node_page_state(NR_FILE_PAGES) - i.bufferram;
	if (cached < 0)
		cached = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0)
    sreclaimable = global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
    sreclaimable = global_node_page_state(NR_SLAB_RECLAIMABLE);
#else
#error "please port form fs/proc/meminfo.c"
#endif

    used = i.totalram - i.freeram - i.bufferram - cached - sreclaimable;
    rate = (u16) (used * 100 * 100 / i.totalram);
    write_lock(&mem_stat.rwlock);
    mem_stat.total = i.totalram << (PAGE_SHIFT - 10);
    history_record_update(&mem_stat.hist, rate);
    write_unlock(&mem_stat.rwlock);
}

int __init memory_stat_init(void)
{
    const char *path;
    struct proc_dir_entry *entry_root;
    struct proc_dir_entry *entry;

    mem_stat.total = 1;
    rwlock_init(&mem_stat.rwlock);
    history_record_init(&mem_stat.hist);
    entry_root = monitor_proc_fs_entry(MONITOR_PROC_FS_ENTRY_MEM_ROOT);
    if (unlikely(!entry_root)) {
        printk(KERN_ERR "get monitor entry of memory root fail!");
        return -EINVAL;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1MIN, 0444, entry_root, &hist_proc_ops,
        (void *) offsetof(struct memory_stat_history, hist.h_1min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_HIST_1MIN_MEMORY;
        goto err;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_5MIN, 0444, entry_root, &hist_proc_ops,
        (void *) offsetof(struct memory_stat_history, hist.h_5min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_HIST_5MIN_MEMORY;
        goto err;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1HOUR, 0444, entry_root, &hist_proc_ops,
        (void *) offsetof(struct memory_stat_history, hist.h_1hour));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_HIST_1HOUR_MEMORY;
        goto err;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1DAY, 0444, entry_root, &hist_proc_ops,
        (void *) offsetof(struct memory_stat_history, hist.h_1day));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_HIST_1DAY_MEMORY;
        goto err;
    }

    return 0;
err:
    printk(KERN_ERR "MONITOR: create %s fail!", path);
    memory_stat_exit();
    return -EINVAL;
}

void memory_stat_exit(void)
{
    struct proc_dir_entry *entry;

    entry = monitor_proc_fs_entry(MONITOR_PROC_FS_ENTRY_MEM_ROOT);
    if (unlikely(!entry))
        return;

    remove_proc_entry(MONITOR_PROC_HIST_1MIN, entry);
    remove_proc_entry(MONITOR_PROC_HIST_5MIN, entry);
    remove_proc_entry(MONITOR_PROC_HIST_1HOUR, entry);
    remove_proc_entry(MONITOR_PROC_HIST_1DAY, entry);
}
