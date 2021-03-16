#ifndef __CUSTOM_MUTATOR_HELPERS_H__
#define __CUSTOM_MUTATOR_HELPERS_H__

#include <stdlib.h>
#include "afl-fuzz.h"

#define INITIAL_GROWTH_SIZE (64)

/* Use in a struct: creates a name_buf and a name_size variable. */
#define BUF_VAR(type, name) \
  type * name##_buf;        \
  size_t name##_size
/* this fills in `&structptr->something_buf, &structptr->something_size`. */
#define BUF_PARAMS(name) \
  (void **)&name##_buf, &name##_size

#define PMRACE_THREAD_NUM_ENV_VAR "PMRACE_THREAD_NUM"

/* This function makes sure *size is > size_needed after call.
 It will realloc *buf otherwise.
 *size will grow exponentially as per:
 https://blog.mozilla.org/nnethercote/2014/11/04/please-grow-your-buffers-exponentially/
 Will return NULL and free *buf if size_needed is <1 or realloc failed.
 @return For convenience, this function returns *buf.
 */
static inline void *maybe_grow(void **buf, size_t *size, size_t size_needed) {

  /* No need to realloc */
  if (likely(size_needed && *size >= size_needed)) return *buf;

  /* No initial size was set */
  if (size_needed < INITIAL_GROWTH_SIZE) size_needed = INITIAL_GROWTH_SIZE;

  /* grow exponentially */
  size_t next_size = next_pow2(size_needed);

  /* handle overflow */
  if (!next_size) { next_size = size_needed; }

  /* alloc */
  *buf = realloc(*buf, next_size);
  *size = *buf ? next_size : 0;

  return *buf;

}

#undef INITIAL_GROWTH_SIZE

#endif
