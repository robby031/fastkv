#include "rtree.h"

#include "mem/allocator.h"
#include "util/log.h"

#include <float.h>
#include <math.h>
#include <string.h>

/* max dan min entri per node (Guttman 1984 merekomendasikan m >= 2, M >= 2m) */
#define RT_M 9
#define RT_m 3

typedef struct rnode rnode_t;

/* satu entri dalam node: MBR + pointer anak (internal) atau kunci (daun) */
typedef struct {
    fastkv_coord_t min[RTREE_MAX_DIMS];
    fastkv_coord_t max[RTREE_MAX_DIMS];
    union {
        rnode_t *child;
        struct {
            uint8_t *data;
            size_t   len;
        } key;
    };
} rentry_t;

struct rnode {
    bool     is_leaf;
    int      n;
    rentry_t e[RT_M];
};

struct fastkv_rtree {
    uint8_t          ndims;
    rnode_t         *root;
    pthread_rwlock_t lock;
};

/* luas MBR dalam ndims dimensi */
static double rect_area(const fastkv_coord_t *mn, const fastkv_coord_t *mx, int ndims) {
    double a = 1.0;
    for (int i = 0; i < ndims; i++) {
        double d = mx[i] - mn[i];
        if (d <= 0)
            return 0.0;
        a *= d;
    }
    return a;
}

/* perbesar MBR dst agar mencakup src */
static void rect_expand(fastkv_coord_t *dst_mn, fastkv_coord_t *dst_mx,
    const fastkv_coord_t *src_mn, const fastkv_coord_t *src_mx, int ndims) {
    for (int i = 0; i < ndims; i++) {
        if (src_mn[i] < dst_mn[i])
            dst_mn[i] = src_mn[i];
        if (src_mx[i] > dst_mx[i])
            dst_mx[i] = src_mx[i];
    }
}

/* hitung luas MBR jika diperbesar mencakup entry e */
static double rect_enlarged_area(
    const fastkv_coord_t *mn, const fastkv_coord_t *mx, const rentry_t *e, int ndims) {
    fastkv_coord_t nmn[RTREE_MAX_DIMS];
    fastkv_coord_t nmx[RTREE_MAX_DIMS];
    for (int i = 0; i < ndims; i++) {
        nmn[i] = mn[i] < e->min[i] ? mn[i] : e->min[i];
        nmx[i] = mx[i] > e->max[i] ? mx[i] : e->max[i];
    }
    return rect_area(nmn, nmx, ndims);
}

/* apakah r sepenuhnya di dalam bounds? */
static bool rect_within(const rentry_t *r, const fastkv_rect_t *b, int ndims) {
    for (int i = 0; i < ndims; i++) {
        if (r->min[i] < b->min[i] || r->max[i] > b->max[i])
            return false;
    }
    return true;
}

/* apakah r berpotongan dengan bounds? */
static bool rect_intersects(const rentry_t *r, const fastkv_rect_t *b, int ndims) {
    for (int i = 0; i < ndims; i++) {
        if (r->max[i] < b->min[i] || r->min[i] > b->max[i])
            return false;
    }
    return true;
}

/* jarak kuadrat minimal dari titik pt ke MBR entry */
static double mindist_sq(const fastkv_coord_t *pt, const rentry_t *e, int ndims) {
    double d = 0;
    for (int i = 0; i < ndims; i++) {
        double v = pt[i];
        if (v < e->min[i])
            v = e->min[i];
        else if (v > e->max[i])
            v = e->max[i];
        double diff = pt[i] - v;
        d += diff * diff;
    }
    return d;
}

static rnode_t *rnode_new(bool is_leaf) {
    rnode_t *n = fkv_malloc(sizeof(*n));
    if (!n)
        return NULL;
    n->is_leaf = is_leaf;
    n->n       = 0;
    return n;
}

static void rnode_free_recursive(rnode_t *n) {
    if (!n)
        return;
    if (!n->is_leaf) {
        for (int i = 0; i < n->n; i++)
            rnode_free_recursive(n->e[i].child);
    } else {
        for (int i = 0; i < n->n; i++)
            fkv_free(n->e[i].key.data);
    }
    fkv_free(n);
}

/* hitung MBR dari semua entri dalam node */
static void node_recompute_mbr(rnode_t *n, fastkv_coord_t *mn, fastkv_coord_t *mx, int ndims) {
    for (int i = 0; i < ndims; i++) {
        mn[i] = DBL_MAX;
        mx[i] = -DBL_MAX;
    }
    for (int i = 0; i < n->n; i++)
        rect_expand(mn, mx, n->e[i].min, n->e[i].max, ndims);
}

/* pilih anak terbaik untuk memasukkan entry baru (minimum area enlargement) */
static int choose_child(rnode_t *p, const rentry_t *e, int ndims) {
    int    best         = 0;
    double best_enlarge = DBL_MAX;
    double best_area    = DBL_MAX;

    for (int i = 0; i < p->n; i++) {
        double orig     = rect_area(p->e[i].min, p->e[i].max, ndims);
        double enlarged = rect_enlarged_area(p->e[i].min, p->e[i].max, e, ndims);
        double enlarge  = enlarged - orig;
        if (enlarge < best_enlarge || (enlarge == best_enlarge && orig < best_area)) {
            best         = i;
            best_enlarge = enlarge;
            best_area    = orig;
        }
    }
    return best;
}

/* linear split: bagi node menjadi dua */
static void linear_split(rnode_t *full, rentry_t *new_e, rnode_t *left, rnode_t *right, int ndims) {
    /* kumpulkan semua RT_M+1 entri */
    rentry_t all[RT_M + 1];
    for (int i = 0; i < full->n; i++)
        all[i] = full->e[i];
    all[full->n] = *new_e;
    int total    = full->n + 1;

    /* cari dua benih: pasangan yang paling "ekstrem" per dimensi */
    /* versi sederhana: ambil entri pertama dan terakhir sebagai benih */
    int seed1 = 0, seed2 = total - 1;

    /* cari pasangan yang paling jauh (benih linear) */
    double worst_waste = -DBL_MAX;
    for (int i = 0; i < total - 1; i++) {
        for (int j = i + 1; j < total; j++) {
            fastkv_coord_t mn[RTREE_MAX_DIMS], mx[RTREE_MAX_DIMS];
            for (int d = 0; d < ndims; d++) {
                mn[d] = all[i].min[d] < all[j].min[d] ? all[i].min[d] : all[j].min[d];
                mx[d] = all[i].max[d] > all[j].max[d] ? all[i].max[d] : all[j].max[d];
            }
            double combined = rect_area(mn, mx, ndims);
            double waste    = combined - rect_area(all[i].min, all[i].max, ndims) -
                              rect_area(all[j].min, all[j].max, ndims);
            if (waste > worst_waste) {
                worst_waste = waste;
                seed1       = i;
                seed2       = j;
            }
        }
    }

    left->n              = 0;
    right->n             = 0;
    left->e[left->n++]   = all[seed1];
    right->e[right->n++] = all[seed2];

    bool used[RT_M + 1] = {false};
    used[seed1] = used[seed2] = true;

    /* Hitung MBR awal dua grup */
    fastkv_coord_t lmn[RTREE_MAX_DIMS], lmx[RTREE_MAX_DIMS];
    fastkv_coord_t rmn[RTREE_MAX_DIMS], rmx[RTREE_MAX_DIMS];
    for (int d = 0; d < ndims; d++) {
        lmn[d] = all[seed1].min[d];
        lmx[d] = all[seed1].max[d];
        rmn[d] = all[seed2].min[d];
        rmx[d] = all[seed2].max[d];
    }

    for (int assigned = 2; assigned < total; assigned++) {
        /* pastikan minimum isi terpenuhi */
        int remaining = total - assigned;
        if (left->n + remaining == RT_m) {
            /* semua sisa harus ke kiri */
            for (int i = 0; i < total; i++) {
                if (!used[i]) {
                    left->e[left->n++] = all[i];
                    used[i]            = true;
                }
            }
            break;
        }
        if (right->n + remaining == RT_m) {
            for (int i = 0; i < total; i++) {
                if (!used[i]) {
                    right->e[right->n++] = all[i];
                    used[i]              = true;
                }
            }
            break;
        }

        /* pilih entry berikutnya: ke grup mana perluasannya lebih kecil */
        int    pick      = -1;
        double best_diff = -DBL_MAX;
        for (int i = 0; i < total; i++) {
            if (used[i])
                continue;
            double dl   = rect_enlarged_area(lmn, lmx, &all[i], ndims) - rect_area(lmn, lmx, ndims);
            double dr   = rect_enlarged_area(rmn, rmx, &all[i], ndims) - rect_area(rmn, rmx, ndims);
            double diff = fabs(dl - dr);
            if (diff > best_diff) {
                best_diff = diff;
                pick      = i;
            }
        }

        double dl = rect_enlarged_area(lmn, lmx, &all[pick], ndims) - rect_area(lmn, lmx, ndims);
        double dr = rect_enlarged_area(rmn, rmx, &all[pick], ndims) - rect_area(rmn, rmx, ndims);
        if (dl < dr || (dl == dr && left->n <= right->n)) {
            left->e[left->n++] = all[pick];
            rect_expand(lmn, lmx, all[pick].min, all[pick].max, ndims);
        } else {
            right->e[right->n++] = all[pick];
            rect_expand(rmn, rmx, all[pick].min, all[pick].max, ndims);
        }
        used[pick] = true;
    }
}

/*
 * Sisipkan entry baru ke subtree yang di-root oleh node.
 * Jika node meluap, isi split_out dan kembalikan true.
 * split_out.e[0] = MBR node kiri (node itu sendiri setelah split)
 * split_out.e[1] = node kanan baru
 */
typedef struct {
    rentry_t left_entry; /* MBR baru untuk node kiri setelah split */
    rnode_t *right;      /* node kanan baru, NULL jika tidak split */
} rsplit_t;

static rsplit_t rtree_insert_rec(rnode_t *node, rentry_t *e, int ndims);

static rsplit_t rtree_insert_leaf(rnode_t *leaf, rentry_t *e, int ndims) {
    rsplit_t result = {.right = NULL};

    if (leaf->n < RT_M) {
        leaf->e[leaf->n++] = *e;
        node_recompute_mbr(leaf, result.left_entry.min, result.left_entry.max, ndims);
        return result;
    }

    /* node penuh — split */
    rnode_t *left_tmp = rnode_new(true);
    rnode_t *right    = rnode_new(true);
    if (!left_tmp || !right) {
        fkv_free(left_tmp);
        fkv_free(right);
        return result;
    }
    left_tmp->is_leaf = right->is_leaf = true;
    linear_split(leaf, e, left_tmp, right, ndims);

    /* salin hasil split ke leaf yang ada */
    leaf->n = left_tmp->n;
    for (int i = 0; i < left_tmp->n; i++)
        leaf->e[i] = left_tmp->e[i];
    fkv_free(left_tmp);

    node_recompute_mbr(leaf, result.left_entry.min, result.left_entry.max, ndims);
    result.right = right;
    return result;
}

static rsplit_t rtree_insert_internal(rnode_t *node, rentry_t *e, int ndims) {
    rsplit_t result       = {.right = NULL};
    int      ci           = choose_child(node, e, ndims);
    rsplit_t child_result = rtree_insert_rec(node->e[ci].child, e, ndims);

    /* perbarui MBR anak ci */
    for (int d = 0; d < ndims; d++) {
        node->e[ci].min[d] = child_result.left_entry.min[d];
        node->e[ci].max[d] = child_result.left_entry.max[d];
    }

    if (!child_result.right) {
        node_recompute_mbr(node, result.left_entry.min, result.left_entry.max, ndims);
        return result;
    }

    /* anak split — tambahkan node kanan baru ke node ini */
    rentry_t new_child_entry;
    node_recompute_mbr(child_result.right, new_child_entry.min, new_child_entry.max, ndims);
    new_child_entry.child = child_result.right;

    if (node->n < RT_M) {
        node->e[node->n++] = new_child_entry;
        node_recompute_mbr(node, result.left_entry.min, result.left_entry.max, ndims);
        return result;
    }

    /* node internal penuh — split */
    rnode_t *left_tmp = rnode_new(false);
    rnode_t *right    = rnode_new(false);
    if (!left_tmp || !right) {
        fkv_free(left_tmp);
        fkv_free(right);
        return result;
    }
    linear_split(node, &new_child_entry, left_tmp, right, ndims);

    node->n = left_tmp->n;
    for (int i = 0; i < left_tmp->n; i++)
        node->e[i] = left_tmp->e[i];
    fkv_free(left_tmp);

    node_recompute_mbr(node, result.left_entry.min, result.left_entry.max, ndims);
    result.right = right;
    return result;
}

static rsplit_t rtree_insert_rec(rnode_t *node, rentry_t *e, int ndims) {
    if (node->is_leaf)
        return rtree_insert_leaf(node, e, ndims);
    return rtree_insert_internal(node, e, ndims);
}

/* hapus entry dari subtree — kembalikan true jika berhasil ditemukan */
static bool rtree_delete_rec(
    rnode_t *node, const fastkv_rect_t *rect, const uint8_t *key_data, size_t key_len, int ndims) {
    if (node->is_leaf) {
        for (int i = 0; i < node->n; i++) {
            if (node->e[i].key.len != key_len)
                continue;
            if (memcmp(node->e[i].key.data, key_data, key_len) != 0)
                continue;
            /* cocok — hapus */
            fkv_free(node->e[i].key.data);
            node->e[i] = node->e[--node->n];
            return true;
        }
        return false;
    }

    fastkv_rect_t bounds = *rect;
    for (int i = 0; i < node->n; i++) {
        if (!rect_intersects(&node->e[i], &bounds, ndims))
            continue;
        if (rtree_delete_rec(node->e[i].child, rect, key_data, key_len, ndims)) {
            /* perbarui MBR anak */
            node_recompute_mbr(node->e[i].child, node->e[i].min, node->e[i].max, ndims);
            return true;
        }
    }
    return false;
}

/* query rekursif */
static fastkv_err_t rtree_within_rec(
    rnode_t *node, const fastkv_rect_t *b, fastkv_rtree_cb cb, void *udata, int ndims) {
    for (int i = 0; i < node->n; i++) {
        if (!rect_intersects(&node->e[i], b, ndims))
            continue;
        if (node->is_leaf) {
            if (!rect_within(&node->e[i], b, ndims))
                continue;
            fastkv_rect_t r;
            r.ndims = (uint8_t)ndims;
            for (int d = 0; d < ndims; d++) {
                r.min[d] = node->e[i].min[d];
                r.max[d] = node->e[i].max[d];
            }
            fastkv_slice_t k  = FASTKV_SLICE(node->e[i].key.data, node->e[i].key.len);
            fastkv_err_t   rc = cb(r, k, udata);
            if (rc != FASTKV_OK)
                return rc;
        } else {
            fastkv_err_t rc = rtree_within_rec(node->e[i].child, b, cb, udata, ndims);
            if (rc != FASTKV_OK)
                return rc;
        }
    }
    return FASTKV_OK;
}

static fastkv_err_t rtree_intersects_rec(
    rnode_t *node, const fastkv_rect_t *b, fastkv_rtree_cb cb, void *udata, int ndims) {
    for (int i = 0; i < node->n; i++) {
        if (!rect_intersects(&node->e[i], b, ndims))
            continue;
        if (node->is_leaf) {
            fastkv_rect_t r;
            r.ndims = (uint8_t)ndims;
            for (int d = 0; d < ndims; d++) {
                r.min[d] = node->e[i].min[d];
                r.max[d] = node->e[i].max[d];
            }
            fastkv_slice_t k  = FASTKV_SLICE(node->e[i].key.data, node->e[i].key.len);
            fastkv_err_t   rc = cb(r, k, udata);
            if (rc != FASTKV_OK)
                return rc;
        } else {
            fastkv_err_t rc = rtree_intersects_rec(node->e[i].child, b, cb, udata, ndims);
            if (rc != FASTKV_OK)
                return rc;
        }
    }
    return FASTKV_OK;
}

/* min-heap sederhana untuk nearby — menyimpan (jarak, node atau entry daun) */
typedef struct {
    double   dist;
    bool     is_leaf_entry;
    rnode_t *node;   /* node internal */
    rentry_t leaf_e; /* entry daun */
} heap_item_t;

typedef struct {
    heap_item_t *data;
    int          n, cap;
} minheap_t;

static bool heap_push(minheap_t *h, heap_item_t item) {
    if (h->n == h->cap) {
        int          newcap = h->cap ? h->cap * 2 : 16;
        heap_item_t *nd     = fkv_malloc((size_t)newcap * sizeof(heap_item_t));
        if (!nd)
            return false;
        if (h->data) {
            memcpy(nd, h->data, (size_t)h->n * sizeof(heap_item_t));
            fkv_free(h->data);
        }
        h->data = nd;
        h->cap  = newcap;
    }
    int i      = h->n++;
    h->data[i] = item;
    /* swim up */
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->data[parent].dist <= h->data[i].dist)
            break;
        heap_item_t tmp = h->data[parent];
        h->data[parent] = h->data[i];
        h->data[i]      = tmp;
        i               = parent;
    }
    return true;
}

static heap_item_t heap_pop(minheap_t *h) {
    heap_item_t top = h->data[0];
    h->data[0]      = h->data[--h->n];
    /* sink down */
    int i = 0;
    while (true) {
        int l = 2 * i + 1, r = 2 * i + 2, smallest = i;
        if (l < h->n && h->data[l].dist < h->data[smallest].dist)
            smallest = l;
        if (r < h->n && h->data[r].dist < h->data[smallest].dist)
            smallest = r;
        if (smallest == i)
            break;
        heap_item_t tmp   = h->data[smallest];
        h->data[smallest] = h->data[i];
        h->data[i]        = tmp;
        i                 = smallest;
    }
    return top;
}

/* API publik */

fastkv_err_t fastkv_rtree_create(fastkv_rtree_t **out, uint8_t ndims) {
    if (!out || ndims == 0 || ndims > RTREE_MAX_DIMS)
        return FASTKV_ERR_INVAL;

    fastkv_rtree_t *t = fkv_malloc(sizeof(*t));
    if (!t)
        return FASTKV_ERR_NOMEM;

    t->root = rnode_new(true);
    if (!t->root) {
        fkv_free(t);
        return FASTKV_ERR_NOMEM;
    }

    t->ndims = ndims;
    pthread_rwlock_init(&t->lock, NULL);
    *out = t;
    return FASTKV_OK;
}

void fastkv_rtree_destroy(fastkv_rtree_t *t) {
    if (!t)
        return;
    rnode_free_recursive(t->root);
    pthread_rwlock_destroy(&t->lock);
    fkv_free(t);
}

fastkv_err_t fastkv_rtree_insert(fastkv_rtree_t *t, fastkv_rect_t rect, fastkv_slice_t key) {
    if (!t || !key.data)
        return FASTKV_ERR_INVAL;

    uint8_t *kdata = fkv_malloc(key.len);
    if (!kdata)
        return FASTKV_ERR_NOMEM;
    memcpy(kdata, key.data, key.len);

    rentry_t e;
    for (int i = 0; i < t->ndims; i++) {
        e.min[i] = rect.min[i];
        e.max[i] = rect.max[i];
    }
    e.key.data = kdata;
    e.key.len  = key.len;

    pthread_rwlock_wrlock(&t->lock);

    rsplit_t s = rtree_insert_rec(t->root, &e, t->ndims);

    if (s.right) {
        /* root split — buat root baru */
        rnode_t *newroot = rnode_new(false);
        if (!newroot) {
            rnode_free_recursive(s.right);
            pthread_rwlock_unlock(&t->lock);
            return FASTKV_ERR_NOMEM;
        }

        rentry_t left_e, right_e;
        node_recompute_mbr(t->root, left_e.min, left_e.max, t->ndims);
        node_recompute_mbr(s.right, right_e.min, right_e.max, t->ndims);
        left_e.child  = t->root;
        right_e.child = s.right;

        newroot->e[0] = left_e;
        newroot->e[1] = right_e;
        newroot->n    = 2;
        t->root       = newroot;
    }

    pthread_rwlock_unlock(&t->lock);
    return FASTKV_OK;
}

fastkv_err_t fastkv_rtree_delete(fastkv_rtree_t *t, fastkv_rect_t rect, fastkv_slice_t key) {
    if (!t || !key.data)
        return FASTKV_ERR_INVAL;

    pthread_rwlock_wrlock(&t->lock);
    bool found = rtree_delete_rec(t->root, &rect, key.data, key.len, t->ndims);
    pthread_rwlock_unlock(&t->lock);

    return found ? FASTKV_OK : FASTKV_ERR_NOTFOUND;
}

fastkv_err_t fastkv_rtree_within(
    fastkv_rtree_t *t, fastkv_rect_t bounds, fastkv_rtree_cb cb, void *udata) {
    if (!t || !cb)
        return FASTKV_ERR_INVAL;
    pthread_rwlock_rdlock(&t->lock);
    fastkv_err_t rc = rtree_within_rec(t->root, &bounds, cb, udata, t->ndims);
    pthread_rwlock_unlock(&t->lock);
    return rc;
}

fastkv_err_t fastkv_rtree_intersects(
    fastkv_rtree_t *t, fastkv_rect_t bounds, fastkv_rtree_cb cb, void *udata) {
    if (!t || !cb)
        return FASTKV_ERR_INVAL;
    pthread_rwlock_rdlock(&t->lock);
    fastkv_err_t rc = rtree_intersects_rec(t->root, &bounds, cb, udata, t->ndims);
    pthread_rwlock_unlock(&t->lock);
    return rc;
}

fastkv_err_t fastkv_rtree_nearby(
    fastkv_rtree_t *t, fastkv_coord_t *point, uint64_t limit, fastkv_rtree_cb cb, void *udata) {
    if (!t || !point || !cb)
        return FASTKV_ERR_INVAL;

    pthread_rwlock_rdlock(&t->lock);

    minheap_t    heap  = {NULL, 0, 0};
    fastkv_err_t rc    = FASTKV_OK;
    uint64_t     found = 0;

    /* mulai dari root */
    heap_item_t root_item = {
        .dist          = 0.0,
        .is_leaf_entry = false,
        .node          = t->root,
    };
    if (!heap_push(&heap, root_item)) {
        rc = FASTKV_ERR_NOMEM;
        goto done;
    }

    while (heap.n > 0 && found < limit) {
        heap_item_t item = heap_pop(&heap);

        if (item.is_leaf_entry) {
            fastkv_rect_t r;
            r.ndims = t->ndims;
            for (int d = 0; d < t->ndims; d++) {
                r.min[d] = item.leaf_e.min[d];
                r.max[d] = item.leaf_e.max[d];
            }
            fastkv_slice_t k = FASTKV_SLICE(item.leaf_e.key.data, item.leaf_e.key.len);
            rc               = cb(r, k, udata);
            if (rc != FASTKV_OK)
                goto done;
            found++;
            continue;
        }

        /* ekspansi node */
        rnode_t *node = item.node;
        for (int i = 0; i < node->n; i++) {
            double      d = mindist_sq(point, &node->e[i], t->ndims);
            heap_item_t child_item;
            child_item.dist = d;
            if (node->is_leaf) {
                child_item.is_leaf_entry = true;
                child_item.leaf_e        = node->e[i];
            } else {
                child_item.is_leaf_entry = false;
                child_item.node          = node->e[i].child;
            }
            if (!heap_push(&heap, child_item)) {
                rc = FASTKV_ERR_NOMEM;
                goto done;
            }
        }
    }

done:
    fkv_free(heap.data);
    pthread_rwlock_unlock(&t->lock);
    return rc;
}
