import json, sys
d = json.load(open(sys.argv[1]))
rts, cases, t = [], [], {}
for r in d["rows"]:
    t[(r["case"], r["runtime"])] = r
    if r["runtime"] not in rts: rts.append(r["runtime"])
    if r["case"] not in cases: cases.append(r["case"])
print("| case | " + " | ".join(rts) + " |")
print("|---|" + "---|" * len(rts))
for c in cases:
    label = c + (" (5 KB test guest, INVALID for case 3)" if c.startswith("3_") else "")
    print(f"| {label} | " + " | ".join(f"{t[(c, r)]['ms']:.3f}" for r in rts if (c, r) in t) + " |")
chk = {r: t[("4_lbfgs", r)]["check"] for r in rts if ("4_lbfgs", r) in t}
print("\nL-BFGS residual sums:", chk)
