// prof_lbfgs <wamr|riscv-interp|riscv-bt> <guest file> [rounds]: only case 4, repeated, for a sampling profiler.
#include "rt.hpp"
#include <chrono>
int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s wamr|riscv-interp|riscv-bt guest [rounds]\n", argv[0]); return 2; }
  std::string k = argv[1];
  int rounds = argc > 3 ? atoi(argv[3]) : 3;
  RuntimeInitArgs ia; memset(&ia, 0, sizeof ia);
  ia.mem_alloc_type = Alloc_With_System_Allocator;
  if (!wasm_runtime_full_init(&ia)) return 1;
  g_host.get = [](int, float *) {}; g_host.set = [](int, const float *) {};
  g_host.get_column = [](int, float *, int) {}; g_host.set_column = [](int, const float *, int) {};
  register_host_api();
  std::unique_ptr<Rt> r((k == "wamr") ? (Rt *)new Wamr() : (Rt *)new Riscv(k == "riscv-bt"));
  FILE *f = fopen(argv[2], "rb"); std::vector<char> img; char buf[65536]; size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) img.insert(img.end(), buf, buf + n);
  fclose(f);
  if (!r->open(img)) return 1;
  for (int i = 0; i < rounds; i++) {
    double out = 0; auto t = std::chrono::steady_clock::now();
    r->lbfgs(50000, 20, &out);
    printf("%s round %d: %.1f ms residual %.9g\n", k.c_str(), i, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(), out);
    fflush(stdout);
  }
  if (auto *rv = dynamic_cast<Riscv *>(r.get()))
    printf("guest instructions executed (all rounds): %llu\n", (unsigned long long)rv->m->instruction_counter());
  return 0;
}
