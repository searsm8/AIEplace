#!/usr/bin/env python3
"""Render selected HPWL-gradient-CU signals from the hw_emu VCD as an SVG digital waveform."""
import re

VCD = "/home/msears/aieplace_tmp/hpwl_cu.vcd"
OUT = "/home/msears/aieplace_tmp/hpwl_cu_waveform.svg"
K = "design_1_wrapper_sim_wrapper/design_1_wrapper_i/design_1_i/VitisRegion/top_1/inst/"

# (label, signal-suffix under the kernel scope)
WANT = [
    ("ap_clk",              K + "ap_clk"),
    ("ap_start",            K + "ap_start"),
    ("ap_idle",             K + "ap_idle"),
    ("ap_done",             K + "ap_done"),
    ("gmem0 ARVALID (node_pos rd addr)", K + "m_axi_gmem0_ARVALID"),
    ("gmem0 RVALID (node_pos rd data)",  K + "m_axi_gmem0_RVALID"),
    ("gmem2 ARVALID (pins rd addr)",     K + "m_axi_gmem2_ARVALID"),
    ("gmem7 AWVALID (node_grad wr)",     K + "m_axi_gmem7_AWVALID"),
    ("hpwl_sweep ap_start", K + "grp_top_Pipeline_hpwl_sweep_fu_622/ap_start"),
    ("hpwl_sweep ap_done",  K + "grp_top_Pipeline_hpwl_sweep_fu_622/ap_done"),
]


def parse(path):
    name2sym, changes = {}, {}
    scope, header = [], False
    t = 0
    for line in open(path):
        s = line.strip()
        if not header:
            if s.startswith("$scope"):
                scope.append(s.split()[2])
            elif s.startswith("$upscope"):
                scope and scope.pop()
            elif s.startswith("$var"):
                p = s.split()
                name2sym["/".join(scope) + "/" + p[4]] = p[3]
            elif s.startswith("$enddefinitions"):
                header = True
            continue
        if s.startswith("#"):
            t = int(s[1:])
        elif s and s[0] in "01xzXZ":
            changes.setdefault(s[1:], []).append((t, s[0]))
        elif s and s[0] in "bBrR":
            v, sym = s[1:].split()
            changes.setdefault(sym, []).append((t, v))
    return name2sym, changes


def val_at(seq, t):
    v = "x"
    for (tt, vv) in seq:
        if tt <= t:
            v = vv
        else:
            break
    return v


def main():
    name2sym, changes = parse(VCD)
    series = []
    for label, full in WANT:
        sym = name2sym.get(full)
        series.append((label, changes.get(sym, []) if sym else []))

    # active window: first ap_start high .. a bit after last ap_done high
    start_seq = series[1][1]
    done_seq = series[3][1]
    t0 = next((t for t, v in start_seq if v == "1"), 0)
    t1 = next((t for t, v in reversed(done_seq) if v == "1"), 0)
    if t1 <= t0:
        t1 = max((seq[-1][0] for _, seq in series if seq), default=t0 + 1000)
    pad = int((t1 - t0) * 0.06) + 100
    t0 -= pad; t1 += pad
    span = max(t1 - t0, 1)

    # layout
    W, left, right = 1180, 260, 40
    plot_w = W - left - right
    row_h, top = 34, 70
    H = top + row_h * len(series) + 50

    def X(t):
        return left + (t - t0) / span * plot_w

    dark_bg = "#0d1117"; fg = "#c9d1d9"; grid = "#30363d"
    hi = "#3fb950"; clk_c = "#58a6ff"; axi = "#d29922"
    out = []
    out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="monospace">')
    out.append(f'<rect width="{W}" height="{H}" fill="{dark_bg}"/>')
    out.append(f'<text x="{left}" y="34" fill="{fg}" font-size="17" font-weight="bold">hpwl_CU (HPWL gradient compute unit) — hw_emu RTL waveform, MODE_HPWL_GRAD</text>')
    out.append(f'<text x="{left}" y="54" fill="#8b949e" font-size="12">synthetic 6-node/3-net/9-pin case · PASS vs CPU golden (rel_rms 1.4e-7) · window {t0/1000:.1f}–{t1/1000:.1f} ns</text>')
    # time gridlines
    for i in range(0, 11):
        gx = left + i * plot_w / 10
        tt = t0 + i * span / 10
        out.append(f'<line x1="{gx:.1f}" y1="{top-8}" x2="{gx:.1f}" y2="{H-40}" stroke="{grid}" stroke-width="0.5"/>')
        out.append(f'<text x="{gx:.1f}" y="{H-24}" fill="#8b949e" font-size="10" text-anchor="middle">{tt/1000:.0f}ns</text>')

    for i, (label, seq) in enumerate(series):
        y = top + i * row_h
        yhi, ylo = y + 6, y + row_h - 10
        color = clk_c if "clk" in label else (axi if ("VALID" in label) else hi)
        out.append(f'<text x="{left-12}" y="{ylo}" fill="{fg}" font-size="11" text-anchor="end">{label}</text>')
        out.append(f'<line x1="{left}" y1="{ylo}" x2="{W-right}" y2="{ylo}" stroke="{grid}" stroke-width="0.5"/>')
        if not seq:
            out.append(f'<text x="{left+6}" y="{ylo-3}" fill="#8b949e" font-size="10">(no data)</text>')
            continue
        if "clk" in label:
            # too dense to draw every edge; estimate period and draw a labelled band
            edges = [t for t, v in seq if t0 <= t <= t1]
            per = (edges[-1] - edges[0]) / max(len(edges) - 1, 1) * 2 if len(edges) > 2 else 0
            out.append(f'<rect x="{left}" y="{yhi}" width="{plot_w}" height="{ylo-yhi}" fill="{clk_c}" opacity="0.10"/>')
            freq = (1e6 / per) if per else 0
            out.append(f'<text x="{left+6}" y="{ylo-3}" fill="{clk_c}" font-size="10">≈{freq:.0f} MHz ({(len(edges))} edges in window)</text>')
            continue
        # digital trace
        v = val_at(seq, t0)
        px = X(t0)
        pts = []
        def lvl(v): return yhi if v == "1" else ylo
        pts.append((px, lvl(v)))
        for (tt, vv) in seq:
            if tt < t0 or tt > t1:
                continue
            x = X(tt)
            pts.append((x, lvl(v)))     # horizontal to edge
            pts.append((x, lvl(vv)))    # vertical transition
            v = vv
        pts.append((X(t1), lvl(v)))
        path = " ".join(f"{'M' if j==0 else 'L'}{x:.1f},{yv:.1f}" for j, (x, yv) in enumerate(pts))
        out.append(f'<path d="{path}" fill="none" stroke="{color}" stroke-width="1.8"/>')
    out.append('</svg>')
    open(OUT, "w").write("\n".join(out))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
