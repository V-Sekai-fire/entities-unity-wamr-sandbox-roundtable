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

#include "rt.hpp"

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
  g_host.get = [](int i, float *t) { memcpy(t, &world_aos[(size_t)i * 12], 48); };
  g_host.set = [](int i, const float *t) { memcpy(&world_aos[(size_t)i * 12], t, 48); };
  g_host.get_column = [](int c, float *d, int n) { memcpy(d, &world_soa[(size_t)c * kTransforms], (size_t)n * 4); };
  g_host.set_column = [](int c, const float *s, int n) { memcpy(&world_soa[(size_t)c * kTransforms], s, (size_t)n * 4); };
  register_host_api();

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
