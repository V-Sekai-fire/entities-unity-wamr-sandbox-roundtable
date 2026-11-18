// Case 3 with the real OpenUSD core under WAMR: load usd.wasm (or its AOT image), instantiate, usd_init,
// copy a .usdz into the guest, usd_open_staged, count. One fresh instance per run.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "wasm_export.h"

using clk = std::chrono::steady_clock;
static double ms(clk::time_point a) { return std::chrono::duration<double, std::milli>(clk::now() - a).count(); }
static std::vector<char> slurp(const char *p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: usdbench <usd.wasm|usd.aot> <pkg.usdz> [reps]\n"); return 2; }
  const auto image = slurp(argv[1]);
  const auto pkg = slurp(argv[2]);
  const int reps = argc > 3 ? atoi(argv[3]) : 3;
  RuntimeInitArgs init;
  memset(&init, 0, sizeof init);
  init.mem_alloc_type = Alloc_With_System_Allocator;
  if (!wasm_runtime_full_init(&init)) { fprintf(stderr, "wamr init failed\n"); return 1; }
  char err[256];
  for (int r = 0; r < reps; ++r) {
    const auto t0 = clk::now();
    // The loader patches the buffer in place, so each run gets its own copy.
    std::vector<char> own(image);
    wasm_module_t mod = wasm_runtime_load((uint8_t *)own.data(), own.size(), err, sizeof err);
    if (!mod) { fprintf(stderr, "load: %s\n", err); return 1; }
    wasm_runtime_set_wasi_args_ex(mod, nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, 0, 1, 2);
    wasm_module_inst_t inst = wasm_runtime_instantiate(mod, 16u << 20, 0, err, sizeof err);
    if (!inst) { fprintf(stderr, "instantiate: %s\n", err); return 1; }
    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, 16u << 20);
    const double t_inst = ms(t0);
    auto fn = [&](const char *n) { auto f = wasm_runtime_lookup_function(inst, n); if (!f) fprintf(stderr, "missing %s\n", n); return f; };
    // WAMR runs a reactor's _initialize itself at instantiation; a second call hits the module's "already initialized" trap.
    uint32_t a[4] = {0, 0, 0, 0};
    auto t1 = clk::now();
    bool ok = wasm_runtime_call_wasm(env, fn("usd_init"), 0, a);
    const double t_init = ms(t1);
    if (!ok) { fprintf(stderr, "usd_init: %s\n", wasm_runtime_get_exception(inst)); wasm_runtime_dump_call_stack(env); return 1; }
    t1 = clk::now();
    a[0] = (uint32_t)pkg.size();
    ok = wasm_runtime_call_wasm(env, fn("usd_buffer"), 1, a);
    if (!ok) { fprintf(stderr, "usd_buffer: %s\n", wasm_runtime_get_exception(inst)); return 1; }
    void *dst = wasm_runtime_addr_app_to_native(inst, a[0]);
    memcpy(dst, pkg.data(), pkg.size());
    const double t_copy = ms(t1);
    t1 = clk::now();
    a[0] = 0;
    ok = wasm_runtime_call_wasm(env, fn("usd_open_staged"), 0, a);
    const double t_open = ms(t1);
    const int rc = ok ? (int)a[0] : -99;
    if (!ok) fprintf(stderr, "usd_open_staged: %s\n", wasm_runtime_get_exception(inst));
    auto count = [&](const char *n) { uint32_t b[2] = {0, 0}; wasm_runtime_call_wasm(env, fn(n), 0, b); return (int)b[0]; };
    const int meshes = count("usd_mesh_count"), tex = count("usd_texture_count");
    uint32_t m[2] = {0, 0};
    wasm_runtime_call_wasm(env, fn("usd_message"), 0, m);
    const char *msg = (const char *)wasm_runtime_addr_app_to_native(inst, m[0]);
    printf("run %d rc=%d load+instantiate=%.1f init=%.1f copy=%.1f open=%.1f total=%.1f ms meshes=%d textures=%d msg=%.70s\n", r, rc, t_inst, t_init, t_copy, t_open, ms(t0), meshes, tex, msg ? msg : "");
    wasm_runtime_destroy_exec_env(env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(mod);
  }
  return 0;
}
