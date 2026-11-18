/* The equal-comparison guest: cases 1 and 4 only, no host API, one source for wasm32, wasm64 and rv64gc.
 * update/transforms/transforms_soa are empty stubs, there only so the shared host harness finds its entry points. */
#ifdef __wasm__
#define EXPORT(n) __attribute__((export_name(#n)))
#else
#define EXPORT(n) __attribute__((used, visibility("default")))
#endif

EXPORT(empty) void empty(void) {}
EXPORT(update) int update(void) { return 1; }
EXPORT(transforms) int transforms(int n) { return n; }
EXPORT(transforms_soa) int transforms_soa(int n) { return n; }

#include "lbfgs.h"
EXPORT(lbfgs) double lbfgs(int solves, int iters) { return lbfgs_run(solves, iters); }

#ifndef __wasm__
__attribute__((used)) void sys_exit(void) { register long a7 __asm__("a7") = 93; __asm__ volatile("ecall" ::"r"(a7)); }
__attribute__((naked, used)) void _start(void) {
  __asm__ volatile("li a0, 0\n li a7, 93\n ecall");
}
#endif
