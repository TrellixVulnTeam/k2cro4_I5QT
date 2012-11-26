/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "native_client/src/include/nacl_assert.h"
#include "native_client/src/include/nacl_macros.h"
#include "native_client/src/untrusted/valgrind/dynamic_annotations.h"


/*
 * This tests nacl-glibc's futex implementation.
 *
 * Technically, the futex interface is allowed to generate spurious
 * wakeups, and our futex implementation uses host OS interfaces which
 * are allowed to generate spurious wakeups too.  Some test cases
 * below assert cases where futex wakeups shouldn't occur, so these
 * could fail if the host OS produces spurious wakeups.
 *
 * If spurious wakeups occur in practice, we will have to change the
 * test to disregard them and retry.
 */


/*
 * nacl-glibc does not provide a header file that declares these
 * functions, so we declare them here.
 */
int __nacl_futex_wait(volatile int *addr, int val, unsigned int bitset,
                      const struct timespec *timeout);

int __nacl_futex_wake(volatile int *addr, int nwake, unsigned int bitset,
                      int *count);

#define __FUTEX_BITSET_MATCH_ANY 0xffffffff


/*
 * We do not test the futex bitset functionality yet, so we use these
 * wrappers to avoid specifying bitsets in the test functions.
 */
static int futex_wait(volatile int *addr, int val,
                      const struct timespec *timeout) {
  return __nacl_futex_wait(addr, val, __FUTEX_BITSET_MATCH_ANY, timeout);
}

static int futex_wake(volatile int *addr, int nwake, int *count) {
  return __nacl_futex_wake(addr, nwake, __FUTEX_BITSET_MATCH_ANY, count);
}


void test_futex_wait_value_mismatch(void) {
  int futex_value = 123;
  int rc = futex_wait(&futex_value, futex_value + 1, NULL);
  /*
   * This should return EWOULDBLOCK, but the implementation in
   * futex_emulation.c in nacl-glibc has a bug.
   */
  ASSERT_EQ(rc, 0);
}

void test_futex_wait_timeout(void) {
  struct timespec timeout = { 0, 1000 /* nanoseconds */ };
  int futex_value = 123;
  int rc = futex_wait(&futex_value, futex_value, &timeout);
  ASSERT_EQ(rc, -ETIMEDOUT);
}


struct ThreadState {
  pthread_t tid;
  volatile int *futex_value;
  volatile enum {
    STATE_STARTED = 100,
    STATE_ABOUT_TO_WAIT = 200,
    STATE_WAIT_RETURNED = 300,
  } state;
};

void *wakeup_test_thread(void *thread_arg) {
  struct ThreadState *thread = thread_arg;
  ANNOTATE_IGNORE_WRITES_BEGIN();
  thread->state = STATE_ABOUT_TO_WAIT;
  ANNOTATE_IGNORE_WRITES_END();
  int rc = futex_wait(thread->futex_value, *thread->futex_value, NULL);
  ASSERT_EQ(rc, 0);
  ANNOTATE_IGNORE_WRITES_BEGIN();
  thread->state = STATE_WAIT_RETURNED;
  ANNOTATE_IGNORE_WRITES_END();
  return NULL;
}

void create_waiting_thread(volatile int *futex_value,
                           struct ThreadState *thread) {
  thread->futex_value = futex_value;
  thread->state = STATE_STARTED;
  ASSERT_EQ(pthread_create(&thread->tid, NULL, wakeup_test_thread, thread), 0);
  while (thread->state == STATE_STARTED) {
    sched_yield();
  }
  /* Note that this could fail if futex_wait() gets a spurious wakeup. */
  ASSERT_EQ(thread->state, STATE_ABOUT_TO_WAIT);
  /*
   * This should be long enough for wakeup_test_thread() to enter
   * futex_wait() and add the thread to the wait queue.
   */
  struct timespec wait_time = { 0, 100 * 1000000 /* nanoseconds */ };
  ASSERT_EQ(nanosleep(&wait_time, NULL), 0);
  /* This could also fail if futex_wait() gets a spurious wakeup. */
  ASSERT_EQ(thread->state, STATE_ABOUT_TO_WAIT);
}

void check_futex_wake(volatile int *futex_value, int nwake,
                      int expected_woken) {
  /*
   * Change *futex_value just in case our nanosleep() call did not
   * wait long enough for futex_wait() to enter the wait queue,
   * although that is unlikely.  This prevents the test from hanging
   * if that happens, though the test will fail because futex_wake()
   * will return a count of 0.
   */
  ANNOTATE_IGNORE_WRITES_BEGIN();
  (*futex_value)++;
  ANNOTATE_IGNORE_WRITES_END();

  int count = 9999; /* Dummy value: should be overwritten. */
  int rc = futex_wake(futex_value, nwake, &count);
  ASSERT_EQ(rc, 0);
  /* This could fail if futex_wait() gets a spurious wakeup. */
  ASSERT_EQ(count, expected_woken);
}

void assert_thread_woken(struct ThreadState *thread) {
  while (thread->state == STATE_ABOUT_TO_WAIT) {
    sched_yield();
  }
  ASSERT_EQ(thread->state, STATE_WAIT_RETURNED);
}

void assert_thread_not_woken(struct ThreadState *thread) {
  ASSERT_EQ(thread->state, STATE_ABOUT_TO_WAIT);
}

/* Test that we can wake up a single thread. */
void test_futex_wakeup(void) {
  volatile int futex_value = 1;
  struct ThreadState thread;
  create_waiting_thread(&futex_value, &thread);
  check_futex_wake(&futex_value, INT_MAX, 1);
  assert_thread_woken(&thread);

  /* Clean up. */
  ASSERT_EQ(pthread_join(thread.tid, NULL), 0);
}

/*
 * Test that we can wake up multiple threads, and that futex_wake()
 * heeds the wakeup limit.
 */
void test_futex_wakeup_limit(void) {
  volatile int futex_value = 1;
  struct ThreadState threads[4];
  int i;
  for (i = 0; i < NACL_ARRAY_SIZE(threads); i++) {
    create_waiting_thread(&futex_value, &threads[i]);
  }
  check_futex_wake(&futex_value, 2, 2);
  /*
   * Test that threads are woken up in the order that they were added
   * to the wait queue.  This is not necessarily true for the Linux
   * implementation of futexes, but it is true for NaCl's
   * implementation.
   */
  assert_thread_woken(&threads[0]);
  assert_thread_woken(&threads[1]);
  assert_thread_not_woken(&threads[2]);
  assert_thread_not_woken(&threads[3]);

  /* Clean up: Wake the remaining threads so that they can exit. */
  check_futex_wake(&futex_value, INT_MAX, 2);
  assert_thread_woken(&threads[2]);
  assert_thread_woken(&threads[3]);
  for (i = 0; i < NACL_ARRAY_SIZE(threads); i++) {
    ASSERT_EQ(pthread_join(threads[i].tid, NULL), 0);
  }
}

/*
 * Check that futex_wait() and futex_wake() heed their address
 * arguments properly.  A futex_wait() call on one address should not
 * be woken by a futex_wake() call on another address.
 */
void test_futex_wakeup_address(void) {
  volatile int futex_value1 = 1;
  volatile int futex_value2 = 1;
  volatile int dummy_addr = 1;
  struct ThreadState thread1;
  struct ThreadState thread2;
  create_waiting_thread(&futex_value1, &thread1);
  create_waiting_thread(&futex_value2, &thread2);

  check_futex_wake(&dummy_addr, INT_MAX, 0);
  assert_thread_not_woken(&thread1);
  assert_thread_not_woken(&thread2);

  check_futex_wake(&futex_value1, INT_MAX, 1);
  assert_thread_woken(&thread1);
  assert_thread_not_woken(&thread2);

  /* Clean up: Wake the remaining thread so that it can exit. */
  check_futex_wake(&futex_value2, INT_MAX, 1);
  assert_thread_woken(&thread2);
  ASSERT_EQ(pthread_join(thread1.tid, NULL), 0);
  ASSERT_EQ(pthread_join(thread2.tid, NULL), 0);
}


void run_test(const char *test_name, void (*test_func)(void)) {
  printf("Running %s...\n", test_name);
  test_func();
}

#define RUN_TEST(test_func) (run_test(#test_func, test_func))

int main(void) {
  /* Turn off stdout buffering to aid debugging. */
  setvbuf(stdout, NULL, _IONBF, 0);

  RUN_TEST(test_futex_wait_value_mismatch);
  RUN_TEST(test_futex_wait_timeout);
  RUN_TEST(test_futex_wakeup);
  RUN_TEST(test_futex_wakeup_limit);
  RUN_TEST(test_futex_wakeup_address);

  return 0;
}