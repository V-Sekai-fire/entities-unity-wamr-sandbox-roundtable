// C ABI of ub_native: WAMR and libriscv behind ids, for the Unity benchmark.
// Machines are named by int ids, never pointers, so a stale id after a domain reload is refused.
#include "rt.hpp"

#if defined(_WIN32)
#define UB_API extern "C" __declspec(dllexport)
#else
#define UB_API extern "C" __attribute__((visibility("default")))
#endif

static std::vector<std::unique_ptr<Rt>> g_rts;  // index = id - 1; null when closed
static bool g_inited = false;

static Rt *get(int id) {
  if (id < 1 || (size_t)id > g_rts.size()) return nullptr;
  return g_rts[id - 1].get();
}

UB_API int ub_init(void (*get_t)(int, float *), void (*set_t)(int, const float *),
                   void (*get_c)(int, float *, int), void (*set_c)(int, const float *, int)) {
  g_host.get = get_t; g_host.set = set_t; g_host.get_column = get_c; g_host.set_column = set_c;
  if (g_inited) return 0;
  RuntimeInitArgs ia; memset(&ia, 0, sizeof ia);
  ia.mem_alloc_type = Alloc_With_System_Allocator;
  if (!wasm_runtime_full_init(&ia)) return -1;
  register_host_api();
  g_inited = true;
  return 0;
}

// kind: 0 = WAMR (wasm or AOT image, detected by the loader), 1 = libriscv interpreter,
// 2 = libriscv with binary translation (compiled by the system compiler, cached on disk).
UB_API int ub_open(int kind, const void *image, int len) {
  if (!g_inited || kind < 0 || kind > 2) return -1;
#ifndef RISCV_BINARY_TRANSLATION
  if (kind == 2) return -3;  // this build has no binary translation (Windows)
#endif
#if !defined(_WIN32)
  if (kind == 2) { setenv("CC", "clang", 0); setenv("CFLAGS", "-O3", 0); }
#endif
  auto rt = make_rt(kind == 0, kind == 2);
  std::vector<char> bytes((const char *)image, (const char *)image + len);
  if (!rt->open(bytes)) return -2;
  g_rts.push_back(std::move(rt));
  return (int)g_rts.size();
}
UB_API void ub_close(int id) {
  if (id < 1 || (size_t)id > g_rts.size() || !g_rts[id - 1]) return;
  g_rts[id - 1]->close();
  g_rts[id - 1].reset();
}
UB_API int ub_translated(int id) { Rt *r = get(id); return r && r->translated(); }
UB_API int ub_empty(int id) { Rt *r = get(id); return r && r->empty(); }
UB_API int ub_empty_n(int id, int n) {  // n calls without leaving native code
  Rt *r = get(id);
  if (!r) return 0;
  for (int i = 0; i < n; i++) if (!r->empty()) return 0;
  return 1;
}
UB_API int ub_update(int id) { Rt *r = get(id); return r && r->update(); }
UB_API int ub_transforms(int id, int n, int soa) { Rt *r = get(id); return r && r->transforms(n, soa != 0); }
UB_API int ub_lbfgs(int id, int solves, int iters, double *out) { Rt *r = get(id); return r && r->lbfgs(solves, iters, out); }
