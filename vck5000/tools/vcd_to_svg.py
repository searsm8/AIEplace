#!/usr/bin/env python3
"""Render pl_algo hw_emu kernel signals from a VCD as an SVG digital waveform.
Usage: vcd_to_svg2.py <vcd> <out.svg> "<title>" "<subtitle>" """
import re
import sys

K = "top_1/inst/"


def parse(path):
    name2sym, changes, scope, header, t = {}, {}, [], False, 0
    for line in open(path):
        s = line.strip()
        if not header:
            if s.startswith("$scope"):
                scope.append(s.split()[2])
            elif s.startswith("$upscope"):
                scope and scope.pop()
            elif s.startswith("$var"):
                p = s.split(); name2sym["/".join(scope) + "/" + p[4]] = p[3]
            elif s.startswith("$enddefinitions"):
                header = True
            continue
        if s.startswith("#"):
            t = int(s[1:])
        elif s and s[0] in "01xzXZ":
            changes.setdefault(s[1:], []).append((t, s[0]))
        elif s and s[0] in "bBrR":
            v, sym = s[1:].split(); changes.setdefault(sym, []).append((t, v))
    return name2sym, changes


def main():
    vcd, out, title = sys.argv[1], sys.argv[2], sys.argv[3]
    sub = sys.argv[4] if len(sys.argv) > 4 else ""
    name2sym, changes = parse(vcd)

    def kget(suf):
        for full, sym in name2sym.items():
            if full.endswith(K + suf):
                return sym
        return None

    series = [(l, changes.get(kget(s), [])) for l, s in
              [("ap_clk", "ap_clk"), ("ap_start", "ap_start"), ("ap_idle", "ap_idle"), ("ap_done", "ap_done")]]
    axi = []
    for full, sym in name2sym.items():
        m = re.search(r"top_1/inst/(m_axi_gmem(\d+)_(AR|R|AW|W)VALID)$", full)
        if m and len(changes.get(sym, [])) > 2:
            axi.append((int(m.group(2)), m.group(3), sym))
    axi.sort()
    kn = {"AR": "rd-addr", "R": "rd-data", "AW": "wr-addr", "W": "wr-data"}
    for gid, kind, sym in axi:
        series.append(("gmem%d %sVALID (%s)" % (gid, kind, kn[kind]), changes.get(sym, [])))

    ss, ds = series[1][1], series[3][1]
    t0 = next((t for t, v in ss if v == "1"), 0)
    t1 = next((t for t, v in reversed(ds) if v == "1"), 0)
    if t1 <= t0:
        t1 = max((seq[-1][0] for _, seq in series if seq), default=t0 + 1000)
    pad = int((t1 - t0) * 0.05) + 100
    t0 -= pad; t1 += pad; span = max(t1 - t0, 1)
    W, left, right, row_h, top = 1200, 300, 40, 30, 74
    plot_w = W - left - right; H = top + row_h * len(series) + 46
    def X(t): return left + (t - t0) / span * plot_w
    def lvl(v, a, b): return a if v == "1" else b
    bg, fg, grid = "#0d1117", "#c9d1d9", "#30363d"
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="monospace">',
         f'<rect width="{W}" height="{H}" fill="{bg}"/>',
         f'<text x="{left}" y="30" fill="{fg}" font-size="16" font-weight="bold">{title}</text>',
         f'<text x="{left}" y="52" fill="#8b949e" font-size="11">{sub}</text>']
    for i in range(11):
        gx = left + i * plot_w / 10
        o.append(f'<line x1="{gx:.1f}" y1="{top-8}" x2="{gx:.1f}" y2="{H-38}" stroke="{grid}" stroke-width="0.5"/>')
        o.append(f'<text x="{gx:.1f}" y="{H-22}" fill="#8b949e" font-size="9.5" text-anchor="middle">{(t0+i*span/10)/1000:.0f}ns</text>')
    for i, (label, seq) in enumerate(series):
        y = top + i * row_h; yhi, ylo = y + 5, y + row_h - 9
        color = "#58a6ff" if "clk" in label else ("#d29922" if "VALID" in label else "#3fb950")
        o.append(f'<text x="{left-10}" y="{ylo}" fill="{fg}" font-size="10.5" text-anchor="end">{label}</text>')
        o.append(f'<line x1="{left}" y1="{ylo}" x2="{W-right}" y2="{ylo}" stroke="{grid}" stroke-width="0.4"/>')
        if not seq:
            o.append(f'<text x="{left+5}" y="{ylo-3}" fill="#8b949e" font-size="9">(no data)</text>'); continue
        if "clk" in label:
            e = [t for t, v in seq if t0 <= t <= t1]
            per = (e[-1]-e[0])/max(len(e)-1, 1)*2 if len(e) > 2 else 0
            o.append(f'<rect x="{left}" y="{yhi}" width="{plot_w}" height="{ylo-yhi}" fill="#58a6ff" opacity="0.10"/>')
            o.append(f'<text x="{left+5}" y="{ylo-3}" fill="#58a6ff" font-size="9">clock ~{(1e6/per if per else 0):.0f} MHz</text>'); continue
        v = "x"
        for (tt, vv) in seq:
            if tt <= t0: v = vv
            else: break
        pts = [(X(t0), lvl(v, yhi, ylo))]
        for (tt, vv) in seq:
            if tt < t0 or tt > t1: continue
            x = X(tt); pts.append((x, lvl(v, yhi, ylo))); pts.append((x, lvl(vv, yhi, ylo))); v = vv
        pts.append((X(t1), lvl(v, yhi, ylo)))
        d = " ".join(f"{'M' if j==0 else 'L'}{x:.1f},{yy:.1f}" for j, (x, yy) in enumerate(pts))
        o.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="1.6"/>')
    o.append('</svg>')
    open(out, "w").write("\n".join(o))
    print("wrote", out, "rows=", len(series))


if __name__ == "__main__":
    main()
