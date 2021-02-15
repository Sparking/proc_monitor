#include <linux/slab.h>
#include <linux/init.h>
#include <linux/log2.h>
#include "hash_table.h"

#define HASH_QUICK_MATH 1

#if defined (HASH_QUICK_MATH) && (HASH_QUICK_MATH == 1)
static inline unsigned int hash_table_round_size(const unsigned int size)
{
    return (1u << __ilog2_u32(size)) - 1;
}

static inline unsigned int hash_table_hash2index(const unsigned int size, const unsigned int hash)
{
    return hash & size;
}

#define hash_oob(index, size)    ((index) > (size))
#else
static inline unsigned int hash_table_round_size(const unsigned int size)
{
    return size;
}

static inline unsigned int hash_table_hash2index(const unsigned int size, const unsigned int hash)
{
    return hash % size;
}

#define hash_oob(index, size)    ((index) >= (size))
#endif

dual_hash_table_t * __init dual_hash_table_create(const unsigned int size,
                            const hash_table_index_func_t hash,
                            const hash_table_release_func_t release)
{
    unsigned int i;
    unsigned int mask;
    dual_hash_table_t *table;
    struct hlist_head *head1, *head2;

    if (size < 8 || !hash || !release)
        return NULL;

    mask = hash_table_round_size(size);
    table = (dual_hash_table_t *) kmalloc((mask + 1) * sizeof(struct hlist_head) * 2
        + sizeof(*table), GFP_KERNEL);
    if (table) {
        table->using_index = 0;
        table->index_mask = mask;
        table->hash = hash;
        table->release = release;
        head1 = (void *) table + sizeof(*table);
        head2 = head1 + size;
        table->table[0] = head1;
        table->table[1] = head2;
        for (i = 0; !hash_oob(i, mask); i++) {
            INIT_HLIST_HEAD(head1 + i);
            INIT_HLIST_HEAD(head2 + i);
        }
    }

    return table;
}

static bool dual_hash_table_add(dual_hash_table_t *__restrict t, struct hlist_node *__restrict node,
            const unsigned int i)
{
    unsigned int index;
    struct hlist_head *head;

    if (unlikely(!t || !node))
        return false;

    /* insert directly, no checking if has the same key */
    index = hash_table_hash2index(t->index_mask, t->hash(node));
    head = t->table[i] + index;
    hlist_add_head(node, head);

    return true;
}

bool dual_hash_table_add_using(dual_hash_table_t *__restrict t, struct hlist_node *__restrict node)
{
    return dual_hash_table_add(t, node, t->using_index);
}

static struct hlist_node *dual_hash_table_find(dual_hash_table_t *__restrict t,
                            const unsigned int hash, const unsigned int i)
{
    unsigned int index;
    struct hlist_node *ret;
    struct hlist_node *node;
    struct hlist_head *head;

    ret = NULL;
    index = hash_table_hash2index(t->index_mask, hash);
    head = t->table[i] + index;
    for (node = head->first; node; node = node->next) {
        if (t->hash(node) == hash) {
            ret = node;
            break;
        }
    }

    return ret;
}

struct hlist_node *dual_hash_table_find_using(dual_hash_table_t *__restrict t,
                    const unsigned int hash)
{
    if (likely(t))
        return dual_hash_table_find(t, hash, t->using_index);

    return NULL;
}

struct hlist_node *dual_hash_table_find_last(dual_hash_table_t *__restrict t,
                    const unsigned int hash)
{
    if (likely(t))
        return dual_hash_table_find(t, hash, t->using_index ^ 1);

    return NULL;
}

static void dual_hash_table_clean(dual_hash_table_t *__restrict t, const unsigned int i)
{
    unsigned int index;
    struct hlist_node *tmp;
    struct hlist_node *node;
    struct hlist_head *table;
    struct hlist_head *head;

    table = t->table[i];
    for (index = 0; !hash_oob(index, t->index_mask); index++) {
        head = table + index;
        hlist_for_each_safe(node, tmp, head)
            t->release(node);
        INIT_HLIST_HEAD(head);
    }
}

void dual_hash_table_clean_using(dual_hash_table_t *__restrict t)
{
    if (likely(t))
        dual_hash_table_clean(t, t->using_index);
}

void dual_hash_table_clean_last(dual_hash_table_t *__restrict t)
{
    if (likely(t))
        dual_hash_table_clean(t, t->using_index ^ 1);
}

void dual_hash_table_destory(dual_hash_table_t *__restrict t)
{
    if (likely(t)) {
        dual_hash_table_clean(t, t->using_index);
        dual_hash_table_clean(t, t->using_index ^ 1);
        kfree(t);
    }
}

static void dual_hash_table_trave(const dual_hash_table_t *__restrict t,
        const hash_table_trave_func_t fn, struct seq_file *__restrict file, void *__restrict arg,
        unsigned int i)
{
    unsigned int index;
    struct hlist_node *node;
    struct hlist_head *table;
    struct hlist_head *head;

    if (!t || !fn)
        return;

    table = t->table[i];
    for (index = 0; !hash_oob(index, t->index_mask); index++) {
        head = table + index;
        hlist_for_each(node, head)
            fn(node, file, arg);
    }
}

void dual_hash_table_trave_last(const dual_hash_table_t *__restrict hash_table,
        const hash_table_trave_func_t fn, struct seq_file *__restrict file, void *__restrict arg)
{
    dual_hash_table_trave(hash_table, fn, file, arg, hash_table->using_index ^ 1);
}

void dual_hash_table_trave_using(const dual_hash_table_t *__restrict hash_table,
        const hash_table_trave_func_t fn, struct seq_file *__restrict file, void *__restrict arg)
{
    dual_hash_table_trave(hash_table, fn, file, arg, hash_table->using_index);
}
