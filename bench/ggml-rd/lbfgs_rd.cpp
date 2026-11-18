// Case 4g: the L-BFGS of case 4 with every vector and matrix operation run by ggml-rd's kernels (their slangc cpp
// emits, the CPU path of the backend that otherwise runs the same kernels as SPIR-V on the GPU). Each op is built
// as a ggml node and packed once by the guest's own packers (rd_pack.cpp, ops/*.cpp); an iteration then only
// re-runs the packed dispatches. Scalar bookkeeping (rho, alpha, the line-search test) is plain host code.
// ggml-rd's kernels are f32, so this case has its own residual; it is compared across runtimes, not with case 4.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ggml.h"
#include "rd_pack.h"
#include "run_kernel.h"

namespace {

using namespace ggml_rd;

constexpr int N = 16;
constexpr int M = 5;

struct Disp {
	int kernel = -1;
	uint32_t w[kWordsPerSlot];
	uint32_t groups[3];
};

struct Arena {
	ggml_context *ctx = nullptr;
	uint8_t *base = nullptr;
	size_t bytes = 0;
};
Arena A;

bool host_offset(const ggml_tensor *t, void *, uint64_t *off) {
	const uint8_t *d = static_cast<const uint8_t *>(t->data);
	if (d < A.base || d >= A.base + A.bytes) {
		return false;
	}
	*off = uint64_t(d - A.base);
	return true;
}

// Pack a node into d. False means ggml-rd has no kernel for it: the run stops, nothing is faked.
bool pack(ggml_tensor *node, Disp &d) {
	const OpEntry *e = find_op(node);
	std::string why;
	if (e == nullptr || !fill_standard(d.w, node, host_offset, nullptr, &why)) {
		std::fprintf(stderr, "no ggml-rd packer for %s %s\n", ggml_op_desc(node), why.c_str());
		return false;
	}
	Pack p;
	p.node = node;
	p.w = d.w;
	if (!e->pack(p) || p.kernel < 0) {
		std::fprintf(stderr, "packer %s refused\n", e->name);
		return false;
	}
	d.w[W_KERNEL] = uint32_t(p.kernel);
	d.kernel = p.kernel;
	std::memcpy(d.groups, p.groups, sizeof d.groups);
	return true;
}

inline void run(Disp &d) {
	run_kernel(d.kernel, d.w, A.base, A.bytes, d.groups);
}

ggml_tensor *vec(const char *) {
	return ggml_new_tensor_2d(A.ctx, GGML_TYPE_F32, N, 1);
}
ggml_tensor *scalar() {
	return ggml_new_tensor_2d(A.ctx, GGML_TYPE_F32, 1, 1);
}
inline float *F(ggml_tensor *t) {
	return static_cast<float *>(t->data);
}

struct Solver {
	bool ok = true;
	ggml_tensor *H, *b, *x, *g, *gn, *d, *S[M], *Y[M];
	ggml_tensor *sc[3]; // scalar operands: step / alpha / gam, and -1
	ggml_tensor *neg1;
	// fgrad(x -> g) and fgrad(xn -> gn)
	struct FG {
		ggml_tensor *in, *r, *f, *out;
		Disp hx, sub, rr, hr;
	} fg[2];
	ggml_tensor *xn;
	Disp dstep, dxn; // xn = x + step * d
	Disp negg;       // d = -g
	Disp ax[2 * M][2]; // d += v * s: scaled copy, then in-place add
	Disp scal;         // d *= gam
	Disp dots[2 * M + 2][2 * M + 2];
	bool dot_made[2 * M + 2][2 * M + 2] = {};
	ggml_tensor *dot_out[2 * M + 2][2 * M + 2];
	ggml_tensor *vecs[2 * M + 2]; // S0..S4, Y0..Y4, d, g
	Disp cs[M], cy[M], cx, cg; // S[head] = xn - x, Y[head] = gn - g, x = xn, g = gn
	ggml_tensor *tmp_axpy[2 * M];

	bool make_dot(int a, int c) {
		if (dot_made[a][c]) {
			return true;
		}
		ggml_tensor *r = ggml_mul_mat(A.ctx, vecs[a], vecs[c]);
		dot_out[a][c] = r;
		dot_made[a][c] = true;
		return pack(r, dots[a][c]);
	}
	float dot(int a, int c) {
		if (!make_dot(a, c)) {
			ok = false;
			return 0;
		}
		run(dots[a][c]);
		return F(dot_out[a][c])[0];
	}

	bool build() {
		H = ggml_new_tensor_2d(A.ctx, GGML_TYPE_F32, N, N);
		b = vec("b");
		x = vec("x");
		g = vec("g");
		gn = vec("gn");
		d = vec("d");
		for (int j = 0; j < M; j++) {
			S[j] = vec("S");
			Y[j] = vec("Y");
			vecs[j] = S[j];
			vecs[M + j] = Y[j];
		}
		vecs[2 * M] = d;
		vecs[2 * M + 1] = g;
		for (auto &s : sc) {
			s = scalar();
		}
		neg1 = scalar();
		F(neg1)[0] = -1.0f;
		for (int i = 0; i < N; i++) {
			for (int k = 0; k < N; k++) {
				double v = 0;
				if (i == k) {
					v = (i & 1) ? -2.0 - 0.1 * i : 2.0 + 0.1 * i;
				} else if (k == i - 1 || k == i + 1) {
					v = 0.5;
				}
				F(H)[i * N + k] = float(v);
			}
		}
		// fgrad: r = H x - b; f = 0.5 r.r; g = H r
		ggml_tensor *ins[2];
		xn = vec("xn");
		ins[0] = x;
		ins[1] = xn;
		ggml_tensor *outs[2] = { g, gn };
		for (int k = 0; k < 2; k++) {
			FG &q = fg[k];
			q.in = ins[k];
			ggml_tensor *hx = ggml_mul_mat(A.ctx, H, q.in);
			ggml_tensor *r = ggml_sub(A.ctx, hx, b);
			ggml_tensor *rr = ggml_mul_mat(A.ctx, r, r);
			ggml_tensor *hr = ggml_mul_mat(A.ctx, H, r);
			ggml_tensor *o = ggml_cpy(A.ctx, hr, outs[k]);
			q.r = r;
			q.f = rr;
			q.out = o;
			if (!pack(hx, q.hx) || !pack(r, q.sub) || !pack(rr, q.rr) || !pack(hr, q.hr)) {
				return false;
			}
			// the copy into g / gn is its own dispatch
			static Disp cp[2];
			if (!pack(o, cp[k])) {
				return false;
			}
			cps[k] = &cp[k];
		}
		// xn = x + step * d
		ggml_tensor *sd = ggml_mul(A.ctx, d, sc[0]);
		ggml_tensor *sum = ggml_cpy(A.ctx, ggml_add(A.ctx, x, sd), xn);
		if (!pack(sd, dstep) || !pack(sum->src[0], dxn) || !pack(sum, dxn_cp)) {
			return false;
		}
		// d = -g
		ggml_tensor *ng = ggml_mul(A.ctx, g, neg1);
		ggml_tensor *ngc = ggml_cpy(A.ctx, ng, d);
		if (!pack(ng, negg) || !pack(ngc, negg_cp)) {
			return false;
		}
		// d += v * s for each S, Y
		for (int v = 0; v < 2 * M; v++) {
			ggml_tensor *t = ggml_mul(A.ctx, vecs[v], sc[1]);
			tmp_axpy[v] = t;
			ggml_tensor *a = ggml_add_inplace(A.ctx, d, t);
			if (!pack(t, ax[v][0]) || !pack(a, ax[v][1])) {
				return false;
			}
		}
		ggml_tensor *sg = ggml_mul_inplace(A.ctx, d, sc[2]);
		if (!pack(sg, scal)) {
			return false;
		}
		// S[head] = xn - x, Y[head] = gn - g, x = xn, g = gn
		for (int j = 0; j < M; j++) {
			ggml_tensor *s1 = ggml_cpy(A.ctx, ggml_sub(A.ctx, xn, x), S[j]);
			ggml_tensor *y1 = ggml_cpy(A.ctx, ggml_sub(A.ctx, gn, g), Y[j]);
			if (!pack(s1->src[0], cs_sub[j]) || !pack(s1, cs[j]) || !pack(y1->src[0], cy_sub[j]) || !pack(y1, cy[j])) {
				return false;
			}
		}
		ggml_tensor *cxt = ggml_cpy(A.ctx, xn, x);
		ggml_tensor *cgt = ggml_cpy(A.ctx, gn, g);
		return pack(cxt, cx) && pack(cgt, cg);
	}
	Disp *cps[2];
	Disp dxn_cp, negg_cp, cs_sub[M], cy_sub[M];

	// returns f; writes the gradient into out
	float fgrad(int k) {
		FG &q = fg[k];
		run(q.hx);
		run(q.sub);
		run(q.rr);
		float f = 0.5f * F(q.f)[0];
		run(q.hr);
		run(*cps[k]);
		return f;
	}
	void axpy(int v, float s) { // d += s * vecs[v]
		F(sc[1])[0] = s;
		run(ax[v][0]);
		run(ax[v][1]);
	}

	float solve(int iters, int seed) {
		for (int i = 0; i < N; i++) {
			F(b)[i] = float(1.0 + 0.01 * i + 0.001 * ((seed + i) & 7));
			F(x)[i] = 0;
		}
		float rho[M] = {}, alpha[M] = {};
		float f = fgrad(0);
		int hist = 0, head = 0;
		for (int it = 0; it < iters && ok; it++) {
			run(negg);
			run(negg_cp);
			for (int k = 0; k < hist; k++) {
				int j = (head - 1 - k + M) % M;
				alpha[j] = rho[j] * dot(j, 2 * M);
				axpy(M + j, -alpha[j]);
			}
			if (hist) {
				int j = (head - 1 + M) % M;
				float gam = dot(j, M + j) / dot(M + j, M + j);
				F(sc[2])[0] = gam;
				run(scal);
			}
			for (int k = hist - 1; k >= 0; k--) {
				int j = (head - 1 - k + M) % M;
				float beta = rho[j] * dot(M + j, 2 * M);
				axpy(j, alpha[j] - beta);
			}
			float gd = dot(2 * M + 1, 2 * M);
			if (gd >= 0) {
				run(negg);
				run(negg_cp);
				gd = -dot(2 * M + 1, 2 * M + 1);
			}
			float step = hist ? 1.0f : 1.0f / (1.0f + __builtin_sqrtf(dot(2 * M + 1, 2 * M + 1)));
			float fn = f;
			for (int ls = 0; ls < 20; ls++) {
				F(sc[0])[0] = step;
				run(dstep);
				run(dxn);
				run(dxn_cp);
				fn = fgrad(1);
				if (fn <= f + 1e-4f * step * gd) {
					break;
				}
				step *= 0.5f;
			}
			run(cs_sub[head]);
			run(cs[head]);
			run(cy_sub[head]);
			run(cy[head]);
			float sy = dot(head, M + head);
			if (sy > 1e-12f) {
				rho[head] = 1.0f / sy;
				head = (head + 1) % M;
				if (hist < M) {
					hist++;
				}
			}
			run(cx);
			run(cg);
			f = fn;
		}
		return f;
	}
};

} // namespace

extern "C" double lbfgs_rd(int solves, int iters) {
	static uint8_t pool[1 << 20] __attribute__((aligned(64)));
	static Solver s;
	static bool built = false;
	if (!built) {
		ggml_init_params ip = { sizeof pool, pool, false };
		A.ctx = ggml_init(ip);
		A.base = pool;
		A.bytes = sizeof pool;
		if (A.ctx == nullptr || !s.build()) {
			return -1.0;
		}
		built = true;
	}
	double f = 0;
	for (int i = 0; i < solves && s.ok; i++) {
		f += s.solve(iters, i);
	}
	return s.ok ? f : -2.0;
}
