/* For pthread_getattr_np, used to learn the thread's stack bounds under ASan. */
#define _GNU_SOURCE
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

// Bulk-move `len` bytes from `src`'s storage to `dst`'s storage, preserving
// byte order. Used by the effect-lowering pass to forward an effect through a
// nested handler's drive without parsing the args by type.
char mco_coro_transfer(mco_coro* src, mco_coro* dst, size_t len) {
    if (len == 0) return 0;
    assert(src != NULL && dst != NULL);
    assert(len <= src->bytes_stored);
    assert(dst->bytes_stored + len <= dst->storage_size);
    memcpy(&dst->storage[dst->bytes_stored],
           &src->storage[src->bytes_stored - len],
           len);
    dst->bytes_stored += len;
    src->bytes_stored -= len;
    return 0;
}

#define MCO_JMP_BUF_MAX 256
#define MCO_ABORT_BUF_SIZE (MCO_JMP_BUF_MAX+64)

void mco_abort_land(void* buf);

// Abort-handler support (Fail/Throw-like effects:
//
//   jmp_buf  jb;                (glibc x86-64: 200 bytes <= MCO_JMP_BUF_MAX)
//   mco_coro* origin;           coroutine (NULL = plain thread stack) that ran setjmp
//   void*    origin_fake_stack; ASan fake-stack handle to restore on landing
//   int      crossed;           nonzero when the longjmp crossed coroutine stacks
//
// The protocol is:
//   prologue:  mco_abort_prepare(buf);
//              idx = _setjmp(buf); ...
//   wrapper:   // possibly on ANOTHER coroutine's stack!
//              store result;
//              mco_abort_longjmp(buf, 1);
//   landing:   mco_abort_land(buf);
//
typedef struct {
    jmp_buf jb;
    mco_coro* origin;
    void* origin_fake_stack;
    int crossed;
} _mco_abort_ctx;

// The compiler reserves 256+64 bytes.   Fail the build if:
// - jmp_buf is too big
typedef char _mco_jmp_buf_within_bound[sizeof(jmp_buf) <= MCO_JMP_BUF_MAX ? 1 : -1];
// - the layout outgrows the known size
typedef char _mco_abort_ctx_fits[sizeof(_mco_abort_ctx) <= MCO_ABORT_BUF_SIZE ? 1 : -1];
// - the layout outgrows the known size on Windows (even if we are on glibc)
typedef char _mco_abort_ctx_fits_worst_case[
    sizeof(_mco_abort_ctx) - sizeof(jmp_buf) + MCO_JMP_BUF_MAX <= MCO_ABORT_BUF_SIZE ? 1 : -1];

void mco_abort_prepare(void* buf) {
    _mco_abort_ctx* ctx = (_mco_abort_ctx*)buf;
    ctx->origin = mco_running();
#ifdef _MCO_USE_ASAN
    if (ctx->origin == NULL) {
        _mco_asan_capture_thread_stack();
    }
#endif
    ctx->origin_fake_stack = NULL;
    ctx->crossed = 0;
}

// Not a coroutine function but used by Fail/Throw-like effects.
//
// Calls `body(env)` in a context where any wrapper that calls
// `mco_abort_longjmp(buf, val)` will unwind back to this call and
// `mco_abort_call` will return `val`. Returns 0 if body completes normally.
//
// `buf` must point to at least MCO_ABORT_BUF_SIZE bytes.
int mco_abort_call(void* buf, void (*body)(void* env), void* env) {
    mco_abort_prepare(buf);
    int v = setjmp(((_mco_abort_ctx*)buf)->jb);
    if (v == 0) {
        body(env);
        return 0;
    }
    mco_abort_land(buf);
    return v;
}

// Unwind back to the matching setjmp, setting its return value to `val`.
// Never returns. Safe to call from a coroutine stack nested.
void mco_abort_longjmp(void* buf, int val) {
    _mco_abort_ctx* ctx = (_mco_abort_ctx*)buf;
    mco_coro* co = mco_running();
    while (co && co != ctx->origin) {
        mco_coro* prev = co->prev_co;
        // Mirror _mco_prepare_jumpout's bookkeeping without switching
        // contexts: the abandoned coroutine stays suspended forever.
        co->prev_co = NULL;
        co->state = MCO_SUSPENDED;
        if (prev) {
            prev->state = MCO_RUNNING;
        }
        mco_current_co = prev;
        ctx->crossed = 1;
        // The handle saved when co's resumer entered it restores the
        // resumer's fake stack; keep only the origin's.
        ctx->origin_fake_stack = co->asan_prev_stack;
        co->asan_prev_stack = NULL;
#ifdef _MCO_USE_TSAN
        void* tsan_prev = co->tsan_prev_fiber;
        co->tsan_prev_fiber = NULL;
        __tsan_switch_to_fiber(tsan_prev, 0);
#endif
        co = prev;
    }
#ifdef _MCO_USE_ASAN
    if (ctx->crossed) {
        // We are about to move from this coroutine's stack to the origin's.
        const void* bottom_old = NULL;
        size_t size_old = 0;
        __sanitizer_finish_switch_fiber(NULL, &bottom_old, &size_old);
        if (ctx->origin) {
            __sanitizer_start_switch_fiber(NULL, ctx->origin->stack_base, ctx->origin->stack_size);
        } else {
            __sanitizer_start_switch_fiber(NULL, _mco_asan_main_bottom, _mco_asan_main_size);
        }
    }
#endif
    longjmp(ctx->jb, val);
}

#ifndef __has_feature
# define __has_feature(x) 0
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
void __asan_unpoison_memory_region(void const volatile* addr, size_t size);
#endif

// Called on the origin stack. When the longjmp crossed coroutine
// stacks, address sanitization still believes we are on the abandoned
// coroutine's stack: restore this stack's identity.
void mco_abort_land(void* buf) {
    _mco_abort_ctx* ctx = (_mco_abort_ctx*)buf;
    if (!ctx->crossed) {
        return;
    }
    ctx->crossed = 0;
#ifdef _MCO_USE_ASAN
    void* bottom_old = NULL;
    size_t size_old = 0;
    char land_here; // approximates the landing stack pointer
    void* origin_bottom;
    __sanitizer_finish_switch_fiber(ctx->origin_fake_stack, (const void**)&bottom_old, &size_old);
    // Unpoison the origin frames the longjmp skipped: everything on this stack
    // below us is dead, but its shadow still holds the dead frames' redzones,
    // which would read as wild stack-buffer-overflows once new frames (or an
    // interceptor's access check) reach those addresses.
    origin_bottom = ctx->origin ? ctx->origin->stack_base : _mco_asan_main_bottom;
    if (origin_bottom && (char*)origin_bottom < &land_here) {
        __asan_unpoison_memory_region(origin_bottom, &land_here - (char*)origin_bottom);
    }
    if (ctx->origin) {
        // Restore state for the origin (or its next yield breaks).
        __sanitizer_start_switch_fiber(&ctx->origin->asan_prev_stack, ctx->origin->stack_base, ctx->origin->stack_size);
    }
#endif
    ctx->origin_fake_stack = NULL;
}
