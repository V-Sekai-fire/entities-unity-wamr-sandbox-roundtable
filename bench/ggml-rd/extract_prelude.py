#!/usr/bin/env python3
"""Write slang-cpp-prelude.h from the prelude every slangc cpp emit carries at its top (one include-guarded block).

usage: extract_prelude.py <any *_emit.cpp> <out.h>
The block starts with `#ifndef SLANG_CPP_PRELUDE_H`; it ends at the #endif that brings the #if depth back to zero."""
import re
import sys

lines = open(sys.argv[1], encoding="utf-8").read().split("\n")
if not lines[0].startswith("#ifndef SLANG_CPP_PRELUDE_H"):
    sys.exit("extract_prelude: %s does not start with the prelude guard" % sys.argv[1])
depth = 0
for i, l in enumerate(lines):
    t = l.strip()
    if re.match(r"#\s*if(n?def)?\b", t):
        depth += 1
    elif re.match(r"#\s*endif\b", t):
        depth -= 1
        if depth == 0:
            open(sys.argv[2], "w", encoding="utf-8").write("\n".join(lines[: i + 1]) + "\n")
            sys.exit(0)
sys.exit("extract_prelude: the guard never closed")
