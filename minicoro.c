#define MINICORO_IMPL
#include "minicoro.h"

char mco_coro_is_suspended(mco_coro* k) {
    return mco_status(k) == MCO_SUSPENDED;
}

mco_coro* mco_coro_init(void (*effectful_fn)(mco_coro* co)) {
    mco_coro* co;
    mco_desc desc = mco_desc_init(effectful_fn, 0);
    mco_result res = mco_create(&co, &desc);
    return co;
}
