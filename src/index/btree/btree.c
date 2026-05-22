#include "btree.h"

#include "mem/allocator.h"
#include "util/log.h"

#include <pthread.h>
#include <string.h>

/* batas isi node */
#define LEAF_MAX BTREE_ORDER
#define INTR_MAX (BTREE_ORDER - 1)
#define HALF (BTREE_ORDER / 2)

/* tinggi pohon maksimum — log32(2^64) < 13, jadi 20 lebih dari cukup */
#define MAX_HEIGHT 20

typedef enum { NLEAF, NINTR } nkind_t;

typedef struct lnode lnode_t;
typedef struct inode inode_t;

/* node daun: menyimpan pasangan kunci-nilai */
struct lnode {
    nkind_t        type;
    int            n;
    fastkv_slice_t k[LEAF_MAX];
    fastkv_slice_t v[LEAF_MAX];
    lnode_t       *prev;
    lnode_t       *next;
};

/* node internal: hanya menyimpan kunci pemisah dan pointer ke anak */
struct inode {
    nkind_t        type;
    int            n;
    fastkv_slice_t k[INTR_MAX];
    void          *c[BTREE_ORDER];
};

struct fastkv_btree {
    void            *root;
    lnode_t         *leftmost;
    pthread_rwlock_t lock;
};

/* bandingkan dua slice secara leksikografis */
static int cmp(fastkv_slice_t a, fastkv_slice_t b) {
    size_t n = a.len < b.len ? a.len : b.len;
    int    r = memcmp(a.data, b.data, n);
    if (r)
        return r;
    return (a.len > b.len) - (a.len < b.len);
}

/* salin slice ke buffer baru (dimiliki pemanggil) */
static fastkv_slice_t sdup(fastkv_slice_t s) {
    if (!s.len || !s.data)
        return FASTKV_SLICE_NULL;
    void *p = fkv_malloc(s.len);
    if (!p)
        return FASTKV_SLICE_NULL;
    memcpy(p, s.data, s.len);
    return FASTKV_SLICE(p, s.len);
}

static void sfree(fastkv_slice_t s) {
    fkv_free((void *)s.data);
}

static lnode_t *lnode_new(void) {
    lnode_t *l = fkv_malloc(sizeof(*l));
    if (!l)
        return NULL;
    l->type = NLEAF;
    l->n    = 0;
    l->prev = l->next = NULL;
    return l;
}

static inode_t *inode_new(void) {
    inode_t *p = fkv_malloc(sizeof(*p));
    if (!p)
        return NULL;
    p->type = NINTR;
    p->n    = 0;
    return p;
}

static void node_free_recursive(void *node) {
    if (!node)
        return;
    if (*(nkind_t *)node == NLEAF) {
        lnode_t *l = node;
        for (int i = 0; i < l->n; i++) {
            sfree(l->k[i]);
            sfree(l->v[i]);
        }
        fkv_free(l);
    } else {
        inode_t *p = node;
        for (int i = 0; i <= p->n; i++)
            node_free_recursive(p->c[i]);
        for (int i = 0; i < p->n; i++)
            sfree(p->k[i]);
        fkv_free(p);
    }
}

/* cari indeks pertama di daun l dimana k[i] >= key */
static int leaf_lower(lnode_t *l, fastkv_slice_t key) {
    int lo = 0, hi = l->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (cmp(l->k[mid], key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* cari indeks anak yang tepat di node internal */
static int intr_child_idx(inode_t *p, fastkv_slice_t key) {
    int lo = 0, hi = p->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (cmp(p->k[mid], key) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* hasil split: kunci pemisah dan node kanan baru */
typedef struct {
    fastkv_slice_t sep;
    void          *right;
} split_t;

static const split_t NO_SPLIT = {{NULL, 0}, NULL};

/* sisipkan ke daun, kembalikan split jika penuh */
static split_t leaf_insert(lnode_t *l, fastkv_slice_t key, fastkv_slice_t val) {
    int i = leaf_lower(l, key);

    /* kunci sudah ada — perbarui nilai */
    if (i < l->n && cmp(l->k[i], key) == 0) {
        sfree(l->v[i]);
        l->v[i] = sdup(val);
        return NO_SPLIT;
    }

    if (l->n < LEAF_MAX) {
        memmove(&l->k[i + 1], &l->k[i], (size_t)(l->n - i) * sizeof(fastkv_slice_t));
        memmove(&l->v[i + 1], &l->v[i], (size_t)(l->n - i) * sizeof(fastkv_slice_t));
        l->k[i] = sdup(key);
        l->v[i] = sdup(val);
        l->n++;
        return NO_SPLIT;
    }

    /* daun penuh — bagi jadi dua */
    fastkv_slice_t tk[LEAF_MAX + 1];
    fastkv_slice_t tv[LEAF_MAX + 1];
    int            total = 0;

    for (int j = 0; j < l->n; j++) {
        if (j == i) {
            tk[total] = sdup(key);
            tv[total] = sdup(val);
            total++;
        }
        tk[total] = l->k[j];
        tv[total] = l->v[j];
        total++;
    }
    if (i == l->n) {
        tk[total] = sdup(key);
        tv[total] = sdup(val);
        total++;
    }

    lnode_t *r = lnode_new();
    if (!r)
        return NO_SPLIT;

    int left_n = total / 2;
    l->n       = left_n;
    for (int j = 0; j < left_n; j++) {
        l->k[j] = tk[j];
        l->v[j] = tv[j];
    }

    r->n = total - left_n;
    for (int j = 0; j < r->n; j++) {
        r->k[j] = tk[left_n + j];
        r->v[j] = tv[left_n + j];
    }

    /* sambung ke linked list */
    r->next = l->next;
    r->prev = l;
    if (l->next)
        l->next->prev = r;
    l->next = r;

    split_t s = {sdup(r->k[0]), r};
    return s;
}

/* deklarasi maju */
static split_t node_insert(void *node, fastkv_slice_t key, fastkv_slice_t val);

/* sisipkan ke node internal, propagasi split dari bawah */
static split_t intr_insert(inode_t *p, fastkv_slice_t key, fastkv_slice_t val) {
    int     ci = intr_child_idx(p, key);
    split_t s  = node_insert(p->c[ci], key, val);

    if (!s.right)
        return NO_SPLIT;

    /* anak ci baru saja split — sisipkan pemisah baru */
    if (p->n < INTR_MAX) {
        memmove(&p->k[ci + 1], &p->k[ci], (size_t)(p->n - ci) * sizeof(fastkv_slice_t));
        memmove(&p->c[ci + 2], &p->c[ci + 1], (size_t)(p->n - ci) * sizeof(void *));
        p->k[ci]     = s.sep;
        p->c[ci + 1] = s.right;
        p->n++;
        return NO_SPLIT;
    }

    /* node internal penuh — bagi dua, kunci tengah naik ke parent */
    fastkv_slice_t tk[INTR_MAX + 1];
    void          *tc[BTREE_ORDER + 1];

    for (int j = 0; j < ci; j++) {
        tk[j] = p->k[j];
        tc[j] = p->c[j];
    }
    tk[ci]     = s.sep;
    tc[ci]     = p->c[ci];
    tc[ci + 1] = s.right;
    for (int j = ci; j < p->n; j++) {
        tk[j + 1] = p->k[j];
        tc[j + 2] = p->c[j + 1];
    }

    int            mid    = p->n / 2;
    fastkv_slice_t median = tk[mid];

    inode_t *r = inode_new();
    if (!r)
        return NO_SPLIT;

    p->n = mid;
    for (int j = 0; j < mid; j++) {
        p->k[j] = tk[j];
        p->c[j] = tc[j];
    }
    p->c[mid] = tc[mid];

    r->n = p->n; /* simetris: kanan dapat jumlah kunci yang sama */
    /* koreksi: kanan dapat sisa setelah median */
    r->n = (INTR_MAX - mid);
    for (int j = 0; j < r->n; j++) {
        r->k[j] = tk[mid + 1 + j];
        r->c[j] = tc[mid + 1 + j];
    }
    r->c[r->n] = tc[mid + 1 + r->n];

    split_t ret = {median, r};
    return ret;
}

static split_t node_insert(void *node, fastkv_slice_t key, fastkv_slice_t val) {
    if (*(nkind_t *)node == NLEAF)
        return leaf_insert(node, key, val);
    return intr_insert(node, key, val);
}

/* telusuri ke daun yang berisi key */
static lnode_t *find_leaf(void *root, fastkv_slice_t key) {
    void *node = root;
    while (*(nkind_t *)node == NINTR) {
        inode_t *p = node;
        node       = p->c[intr_child_idx(p, key)];
    }
    return node;
}

/* API publik */

fastkv_err_t fastkv_btree_create(fastkv_btree_t **out) {
    fastkv_btree_t *t = fkv_malloc(sizeof(*t));
    if (!t)
        return FASTKV_ERR_NOMEM;

    lnode_t *root_leaf = lnode_new();
    if (!root_leaf) {
        fkv_free(t);
        return FASTKV_ERR_NOMEM;
    }

    t->root     = root_leaf;
    t->leftmost = root_leaf;
    pthread_rwlock_init(&t->lock, NULL);
    *out = t;
    return FASTKV_OK;
}

void fastkv_btree_destroy(fastkv_btree_t *t) {
    if (!t)
        return;
    node_free_recursive(t->root);
    pthread_rwlock_destroy(&t->lock);
    fkv_free(t);
}

fastkv_err_t fastkv_btree_insert(fastkv_btree_t *t, fastkv_slice_t key, fastkv_slice_t val) {
    pthread_rwlock_wrlock(&t->lock);

    split_t s = node_insert(t->root, key, val);

    if (s.right) {
        /* root split — buat root baru */
        inode_t *newroot = inode_new();
        if (!newroot) {
            sfree(s.sep);
            pthread_rwlock_unlock(&t->lock);
            return FASTKV_ERR_NOMEM;
        }
        newroot->n    = 1;
        newroot->k[0] = s.sep;
        newroot->c[0] = t->root;
        newroot->c[1] = s.right;
        t->root       = newroot;
    }

    pthread_rwlock_unlock(&t->lock);
    return FASTKV_OK;
}

fastkv_err_t fastkv_btree_delete(fastkv_btree_t *t, fastkv_slice_t key) {
    pthread_rwlock_wrlock(&t->lock);

    lnode_t *l = find_leaf(t->root, key);
    int      i = leaf_lower(l, key);

    if (i >= l->n || cmp(l->k[i], key) != 0) {
        pthread_rwlock_unlock(&t->lock);
        return FASTKV_ERR_NOTFOUND;
    }

    sfree(l->k[i]);
    sfree(l->v[i]);
    memmove(&l->k[i], &l->k[i + 1], (size_t)(l->n - i - 1) * sizeof(fastkv_slice_t));
    memmove(&l->v[i], &l->v[i + 1], (size_t)(l->n - i - 1) * sizeof(fastkv_slice_t));
    l->n--;

    pthread_rwlock_unlock(&t->lock);
    return FASTKV_OK;
}

fastkv_err_t fastkv_btree_get(fastkv_btree_t *t, fastkv_slice_t key, fastkv_slice_t *out) {
    pthread_rwlock_rdlock(&t->lock);

    lnode_t *l = find_leaf(t->root, key);
    int      i = leaf_lower(l, key);

    if (i < l->n && cmp(l->k[i], key) == 0) {
        *out = l->v[i];
        pthread_rwlock_unlock(&t->lock);
        return FASTKV_OK;
    }

    pthread_rwlock_unlock(&t->lock);
    return FASTKV_ERR_NOTFOUND;
}

fastkv_err_t fastkv_btree_scan(fastkv_btree_t *t, fastkv_slice_t min, fastkv_slice_t max,
    fastkv_cursor_dir_t dir, fastkv_btree_scan_cb cb, void *udata) {
    pthread_rwlock_rdlock(&t->lock);
    fastkv_err_t rc = FASTKV_OK;

    if (dir == FASTKV_CURSOR_FORWARD) {
        lnode_t *l;
        int      start;

        if (min.data) {
            l     = find_leaf(t->root, min);
            start = leaf_lower(l, min);
        } else {
            l     = t->leftmost;
            start = 0;
        }

        while (l) {
            for (int i = start; i < l->n; i++) {
                if (max.data && cmp(l->k[i], max) > 0)
                    goto done;
                rc = cb(l->k[i], l->v[i], udata);
                if (rc != FASTKV_OK)
                    goto done;
            }
            l     = l->next;
            start = 0;
        }
    } else {
        /* mundur: mulai dari max, kiri ke kanan */
        lnode_t *l;
        int      start;

        if (max.data) {
            l     = find_leaf(t->root, max);
            int i = leaf_lower(l, max);
            /* sertakan max itu sendiri jika ada */
            start = (i < l->n && cmp(l->k[i], max) == 0) ? i : i - 1;
        } else {
            /* mulai dari daun paling kanan */
            void *node = t->root;
            while (*(nkind_t *)node == NINTR) {
                inode_t *p = node;
                node       = p->c[p->n];
            }
            l     = node;
            start = l->n - 1;
        }

        while (l) {
            for (int i = start; i >= 0; i--) {
                if (min.data && cmp(l->k[i], min) < 0)
                    goto done;
                rc = cb(l->k[i], l->v[i], udata);
                if (rc != FASTKV_OK)
                    goto done;
            }
            l = l->prev;
            if (l)
                start = l->n - 1;
        }
    }

done:
    pthread_rwlock_unlock(&t->lock);
    return rc == FASTKV_ERR_CURSOR_EOF ? FASTKV_OK : rc;
}
