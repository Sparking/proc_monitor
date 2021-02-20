#include <linux/init.h>
#include <linux/log2.h>
#include "hash_table.h"

#define HASH_QUICK_MATH 1

#if defined (HASH_QUICK_MATH) && (HASH_QUICK_MATH == 1)
static inline unsigned int hash_table_mask(const unsigned int size)
{
    return size - 1;
}

static inline unsigned int hash_table_round_size(const unsigned int size)
{
    return roundup_pow_of_two(size);
}

static inline unsigned int hash_table_hash2index(const unsigned int size, const unsigned int hash)
{
    return hash & size;
}

#define hash_oob(index, size)    ((index) > (size))
#else
static inline unsigned int hash_table_size(const unsigned int size)
{
    return size;
}

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
                            const hash_table_key_fn_t hash,
                            const hash_table_release_fn_t release)
{
    unsigned int i;
    unsigned int table_size;
    dual_hash_table_t *table;
    struct hlist_head *head1, *head2;

    table_size = hash_table_round_size(size);
    table = (dual_hash_table_t *) kmalloc(table_size * sizeof(struct hlist_head) * 2
        + sizeof(*table), GFP_KERNEL);
    if (table) {
        table->using_index = 0;
        table->hash = hash;
        table->release = release;
        head1 = (void *) table + sizeof(*table);
        head2 = head1 + table_size;
        table->table[0] = head1;
        table->table[1] = head2;
        table_size = hash_table_mask(table_size);
        table->index_mask = table_size;
        for (i = 0; !hash_oob(i, table_size); i++) {
            INIT_HLIST_HEAD(head1 + i);
            INIT_HLIST_HEAD(head2 + i);
        }
    }

    return table;
}

void dual_hash_table_add(dual_hash_table_t *__restrict t, struct hlist_node *__restrict node,
        const unsigned int i)
{
    /* insert directly, no checking if has the same key */
    hlist_add_head(node, t->table[i] + hash_table_hash2index(t->index_mask, t->hash(node)));
}

struct hlist_node *dual_hash_table_find(dual_hash_table_t *__restrict t, const unsigned int hash,
                    const unsigned int i)
{
    unsigned int index;
    struct hlist_node *ret;
    struct hlist_node *node;
    struct hlist_head *head;

    ret = NULL;
    index = hash_table_hash2index(t->index_mask, hash);
    head = t->table[i] + index;
    hlist_for_each(node, head) {
        if (t->hash(node) == hash) {
            ret = node;
            break;
        }
    }

    return ret;
}

void dual_hash_table_clean(dual_hash_table_t *__restrict t, const unsigned int i)
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

void dual_hash_table_trave(const dual_hash_table_t *__restrict t, const hash_table_trave_fn_t fn,
        struct seq_file *__restrict file, void *__restrict arg, unsigned int i)
{
    unsigned int index;
    struct hlist_node *node;
    struct hlist_head *table;
    struct hlist_head *head;

    table = t->table[i];
    for (index = 0; !hash_oob(index, t->index_mask); index++) {
        head = table + index;
        hlist_for_each(node, head)
            fn(node, file, arg);
    }
}
