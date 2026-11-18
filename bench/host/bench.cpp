// WAMR (interpreter and AOT) vs libriscv (interpreter and binary translation) on one guest source.
// Usage: bench guest.wasm guest.aot guest.rv64.elf [scale]   (scale divides the iteration counts)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <glob.h>

extern "C" {
#include "wasm_export.h"
}
#include <libriscv/machine.hpp>
#include <libriscv/prepared_call.hpp>

using clk = std::chrono::steady_clock;
static double ms_since(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

// ---- host world: the transforms the guest reads and writes
static const int kTransforms = 10000;
static std::vector<float> world_aos(kTransforms * 12);
static std::vector<float> world_soa(12 * kTransforms);  // 12 columns
static void reset_world() { std::fill(world_aos.begin(), world_aos.end(), 0.0f); std::fill(world_soa.begin(), world_soa.end(), 0.0f); }
static double world_sum() { double s = 0; for (float f : world_aos) s += f; for (float f : world_soa) s += f; return s; }

static std::vector<char> read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<char> b(n);
  if (fread(b.data(), 1, n, f) != (size_t)n) exit(1);
  fclose(f);
  return b;
}

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
static void *app_ptr(wasm_exec_env_t env, int32_t p) {
  return wasm_runtime_addr_app_to_native(wasm_runtime_get_module_inst(env), (uint64_t)(uint32_t)p);
}
static void w_get(wasm_exec_env_t e, int32_t i, int32_t p) { memcpy(app_ptr(e, p), &world_aos[(size_t)i * 12], 48); }
static void w_set(wasm_exec_env_t e, int32_t i, int32_t p) { memcpy(&world_aos[(size_t)i * 12], app_ptr(e, p), 48); }
static void w_getc(wasm_exec_env_t e, int32_t c, int32_t p, int32_t n) { memcpy(app_ptr(e, p), &world_soa[(size_t)c * kTransforms], (size_t)n * 4); }
static void w_setc(wasm_exec_env_t e, int32_t c, int32_t p, int32_t n) { memcpy(&world_soa[(size_t)c * kTransforms], app_ptr(e, p), (size_t)n * 4); }
static NativeSymbol w_natives[] = {
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
static void r_get(M &m) {
  auto i = m.cpu.reg(10); auto p = m.cpu.reg(11);
  memcpy(m.memory.memarray<float>(p, 12), &world_aos[(size_t)i * 12], 48);
}
static void r_set(M &m) {
  auto i = m.cpu.reg(10); auto p = m.cpu.reg(11);
  memcpy(&world_aos[(size_t)i * 12], m.memory.memarray<float>(p, 12), 48);
}
static void r_getc(M &m) {
  auto c = m.cpu.reg(10); auto p = m.cpu.reg(11); auto n = m.cpu.reg(12);
  memcpy(m.memory.memarray<float>(p, n), &world_soa[(size_t)c * kTransforms], (size_t)n * 4);
}
static void r_setc(M &m) {
  auto c = m.cpu.reg(10); auto p = m.cpu.reg(11); auto n = m.cpu.reg(12);
  memcpy(&world_soa[(size_t)c * kTransforms], m.memory.memarray<float>(p, n), (size_t)n * 4);
}
static void r_exit(M &m) { m.stop(); }

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
    o.translate_enabled = bt;
    o.translate_invoke_compiler = bt;
    o.translate_ignore_instruction_limit = true;
    o.translation_cache = true;
    o.translation_prefix = "rvbintr-";
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

static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
static void clear_translation_cache() {
  glob_t g;
  if (glob("rvbintr-*", 0, nullptr, &g) == 0) { for (size_t i = 0; i < g.gl_pathc; i++) remove(g.gl_pathv[i]); }
  globfree(&g);
}

int main(int argc, char **argv) {
  if (argc < 4) { fprintf(stderr, "usage: %s guest.wasm guest.aot guest.rv64.elf [scale]\n", argv[0]); return 2; }
  const int scale = argc > 4 ? atoi(argv[4]) : 1;
  const int calls = 1000000 / scale, solves = 50000 / scale, iters = 20, reps = 200;

  RuntimeInitArgs ia; memset(&ia, 0, sizeof ia);
  ia.mem_alloc_type = Alloc_With_System_Allocator;
  if (!wasm_runtime_full_init(&ia)) { fprintf(stderr, "wamr init failed\n"); return 1; }
  wasm_runtime_register_natives("env", w_natives, 4);
  M::install_syscall_handler(500, r_get);
  M::install_syscall_handler(501, r_set);
  M::install_syscall_handler(502, r_getc);
  M::install_syscall_handler(503, r_setc);
  M::install_syscall_handler(93, r_exit);

  struct Entry { const char *name; std::unique_ptr<Rt> rt; const char *file; };
  std::vector<Entry> list;
  list.push_back({"wamr-interp", std::make_unique<Wamr>(), argv[1]});
  list.push_back({"wamr-aot", std::make_unique<Wamr>(), argv[2]});
  list.push_back({"libriscv-interp", std::make_unique<Riscv>(false), argv[3]});
  list.push_back({"libriscv-bt", std::make_unique<Riscv>(true), argv[3]});

  clear_translation_cache();
  printf("{\"scale\":%d,\"calls\":%d,\"lbfgs_iterations\":%d,\"rows\":[\n", scale, calls, solves * iters);
  bool first_row = true;
  auto row = [&](const char *cs, const char *rt, double ms, double check) {
    printf("%s {\"case\":\"%s\",\"runtime\":\"%s\",\"ms\":%.3f,\"check\":%.9g}", first_row ? "" : ",\n", cs, rt, ms, check);
    first_row = false; fflush(stdout);
  };

  for (auto &e : list) {
    auto image = read_file(e.file);
    // case 3 first: the first load ever (libriscv-bt compiles and caches its translation here)
    { reset_world(); auto t = clk::now(); auto b = read_file(e.file); auto x = std::unique_ptr<Rt>(
          std::string(e.name).rfind("wamr", 0) == 0 ? (Rt *)new Wamr() : (Rt *)new Riscv(std::string(e.name) == "libriscv-bt"));
      bool ok = x->open(b) && x->update(); double ms = ms_since(t); double chk = ok ? world_sum() : -1; x->close();
      row("3_load_first_update_first_ever", e.name, ms, chk); }
    std::vector<double> v; double chk = 0;
    for (int k = 0; k < reps; k++) {
      reset_world(); auto t = clk::now(); auto b = read_file(e.file);
      auto x = std::unique_ptr<Rt>(std::string(e.name).rfind("wamr", 0) == 0 ? (Rt *)new Wamr() : (Rt *)new Riscv(std::string(e.name) == "libriscv-bt"));
      bool ok = x->open(b) && x->update(); v.push_back(ms_since(t)); chk = ok ? world_sum() : -1; x->close();
    }
    row("3_load_first_update", e.name, median(v), chk);

    Rt &r = *e.rt;
    if (!r.open(image)) { fprintf(stderr, "%s: open failed\n", e.name); return 1; }
    { auto t = clk::now(); bool ok = true; for (int i = 0; i < calls && ok; i++) ok = r.empty(); row("1_empty_call", e.name, ms_since(t), ok); }
    for (int soa = 0; soa < 2; soa++) {
      std::vector<double> w; double c = 0;
      for (int k = 0; k < reps; k++) { reset_world(); auto t = clk::now(); r.transforms(kTransforms, soa); w.push_back(ms_since(t)); c = world_sum(); }
      row(soa ? "2_transforms_10k_soa" : "2_transforms_10k_aos", e.name, median(w), c);
    }
    { double f = 0; auto t = clk::now(); bool ok = r.lbfgs(solves, iters, &f); row("4_lbfgs", e.name, ms_since(t), ok ? f : -1); }
    r.close();
  }
  printf("\n]}\n");
  wasm_runtime_destroy();
  return 0;
}
