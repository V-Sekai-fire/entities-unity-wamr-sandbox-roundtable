// The benchmark guest as a godot-sandbox program: cases 1 and 4. The L-BFGS source is the same file the wasm
// and rv64 guests use (../guest/lbfgs.h).
#include <api.hpp>
#include "../guest/lbfgs.h"

// The editor refuses a guest that does not state its Variant size. Same marker as contract-tris-to-quads-guest:
// 'SBXV' and the size, 40 bytes for the double-precision API (this build), 24 for single.
__attribute__((used, retain, section(".sandbox_variant")))
static const unsigned char sandbox_variant_marker[8] = {'S', 'B', 'X', 'V', sizeof(Variant), 0, 0, 0};

static Variant empty() { return Nil; }
static Variant lbfgs(int solves, int iters) { return lbfgs_run(solves, iters); }

int main() {
	ADD_API_FUNCTION(empty, "void", "", "Case 1: an empty export");
	ADD_API_FUNCTION(lbfgs, "double", "int solves, int iters", "Case 4: L-BFGS on a saddle problem");
	halt();
}
