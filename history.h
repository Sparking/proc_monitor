#ifndef _PROC_MONITOR_HISTORY_H_
#define _PROC_MONITOR_HISTORY_H_

#include <linux/string.h>
#include <linux/stddef.h>

#define vsmall_ring_buffer_def(name, size) struct { \
    vsmall_ring_buffer_t _h; \
    u16 _b[size]; } name

#define vsmall_ring_buffer(name) name._h

#define vsmall_ring_buffer_ptr(name) (&name._h)

#define vsmall_ring_buffer_init(name, max) \
    do { \
        (void) memset(&name, 0, sizeof(name)); \
        vsmall_ring_buffer(name).size = (sizeof(name) - sizeof(vsmall_ring_buffer_t)) \
            / sizeof(unsigned short); \
        vsmall_ring_buffer(name).accum_times_max = max; \
    } while (0)

#define vsmall_ring_buffer_count(name) vsmall_ring_buffer(name).cnt

#define vsmall_ring_buffer_sum(name) vsmall_ring_buffer(name).sum

#define vsmall_ring_buffer_accum_times(name) vsmall_ring_buffer(name).accum_times

typedef struct {
    u32 sum;
    u32 accum_val;
    u16 accum_times;
    u16 accum_times_max;
    u8 front, rear;
    u8 cnt, size;
    u16 buffer[0];
} vsmall_ring_buffer_t;

typedef struct {
    vsmall_ring_buffer_def(h_1min, 60 / 5);     /* 1min的历史记录, 5s一个间隔 */
    vsmall_ring_buffer_def(h_5min, 300 / 30);   /* 5min的历史记录，30s一个间隔 */
    vsmall_ring_buffer_def(h_1hour, 60 / 5);    /* 1hour的历史记录，5min一个间隔 */
    vsmall_ring_buffer_def(h_1day, 24 / 1);     /* 1day的历史记录，1hour一个间隔 */
} history_record_t;

extern unsigned char vsmall_ring_buffer_add(vsmall_ring_buffer_t *__restrict pvrb, const u16 val);

extern void history_record_init(history_record_t *__restrict h);

extern void history_record_update(history_record_t *__restrict h, const u16 val);

#endif /* _PROC_MONITOR_HISTORY_H_ */
