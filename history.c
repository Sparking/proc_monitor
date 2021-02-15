#include <linux/init.h>
#include "history.h"

unsigned char vsmall_ring_buffer_add(vsmall_ring_buffer_t *__restrict pvrb, const u16 val)
{
    u16 d;

    if (unlikely(!pvrb)) {
        return 0;
    }

    pvrb->accum_times++;
    pvrb->accum_val += val;
    if (likely(pvrb->accum_times < pvrb->accum_times_max))
        return pvrb->cnt;

    if (likely(pvrb->cnt != 0)) {
        pvrb->rear++;
        if (unlikely(pvrb->rear >= pvrb->size))
            pvrb->rear = 0;

        if (likely(pvrb->cnt == pvrb->size)) {
            pvrb->sum -= pvrb->buffer[pvrb->front];
            pvrb->front++;
            if (unlikely(pvrb->front >= pvrb->size))
                pvrb->front = 0;
        } else {
            pvrb->cnt++;
        }
    } else {
        pvrb->cnt = 1;
    }

    d = (u16) (pvrb->accum_val / (u32) pvrb->accum_times_max);
    pvrb->sum += d;
    pvrb->buffer[pvrb->rear] = d;
    pvrb->accum_times = 0;
    pvrb->accum_val = 0;

    return pvrb->cnt;
}

void history_record_init(history_record_t *__restrict h)
{
    if (unlikely(!h))
        return;

    /* 5s更新一次记录 */
    vsmall_ring_buffer_init(h->h_1min, 5 / 5);              /* 5s一个数据 */
    vsmall_ring_buffer_init(h->h_5min, 30 / 5);             /* 30s一个数据 */
    vsmall_ring_buffer_init(h->h_1hour, 5 * 60 / 5);        /* 5min一个数据 */
    vsmall_ring_buffer_init(h->h_1day, 1 * 60 * 60 / 5);    /* 1hour一个数据 */
}

void history_record_update(history_record_t *__restrict h, const u16 val)
{
    if (unlikely(!h))
        return;

    (void) vsmall_ring_buffer_add(vsmall_ring_buffer_ptr(h->h_1min), val);
    (void) vsmall_ring_buffer_add(vsmall_ring_buffer_ptr(h->h_5min), val);
    (void) vsmall_ring_buffer_add(vsmall_ring_buffer_ptr(h->h_1hour), val);
    (void) vsmall_ring_buffer_add(vsmall_ring_buffer_ptr(h->h_1day), val);
}

int show_history_record(struct seq_file *__restrict p, const vsmall_ring_buffer_t *__restrict phis)
{
    u8 c;
    u16 index;
    u16 first_v;

    c = phis->rear;
    if (likely(phis->accum_times)) {
        if (likely(phis->accum_val)) {
            first_v = phis->accum_val / phis->accum_times;
        } else {
            first_v = 0;
        }
    } else {
        first_v = phis->buffer[c];
    }
    seq_printf(p, "%hu", first_v);

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
