// Guest entry points. wasm: a reactor, so WAMR runs _initialize (the op registrars) at instantiation.
// rv64: a static executable whose main only returns, after the C runtime ran the registrars.
extern "C" double lbfgs_rd(int, int);
extern "C" __attribute__((used, visibility("default"))) double lbfgs(int solves, int iters) { return lbfgs_rd(solves, iters); }
#ifndef __wasm__
// _exit, not return: exit() would run the static destructors (the op table) before the host calls lbfgs.
#include <unistd.h>
int main() { _exit(0); }
#endif

// ggml.c names three backend-buffer calls. These tensors live in the context's own memory, never in a backend
// buffer, so the calls cannot be reached; a stub that aborts makes a mistake loud.
#include <cstdlib>
extern "C" {
int ggml_backend_buffer_get_usage(void *) { std::abort(); }
void ggml_backend_tensor_memset(void *, unsigned char, unsigned long, unsigned long) { std::abort(); }
void ggml_backend_tensor_set(void *, const void *, unsigned long, unsigned long) { std::abort(); }
}
