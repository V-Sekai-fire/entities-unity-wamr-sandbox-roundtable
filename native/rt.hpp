// Shared by the standalone bench and the Unity plugin: WAMR and libriscv behind one interface.
// The host API (transform get/set, whole-column get/set) arrives as callbacks in `g_host`.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "wasm_export.h"
}
#include <libriscv/machine.hpp>
#include <libriscv/prepared_call.hpp>

struct Host {
  void (*get)(int idx, float *t12) = nullptr;
  void (*set)(int idx, const float *t12) = nullptr;
  void (*get_column)(int c, float *dst, int n) = nullptr;
  void (*set_column)(int c, const float *src, int n) = nullptr;
};
inline Host g_host;

struct Rt {
  virtual ~Rt() {}
  virtual bool open(const std::vector<char> &image) = 0;
  virtual void close() = 0;
  virtual bool empty() = 0;
  virtual bool update() = 0;
  virtual bool transforms(int n, bool soa) = 0;
  virtual bool lbfgs(int solves, int iters, double *out) = 0;
};

// ---- WAMR
inline void *app_ptr(wasm_exec_env_t env, int32_t p) {
  return wasm_runtime_addr_app_to_native(wasm_runtime_get_module_inst(env), (uint64_t)(uint32_t)p);
}
inline void w_get(wasm_exec_env_t e, int32_t i, int32_t p) { g_host.get(i, (float *)app_ptr(e, p)); }
inline void w_set(wasm_exec_env_t e, int32_t i, int32_t p) { g_host.set(i, (const float *)app_ptr(e, p)); }
inline void w_getc(wasm_exec_env_t e, int32_t c, int32_t p, int32_t n) { g_host.get_column(c, (float *)app_ptr(e, p), n); }
inline void w_setc(wasm_exec_env_t e, int32_t c, int32_t p, int32_t n) { g_host.set_column(c, (const float *)app_ptr(e, p), n); }
inline NativeSymbol w_natives[] = {
    {"host_get", (void *)w_get, "(ii)", nullptr},
    {"host_set", (void *)w_set, "(ii)", nullptr},
    {"host_get_column", (void *)w_getc, "(iii)", nullptr},
    {"host_set_column", (void *)w_setc, "(iii)", nullptr},
};

struct Wamr : Rt {
  std::vector<char> bytes;
  wasm_module_t mod = nullptr;
  wasm_module_inst_t inst = nullptr;
  wasm_exec_env_t env = nullptr;
  wasm_function_inst_t f_empty = nullptr, f_update = nullptr, f_tr = nullptr, f_trs = nullptr, f_lb = nullptr;
  char err[256] = {0};
  bool open(const std::vector<char> &image) override {
    bytes = image;
    mod = wasm_runtime_load((uint8_t *)bytes.data(), bytes.size(), err, sizeof err);
    if (!mod) { fprintf(stderr, "wamr load: %s\n", err); return false; }
    inst = wasm_runtime_instantiate(mod, 64 * 1024, 0, err, sizeof err);
    if (!inst) { fprintf(stderr, "wamr inst: %s\n", err); return false; }
    env = wasm_runtime_create_exec_env(inst, 64 * 1024);
    f_empty = wasm_runtime_lookup_function(inst, "empty");
    f_update = wasm_runtime_lookup_function(inst, "update");
    f_tr = wasm_runtime_lookup_function(inst, "transforms");
    f_trs = wasm_runtime_lookup_function(inst, "transforms_soa");
    f_lb = wasm_runtime_lookup_function(inst, "lbfgs");
    return env && f_empty && f_update && f_tr && f_trs && f_lb;
  }
  void close() override {
    if (env) wasm_runtime_destroy_exec_env(env);
    if (inst) wasm_runtime_deinstantiate(inst);
    if (mod) wasm_runtime_unload(mod);
    env = nullptr; inst = nullptr; mod = nullptr;
  }
  bool empty() override { uint32_t a[2]; return wasm_runtime_call_wasm(env, f_empty, 0, a); }
  bool update() override { uint32_t a[2]; return wasm_runtime_call_wasm(env, f_update, 0, a); }
  bool transforms(int n, bool soa) override { uint32_t a[2] = {(uint32_t)n, 0}; return wasm_runtime_call_wasm(env, soa ? f_trs : f_tr, 1, a); }
  bool lbfgs(int s, int it, double *out) override {
    uint32_t a[2] = {(uint32_t)s, (uint32_t)it};
    bool ok = wasm_runtime_call_wasm(env, f_lb, 2, a);
    memcpy(out, a, 8);
    return ok;
  }
};

// ---- libriscv, through its C++ API: flat arena, threaded dispatch, prepared calls,
// and (when bt is set) binary translation compiled by the system compiler and cached.
using M = riscv::Machine<riscv::RISCV64>;
inline void r_get(M &m) {
  auto i = m.cpu.reg(10); auto p = m.cpu.reg(11);
  g_host.get((int)i, m.memory.memarray<float>(p, 12));
}
inline void r_set(M &m) {
  auto i = m.cpu.reg(10); auto p = m.cpu.reg(11);
  g_host.set((int)i, m.memory.memarray<float>(p, 12));
}
inline void r_getc(M &m) {
  auto c = m.cpu.reg(10); auto p = m.cpu.reg(11); auto n = m.cpu.reg(12);
  g_host.get_column((int)c, m.memory.memarray<float>(p, n), (int)n);
}
inline void r_setc(M &m) {
  auto c = m.cpu.reg(10); auto p = m.cpu.reg(11); auto n = m.cpu.reg(12);
  g_host.set_column((int)c, m.memory.memarray<float>(p, n), (int)n);
}
inline void r_exit(M &m) { m.stop(); }

struct Riscv : Rt {
  bool bt;
  explicit Riscv(bool b) : bt(b) {}
  std::vector<char> bytes;
  std::unique_ptr<M> m;
  std::unique_ptr<riscv::PreparedCall<riscv::RISCV64, void()>> p_empty;
  std::unique_ptr<riscv::PreparedCall<riscv::RISCV64, long()>> p_update;
  std::unique_ptr<riscv::PreparedCall<riscv::RISCV64, long(int)>> p_tr, p_trs;
  std::unique_ptr<riscv::PreparedCall<riscv::RISCV64, double(int, int)>> p_lb;
  bool open(const std::vector<char> &image) override {
    bytes = image;
    riscv::MachineOptions<riscv::RISCV64> o;
    o.memory_max = 64ull << 20;
    o.default_exit_function = "sys_exit";
#ifdef RISCV_BINARY_TRANSLATION
    o.translate_enabled = bt;
    o.translate_invoke_compiler = bt;
    o.translate_ignore_instruction_limit = true;
    o.translation_cache = true;
    o.translation_prefix = "rvbintr-";
    // Mask guest addresses into the power-of-two arena instead of bounds-checking each one; the
    // guest stays inside its arena, as a wasm module stays inside its linear memory.
    o.translate_automatic_nbit_address_space = bt;
#endif
    try {
      m = std::make_unique<M>(std::string_view(bytes.data(), bytes.size()), o);
      m->simulate(1000000);  // _start: reaches the exit ecall
      p_empty = std::make_unique<riscv::PreparedCall<riscv::RISCV64, void()>>(*m, "empty");
      p_update = std::make_unique<riscv::PreparedCall<riscv::RISCV64, long()>>(*m, "update");
      p_tr = std::make_unique<riscv::PreparedCall<riscv::RISCV64, long(int)>>(*m, "transforms");
      p_trs = std::make_unique<riscv::PreparedCall<riscv::RISCV64, long(int)>>(*m, "transforms_soa");
      p_lb = std::make_unique<riscv::PreparedCall<riscv::RISCV64, double(int, int)>>(*m, "lbfgs");
    } catch (const std::exception &e) { fprintf(stderr, "libriscv open: %s\n", e.what()); return false; }
    return true;
  }
  void close() override { p_empty.reset(); p_update.reset(); p_tr.reset(); p_trs.reset(); p_lb.reset(); m.reset(); }
  bool empty() override { (*p_empty)(); return true; }
  bool update() override { return (*p_update)() == 1; }
  bool transforms(int n, bool soa) override { return (soa ? *p_trs : *p_tr)(n) == n; }
  bool lbfgs(int s, int it, double *out) override { *out = (*p_lb)(s, it); return true; }
};


// Register the host API with both runtimes. Call once, after wasm_runtime_full_init.
inline void register_host_api() {
  wasm_runtime_register_natives("env", w_natives, 4);
  M::install_syscall_handler(500, r_get);
  M::install_syscall_handler(501, r_set);
  M::install_syscall_handler(502, r_getc);
  M::install_syscall_handler(503, r_setc);
  M::install_syscall_handler(93, r_exit);
}
inline std::unique_ptr<Rt> make_rt(bool wamr, bool bt) {
  if (wamr) return std::make_unique<Wamr>();
  return std::make_unique<Riscv>(bt);
}
