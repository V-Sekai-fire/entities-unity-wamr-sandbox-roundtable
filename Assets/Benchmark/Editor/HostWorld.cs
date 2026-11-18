using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace Benchmark
{
    // The host API the guests call: N real Unity Transforms, read and written from native code.
    // A transform is 12 floats: three basis rows (from localRotation), then the origin (localPosition).
    // Writes apply the origin only.
    public static unsafe class HostWorld
    {
        public const int Count = 10000;
        static Transform[] s_transforms;
        static GameObject s_root;
        static float[] s_mirror;  // SOA: 12 columns of Count floats, filled in one pass and flushed in one pass

        // Kept alive so the native side never holds a collected delegate.
        static NativeBench.GetFn s_get = Get;
        static NativeBench.SetFn s_set = Set;
        static NativeBench.GetColFn s_getCol = GetColumn;
        static NativeBench.SetColFn s_setCol = SetColumn;

        public static void Create()
        {
            Destroy();
            s_root = new GameObject("BenchWorld") { hideFlags = HideFlags.HideAndDontSave };
            s_transforms = new Transform[Count];
            for (int i = 0; i < Count; i++)
            {
                var go = new GameObject("t" + i);
                go.transform.SetParent(s_root.transform, false);
                s_transforms[i] = go.transform;
            }
            s_mirror = new float[12 * Count];
        }

        public static void Destroy()
        {
            if (s_root != null) UnityEngine.Object.DestroyImmediate(s_root);
            s_root = null; s_transforms = null;
        }

        public static void Reset()
        {
            for (int i = 0; i < Count; i++) s_transforms[i].localPosition = Vector3.zero;
        }

        public static double Sum()
        {
            double s = 0;
            for (int i = 0; i < Count; i++) { var p = s_transforms[i].localPosition; s += p.x + p.y + p.z; }
            return s;
        }

        public static int Init()
        {
            return NativeBench.ub_init(
                Marshal.GetFunctionPointerForDelegate(s_get),
                Marshal.GetFunctionPointerForDelegate(s_set),
                Marshal.GetFunctionPointerForDelegate(s_getCol),
                Marshal.GetFunctionPointerForDelegate(s_setCol));
        }

        static void Read(int i, float* t)
        {
            var tr = s_transforms[i];
            var m = Matrix4x4.Rotate(tr.localRotation);
            t[0] = m.m00; t[1] = m.m01; t[2] = m.m02;
            t[3] = m.m10; t[4] = m.m11; t[5] = m.m12;
            t[6] = m.m20; t[7] = m.m21; t[8] = m.m22;
            var p = tr.localPosition;
            t[9] = p.x; t[10] = p.y; t[11] = p.z;
        }

        static void Get(int idx, float* t) { Read(idx, t); }
        static void Set(int idx, float* t) { s_transforms[idx].localPosition = new Vector3(t[9], t[10], t[11]); }

        static void GetColumn(int c, float* dst, int n)
        {
            if (c == 0)
            {
                float* tmp = stackalloc float[12];
                for (int i = 0; i < n; i++)
                {
                    Read(i, tmp);
                    for (int k = 0; k < 12; k++) s_mirror[k * Count + i] = tmp[k];
                }
            }
            for (int i = 0; i < n; i++) dst[i] = s_mirror[c * Count + i];
        }

        static void SetColumn(int c, float* src, int n)
        {
            for (int i = 0; i < n; i++) s_mirror[c * Count + i] = src[i];
            if (c == 11)
                for (int i = 0; i < n; i++)
                    s_transforms[i].localPosition = new Vector3(s_mirror[9 * Count + i], s_mirror[10 * Count + i], s_mirror[11 * Count + i]);
        }
    }
}
