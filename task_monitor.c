#include <uapi/linux/sched.h>
#include <linux/init_task.h>
#include <linux/mm_types.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include "task_monitor.h"

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

void task_stat_update(void)
{
    unsigned int nprocess;
    unsigned int nthread;
    struct task_struct *process;
    struct task_struct *thread;

    nthread = 0;
    nprocess = 0;
    rcu_read_lock();
    for_each_process(process) {
        (void) get_task_info(process);
        for_each_thread(process, thread) {
            (void) get_task_info(thread);
            nthread++;
        }
        nprocess++;
    }
    rcu_read_unlock();
}
