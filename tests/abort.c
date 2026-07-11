/* Cross-coroutine abort handlers. */

#include "minicoro.h"
#include <assert.h>
#include <stdio.h>

/* Abort-handler support layer, defined in ../minicoro.c */
int mco_abort_call(void* buf, void (*body)(void* env), void* env);
void mco_abort_longjmp(void* buf, int val);

/* The compiler stack-allocates MCO_ABORT_BUF_SIZE = 320 bytes per optimized
   handle (a jmp_buf padded to the largest known platform's, plus the abort
   context; see minicoro.c). */
static char abort_buf[320];
static mco_coro* co_outer;
static mco_coro* co_inner;

static void inner_entry(mco_coro* co) {
  (void)co;
  assert(mco_running() == co_inner);
  assert(mco_status(co_outer) == MCO_NORMAL);
  /* Two coroutine stacks deep: unwind all the way back to main's setjmp. */
  mco_abort_longjmp(abort_buf, 42);
  assert(0 && "mco_abort_longjmp returned");
}

static void outer_entry(mco_coro* co) {
  (void)co;
  mco_desc desc = mco_desc_init(inner_entry, 0);
  assert(mco_create(&co_inner, &desc) == MCO_SUCCESS);
  assert(mco_resume(co_inner) == MCO_SUCCESS);
  assert(0 && "resume returned after an abort");
}

static void body(void* env) {
  (void)env;
  mco_desc desc = mco_desc_init(outer_entry, 0);
  assert(mco_create(&co_outer, &desc) == MCO_SUCCESS);
  assert(mco_resume(co_outer) == MCO_SUCCESS);
  assert(0 && "resume returned after an abort");
}

static void after_entry(mco_coro* co) {
  assert(mco_running() == co);
  assert(mco_yield(co) == MCO_SUCCESS);
}

/* Scenario 1: the abort lands on the plain thread stack. */
static void abort_to_main(void) {
  int v;
  assert(mco_running() == NULL);

  v = mco_abort_call(abort_buf, body, NULL);
  assert(v == 42);

  /* The longjmp crossed two coroutine stacks. minicoro must agree that we are
     back on the origin stack with nothing running. */
  assert(mco_running() == NULL);

  /* Both skipped coroutines are abandoned, never to be resumed again, but must
     be left in a coherent state rather than still marked running/normal. */
  assert(mco_status(co_inner) == MCO_SUSPENDED);
  assert(mco_status(co_outer) == MCO_SUSPENDED);

  mco_destroy(co_inner);
  mco_destroy(co_outer);

  /* Life goes on: a full resume/yield/resume cycle must work after an abort. */
  {
    mco_coro* co_after;
    mco_desc desc = mco_desc_init(after_entry, 0);
    assert(mco_create(&co_after, &desc) == MCO_SUCCESS);
    assert(mco_resume(co_after) == MCO_SUCCESS);
    assert(mco_status(co_after) == MCO_SUSPENDED);
    assert(mco_resume(co_after) == MCO_SUCCESS);
    assert(mco_status(co_after) == MCO_DEAD);
    assert(mco_running() == NULL);
    mco_destroy(co_after);
  }
}

/* Scenario 2: the setjmp side lives INSIDE a coroutine (an Ante `handle` whose
   body runs on a coroutine stack), and the abort crosses back to it from a
   coroutine nested one deeper. */
static char host_buf[320];
static mco_coro* co_host;
static mco_coro* co_aborter;

static void aborter_entry(mco_coro* co) {
  (void)co;
  assert(mco_running() == co_aborter);
  mco_abort_longjmp(host_buf, 7);
  assert(0 && "mco_abort_longjmp returned");
}

static void host_body(void* env) {
  (void)env;
  mco_desc desc = mco_desc_init(aborter_entry, 0);
  assert(mco_create(&co_aborter, &desc) == MCO_SUCCESS);
  assert(mco_resume(co_aborter) == MCO_SUCCESS);
  assert(0 && "resume returned after an abort");
}

static void host_entry(mco_coro* co) {
  int v = mco_abort_call(host_buf, host_body, NULL);
  assert(v == 7);
  /* Landed one coroutine up, not on the thread stack. */
  assert(mco_running() == co_host);
  assert(mco_status(co_aborter) == MCO_SUSPENDED);
  /* The host must still be able to yield -- this is where stale bookkeeping
     shows up: pre-fix the host is not marked running; and if the abort retired
     the host's pending fiber-switch announcement, the fake stack reactivates
     and mco_yield's own stack-overflow sanity check trips under ASan. */
  assert(mco_yield(co) == MCO_SUCCESS);
}

static void abort_within_coroutine(void) {
  mco_desc desc = mco_desc_init(host_entry, 0);
  assert(mco_create(&co_host, &desc) == MCO_SUCCESS);
  assert(mco_resume(co_host) == MCO_SUCCESS); /* abort + post-abort yield */
  assert(mco_status(co_host) == MCO_SUSPENDED);
  assert(mco_running() == NULL);
  assert(mco_resume(co_host) == MCO_SUCCESS); /* run host to completion */
  assert(mco_status(co_host) == MCO_DEAD);
  assert(mco_running() == NULL);
  mco_destroy(co_host);
  mco_destroy(co_aborter);
}

int main(void) {
  abort_to_main();
  abort_within_coroutine();
  printf("abort ok\n");
  return 0;
}
