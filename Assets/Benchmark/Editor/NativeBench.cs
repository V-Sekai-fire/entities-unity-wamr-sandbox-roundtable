using System;
using System.Runtime.InteropServices;

namespace Benchmark
{
    // The C ABI of ub_native (native/ub_native.cpp).
    public static unsafe class NativeBench
    {
        const string Lib = "ub_native";

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void GetFn(int idx, float* t12);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void SetFn(int idx, float* t12);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void GetColFn(int c, float* dst, int n);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void SetColFn(int c, float* src, int n);

        [DllImport(Lib)] public static extern int ub_init(IntPtr get, IntPtr set, IntPtr getColumn, IntPtr setColumn);
        [DllImport(Lib)] public static extern int ub_open(int kind, byte[] image, int len);
        [DllImport(Lib)] public static extern void ub_close(int id);
        [DllImport(Lib)] public static extern int ub_translated(int id);
        [DllImport(Lib)] public static extern int ub_empty(int id);
        [DllImport(Lib)] public static extern int ub_empty_n(int id, int n);
        [DllImport(Lib)] public static extern int ub_update(int id);
        [DllImport(Lib)] public static extern int ub_transforms(int id, int n, int soa);
        [DllImport(Lib)] public static extern int ub_lbfgs(int id, int solves, int iters, out double result);
    }
}
