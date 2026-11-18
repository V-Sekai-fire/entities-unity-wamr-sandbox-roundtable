// eq <wamr|riscv-interp|riscv-bt> <guest> [rounds] [solves]: the equal comparison. The same two cases on every runtime and
// pointer width: case 1 (1M empty calls) and case 4 (solves x 20 L-BFGS iterations), each round timed on its own.
#include "rt.hpp"
#include <chrono>
using clk = std::chrono::steady_clock;
static double ms(clk::time_point t) { return std::chrono::duration<double, std::milli>(clk::now() - t).count(); }
int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s wamr|riscv-interp|riscv-bt guest [rounds] [solves]\n", argv[0]); return 2; }
  const std::string k = argv[1];
  const int rounds = argc > 3 ? atoi(argv[3]) : 3, solves = argc > 4 ? atoi(argv[4]) : 50000;
  RuntimeInitArgs ia; memset(&ia, 0, sizeof ia);
  ia.mem_alloc_type = Alloc_With_System_Allocator;
  if (!wasm_runtime_full_init(&ia)) return 1;
  g_host.get = [](int, float *) {}; g_host.set = [](int, const float *) {};
  g_host.get_column = [](int, float *, int) {}; g_host.set_column = [](int, const float *, int) {};
  register_host_api();
  std::unique_ptr<Rt> r((k == "wamr") ? (Rt *)new Wamr() : (Rt *)new Riscv(k == "riscv-bt"));
  FILE *f = fopen(argv[2], "rb"); if (!f) { perror(argv[2]); return 1; }
  std::vector<char> img; char buf[65536]; size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) img.insert(img.end(), buf, buf + n);
  fclose(f);
  if (!r->open(img)) return 1;
  for (int i = 0; i < rounds; i++) {
    auto t = clk::now();
    for (int c = 0; c < 1000000; c++) r->empty();
    const double c1 = ms(t);
    double out = 0; t = clk::now();
    r->lbfgs(solves, 20, &out);
    printf("%-13s round %d: case1 %.1f ms (1M calls)  case4 %.1f ms (%d iterations)  residual %.9g\n", k.c_str(), i, c1, ms(t), solves * 20, out);
    fflush(stdout);
  }
  return 0;
}
