// WASI has no statfs; OpenUSD only needs the struct name to compile.
#pragma once
#include <sys/types.h>
struct statfs { long f_type; };
static inline int statfs(const char *, struct statfs *s) { s->f_type = 0; return -1; }
