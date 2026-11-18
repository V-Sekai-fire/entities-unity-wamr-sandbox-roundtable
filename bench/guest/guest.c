/* One guest source, built for wasm32 (WAMR) and rv64gc (libriscv).
 * Host API: host_get / host_set move a 12-float transform by index. */
#ifdef __wasm__
__attribute__((import_module("env"), import_name("host_get"))) extern void host_get(int idx, float *t);
__attribute__((import_module("env"), import_name("host_set"))) extern void host_set(int idx, const float *t);
__attribute__((import_module("env"), import_name("host_get_column"))) extern void host_get_column(int c, float *dst, int n);
__attribute__((import_module("env"), import_name("host_set_column"))) extern void host_set_column(int c, const float *src, int n);
#define EXPORT(n) __attribute__((export_name(#n)))
#else
static inline void host_get(int idx, float *t) {
  register long a0 __asm__("a0") = idx;
  register long a1 __asm__("a1") = (long)t;
  register long a7 __asm__("a7") = 500;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
}
static inline void host_set(int idx, const float *t) {
  register long a0 __asm__("a0") = idx;
  register long a1 __asm__("a1") = (long)t;
  register long a7 __asm__("a7") = 501;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");
}
static inline void host_get_column(int c, float *dst, int n) {
  register long a0 __asm__("a0") = c;
  register long a1 __asm__("a1") = (long)dst;
  register long a2 __asm__("a2") = n;
  register long a7 __asm__("a7") = 502;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}
static inline void host_set_column(int c, const float *src, int n) {
  register long a0 __asm__("a0") = c;
  register long a1 __asm__("a1") = (long)src;
  register long a2 __asm__("a2") = n;
  register long a7 __asm__("a7") = 503;
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}
#define EXPORT(n) __attribute__((used, visibility("default")))
#endif

/* Case 1 */
EXPORT(empty) void empty(void) {}

/* Case 3: state set up by init, one update reads and writes one transform */
static float g_state[12];
EXPORT(update) int update(void) {
  host_get(0, g_state);
  g_state[9] += 1.0f;
  host_set(0, g_state);
  return 1;
}

/* Case 2, AOS: one host call per transform, 12 contiguous floats each way. */
EXPORT(transforms) int transforms(int n) {
  float t[12];
  for (int i = 0; i < n; i++) {
    host_get(i, t);
    t[9] += 1.0f;
    t[10] += 2.0f;
    t[11] += 3.0f;
    host_set(i, t);
  }
  return n;
}

/* Case 2, SOA: the guest keeps 12 component arrays; the host moves whole columns
 * (12 reads, then 12 writes), and the guest updates the origin columns in a loop. */
#define MAXN 10000
static float g_col[12][MAXN];
EXPORT(transforms_soa) int transforms_soa(int n) {
  for (int c = 0; c < 12; c++) host_get_column(c, g_col[c], n);
  for (int i = 0; i < n; i++) {
    g_col[9][i] += 1.0f;
    g_col[10][i] += 2.0f;
    g_col[11][i] += 3.0f;
  }
  for (int c = 0; c < 12; c++) host_set_column(c, g_col[c], n);
  return n;
}

#include "lbfgs.h"
EXPORT(lbfgs) double lbfgs(int solves, int iters) { return lbfgs_run(solves, iters); }

/* No-argument entry for hosts that can only call a function by name (rvlinux --call). */
__attribute__((used, visibility("default"))) double lbfgs_1m(void) { return lbfgs(50000, 20); }

#ifndef __wasm__
__attribute__((used)) void sys_exit(void) { register long a7 __asm__("a7") = 93; __asm__ volatile("ecall" ::"r"(a7)); }
__attribute__((naked, used)) void _start(void) {
  __asm__ volatile("li a0, 0\n li a7, 93\n ecall");
}
#endif
