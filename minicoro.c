#define MINICORO_IMPL
// Ante passes effect arguments and results through the message field instead of the storage buffer
#define MCO_DEFAULT_STORAGE_SIZE 0
#include <stdio.h>
#include "minicoro.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

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

bool mco_coro_is_suspended(mco_coro* k) {
    return mco_status(k) == MCO_SUSPENDED;
}

char mco_coro_set_message(mco_coro* k, void* message) {
    k->message = message;
    return 0;
}

void* mco_coro_get_message(mco_coro* k) {
    return k->message;
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

/* Used by Ante's stdlib to write to stdout and stderr portably */
void ante_write_stdout(const char* data, size_t length) {
    fwrite(data, 1, length, stdout);
}

void ante_write_stderr(const char* data, size_t length) {
    fwrite(data, 1, length, stderr);
}

int ante_create_dir(const char* path) {
#ifdef _WIN32
    if (_mkdir(path) == 0) return 0;
#else
    if (mkdir(path, 0777) == 0) return 0;
#endif
    if (errno != EEXIST) return -1;

    struct stat info;
    return stat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR ? 0 : -1;
}
