#define MINICORO_IMPL
#include "minicoro.h"
#include <assert.h>

mco_coro* mco_coro_init(void (*effectful_fn)(mco_coro* co)) {
    mco_coro* co;
    mco_desc desc = mco_desc_init(effectful_fn, 0);
    mco_result res = mco_create(&co, &desc);
    assert(res == MCO_SUCCESS);
    return co;
}
// For some reason rust requires a leading underscore on these names
// so we define both versions. These versions are only used within the cranelift JIT.
mco_coro* _mco_coro_init(void (*effectful_fn)(mco_coro* co)) {
    return mco_coro_init(effectful_fn);
}

char mco_coro_free(mco_coro* k) {
    mco_result res = mco_destroy(k);
    assert(res == MCO_SUCCESS);
    // Ante's Cranelift codegen currently doesn't filter out Unit values.
    // So since this function (and other void-returning functions) are thought
    // to return unit values we still need to return one here.
    return 0;
}
char _mco_coro_free(mco_coro* k) {
    return mco_coro_free(k);
}

char mco_coro_is_suspended(mco_coro* k) {
    return mco_status(k) == MCO_SUSPENDED;
}
char _mco_coro_is_suspended(mco_coro* k) {
    return mco_coro_is_suspended(k);
}

char mco_coro_push(mco_coro* k, const void* arg, size_t size) {
    mco_result res = mco_push(k, arg, size);
    assert(res == MCO_SUCCESS);
    return 0;
}
char _mco_coro_push(mco_coro* k, const void* arg, size_t size) {
    return mco_coro_push(k, arg, size);
}

char mco_coro_pop(mco_coro* k, void* arg, size_t size) {
    mco_result res = mco_pop(k, arg, size);
    assert(res == MCO_SUCCESS);
    return 0;
}
char _mco_coro_pop(mco_coro* k, void* arg, size_t size) {
    return mco_coro_pop(k, arg, size);
}

char mco_coro_suspend(mco_coro* k) {
    mco_result res = mco_yield(k);
    assert(res == MCO_SUCCESS);
    return 0;
}
char _mco_coro_suspend(mco_coro* k) {
    return mco_coro_suspend(k);
}

char mco_coro_resume(mco_coro* k) {
    mco_result res = mco_resume(k);
    assert(res == MCO_SUCCESS);
    return 0;
}
char _mco_coro_resume(mco_coro* k) {
    return mco_coro_resume(k);
}
