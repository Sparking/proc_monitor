#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm_types.h>
#include <linux/spinlock.h>
#include <linux/sched/mm.h>
#include <linux/init_task.h>
#include <uapi/linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/cputime.h>
#include "proc_fs.h"
#include "history.h"
#include "hash_table.h"
#include "monitor_task.h"
#include "monitor_memory.h"
#include "monitor_percpu.h"

/* proc 传参使用，缩减代码大小(往地址最高位传递1，因为结构体不可能会这么大，往最高位传递1是安全的) */
#define offsetof_substruct(pt,t,m)  (sizeof(pt) + offsetof(t, m))
#define _hi_bitmask_ptr(b, w)       ((b) << ((sizeof(void *) << 3ul) - (w)))
#define hi_bitmask_set(val)         ((val) | _hi_bitmask_ptr(1ul, 1))
#define hi_bitmask_get(val)         ((val) & _hi_bitmask_ptr(1ul, 1))
#define hi_bitmask_unmask(val)      ((val) & ~_hi_bitmask_ptr(1ul, 1))

#define monitor_process_member_offset(m) \
    hi_bitmask_set(offsetof_substruct(struct monitor_task, struct monitor_process, m))

#define monitor_thread_member_offset(m) \
    offsetof_substruct(struct monitor_task, struct monitor_thread, m)

#define proc_data_task_is_process(param)    (!!hi_bitmask_get(param))
#define proc_data_task_get_real_parm(param) hi_bitmask_unmask(param)

struct process_memory_info {
    unsigned long peak;           /* VmPeak, 进程所使用的虚拟内存的峰值 */
    unsigned long total;          /* VmSize, 进程当前使用的虚拟内存的大小 */
    unsigned long hwm;            /* VmHWM, 进程所使用的物理内存的峰值 */
    unsigned long rss;            /* VmRSS, 进程当前使用的物理内存的大小 */
    unsigned long swap;           /* VmSwap, 进程所使用的交换区的大小 */
    unsigned long text;           /* VmExe, 文本段 */
    unsigned long data;           /* VmData, 数据段 */
    unsigned long stack;          /* VmStk, 主线程栈段 */
    unsigned long lib;            /* VmLib, 进程所加载的动态库所占用的内存大小(可能与其它进程共享) */
};

struct monitor_process {
    history_record_t hist_mem;
    struct process_memory_info vm;
    unsigned int nthreads;
};

struct monitor_thread {
    history_record_t hist_cpu;
    u64 last_cpu_time;
};

struct monitor_task {
    struct hlist_node node;
    unsigned long long start_time;
    char state;
    char comm[63];
    int prio;
    int nice;
    pid_t task_id;
    pid_t super_id;                 /* 对进程来说，是父进程id；对线程来说，是主线程的id */
    unsigned int cpu_running;
    cpumask_t cpu_allowed;
    char private[0];
};

static uint process_hash_size = 32;
static uint thread_hash_size = 64;
module_param(process_hash_size, uint, 0444);
module_param(thread_hash_size, uint, 0444);
static rwlock_t task_rwlock;        /* 保护以下两个hash表 */
static dual_hash_table_t *process_hash_table = NULL;
static dual_hash_table_t *thread_hash_table = NULL;

static void get_task_mem(struct process_memory_info *__restrict vm, struct mm_struct *__restrict mm)
{
    unsigned long text, lib, anon, file, shmem;
    unsigned long hiwater_vm, total_vm, hiwater_rss, total_rss;

    anon = get_mm_counter(mm, MM_ANONPAGES);
    file = get_mm_counter(mm, MM_FILEPAGES);
    shmem = get_mm_counter(mm, MM_SHMEMPAGES);
    hiwater_vm = total_vm = mm->total_vm;
    if (hiwater_vm < mm->hiwater_vm)
        hiwater_vm = mm->hiwater_vm;
    hiwater_rss = total_rss = anon + file + shmem;
    if (hiwater_rss < mm->hiwater_rss)
        hiwater_rss = mm->hiwater_rss;

    text = PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK);
    text = min(text, mm->exec_vm << PAGE_SHIFT);
    lib = (mm->exec_vm << PAGE_SHIFT) - text;

#define MM_CONV_DEC(val)    ((val) << (PAGE_SHIFT - 10))
    vm->peak = MM_CONV_DEC(hiwater_vm);
    vm->total = MM_CONV_DEC(total_vm);
    vm->hwm = MM_CONV_DEC(hiwater_rss);
    vm->rss = MM_CONV_DEC(total_rss);
    vm->swap = MM_CONV_DEC(get_mm_counter(mm, MM_SWAPENTS));
    vm->text = text >> 10;
    vm->data = MM_CONV_DEC(mm->data_vm);
    vm->stack = MM_CONV_DEC(mm->stack_vm);
    vm->lib = lib >> 10;
#undef MM_CONV_DEC
}

static void monitor_task_update_memory_history(struct task_struct *__restrict task,
                struct monitor_process *__restrict ps)
{
    u16 rate;
    struct mm_struct *mm;

    mm = get_task_mm(task);
    if (likely(mm)) {
        get_task_mem(&ps->vm, mm);
        mmput(mm);
        rate = ps->vm.rss * 10000 / memory_total_size_kb();
    } else {
        memzero_explicit(&ps->vm, sizeof(ps->vm));
        rate = 0;
    }

    history_record_update(&ps->hist_mem, rate);
}

static void monitor_task_update_cpu_history(struct task_struct *__restrict task,
                struct monitor_thread *__restrict pt)
{
    u16 rate;
    u64 utime, stime;
    u64 res;

    utime = 0;
    stime = 0;
    task_cputime_adjusted(task, &utime, &stime);
    res = utime + stime;
    if (unlikely(res <= pt->last_cpu_time)) {
        rate = 0;
    } else {
        rate = div64_u64((res - pt->last_cpu_time) * 100 * 100, cpu_delta_tick(task_cpu(task)));
        pt->last_cpu_time = res;
    }

    history_record_update(&pt->hist_cpu, rate);
}

static void monitor_task_init_common(struct task_struct *__restrict task,
                struct monitor_task *__restrict t)
{
    t->prio = task->prio - MAX_RT_PRIO;
    t->nice = task_nice(task);
    t->state = task_state_to_char(task);
    t->task_id = task_pid_nr(task);
    t->cpu_running = task_cpu(task);
    t->cpu_allowed = *task->cpus_ptr;
    t->start_time = task->start_time;
    strncpy(t->comm, task->comm, sizeof(t->comm));
}

static void monitor_task_init_process(struct task_struct *__restrict task,
                struct monitor_task *__restrict ps)
{
    struct monitor_process *pp;

    monitor_task_init_common(task, ps);
    ps->super_id = task_ppid_nr(task);
    pp = (struct monitor_process *) ps->private;
    history_record_init(&pp->hist_mem);
    memzero_explicit(&pp->vm, sizeof(pp->vm));
    pp->nthreads = 0;
}

static void monitor_task_init_thread(struct task_struct *__restrict task,
                struct monitor_task *__restrict ps, const pid_t main_thread_id)
{
    struct monitor_thread *pt;

    monitor_task_init_common(task, ps);
    ps->super_id = main_thread_id;
    pt = (struct monitor_thread *) ps->private;
    history_record_init(&pt->hist_cpu);
    pt->last_cpu_time = 0;
}

static inline struct monitor_task *monitor_task_create_node(const bool is_ps)
{
    return (struct monitor_task *) kmalloc(sizeof(struct monitor_task)
        + (is_ps ? sizeof(struct monitor_process) : sizeof(struct monitor_thread)), GFP_ATOMIC);
}

void task_stat_update(void)
{
    pid_t id;
    pid_t main_thread_id;
    struct monitor_task *t;
    struct hlist_node *t_node;
    struct monitor_process *ps;
    struct task_struct *thread;
    struct task_struct *process;

    write_lock(&task_rwlock);   /* rcu锁放置于内部, hash表的锁可能会陷入等待 */
    rcu_read_lock();
    for_each_process(process) {
        main_thread_id = task_pid_nr(process);
        t_node = dual_hash_table_find_last(process_hash_table, main_thread_id);
        if (likely(t_node)) {
            t = hlist_entry(t_node, struct monitor_task, node);
            hlist_del(t_node);
        } else {
            t = monitor_task_create_node(true);
            if (unlikely(!t)) {
                printk(KERN_ERR "MONITOR: no memory for new process node!");
                goto out;
            }

            monitor_task_init_process(process, t);
            t_node = &t->node;
        }

        ps = (struct monitor_process *) t->private;
        ps->nthreads = 0;
        monitor_task_update_memory_history(process, ps);
        dual_hash_table_add_using(process_hash_table, t_node);
        for_each_thread(process, thread) {
            id = task_pid_nr(thread);
            t_node = dual_hash_table_find_last(thread_hash_table, id);
            if (likely(t_node)) {
                t = hlist_entry(t_node, struct monitor_task, node);
                hlist_del(t_node);
            } else {
                t = monitor_task_create_node(false);
                if (unlikely(!t)) {
                    printk(KERN_ERR "MONITOR: no memory for new thread node!");
                    goto out;
                }

                monitor_task_init_thread(thread, t, main_thread_id);
                t_node = &t->node;
            }

            monitor_task_update_cpu_history(thread, (struct monitor_thread *) t->private);
            dual_hash_table_add_using(thread_hash_table, t_node);
            ps->nthreads++;
        }
    }
out:
    rcu_read_unlock();
    dual_hash_table_clean_last(thread_hash_table);
    dual_hash_table_clean_last(process_hash_table);
    dual_hash_table_switch_table(thread_hash_table);
    dual_hash_table_switch_table(process_hash_table);
    write_unlock(&task_rwlock);
}

static unsigned int task_get_id(const struct hlist_node *__restrict node)
{
    return hlist_entry(node, struct monitor_task, node)->task_id;
}

static void realse_task(struct hlist_node *__restrict node)
{
    struct monitor_task *t;

    t = hlist_entry(node, struct monitor_task, node);
    printk(KERN_DEBUG "free node %u\n", t->task_id);

    kfree(t);
}

static void show_ps_hist(struct hlist_node *__restrict node, struct seq_file *__restrict p,
            void *__restrict v)
{
    struct monitor_task *task;
    unsigned long off;

    task = hlist_entry(node, struct monitor_task, node);
    off = (unsigned long) v;
    seq_printf(p, "%d ", task->task_id);
    show_history_record(p, (vsmall_ring_buffer_t *) ((void *) task + off));
}

static void show_thread_hist(struct hlist_node *__restrict node, struct seq_file *__restrict p,
            void *__restrict v)
{
    struct monitor_task *task;

    task = hlist_entry(node, struct monitor_task, node);
    seq_printf(p, "%d ", task->task_id);
    seq_printf(p, "%d ", task->cpu_running);
    show_history_record(p, (vsmall_ring_buffer_t *) ((void *) task + (unsigned long) v));
}

static int show_hist(struct seq_file *p, void *v)
{
    void *off;

    off = (void *) proc_data_task_get_real_parm((unsigned long long) p->private);
    read_lock(&task_rwlock);
    if (proc_data_task_is_process((unsigned long long) p->private)) {
        dual_hash_table_trave_last(process_hash_table, show_ps_hist, p, off);
    } else {
        dual_hash_table_trave_last(thread_hash_table, show_thread_hist, p, off);
    }
    read_unlock(&task_rwlock);

    return 0;
}

static void do_show_status(struct hlist_node *__restrict node, struct seq_file *__restrict p,
                void *__restrict v)
{
    struct monitor_task *task;

    task = hlist_entry(node, struct monitor_task, node);
    seq_printf(p, "(%s) %d %d %c %d %d %d", task->comm, task->task_id, task->super_id, task->state,
        task->prio, task->nice, task->cpu_running);
    seq_putc(p, '\n');
}

static int show_status(struct seq_file *p, void *v)
{
    read_lock(&task_rwlock);
    dual_hash_table_trave_last((dual_hash_table_t *) p->private, do_show_status, p, NULL);
    read_unlock(&task_rwlock);

    return 0;
}

static int hist_open(struct inode *inode, struct file *file)
{
    return single_open(file, show_hist, PDE_DATA(inode));
}

static int status_open(struct inode *inode, struct file *file)
{
    return single_open(file, show_status, PDE_DATA(inode));
}

static const struct file_operations status_proc_ops = {
    .owner   = THIS_MODULE,
    .open    = status_open,
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

int __init task_stat_init(void)
{
    int ret;
    const char *path;
    struct proc_dir_entry *entry_root;
    struct proc_dir_entry *entry;

    if (!process_hash_size || !thread_hash_size) {
        printk(KERN_ERR "MONITOR: hash size invalid(process: %u, thread: %u)", process_hash_size,
            thread_hash_size);
        return -EINVAL;
    }

    rwlock_init(&task_rwlock);
    ret = -ENOMEM;
    process_hash_table = dual_hash_table_create(process_hash_size, task_get_id, realse_task);
    if (!process_hash_table) {
        printk(KERN_ERR "MONITOR: create process hash table fail!");
        goto create_hash_fail_out;
    }

    thread_hash_table = dual_hash_table_create(thread_hash_size, task_get_id, realse_task);
    if (!thread_hash_table) {
        printk(KERN_ERR "MONITOR: create thread hash table fail!");
        goto create_hash_fail_out;
    }

    ret = -EINVAL;
    entry_root = monitor_proc_fs_entry(MONITOR_PROC_FS_ENTRY_PROCESS_ROOT);
    if (!entry_root) {
        printk(KERN_ERR "MONITOR: get process proc entry failed!");
        goto create_hash_fail_out;
    }

    entry = proc_create_data(MONITOR_PROC_TASK_STATUS_NAME, 0444, entry_root, &status_proc_ops,
        (void *) process_hash_table);
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_RPOCESS_STATUS;
        goto create_proc_data_failed;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1MIN, 0444, entry_root, &hist_proc_ops,
        (void *) monitor_process_member_offset(hist_mem.h_1min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_1MIN_MEM;
        goto create_proc_data_failed;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_5MIN, 0444, entry_root, &hist_proc_ops,
        (void *) monitor_process_member_offset(hist_mem.h_5min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_5MIN_MEM;
        goto create_proc_data_failed;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1HOUR, 0444, entry_root, &hist_proc_ops,
        (void *) monitor_process_member_offset(hist_mem.h_1hour));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_1HOUR_MEM;
        goto create_proc_data_failed;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1DAY, 0444, entry_root, &hist_proc_ops,
        (void *) monitor_process_member_offset(hist_mem.h_1day));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_1DAY_MEM;
        goto create_proc_data_failed;
    }

    entry_root = monitor_proc_fs_entry(MONITOR_PROC_FS_ENTRY_THREAD_ROOT);
    if (!entry_root) {
        printk(KERN_ERR "MONITOR: get thread proc entry failed!");
        goto create_hash_fail_out;
    }

    entry = proc_create_data(MONITOR_PROC_TASK_STATUS_NAME, 0444, entry_root, &status_proc_ops,
        (void *) thread_hash_table);
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_THREAD_STATUS;
        goto create_proc_data_failed;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1MIN, 0444, entry_root, &hist_proc_ops,
        (void *) monitor_thread_member_offset(hist_cpu.h_1min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_1MIN_CPU;
        goto create_proc_data_failed;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_5MIN, 0444, entry_root, &hist_proc_ops,
        (void *) monitor_thread_member_offset(hist_cpu.h_5min));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_5MIN_CPU;
        goto create_proc_data_failed;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1HOUR, 0444, entry_root, &hist_proc_ops,
        (void *) monitor_thread_member_offset(hist_cpu.h_1hour));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_1HOUR_CPU;
        goto create_proc_data_failed;
    }

    entry = proc_create_data(MONITOR_PROC_HIST_1DAY, 0444, entry_root, &hist_proc_ops,
        (void *) monitor_thread_member_offset(hist_cpu.h_1day));
    if (unlikely(!entry)) {
        path = MONITOR_PROC_TASK_1DAY_CPU;
        goto create_proc_data_failed;
    }

    return 0;

create_proc_data_failed:
    printk(KERN_ERR "MONITOR: create %s fail!", path);
create_hash_fail_out:
    task_stat_exit();
    return ret;
}
#undef OFFSETOF_SUBSTRUCT

void task_stat_exit(void)
{
    unsigned int i;
    struct proc_dir_entry *entry;

    if (process_hash_table) {
        dual_hash_table_destory(process_hash_table);
        process_hash_table = NULL;
    }

    if (thread_hash_table) {
        dual_hash_table_destory(thread_hash_table);
        thread_hash_table = NULL;
    }

    for (i = MONITOR_PROC_FS_ENTRY_PROCESS_ROOT; i <= MONITOR_PROC_FS_ENTRY_THREAD_ROOT; i++) {
        entry = monitor_proc_fs_entry(i);
        if (entry) {
            remove_proc_entry(MONITOR_PROC_TASK_STATUS_NAME, entry);
            remove_proc_entry(MONITOR_PROC_HIST_1MIN, entry);
            remove_proc_entry(MONITOR_PROC_HIST_5MIN, entry);
            remove_proc_entry(MONITOR_PROC_HIST_1HOUR, entry);
            remove_proc_entry(MONITOR_PROC_HIST_1DAY, entry);
        }
    }
}
