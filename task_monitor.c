#include <linux/module.h>
#include <linux/init.h>
#include <uapi/linux/sched.h>
#include <linux/init_task.h>
#include <linux/mm_types.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include "task_monitor.h"
#include "hash_table.h"

struct monitor_task {
    struct hlist_node node;
    pid_t task_id;
    u8 is_process;
    u8 state;
};

#if 0
static struct sighand_struct *lock_task_sighand_static(struct task_struct *tsk, unsigned long *flags)
{
    struct sighand_struct *sighand;

    rcu_read_lock();
    for (;;) {
        sighand = rcu_dereference(tsk->sighand);
        if (unlikely(sighand == NULL))
            break;

        spin_lock_irqsave(&sighand->siglock, *flags);
        if (likely(sighand == rcu_access_pointer(tsk->sighand)))
            break;

        spin_unlock_irqrestore(&sighand->siglock, *flags);
    }
    rcu_read_unlock();
    (void)__cond_lock(&tsk->sighand->siglock, sighand);

    return sighand;
}

static inline void ulock_task_sighand_static(struct task_struct *tsk, unsigned long *flags)
{
    spin_unlock_irqrestore(&tsk->sighand->siglock, *flags);
}

static int get_task_info(struct task_struct *task)
{
    char state;
    unsigned int cpu;
    unsigned int num_threads;
    int prio, nice;
    unsigned long flags;
    /* __attirbute__((unused)) u64 utime, stime; */
    struct mm_struct *mm;
    pid_t pid;

    state = task_state_to_char(task);
    cpu = task_cpu(task);
    if (lock_task_sighand_static(task, &flags)) {
        num_threads = get_nr_threads(task);
        ulock_task_sighand_static(task, &flags);
    }

    pid = pid_nr(task_pid(task));
    prio = task->prio - MAX_RT_PRIO;
    nice = task_nice(task);
    mm = get_task_mm(task);
    if (mm)
        mmput(mm);

    //printk("task: %d %u %u %c %d %d", pid, cpu, num_threads, state, prio, nice);

    return 0;
}
#else
#define get_task_info(x) 0
#endif

static dual_hash_table_t *process_hash_table = NULL;
static dual_hash_table_t *thread_hash_table = NULL;

static uint process_hash_size = 32;
static uint thread_hash_size = 64;
module_param(process_hash_size, uint, 0444);
module_param(thread_hash_size, uint, 0444);

#define update_process_stat(x) do {} while (0)
static struct monitor_task *new_process_node(const pid_t id)
{
    struct monitor_task *t;

    t = (struct monitor_task *) kmalloc(sizeof(*t), GFP_ATOMIC);
    if (likely(t)) {
        t->task_id = id;
        t->is_process = 1;
        t->state = 0;
    }

    return t;
}

void task_stat_update(void)
{
    pid_t id;
    unsigned int nprocess;
    unsigned int nthread;
    struct task_struct *process;
    struct task_struct *thread;
    struct monitor_task *t;
    struct hlist_node *t_node;

    nthread = 0;
    nprocess = 0;
    rcu_read_lock();
    for_each_process(process) {
        id = task_pid_nr(process);
        (void) get_task_info(process);
        t_node = dual_hash_table_find_last(process_hash_table, id);
        if (likely(t_node)) {
            t = hlist_entry(t_node, struct monitor_task, node);
            update_process_stat(t);
            hlist_del(t_node);
        } else {
            t = new_process_node(id);
            if (unlikely(!t)) {
                printk(KERN_ERR "MONITOR: no memory for new process node!");
                goto out;
            }
        }
        dual_hash_table_add_using(process_hash_table, t_node);
        nprocess++;
        
        for_each_thread(process, thread) {
            id = task_pid_nr(thread);
            (void) get_task_info(thread);
            t_node = dual_hash_table_find_last(thread_hash_table, id);
            if (likely(t_node)) {
                t = hlist_entry(t_node, struct monitor_task, node);
                update_process_stat(t);
                hlist_del(t_node);
            } else {
                t = new_process_node(id);
                if (unlikely(!t)) {
                    printk(KERN_ERR "MONITOR: no memory for new thread node!");
                    goto out;
                }
            }
            dual_hash_table_add_using(process_hash_table, t_node);
            nthread++;
        }
    }
out:
    dual_hash_table_clean_last(thread_hash_table);
    dual_hash_table_clean_last(process_hash_table);
    dual_hash_table_switch_table(thread_hash_table);
    dual_hash_table_switch_table(process_hash_table);
    rcu_read_unlock();
}

static unsigned int task_get_id(const struct hlist_node *__restrict node)
{
    return hlist_entry(node, struct monitor_task, node)->task_id;
}

static void realse_task(struct hlist_node *__restrict node)
{
    kfree(hlist_entry(node, struct monitor_task, node));
}

int __init task_stat_init(void)
{
    if (!process_hash_size || !thread_hash_size) {
        printk(KERN_ERR "MONITOR: hash size invalid(process: %u, thread: %u)", process_hash_size,
            thread_hash_size);
        return -EINVAL;
    }

    process_hash_table = dual_hash_table_create(process_hash_size, task_get_id, realse_task);
    if (!process_hash_table) {
        printk(KERN_ERR "MONITOR: create process hash table fail!");
        return -ENOMEM;
    }

    thread_hash_table = dual_hash_table_create(thread_hash_size, task_get_id, realse_task);
    if (!thread_hash_table) {
        dual_hash_table_destory(process_hash_table);
        process_hash_table = NULL;
        printk(KERN_ERR "MONITOR: create thread hash table fail!");
        return -ENOMEM;
    }

    return 0;
}

void task_stat_exit(void)
{
    if (process_hash_table) {
        dual_hash_table_destory(process_hash_table);
        process_hash_table = NULL;
    }

    if (thread_hash_table) {
        dual_hash_table_destory(thread_hash_table);
        thread_hash_table = NULL;
    }
}
