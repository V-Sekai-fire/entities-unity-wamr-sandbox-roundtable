using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using UnityEditor;

namespace Benchmark
{
    // Menu: Benchmark / Run all.  Batch: Unity -batchmode -nographics -projectPath . -executeMethod Benchmark.BenchRunner.RunBatch -quit
    // Writes results-unity.json in the project root.
    public static class BenchRunner
    {
        struct Rt { public string Name; public int Kind; public string File; }

        static string AotFile()
        {
            return UnityEngine.Application.platform == UnityEngine.RuntimePlatform.WindowsEditor ? "guest.win.aot" : "guest.aot";
        }

        static Rt[] Runtimes()
        {
            return new[]
        {
            new Rt { Name = "wamr-interp", Kind = 0, File = "guest.wasm" },
            new Rt { Name = "wamr-aot", Kind = 0, File = AotFile() },
            new Rt { Name = "libriscv-interp", Kind = 1, File = "guest.rv64.elf" },
            new Rt { Name = "libriscv-bt", Kind = 2, File = "guest.rv64.elf" },
        };
        }

        [MenuItem("Benchmark/Run all")]
        public static void RunMenu() { Run(1); }

        public static void RunBatch()
        {
            int scale = 1;
            var s = Environment.GetEnvironmentVariable("BENCH_SCALE");
            if (!string.IsNullOrEmpty(s)) int.TryParse(s, out scale);
            try { Run(Math.Max(1, scale)); }
            catch (Exception e) { UnityEngine.Debug.LogError(e); EditorApplication.Exit(1); }
        }

        static byte[] Image(string file)
        {
            return File.ReadAllBytes(Path.Combine(UnityEngine.Application.dataPath, "StreamingAssets", file));
        }

        static double Median(List<double> v) { v.Sort(); return v[v.Count / 2]; }
        static double Ms(Stopwatch sw) { return sw.Elapsed.TotalMilliseconds; }

        static void Run(int scale)
        {
            int calls = 1000000 / scale, solves = 50000 / scale, iters = 20, reps = 200;
            var rows = new StringBuilder();
            Action<string, string, double, double> row = (cs, rt, ms, chk) =>
            {
                if (rows.Length > 0) rows.Append(",\n");
                rows.Append(string.Format(CultureInfo.InvariantCulture,
                    " {{\"case\":\"{0}\",\"runtime\":\"{1}\",\"ms\":{2:0.###},\"check\":{3}}}", cs, rt, ms, chk.ToString("G17", CultureInfo.InvariantCulture)));
            };

            HostWorld.Create();
            if (HostWorld.Init() != 0) throw new Exception("ub_init failed");

            foreach (var r in Runtimes())
            {
                // libriscv binary translation compiles C with the system compiler; the Windows plugin has none.
                if (r.Kind == 2 && UnityEngine.Application.platform == UnityEngine.RuntimePlatform.WindowsEditor) continue;
                var img = Image(r.File);

                // Case 3: read from disk, instantiate, first update. First ever, then median of reps.
                for (int k = 0; k <= reps; k++)
                {
                    HostWorld.Reset();
                    var sw = Stopwatch.StartNew();
                    var b = Image(r.File);
                    int id = NativeBench.ub_open(r.Kind, b, b.Length);
                    bool ok = id > 0 && NativeBench.ub_update(id) == 1;
                    double ms = Ms(sw);
                    double chk = ok ? HostWorld.Sum() : -1;
                    if (id > 0) NativeBench.ub_close(id);
                    if (k == 0) row("3_load_first_update_first_ever", r.Name, ms, chk);
                    else
                    {
                        if (k == 1) _times = new List<double>();
                        _times.Add(ms);
                        if (k == reps) row("3_load_first_update", r.Name, Median(_times), chk);
                    }
                }

                int h = NativeBench.ub_open(r.Kind, img, img.Length);
                if (h < 1) throw new Exception(r.Name + ": ub_open failed " + h);
                UnityEngine.Debug.Log("[bench] " + r.Name + " binary translated: " + (NativeBench.ub_translated(h) == 1));

                // Case 1: an empty export, 'calls' times. Once with the loop in C# (every call crosses P/Invoke),
                // once with the loop in native code (the cost of the call itself).
                {
                    var sw = Stopwatch.StartNew(); bool ok = true;
                    for (int i = 0; i < calls && ok; i++) ok = NativeBench.ub_empty(h) == 1;
                    row("1_empty_call_from_csharp", r.Name, Ms(sw), ok ? 1 : 0);
                    sw = Stopwatch.StartNew();
                    ok = NativeBench.ub_empty_n(h, calls) == 1;
                    row("1_empty_call_native_loop", r.Name, Ms(sw), ok ? 1 : 0);
                }

                // Case 2: 10,000 Unity transform reads and writes, AOS then SOA.
                for (int soa = 0; soa < 2; soa++)
                {
                    var w = new List<double>(); double c = 0;
                    for (int k = 0; k < reps; k++)
                    {
                        HostWorld.Reset();
                        var sw = Stopwatch.StartNew();
                        NativeBench.ub_transforms(h, HostWorld.Count, soa);
                        w.Add(Ms(sw));
                        c = HostWorld.Sum();
                    }
                    row(soa == 1 ? "2_transforms_10k_soa" : "2_transforms_10k_aos", r.Name, Median(w), c);
                }

                // Case 4: L-BFGS saddle, solves x iters iterations, one call.
                {
                    var sw = Stopwatch.StartNew();
                    double f;
                    bool ok = NativeBench.ub_lbfgs(h, solves, iters, out f) == 1;
                    row("4_lbfgs", r.Name, Ms(sw), ok ? f : -1);
                }
                NativeBench.ub_close(h);
            }
            HostWorld.Destroy();

            var json = string.Format(CultureInfo.InvariantCulture,
                "{{\"engine\":\"Unity {0}\",\"scale\":{1},\"calls\":{2},\"lbfgs_iterations\":{3},\"rows\":[\n{4}\n]}}\n",
                UnityEngine.Application.unityVersion, scale, calls, solves * iters, rows);
            var path = Path.Combine(Path.GetDirectoryName(UnityEngine.Application.dataPath), "results-unity.json");
            File.WriteAllText(path, json);
            UnityEngine.Debug.Log("Benchmark written to " + path + "\n" + json);
        }

        static List<double> _times;
    }
}
