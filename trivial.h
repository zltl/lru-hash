#ifndef UTILS_TRIVIAL_H_
#define UTILS_TRIVIAL_H_

#include <string.h> /* include offsetof */

#ifdef UNUSED
#elif defined(__GNUC__)
#define UNUSED(x) UNUSED_##x __attribute__((unused))
#elif defined(__LCLINT__)
#define UNUSED(x) /*@unused@*/ x
#else
#define UNUSED(x)
#endif

/* Fake linux list.h header. */
#define WRITE_ONCE(var, val) var = val
#define READ_ONCE(x) (x)

#define list_entry(ptr, type, member) \
    (type*)((char*)(ptr)-offsetof(type, member))

#ifndef bool
#define bool int
#endif

#define CASE_FALLTHOUGH __attribute__((fallthrough))

#endif /* UTILS_TRIVIAL_H_ */
