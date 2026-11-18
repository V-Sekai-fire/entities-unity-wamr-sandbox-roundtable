/* Case 4, shared by every guest (wasm, rv64, the Godot sandbox guest): one source, so the runtimes run the same code. */
/* Case 4: L-BFGS (m = 5, Armijo backtracking) on f(x) = 1/2 |Hx - b|^2.
 * H is symmetric indefinite (alternating-sign diagonal, 0.5 off-diagonal), so
 * the minimiser of f is the saddle point of g with grad g = Hx - b. */
#define N 16
#define M 5
static void hmul(const double *x, double *y) {
  for (int i = 0; i < N; i++) {
    double d = (i & 1) ? -2.0 - 0.1 * i : 2.0 + 0.1 * i;
    double v = d * x[i];
    if (i > 0) v += 0.5 * x[i - 1];
    if (i < N - 1) v += 0.5 * x[i + 1];
    y[i] = v;
  }
}
static double fgrad(const double *x, const double *b, double *g) {
  double r[N];
  hmul(x, r);
  double f = 0;
  for (int i = 0; i < N; i++) {
    r[i] -= b[i];
    f += 0.5 * r[i] * r[i];
  }
  hmul(r, g); /* H symmetric: grad = H (Hx - b) */
  return f;
}
static double dot(const double *a, const double *b) {
  double s = 0;
  for (int i = 0; i < N; i++) s += a[i] * b[i];
  return s;
}
static double solve(int iters, int seed) {
  double b[N], x[N], g[N], gn[N], xn[N], d[N];
  double S[M][N], Y[M][N], rho[M], alpha[M];
  for (int i = 0; i < N; i++) { b[i] = 1.0 + 0.01 * i + 0.001 * ((seed + i) & 7); x[i] = 0; }
  double f = fgrad(x, b, g);
  int hist = 0, head = 0;
  for (int it = 0; it < iters; it++) {
    for (int i = 0; i < N; i++) d[i] = -g[i];
    for (int k = 0; k < hist; k++) {
      int j = (head - 1 - k + M) % M;
      alpha[j] = rho[j] * dot(S[j], d);
      for (int i = 0; i < N; i++) d[i] -= alpha[j] * Y[j][i];
    }
    if (hist) {
      int j = (head - 1 + M) % M;
      double gam = dot(S[j], Y[j]) / dot(Y[j], Y[j]);
      for (int i = 0; i < N; i++) d[i] *= gam;
    }
    for (int k = hist - 1; k >= 0; k--) {
      int j = (head - 1 - k + M) % M;
      double beta = rho[j] * dot(Y[j], d);
      for (int i = 0; i < N; i++) d[i] += S[j][i] * (alpha[j] - beta);
    }
    double gd = dot(g, d);
    if (gd >= 0) { for (int i = 0; i < N; i++) d[i] = -g[i]; gd = -dot(g, g); }
    double step = hist ? 1.0 : 1.0 / (1.0 + __builtin_sqrt(dot(g, g)));
    double fn = f;
    for (int ls = 0; ls < 20; ls++) {
      for (int i = 0; i < N; i++) xn[i] = x[i] + step * d[i];
      fn = fgrad(xn, b, gn);
      if (fn <= f + 1e-4 * step * gd) break;
      step *= 0.5;
    }
    double sy = 0;
    for (int i = 0; i < N; i++) {
      S[head][i] = xn[i] - x[i];
      Y[head][i] = gn[i] - g[i];
    }
    sy = dot(S[head], Y[head]);
    if (sy > 1e-12) {
      rho[head] = 1.0 / sy;
      head = (head + 1) % M;
      if (hist < M) hist++;
    }
    for (int i = 0; i < N; i++) { x[i] = xn[i]; g[i] = gn[i]; }
    f = fn;
  }
  return f;
}
/* `solves` solves of `iters` iterations each. Returns the final residual of the last solve. */
static double lbfgs_run(int solves, int iters) {
  double f = 0;
  for (int s = 0; s < solves; s++) f += solve(iters, s);
  return f;
}

