#ifndef _BASIC_LIB_HASH_TABLE_H_
#define _BASIC_LIB_HASH_TABLE_H_

#include <linux/stddef.h>
#include <linux/list.h>
#include <linux/seq_file.h>

/* hash函数直接计算出下标 */
typedef unsigned int (*hash_table_index_func_t)(const struct hlist_node *__restrict element);
typedef void (*hash_table_release_func_t)(struct hlist_node *__restrict node);
typedef void (*hash_table_trave_func_t)(struct hlist_node *__restrict node,
                struct seq_file *__restrict file, void *__restrict arg);

typedef struct {
    unsigned char using_index;
    unsigned int index_mask;
    hash_table_index_func_t hash;
    hash_table_release_func_t release;
    struct hlist_head *table[2];
} dual_hash_table_t;

extern dual_hash_table_t *__init dual_hash_table_create(const unsigned int size,
                            const hash_table_index_func_t func,
                            const hash_table_release_func_t release);

extern bool dual_hash_table_add_using(dual_hash_table_t *__restrict t,
                struct hlist_node *__restrict node);

static inline bool dual_hash_table_switch_node(dual_hash_table_t *__restrict t,
                    struct hlist_node *__restrict node)
{
    hlist_del(node);
    return dual_hash_table_add_using(t, node);
}

static inline void dual_hash_table_switch_table(dual_hash_table_t *__restrict t)
{
    t->using_index ^= 1;
}

extern struct hlist_node *dual_hash_table_find_using(dual_hash_table_t *__restrict t,
                            const unsigned int index);

extern struct hlist_node *dual_hash_table_find_last(dual_hash_table_t *__restrict t,
                            const unsigned int index);

extern void dual_hash_table_clean_last(dual_hash_table_t *__restrict t);

extern void dual_hash_table_destory(dual_hash_table_t *__restrict t);

extern void dual_hash_table_trave_last(const dual_hash_table_t *__restrict hash_table,
                const hash_table_trave_func_t fn, struct seq_file *__restrict file,
                void *__restrict arg);

extern void dual_hash_table_trave_using(const dual_hash_table_t *__restrict hash_table,
                const hash_table_trave_func_t fn, struct seq_file *__restrict file,
                void *__restrict arg);

#endif /* _BASIC_LIB_HASH_TABLE_H_ */
