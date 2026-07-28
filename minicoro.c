#define MINICORO_IMPL
#include "minicoro.h"
#include <assert.h>
#include <string.h>
#include <setjmp.h>

mco_coro* mco_coro_init(void (*effectful_fn)(mco_coro* co), void* user_data) {
    mco_coro* co;
    mco_desc desc = mco_desc_init(effectful_fn, 0);
    desc.user_data = user_data;
    mco_result res = mco_create(&co, &desc);
    assert(res == MCO_SUCCESS);
    return co;
}

void* mco_coro_get_user_data(mco_coro* k) {
    return mco_get_user_data(k);
}

char mco_coro_free(mco_coro* k) {
    mco_result res = mco_destroy(k);
    assert(res == MCO_SUCCESS);
    // Ante's Cranelift codegen currently doesn't filter out Unit values.
    // So since this function (and other void-returning functions) are thought
    // to return unit values we still need to return one here.
    return 0;
}

char mco_coro_is_suspended(mco_coro* k) {
    return mco_status(k) == MCO_SUSPENDED;
}

char mco_coro_push(mco_coro* k, const void* arg, size_t size) {
    mco_result res = mco_push(k, arg, size);
    assert(res == MCO_SUCCESS);
    return 0;
}

char mco_coro_pop(mco_coro* k, void* arg, size_t size) {
    mco_result res = mco_pop(k, arg, size);
    assert(res == MCO_SUCCESS);
    return 0;
}

char mco_coro_suspend(mco_coro* k) {
    mco_result res = mco_yield(k);
    assert(res == MCO_SUCCESS);
    return 0;
}

char mco_coro_resume(mco_coro* k) {
    mco_result res = mco_resume(k);
    assert(res == MCO_SUCCESS);
    return 0;
}

mco_coro* mco_coro_running(void) {
    return mco_running();
}

size_t mco_coro_bytes_stored(mco_coro* k) {
    return mco_get_bytes_stored(k);
}

// Memcpy `len` bytes from `src`'s storage to `dst`'s storage, preserving byte order.
char mco_coro_transfer(mco_coro* src, mco_coro* dst, size_t len) {
    if (len == 0) return 0;
    assert(src != NULL && dst != NULL);
    assert(len <= src->bytes_stored);
    assert(dst->bytes_stored + len <= dst->storage_size);
    memcpy(&dst->storage[dst->bytes_stored], &src->storage[src->bytes_stored - len], len);
    dst->bytes_stored += len;
    src->bytes_stored -= len;
    return 0;
}

// TODO: Investigate whether we need a stack of these and remove this entirely if so.
static MCO_THREAD_LOCAL mco_coro* pending_abort_reap = NULL;

// Unwind back to the matching `_setjmp` call, setting its return value to `val`. Never
// returns.
// `setjmp_coro` can be NULL for the main stack.
//
// TODO: Temporary: breaks RAII, but so does the whole abort optimization.
void mco_abort_longjmp(void* buf, mco_coro* setjmp_coro, int val) {
    mco_coro* coro = mco_running();
    mco_coro* leaf = coro;
    while (coro != setjmp_coro) {
        mco_coro* prev = coro->prev_co;
        coro->state = MCO_SUSPENDED;
        if (coro != leaf) {
            mco_result res = mco_destroy(coro);
            assert(res == MCO_SUCCESS);
        }
        coro = prev;
    }
    if (leaf != setjmp_coro) {
        leaf->prev_co = NULL;
        pending_abort_reap = leaf;
    }
    mco_current_co = setjmp_coro;
    if (setjmp_coro) setjmp_coro->state = MCO_RUNNING;
    longjmp(*(jmp_buf*)buf, val);
}

char mco_reap_pending_abort(void) {
    if (pending_abort_reap) {
        mco_result res = mco_destroy(pending_abort_reap);
        assert(res == MCO_SUCCESS);
        pending_abort_reap = NULL;
    }
    return 0;
}
