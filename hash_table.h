#ifndef _BASIC_LIB_HASH_TABLE_H_
#define _BASIC_LIB_HASH_TABLE_H_

#include <linux/stddef.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

/* hash函数直接计算出下标 */
typedef unsigned int (*hash_table_key_fn_t)(const struct hlist_node *__restrict element);
typedef void (*hash_table_release_fn_t)(struct hlist_node *__restrict node);
typedef void (*hash_table_trave_fn_t)(struct hlist_node *__restrict node,
                struct seq_file *__restrict file, void *__restrict arg);

typedef struct {
    unsigned int using_index;
    unsigned int index_mask;
    hash_table_key_fn_t hash;
    hash_table_release_fn_t release;
    struct hlist_head *table[2];
} dual_hash_table_t;

extern dual_hash_table_t *__init dual_hash_table_create(const unsigned int size,
                                    const hash_table_key_fn_t func,
                                    const hash_table_release_fn_t release);

extern void dual_hash_table_add(dual_hash_table_t *__restrict t, struct hlist_node *__restrict node,
                const unsigned int i);

static inline void dual_hash_table_add_using(dual_hash_table_t *__restrict t,
                    struct hlist_node *__restrict node)
{
    dual_hash_table_add(t, node, t->using_index);
}

static inline void dual_hash_table_switch_table(dual_hash_table_t *__restrict t)
{
    t->using_index ^= 1;
}

extern struct hlist_node *dual_hash_table_find(dual_hash_table_t *__restrict t,
                            const unsigned int hash, const unsigned int i);

static inline struct hlist_node *dual_hash_table_find_using(dual_hash_table_t *__restrict t,
                                    const unsigned int hash)
{
    return dual_hash_table_find(t, hash, t->using_index);
}

static inline struct hlist_node *dual_hash_table_find_last(dual_hash_table_t *__restrict t,
                                    const unsigned int hash)
{
    return dual_hash_table_find(t, hash, t->using_index ^ 1);
}

extern void dual_hash_table_clean(dual_hash_table_t *__restrict t, const unsigned int i);

static inline void dual_hash_table_clean_using(dual_hash_table_t *__restrict t)
{
    dual_hash_table_clean(t, t->using_index);
}

static inline void dual_hash_table_clean_last(dual_hash_table_t *__restrict t)
{
    dual_hash_table_clean(t, t->using_index ^ 1);
}

static inline void dual_hash_table_destory(dual_hash_table_t *__restrict t)
{
    dual_hash_table_clean_last(t);
    dual_hash_table_clean_using(t);
    kfree(t);
}

extern void dual_hash_table_trave(const dual_hash_table_t *__restrict t,
                const hash_table_trave_fn_t fn, struct seq_file *__restrict file,
                void *__restrict arg, unsigned int i);

static inline void dual_hash_table_trave_using(const dual_hash_table_t *__restrict hash_table,
                    const hash_table_trave_fn_t fn, struct seq_file *__restrict file,
                    void *__restrict arg)
{
    dual_hash_table_trave(hash_table, fn, file, arg, hash_table->using_index);
}

static inline void dual_hash_table_trave_last(const dual_hash_table_t *__restrict hash_table,
                    const hash_table_trave_fn_t fn, struct seq_file *__restrict file,
                    void *__restrict arg)
{
    dual_hash_table_trave(hash_table, fn, file, arg, hash_table->using_index ^ 1);
}

#endif /* _BASIC_LIB_HASH_TABLE_H_ */
