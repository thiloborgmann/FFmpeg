/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "config.h"

#include "internal.h"
#include "refstruct.h"

#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"

typedef struct RefCount {
    /**
     * An uintptr_t is big enough to hold the address of every reference,
     * so no overflow can happen when incrementing the refcount as long as
     * the user does not throw away references.
     */
    atomic_uintptr_t  refcount;
    FFRefStructOpaque opaque;
    FFRefStructUnrefCB free_cb;
    void (*free)(void *ref);
#if defined(ASSERT_LEVEL) && ASSERT_LEVEL >= 1
    unsigned flags;
#endif
} RefCount;

#if __STDC_VERSION__ >= 201112L
#define REFCOUNT_OFFSET FFALIGN(sizeof(RefCount), FFMAX3(STRIDE_ALIGN, 16, _Alignof(max_align_t)))
#else
#define REFCOUNT_OFFSET FFALIGN(sizeof(RefCount), FFMAX(STRIDE_ALIGN, 16))
#endif

static RefCount *get_refcount(void *obj)
{
    return (RefCount*)((char*)obj - REFCOUNT_OFFSET);
}

static const RefCount *cget_refcount(const void *data)
{
    return (const RefCount*)((const char*)data - REFCOUNT_OFFSET);
}

static void *get_userdata(void *buf)
{
    return (char*)buf + REFCOUNT_OFFSET;
}

static void refcount_init(RefCount *ref, FFRefStructOpaque opaque,
                          unsigned flags, FFRefStructUnrefCB free_cb)
{
    atomic_init(&ref->refcount, 1);
    ref->opaque  = opaque;
    ref->free_cb = free_cb;
    ref->free    = av_free;
#if defined(ASSERT_LEVEL) && ASSERT_LEVEL >= 1
    ref->flags = flags;
#endif
}

void *ff_refstruct_alloc_ext_c(size_t size, unsigned flags, FFRefStructOpaque opaque,
                               FFRefStructUnrefCB free_cb)
{
    void *buf, *obj;

    if (size > SIZE_MAX - REFCOUNT_OFFSET)
        return NULL;
    buf = av_malloc(size + REFCOUNT_OFFSET);
    if (!buf)
        return NULL;
    refcount_init(buf, opaque, flags, free_cb);
    obj = get_userdata(buf);
    if (!(flags & FF_REFSTRUCT_FLAG_NO_ZEROING))
        memset(obj, 0, size);

    return obj;
}

void *ff_refstruct_allocz(size_t size)
{
    return ff_refstruct_alloc_ext(size, 0, NULL, NULL);
}

void ff_refstruct_unref(void *objp)
{
    void *obj;
    RefCount *ref;

    memcpy(&obj, objp, sizeof(obj));
    if (!obj)
        return;
    memcpy(objp, &(void *){ NULL }, sizeof(obj));

    ref = get_refcount(obj);
    av_assert1(!(ref->flags & FF_REFSTRUCT_FLAG_DYNAMIC_OPAQUE));
    if (atomic_fetch_sub_explicit(&ref->refcount, 1, memory_order_acq_rel) == 1) {
        if (ref->free_cb.unref)
            ref->free_cb.unref(ref->opaque, obj);
        ref->free(ref);
    }

    return;
}

void ff_refstruct_unref_ext_c(FFRefStructOpaque opaque, void *objp)
{
    void *obj;
    RefCount *ref;

    memcpy(&obj, objp, sizeof(obj));
    if (!obj)
        return;
    memcpy(objp, &(void *){ NULL }, sizeof(obj));

    ref = get_refcount(obj);
    av_assert1(ref->flags & FF_REFSTRUCT_FLAG_DYNAMIC_OPAQUE);
    if (atomic_fetch_sub_explicit(&ref->refcount, 1, memory_order_acq_rel) == 1) {
        if (ref->free_cb.unref_ext)
            ref->free_cb.unref_ext(opaque, ref->opaque, obj);
        ref->free(ref);
    }

    return;
}

void *ff_refstruct_ref(void *obj)
{
    RefCount *ref = get_refcount(obj);

    atomic_fetch_add_explicit(&ref->refcount, 1, memory_order_relaxed);

    return obj;
}

const void *ff_refstruct_ref_c(const void *obj)
{
    /* Casting const away here is fine, as it is only supposed
     * to apply to the user's data and not our bookkeeping data. */
    RefCount *ref = get_refcount((void*)obj);

    atomic_fetch_add_explicit(&ref->refcount, 1, memory_order_relaxed);

    return obj;
}

void ff_refstruct_replace(void *dstp, const void *src)
{
    const void *dst;
    memcpy(&dst, dstp, sizeof(dst));

    if (src == dst)
        return;
    ff_refstruct_unref(dstp);
    if (src) {
        dst = ff_refstruct_ref_c(src);
        memcpy(dstp, &dst, sizeof(dst));
    }
}

int ff_refstruct_exclusive(const void *data)
{
    const RefCount *ref = cget_refcount(data);
    /* Casting const away here is safe, because it is a load.
     * It is necessary because atomic_load_explicit() does not
     * accept const atomics in C11 (see also N1807). */
    return atomic_load_explicit((atomic_uintptr_t*)&ref->refcount, memory_order_acquire) == 1;
}

struct FFRefStructPool {
    size_t size;
    FFRefStructOpaque opaque;
    int  (*init_cb)(FFRefStructOpaque opaque, void *obj);
    FFRefStructUnrefCB reset_cb;
    void (*free_entry_cb)(FFRefStructOpaque opaque, void *obj);
    void (*free_cb)(FFRefStructOpaque opaque);

    int uninited;
    unsigned entry_flags;
    unsigned pool_flags;

    /** The number of outstanding entries not in available_entries. */
    atomic_uintptr_t refcount;
    /**
     * This is a linked list of available entries;
     * the RefCount's opaque pointer is used as next pointer
     * for available entries.
     * While the entries are in use, the opaque is a pointer
     * to the corresponding FFRefStructPool.
     */
    RefCount *available_entries;
    pthread_mutex_t mutex;
};

static void pool_free(FFRefStructPool *pool)
{
    pthread_mutex_destroy(&pool->mutex);
    if (pool->free_cb)
        pool->free_cb(pool->opaque);
    av_free(get_refcount(pool));
}

static void pool_free_entry(FFRefStructPool *pool, RefCount *ref)
{
    if (pool->free_entry_cb)
        pool->free_entry_cb(pool->opaque, get_userdata(ref));
    av_free(ref);
}

static void pool_return_entry(void *ref_)
{
    RefCount *ref = ref_;
    FFRefStructPool *pool = ref->opaque.nc;

    pthread_mutex_lock(&pool->mutex);
    if (!pool->uninited) {
        ref->opaque.nc = pool->available_entries;
        pool->available_entries = ref;
        ref = NULL;
    }
    pthread_mutex_unlock(&pool->mutex);

    if (ref)
        pool_free_entry(pool, ref);

    if (atomic_fetch_sub_explicit(&pool->refcount, 1, memory_order_acq_rel) == 1)
        pool_free(pool);
}

static void pool_reset_entry(FFRefStructOpaque opaque, void *entry)
{
    FFRefStructPool *pool = opaque.nc;

    pool->reset_cb.unref(pool->opaque, entry);
}

static void pool_reset_entry_ext(FFRefStructOpaque opaque,
                                 FFRefStructOpaque initial_opaque,
                                 void *entry)
{
    FFRefStructPool *pool = initial_opaque.nc;

    pool->reset_cb.unref_ext(opaque, pool->opaque, entry);
}

static int refstruct_pool_get_ext(void *objp, FFRefStructPool *pool)
{
    void *ret = NULL;

    memcpy(objp, &(void *){ NULL }, sizeof(void*));

    pthread_mutex_lock(&pool->mutex);
    av_assert1(!pool->uninited);
    if (pool->available_entries) {
        RefCount *ref = pool->available_entries;
        ret = get_userdata(ref);
        pool->available_entries = ref->opaque.nc;
        ref->opaque.nc = pool;
        atomic_init(&ref->refcount, 1);
    }
    pthread_mutex_unlock(&pool->mutex);

    if (!ret) {
        RefCount *ref;
#define CB_INIT(suffix) ((FFRefStructUnrefCB) { .unref ## suffix = pool->reset_cb.unref ## suffix ? \
                                                                   pool_reset_entry ## suffix : NULL })
        ret = ff_refstruct_alloc_ext_c(pool->size, pool->entry_flags,
                                       (FFRefStructOpaque){ .nc = pool },
                                       (pool->pool_flags & FF_REFSTRUCT_FLAG_DYNAMIC_OPAQUE) ?
                                       CB_INIT(_ext) : CB_INIT());
#undef CB_INIT
        if (!ret)
            return AVERROR(ENOMEM);
        ref = get_refcount(ret);
        ref->free = pool_return_entry;
        if (pool->init_cb) {
            int err = pool->init_cb(pool->opaque, ret);
            if (err < 0) {
                if (pool->pool_flags & FF_REFSTRUCT_POOL_FLAG_RESET_ON_INIT_ERROR)
                    pool->reset_cb.unref(pool->opaque, ret);
                if (pool->pool_flags & FF_REFSTRUCT_POOL_FLAG_FREE_ON_INIT_ERROR)
                    pool->free_entry_cb(pool->opaque, ret);
                av_free(ref);
                return err;
            }
        }
    }
    atomic_fetch_add_explicit(&pool->refcount, 1, memory_order_relaxed);

    if (pool->pool_flags & FF_REFSTRUCT_POOL_FLAG_ZERO_EVERY_TIME)
        memset(ret, 0, pool->size);

    memcpy(objp, &ret, sizeof(ret));

    return 0;
}

void *ff_refstruct_pool_get(FFRefStructPool *pool)
{
    void *ret;
    refstruct_pool_get_ext(&ret, pool);
    return ret;
}

static void pool_unref(void *ref)
{
    FFRefStructPool *pool = get_userdata(ref);
    if (atomic_fetch_sub_explicit(&pool->refcount, 1, memory_order_acq_rel) == 1)
        pool_free(pool);
}

static void refstruct_pool_uninit(FFRefStructOpaque unused, void *obj)
{
    FFRefStructPool *pool = obj;
    RefCount *entry;

    pthread_mutex_lock(&pool->mutex);
    av_assert1(!pool->uninited);
    pool->uninited = 1;
    entry = pool->available_entries;
    pool->available_entries = NULL;
    pthread_mutex_unlock(&pool->mutex);

    while (entry) {
        void *next = entry->opaque.nc;
        pool_free_entry(pool, entry);
        entry = next;
    }
}

FFRefStructPool *ff_refstruct_pool_alloc(size_t size, unsigned flags)
{
    return ff_refstruct_pool_alloc_ext(size, flags, NULL, NULL, NULL, NULL, NULL);
}

FFRefStructPool *ff_refstruct_pool_alloc_ext_c(size_t size, unsigned flags,
                                               FFRefStructOpaque opaque,
                                               int  (*init_cb)(FFRefStructOpaque opaque, void *obj),
                                               FFRefStructUnrefCB reset_cb,
                                               void (*free_entry_cb)(FFRefStructOpaque opaque, void *obj),
                                               void (*free_cb)(FFRefStructOpaque opaque))
{
    FFRefStructPool *pool = ff_refstruct_alloc_ext(sizeof(*pool), 0, NULL,
                                                   refstruct_pool_uninit);
    int err;

    if (!pool)
        return NULL;
    get_refcount(pool)->free = pool_unref;

    pool->size          = size;
    pool->opaque        = opaque;
    pool->init_cb       = init_cb;
    pool->reset_cb      = reset_cb;
    pool->free_entry_cb = free_entry_cb;
    pool->free_cb       = free_cb;
#define COMMON_FLAGS (FF_REFSTRUCT_POOL_FLAG_NO_ZEROING | FF_REFSTRUCT_POOL_FLAG_DYNAMIC_OPAQUE)
    pool->entry_flags   = flags & COMMON_FLAGS;
    // Dynamic opaque and resetting-on-init-error are incompatible
    // (there is no dynamic opaque available in ff_refstruct_pool_get()).
    av_assert1(!(flags & FF_REFSTRUCT_POOL_FLAG_DYNAMIC_OPAQUE &&
                 flags & FF_REFSTRUCT_POOL_FLAG_RESET_ON_INIT_ERROR));
    // Filter out nonsense combinations to avoid checks later.
    if (flags & FF_REFSTRUCT_POOL_FLAG_RESET_ON_INIT_ERROR &&
        !pool->reset_cb.unref)
        flags &= ~FF_REFSTRUCT_POOL_FLAG_RESET_ON_INIT_ERROR;
    if (!pool->free_entry_cb)
        flags &= ~FF_REFSTRUCT_POOL_FLAG_FREE_ON_INIT_ERROR;
    pool->pool_flags    = flags;

    if (flags & FF_REFSTRUCT_POOL_FLAG_ZERO_EVERY_TIME) {
        // We will zero the buffer before every use, so zeroing
        // upon allocating the buffer is unnecessary.
        pool->entry_flags |= FF_REFSTRUCT_FLAG_NO_ZEROING;
    }

    atomic_init(&pool->refcount, 1);

    err = pthread_mutex_init(&pool->mutex, NULL);
    if (err) {
        // Don't call ff_refstruct_uninit() on pool, as it hasn't been properly
        // set up and is just a POD right now.
        av_free(get_refcount(pool));
        return NULL;
    }
    return pool;
}
