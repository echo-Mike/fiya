#!/usr/bin/env python3
"""
gen_synthetic.py — generate a synthetic C++ workload exercising many
distinct call-stack combinations, suitable for comparing a
__cyg_profile_func_enter/exit-based instrumentation library against a
baseline build.

Structure
---------
Layered call-graph:

    level 0       : 1 root
    level 1..L-1  : W nodes each, F distinct children in the next level
    level L       : NL leaves

    Distinct root->leaf paths  =  F^L

With the defaults (L=8, F=6) that's 1,679,616 paths.

Every node executes a small but realistic std:: workload (std::vector,
std::string, std::sort, std::unique_ptr, virtual dispatch) so the
instrumentation sees non-trivial leaf functions.

A single call to f_0_0() traverses every one of the F^L paths exactly
once (≈ 2M function calls for the defaults). main() times each such
traversal with std::chrono::steady_clock and prints one line per
iteration — that line is your "coarse interval" measurement.

Build/compare
-------------
    # 1) generate
    python3 gen_synthetic.py --levels 8 --fanout 6 --iterations 8 -o synth.cpp

    # 2) build plain
    g++ -O2 -std=c++20 synth.cpp -o synth_plain

    # 3) build instrumented
    g++ -O2 -std=c++20 -finstrument-functions synth.cpp \
        -o synth_instr -I/path/to/yourlib -L/path/to/yourlib -lyourlib

    # 4) run both
    ./synth_plain  8 2 > plain.txt
    ./synth_instr  8 2 > instr.txt

    # 5) compare the "iter N: X ms" lines. If your hook is linear,
    #    instr_ms(N) / plain_ms(N) should be constant. Deviations are
    #    the non-linearity you're hunting for.
"""
import argparse
import random
import sys


def emit_workload(out, i, j, style):
    """Emit workload body for node (i, j)."""
    vsize = 8 + ((i * 13 + j * 7) % 24)
    seed = (i * 101 + j * 31) % 100000
    vec_n = 6 + ((i * j + 1) % 8)

    if style == "none":
        out.append(f'  g_sink += 1u;')
        return

    if style == "light":
        out.append('  {')
        out.append(f'    Payload p({vsize}, {seed}u);')
        out.append(f'    std::sort(p.v.begin(), p.v.end());')
        out.append(f'    g_sink += (uint64_t)p.v[{vsize // 2}];')
        out.append('  }')
        return

    # heavy (default)
    out.append('  {')
    out.append(f'    Payload p({vsize}, {seed}u);')
    out.append(f'    g_sink ^= p.checksum();')
    out.append(f'    std::vector<int> tmp(p.v.begin(), p.v.end());')
    out.append(f'    std::partial_sort(tmp.begin(),')
    out.append(f'                     tmp.begin() + std::min<size_t>(4, tmp.size()),')
    out.append(f'                     tmp.end());')
    out.append(f'    for (size_t k = 0; k < tmp.size(); k += 3)')
    out.append(f'        g_sink += (uint64_t)tmp[k];')
    out.append('  }')
    out.append('  {')
    out.append(f'    std::string s; s.reserve(16); s += "n{i}_{j}";')
    out.append(f'    for (char c : s) g_sink = g_sink * 33u + (uint8_t)c;')
    out.append('  }')
    out.append('  {')
    out.append(f'    std::unique_ptr<BaseTask> t;')
    out.append(f'    if ((({i}) + ({j})) & 1)')
    out.append(f'        t = std::make_unique<MulTask>();')
    out.append(f'    else')
    out.append(f'        t = std::make_unique<VecTask>({vec_n});')
    out.append(f'    g_sink ^= t->run({i} * 31 + {j});')
    out.append('  }')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--levels", type=int, default=8,
                    help="call-graph depth (default 8)")
    ap.add_argument("--fanout", type=int, default=6,
                    help="children per node (default 6)")
    ap.add_argument("--width", type=int, default=64,
                    help="nodes per intermediate level (default 64)")
    ap.add_argument("--leaves", type=int, default=256,
                    help="number of leaf nodes (default 256)")
    ap.add_argument("--iterations", type=int, default=8,
                    help="timed iterations of the full traversal")
    ap.add_argument("--warmup", type=int, default=2,
                    help="untimed warmup iterations")
    ap.add_argument("--workload", choices=["none", "light", "heavy"],
                    default="heavy")
    ap.add_argument("--seed", type=int, default=0xC0FFEE)
    ap.add_argument("-o", "--output", default="synthetic.cpp")
    args = ap.parse_args()

    L, F, W, NL = args.levels, args.fanout, args.width, args.leaves
    if L < 1:
        sys.exit("--levels must be >= 1")
    if F < 1:
        sys.exit("--fanout must be >= 1")
    if W < F or NL < F:
        sys.exit("--width/--leaves must be >= --fanout")

    paths = F ** L
    sys.stderr.write(f"[gen] levels={L} fanout={F} width={W} leaves={NL}\n")
    sys.stderr.write(f"[gen] distinct paths = {paths:,}\n")
    if paths < 800_000:
        sys.stderr.write("[gen] WARNING: fewer than 800k paths\n")

    rng = random.Random(args.seed)

    # Level sizes: [1, W, W, ..., W, NL]
    level_sizes = [1] + [W] * (L - 1) + [NL]

    # Assign children of each non-leaf node
    children = {}
    for i in range(L):
        n_next = level_sizes[i + 1]
        for j in range(level_sizes[i]):
            children[(i, j)] = rng.sample(range(n_next), F)

    out = []
    out.append("// AUTO-GENERATED by gen_synthetic.py — do not edit.")
    out.append(f"// levels={L} fanout={F} paths={paths}")
    out.append("")
    out.append("#include <cstdio>")
    out.append("#include <cstdint>")
    out.append("#include <cstdlib>")
    out.append("#include <vector>")
    out.append("#include <string>")
    out.append("#include <algorithm>")
    out.append("#include <numeric>")
    out.append("#include <chrono>")
    out.append("#include <memory>")
    out.append("")
    out.append("#include <fiya/connectors/fiya-connector-interface.h>")
    out.append("")
    out.append("using Clock = std::chrono::steady_clock;")
    out.append("")
    out.append("static uint64_t g_sink = 0;")
    out.append("")
    out.append("struct Payload {")
    out.append("    std::vector<int> v;")
    out.append("    std::string tag;")
    out.append("    Payload() = default;")
    out.append("    Payload(size_t n, uint32_t seed) : tag(\"p\") {")
    out.append("        v.reserve(n);")
    out.append("        for (size_t i = 0; i < n; ++i)")
    out.append("            v.push_back(int((seed * 1103515245u + i * 12345u) & 0xFFFF));")
    out.append("    }")
    out.append("    uint64_t checksum() const {")
    out.append("        uint64_t s = tag.size();")
    out.append("        for (int x : v) s = s * 1315423911u + uint64_t(x);")
    out.append("        return s;")
    out.append("    }")
    out.append("};")
    out.append("")
    out.append("struct BaseTask {")
    out.append("    virtual ~BaseTask() = default;")
    out.append("    virtual uint64_t run(int seed) = 0;")
    out.append("};")
    out.append("struct MulTask : BaseTask {")
    out.append("    uint64_t run(int seed) override {")
    out.append("        return uint64_t(seed) * 2654435761u;")
    out.append("    }")
    out.append("};")
    out.append("struct VecTask : BaseTask {")
    out.append("    std::vector<int> data;")
    out.append("    explicit VecTask(int n) : data(n) {")
    out.append("        std::iota(data.begin(), data.end(), 0);")
    out.append("    }")
    out.append("    uint64_t run(int seed) override {")
    out.append("        std::sort(data.begin(), data.end(),")
    out.append("                  [seed](int a, int b){")
    out.append("                      return ((a ^ seed) & 7) < ((b ^ seed) & 7);")
    out.append("                  });")
    out.append("        uint64_t s = 0;")
    out.append("        for (int x : data) s += uint64_t(x);")
    out.append("        return s;")
    out.append("    }")
    out.append("};")
    out.append("")

    # Forward declarations
    for i in range(L + 1):
        for j in range(level_sizes[i]):
            out.append(f'__attribute__((noinline)) void f_{i}_{j}();')
    out.append("")

    # Function definitions
    for i in range(L + 1):
        for j in range(level_sizes[i]):
            out.append(f'__attribute__((noinline)) void f_{i}_{j}() {{')
            out.append(f'  // ---- node L{i} #{j} ----')
            emit_workload(out, i, j, args.workload)
            if i < L:
                out.append("  // recurse into children (covers all paths)")
                for k in children[(i, j)]:
                    out.append(f'  f_{i+1}_{k}();')
            out.append("}")
            out.append("")

    # main()
    out.append(f'''
int main(int argc, char** argv) {{
    int iterations = {args.iterations};
    int warmup     = {args.warmup};
    if (argc > 1) iterations = std::atoi(argv[1]);
    if (argc > 2) warmup     = std::atoi(argv[2]);

    // Warm up caches / branch predictors / the hook's internal state.
    for (int w = 0; w < warmup; ++w) {{
        f_0_0();
    }}

    for (int it = 0; it < iterations; ++it) {{
        fiya::connectors::start_flamegraph();
        auto t0 = Clock::now();
        f_0_0();                       // full traversal of every path
        auto t1 = Clock::now();
        fiya::connectors::stop_flamegraph();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        std::printf("iter %d: %.3f ms  (sink=%llu)\\n",
                    it, ns / 1e6, (unsigned long long)g_sink);
        std::fflush(stdout);
    }}
    std::printf("done sink=%llu\\n", (unsigned long long)g_sink);
    return 0;
}}
''')

    with open(args.output, "w") as fp:
        fp.write("\n".join(out))

    total_calls = sum(F ** k for k in range(L + 1))
    sys.stderr.write(f"[gen] nodes = {sum(level_sizes):,}  "
                     f"calls per traversal ≈ {total_calls:,}\n")
    sys.stderr.write(f"[gen] wrote {args.output}\n")


if __name__ == "__main__":
    main()
