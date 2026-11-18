// WAMR vs libriscv (godot-sandbox's machine) on one guest source.
// Usage: bench guest.wasm guest.rv64.elf [scale]   (scale divides the iteration counts)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "wasm_export.h"
#include "libriscv.h"
}

using clk = std::chrono::steady_clock;
static double ms_since(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }

// ---- host world: the transforms the guest reads and writes
static const int kTransforms = 10000;
static std::vector<float> world(kTransforms * 12);
static std::vector<float> world_soa(12 * kTransforms);  // 12 columns of kTransforms floats
static void reset_world() { std::fill(world.begin(), world.end(), 0.0f); std::fill(world_soa.begin(), world_soa.end(), 0.0f); }
static double world_sum() { double s = 0; for (float f : world) s += f; for (float f : world_soa) s += f; return s; }

static std::vector<char> read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<char> b(n);
  if (fread(b.data(), 1, n, f) != (size_t)n) exit(1);
  fclose(f);
  return b;
}

// ---- WAMR
static void w_host_get(wasm_exec_env_t env, int32_t idx, int32_t ptr) {
  wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
  void *p = wasm_runtime_addr_app_to_native(inst, (uint64_t)(uint32_t)ptr);
  memcpy(p, &world[(size_t)idx * 12], 48);
}
static void w_host_set(wasm_exec_env_t env, int32_t idx, int32_t ptr) {
  wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
  void *p = wasm_runtime_addr_app_to_native(inst, (uint64_t)(uint32_t)ptr);
  memcpy(&world[(size_t)idx * 12], p, 48);
}
static void w_host_get_column(wasm_exec_env_t env, int32_t c, int32_t ptr, int32_t n) {
  void *p = wasm_runtime_addr_app_to_native(wasm_runtime_get_module_inst(env), (uint64_t)(uint32_t)ptr);
  memcpy(p, &world_soa[(size_t)c * kTransforms], (size_t)n * 4);
}
static void w_host_set_column(wasm_exec_env_t env, int32_t c, int32_t ptr, int32_t n) {
  void *p = wasm_runtime_addr_app_to_native(wasm_runtime_get_module_inst(env), (uint64_t)(uint32_t)ptr);
  memcpy(&world_soa[(size_t)c * kTransforms], p, (size_t)n * 4);
}
static NativeSymbol w_natives[] = {
    {"host_get_column", (void *)w_host_get_column, "(iii)", nullptr},
    {"host_set_column", (void *)w_host_set_column, "(iii)", nullptr},
    {"host_get", (void *)w_host_get, "(ii)", nullptr},
    {"host_set", (void *)w_host_set, "(ii)", nullptr},
};

struct Wamr {
  std::vector<char> bytes;
  wasm_module_t mod = nullptr;
  wasm_module_inst_t inst = nullptr;
  wasm_exec_env_t env = nullptr;
  char err[256] = {0};
  bool open(const std::vector<char> &b) {
    bytes = b;  // the loader may patch in place
    mod = wasm_runtime_load((uint8_t *)bytes.data(), bytes.size(), err, sizeof err);
    if (!mod) return false;
    inst = wasm_runtime_instantiate(mod, 64 * 1024, 0, err, sizeof err);
    if (!inst) return false;
    env = wasm_runtime_create_exec_env(inst, 64 * 1024);
    return env != nullptr;
  }
  void close() {
    if (env) wasm_runtime_destroy_exec_env(env);
    if (inst) wasm_runtime_deinstantiate(inst);
    if (mod) wasm_runtime_unload(mod);
    env = nullptr; inst = nullptr; mod = nullptr;
  }
  bool call(const char *name, uint32_t argc, uint32_t *argv) {
    wasm_function_inst_t fn = wasm_runtime_lookup_function(inst, name);
    return fn && wasm_runtime_call_wasm(env, fn, argc, argv);
  }
};

// ---- libriscv
static void r_host_get(RISCVMachine *m) {
  RISCVRegisters *r = libriscv_get_registers(m);
  libriscv_copy_to_guest(m, LIBRISCV_ARG_REGISTER(r, 1), &world[(size_t)LIBRISCV_ARG_REGISTER(r, 0) * 12], 48);
}
static void r_host_set(RISCVMachine *m) {
  RISCVRegisters *r = libriscv_get_registers(m);
  libriscv_copy_from_guest(m, &world[(size_t)LIBRISCV_ARG_REGISTER(r, 0) * 12], LIBRISCV_ARG_REGISTER(r, 1), 48);
}
static void r_host_get_column(RISCVMachine *m) {
  RISCVRegisters *r = libriscv_get_registers(m);
  libriscv_copy_to_guest(m, LIBRISCV_ARG_REGISTER(r, 1), &world_soa[(size_t)LIBRISCV_ARG_REGISTER(r, 0) * kTransforms], (unsigned)LIBRISCV_ARG_REGISTER(r, 2) * 4);
}
static void r_host_set_column(RISCVMachine *m) {
  RISCVRegisters *r = libriscv_get_registers(m);
  libriscv_copy_from_guest(m, &world_soa[(size_t)LIBRISCV_ARG_REGISTER(r, 0) * kTransforms], LIBRISCV_ARG_REGISTER(r, 1), (unsigned)LIBRISCV_ARG_REGISTER(r, 2) * 4);
}
static void r_exit(RISCVMachine *m) { libriscv_stop(m); }

static void r_error(void *, int type, const char *msg, long data) { fprintf(stderr, "libriscv error %d: %s (%ld)\n", type, msg, data); }
struct Riscv {
  std::vector<char> bytes;
  RISCVMachine *m = nullptr;
  uint64_t a_empty = 0, a_update = 0, a_transforms = 0, a_transforms_soa = 0, a_lbfgs = 0;
  bool open(const std::vector<char> &b) {
    bytes = b;
    RISCVOptions o;
    libriscv_set_defaults(&o);
    o.max_memory = 64ull << 20;
    o.strict_sandbox = 1;
    o.error = r_error;
    o.default_exit_function = "sys_exit";
    m = libriscv_new(bytes.data(), (unsigned)bytes.size(), &o);
    if (!m) { fprintf(stderr, "libriscv_new failed\n"); return false; }
    int rc = libriscv_run(m, 1000000);
    if (rc < 0) { fprintf(stderr, "run _start: %s\n", libriscv_strerror(rc)); return false; }  // _start: reaches exit
    a_empty = libriscv_address_of(m, "empty");
    a_update = libriscv_address_of(m, "update");
    a_transforms = libriscv_address_of(m, "transforms");
    a_lbfgs = libriscv_address_of(m, "lbfgs");
    a_transforms_soa = libriscv_address_of(m, "transforms_soa");
    return a_empty && a_update && a_transforms && a_lbfgs && a_transforms_soa;
  }
  void close() { if (m) libriscv_delete(m); m = nullptr; }
  bool call(uint64_t addr, int64_t a0, int64_t a1, uint64_t limit) {
    if (libriscv_setup_vmcall(m, addr) != 0) return false;
    RISCVRegisters *r = libriscv_get_registers(m);
    LIBRISCV_ARG_REGISTER(r, 0) = a0;
    LIBRISCV_ARG_REGISTER(r, 1) = a1;
    int rc = libriscv_run(m, limit);
    if (rc != 0) { fprintf(stderr, "vmcall rc=%d %s\n", rc, libriscv_strerror(rc)); }
    return rc == 0;
  }
};

static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s guest.wasm guest.rv64.elf [scale]\n", argv[0]); return 2; }
  int scale = argc > 3 ? atoi(argv[3]) : 1;
  auto wasm_bytes = read_file(argv[1]);
  auto elf_bytes = read_file(argv[2]);

  RuntimeInitArgs ia; memset(&ia, 0, sizeof ia);
  ia.mem_alloc_type = Alloc_With_System_Allocator;
  if (!wasm_runtime_full_init(&ia)) { fprintf(stderr, "wamr init failed\n"); return 1; }
  wasm_runtime_register_natives("env", w_natives, 4);
  libriscv_set_syscall_handler(500, r_host_get);
  libriscv_set_syscall_handler(501, r_host_set);
  libriscv_set_syscall_handler(502, r_host_get_column);
  libriscv_set_syscall_handler(503, r_host_set_column);
  libriscv_set_syscall_handler(93, r_exit);

  const int calls = 1000000 / scale, solves = 50000 / scale, iters = 20, reps = 200;
  printf("{\"scale\":%d,\"calls\":%d,\"lbfgs_iterations\":%d,\"rows\":[\n", scale, calls, solves * iters);
  auto row = [&](const char *cs, const char *rt, double ms, double check, bool last = false) {
    printf(" {\"case\":\"%s\",\"runtime\":\"%s\",\"ms\":%.3f,\"check\":%.9g}%s\n", cs, rt, ms, check, last ? "" : ",");
    fflush(stdout);
  };

  Wamr w; Riscv r;
  if (!w.open(wasm_bytes)) { fprintf(stderr, "wamr open: %s\n", w.err); return 1; }
  if (!r.open(elf_bytes)) { fprintf(stderr, "riscv open failed\n"); return 1; }

  // case 1: empty export, called `calls` times from the host
  { auto t = clk::now(); bool ok = true; uint32_t a[2];
    for (int i = 0; i < calls && ok; i++) ok = w.call("empty", 0, a);
    row("1_empty_call", "wamr", ms_since(t), ok); }
  { auto t = clk::now(); bool ok = true;
    for (int i = 0; i < calls && ok; i++) ok = r.call(r.a_empty, 0, 0, 1000);
    row("1_empty_call", "libriscv", ms_since(t), ok); }

  // case 2: 10,000 transform reads and writes, `reps` repetitions, median per repetition
  { std::vector<double> v; double chk = 0;
    for (int k = 0; k < reps; k++) { reset_world(); uint32_t a[1] = {kTransforms}; auto t = clk::now();
      w.call("transforms", 1, a); v.push_back(ms_since(t)); chk = world_sum(); }
    row("2_transforms_10k_aos", "wamr", median(v), chk); }
  { std::vector<double> v; double chk = 0;
    for (int k = 0; k < reps; k++) { reset_world(); auto t = clk::now();
      r.call(r.a_transforms, kTransforms, 0, 1000000000ull); v.push_back(ms_since(t)); chk = world_sum(); }
    row("2_transforms_10k_aos", "libriscv", median(v), chk); }

  { std::vector<double> v; double chk = 0;
    for (int k = 0; k < reps; k++) { reset_world(); uint32_t a[1] = {kTransforms}; auto t = clk::now();
      w.call("transforms_soa", 1, a); v.push_back(ms_since(t)); chk = world_sum(); }
    row("2_transforms_10k_soa", "wamr", median(v), chk); }
  { std::vector<double> v; double chk = 0;
    for (int k = 0; k < reps; k++) { reset_world(); auto t = clk::now();
      r.call(r.a_transforms_soa, kTransforms, 0, 1000000000ull); v.push_back(ms_since(t)); chk = world_sum(); }
    row("2_transforms_10k_soa", "libriscv", median(v), chk); }

  // case 3: read the module from disk, instantiate, run the first update; median of `reps`
  { std::vector<double> v; double chk = 0;
    for (int k = 0; k < reps; k++) { reset_world(); auto t = clk::now();
      auto b = read_file(argv[1]); Wamr x; bool ok = x.open(b) && x.call("update", 0, (uint32_t[2]){0, 0});
      v.push_back(ms_since(t)); chk = ok ? world_sum() : -1; x.close(); }
    row("3_load_first_update", "wamr", median(v), chk); }
  { std::vector<double> v; double chk = 0;
    for (int k = 0; k < reps; k++) { reset_world(); auto t = clk::now();
      auto b = read_file(argv[2]); Riscv x; bool ok = x.open(b) && x.call(x.a_update, 0, 0, 1000000);
      v.push_back(ms_since(t)); chk = ok ? world_sum() : -1; x.close(); }
    row("3_load_first_update", "libriscv", median(v), chk); }

  // case 4: L-BFGS saddle solves, one guest call, `solves` x `iters` iterations
  { uint32_t a[2] = {(uint32_t)solves, (uint32_t)iters}; auto t = clk::now();
    bool ok = w.call("lbfgs", 2, a); double ms = ms_since(t); double f; memcpy(&f, a, 8);
    row("4_lbfgs", "wamr", ms, ok ? f : -1, false); }
  { auto t = clk::now(); bool ok = r.call(r.a_lbfgs, solves, iters, 1ull << 40); double ms = ms_since(t);
    row("4_lbfgs", "libriscv", ms, ok ? libriscv_get_registers(r.m)->fr[10].f64 : -1, true); }
  printf("]}\n");
  w.close(); r.close();
  wasm_runtime_destroy();
  return 0;
}
