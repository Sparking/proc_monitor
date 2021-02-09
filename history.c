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
