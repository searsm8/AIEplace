#!/usr/bin/env python3
"""Extract a paper PDF into greppable per-section Markdown plus image crops.

    python3 extract_paper.py <paper.pdf> <outdir>

Produces, under <outdir>:

    INDEX.md            generated nav: sections, figures, tables, equations
    NN_<section>.md     body text, one file per top-level section
    figs/figN_*.png     figure crops        (+ hand-written figN_*.md descriptions)
    tables/tableN.png   table crops         -- table TEXT is never trustworthy
    eqs/eqN.png         display-equation crops, linked inline from the section text
    pages/pageNN.png    full-page renders
    _extract.json       provenance

Everything except README.md and figs/*.md is overwritten on each run.

Why the image crops: PDF text extraction destroys math layout.  A fraction comes
out as a flat token run, superscripts vanish ("2^64" becomes "264"), and the
result is not merely unreadable -- it is *plausibly misreadable*, with nothing
marking it as suspect.  So every display equation is also cropped as an image and
linked from the exact spot in the text where its mangled twin appears.

Assumes a text-layer PDF (not a scan) in a one- or two-column layout.
Requires PyMuPDF.
"""
import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

import fitz

# A display equation is indented from its column's left margin; body prose is
# flush to it.  This one geometric fact drives equation detection.
MATH_INDENT_PT = 4.0
# Furniture = a line repeating on this fraction of pages (journal licence banners,
# running heads).  Real body text never does.
FURNITURE_SHARE = 0.4
# No paper numbers an equation (1983). Without this bound, every "(2019)" in a
# reference list is read as an equation label and reported as a missing crop.
MAX_EQ_LABEL = 199
# Trailing text is optional: IEEE puts "TABLE I" on a line by itself with the
# title in the next block, so requiring anything after the number loses every table.
CAPTION_RE = re.compile(r"^(Fig(?:ure)?\.?|TABLE|Table)\s+([IVXL]+|\d+)[.:]?(?:\s|$)", re.I)
# A multi-panel figure's last sub-label ("(c)") sits directly above its own
# caption and the block-splitter glues the two into one block/line -- strip it
# before matching, or the caption is invisible to CAPTION_RE.
SUBFIG_PREFIX_RE = re.compile(r"^\([a-zA-Z0-9]{1,3}\)\s+")


def caption_match(text):
    """CAPTION_RE match, retrying once with a leading sub-label stripped.

    Returns (match, text-to-use) -- text-to-use has the sub-label removed
    when that's what let it match, so downstream consumers see a clean
    caption instead of a stray "(c) " glued onto the front.
    """
    m = CAPTION_RE.match(text)
    if m:
        return m, text
    stripped = SUBFIG_PREFIX_RE.sub("", text, count=1)
    m = CAPTION_RE.match(stripped)
    return (m, stripped) if m else (None, text)
EQ_LABEL_RE = re.compile(r"^\((\d+[a-z]?)\)$")
ROMAN_HEAD_RE = re.compile(r"^([IVXL]+)\.\s+(.{2,70})$")
NUM_HEAD_RE = re.compile(r"^(\d+(?:\.\d+)*)\.?\s+([A-Z].{2,70})$")


def norm(text):
    return " ".join(text.split())


def caption_body(caption):
    """Caption text minus its "Fig. 3." / "TABLE II" label, for slug building."""
    return CAPTION_RE.sub("", norm(caption), count=1)


def slugify(text, limit=6):
    words = re.findall(r"[a-z0-9]+", text.lower())
    drop = {"the", "of", "a", "an", "for", "and", "on", "in", "to", "with", "its",
            "illustration", "illustrations", "overview"}
    keep = [w for w in words if w not in drop] or words
    return "_".join(keep[:limit]) or "item"


# ------------------------------------------------------------------ layout ---

def find_furniture(doc):
    """Lines that repeat across pages: licence banners, running heads, page numbers."""
    counts = Counter()
    for page in doc:
        for text in {norm(b[4]) for b in page.get_text("blocks") if b[4].strip()}:
            counts[text] += 1
    floor = max(3, int(len(doc) * FURNITURE_SHARE))
    return {t for t, n in counts.items() if n >= floor and len(t) > 12}


def find_columns(doc, furniture):
    """Left margin of each text column, from the distribution of block x0."""
    left_edges = Counter()
    for page in doc:
        for b in page.get_text("blocks"):
            if not b[4].strip() or norm(b[4]) in furniture:
                continue
            if (b[2] - b[0]) < 60:          # too narrow to establish a margin
                continue
            left_edges[round(b[0])] += 1
    if not left_edges:
        return [0.0]
    ranked = [x for x, _ in left_edges.most_common(12)]
    cols = [float(ranked[0])]
    # stop at the 2 most-frequent, well-separated margins -- sorting-then-
    # truncating the full candidate set let a rarer, smaller-x indent (e.g. a
    # hanging paragraph) outrank the real 2nd column just by having a lower x
    for x in ranked[1:]:
        if len(cols) >= 2:
            break
        if all(abs(x - c) > 100 for c in cols) and left_edges[x] > 0.10 * left_edges[ranked[0]]:
            cols.append(float(x))
    return sorted(cols)



class Layout:
    def __init__(self, doc):
        self.furniture = find_furniture(doc)
        self.cols = find_columns(doc, self.furniture)
        self.width = doc[0].rect.width

    def column_of(self, rect):
        idx = 0
        for i, x in enumerate(self.cols):
            if rect.x0 >= x - 2:
                idx = i
        return idx

    def col_left(self, rect):
        return self.cols[self.column_of(rect)]

    def is_full_width(self, rect):
        return len(self.cols) > 1 and rect.x0 < self.cols[1] - 2 and rect.x1 > self.cols[1] + 20

    def blocks(self, page):
        """Body blocks of a page, in reading order, furniture removed."""
        out = []
        for b in page.get_text("blocks"):
            text = b[4].strip()
            if not text or norm(text) in self.furniture:
                continue
            if re.fullmatch(r"\d{1,4}", text):        # running page number
                continue
            out.append({"rect": fitz.Rect(b[:4]), "text": text})
        return out


# -------------------------------------------------------- captioned items ---

def graphics(page):
    """Vector + raster bounding boxes, clipped to the page.

    Clipping matters: PDF paths routinely carry absurd bounds (y1 = 26008) that
    would swallow the whole page if unioned raw.

    Hairlines must survive.  A table's horizontal rules have zero height, so they
    are `is_empty` and get dropped by any naive filter -- which silently costs you
    every table on the page, since rules are the only graphics a table has.
    """
    prect, out = page.rect, []
    for d in page.get_drawings():
        r = fitz.Rect(d["rect"])
        if r.is_infinite:
            continue
        r.normalize()
        if r.width < 0.5:
            r.x1 = r.x0 + 0.5
        if r.height < 0.5:
            r.y1 = r.y0 + 0.5
        r = r & prect
        if not r.is_empty and max(r.width, r.height) >= 2:
            out.append(r)
    for img in page.get_images(full=True):
        for r in page.get_image_rects(img[0]):
            r = r & prect
            if not r.is_empty:
                out.append(r)
    return out


def _side_box(cap_rect, gfx, blocks, layout, above, reach=520):
    """Union of content on one side of a caption, within the caption's column."""
    if above:
        near = [r for r in gfx if r.y1 <= cap_rect.y0 + 3 and r.y0 > cap_rect.y0 - reach]
    else:
        near = [r for r in gfx if r.y0 >= cap_rect.y1 - 3 and r.y1 < cap_rect.y1 + reach]
    if not layout.is_full_width(cap_rect) and len(layout.cols) > 1:
        col = layout.column_of(cap_rect)
        near = [r for r in near if layout.column_of(r) == col]
    if not near:
        return None
    box = near[0]
    for r in near[1:]:
        box = box | r
    # sub-captions, axis tick labels and table cells are text, not drawings
    band = (fitz.Rect(box.x0 - 14, box.y0 - 14, box.x1 + 14, cap_rect.y0 - 1) if above
            else fitz.Rect(box.x0 - 14, cap_rect.y1 + 1, box.x1 + 14, box.y1 + 14))
    for blk in blocks:
        br = blk["rect"]
        if band.intersects(br) and (br & band).get_area() > 0.5 * br.get_area():
            box = box | br
    # the band step can pull in a block that pokes past `reach` (e.g. a
    # neighbouring item's own caption, sitting just past the boundary the
    # caller computed from sibling captions) -- reapply the same limit here,
    # not just on the initial gfx seed, or that neighbour's content rides
    # along for free.
    if above:
        box.y0 = max(box.y0, cap_rect.y0 - reach)
    else:
        box.y1 = min(box.y1, cap_rect.y1 + reach)
    return box


def captioned_items(doc, layout):
    """{('fig'|'table', number): dict(page, box, cap_rect, caption)}."""
    found = {}
    for pno, page in enumerate(doc):
        blocks = layout.blocks(page)
        gfx = graphics(page)
        for blk in blocks:
            m, text = caption_match(norm(blk["text"]))
            if not m:
                continue
            kind = "table" if m.group(1).lower().startswith("table") else "fig"
            cap_rect = blk["rect"]
            others = [b for b in blocks if b is not blk]
            # a neighbouring caption in the same column marks where this item's
            # content must stop -- without this, tightly stacked figures/tables
            # (common in a narrow column) merge into one oversized, sometimes
            # inverted, box that spans past the sibling's caption
            sib_caps = [b["rect"] for b in others if caption_match(norm(b["text"]))[0]
                        and (layout.is_full_width(cap_rect) or len(layout.cols) <= 1
                             or layout.column_of(b["rect"]) == layout.column_of(cap_rect))]
            above_reach = min([cap_rect.y0 - r.y1 for r in sib_caps if r.y1 <= cap_rect.y0] + [520])
            below_reach = min([r.y0 - cap_rect.y1 for r in sib_caps if r.y0 >= cap_rect.y1] + [520])
            # figures caption below the artwork, tables above it -- but papers
            # disagree, so try the conventional side first and fall back
            order = [True, False] if kind == "fig" else [False, True]
            box = None
            for above in order:
                box = _side_box(cap_rect, gfx, others, layout, above,
                                 reach=above_reach if above else below_reach)
                if box is not None:
                    break
            if box is None:
                continue
            # the caption bounds the crop, never the content union: a trailing
            # "(c)" sub-panel label can sit in the gap between the two. Trim on
            # the side that `above` (the direction that actually matched) says
            # the content is on -- re-deriving the side from box vs. cap_rect
            # geometry instead is fragile: a content box that legitimately
            # brushes the caption by a couple points (axis line, descender)
            # flips it to the wrong branch.
            if above:
                # the +/-6 crop padding is cosmetic breathing room, not part of
                # the content -- it must not itself cross into a sibling's
                # caption above, so clamp with the same above_reach bound
                # that scoped the content search in the first place.
                y0 = max(box.y0 - 6, cap_rect.y0 - above_reach)
                box = fitz.Rect(box.x0 - 6, y0, box.x1 + 6, cap_rect.y0 - 1.5)
            else:
                y1 = min(box.y1 + 6, cap_rect.y1 + below_reach)
                box = fitz.Rect(box.x0 - 6, cap_rect.y1 + 1.5, box.x1 + 6, y1)
            key = (kind, m.group(2))
            if key not in found:                       # first occurrence wins
                found[key] = {"page": pno, "box": box & page.rect,
                              "cap_rect": cap_rect, "caption": text}
    return found


# ---------------------------------------------------------------- equations ---

def blk_key(pno, rect):
    """Stable identity for a block across passes.

    Page + rounded top-left. NOT id(): equation_groups and assemble each build
    their own block dicts, so object identity matches only by accidental id
    reuse -- which silently mislinked equation crops into the references.
    """
    return (pno, round(rect.y0, 1), round(rect.x0, 1))


def is_math(blk, layout):
    """Display math is indented from the column margin; prose is flush to it."""
    text = norm(blk["text"])
    if EQ_LABEL_RE.match(text):
        return True
    if layout.is_full_width(blk["rect"]):
        return False
    return blk["rect"].x0 > layout.col_left(blk["rect"]) + MATH_INDENT_PT


def equation_groups(doc, layout, items):
    """Contiguous runs of display-math blocks that carry at least one (N) label."""
    dead = [(i["page"], i["box"], i["cap_rect"]) for i in items.values()]
    groups = []
    for pno, page in enumerate(doc):
        blocks = [b for b in layout.blocks(page)
                  if not any(p == pno and ((b["rect"] & r).get_area() > 0.5 * b["rect"].get_area()
                                           or (b["rect"] & c).get_area() > 0.5 * b["rect"].get_area())
                             for p, r, c in dead)]
        run = []
        for blk in blocks + [None]:
            if blk is not None and is_math(blk, layout):
                run.append(blk)
                continue
            if run:
                # the label must TERMINATE a block.  A numbered procedure list
                # ("3) Repeat (1) and (2) 200 times") is indented like display
                # math and cites labels mid-sentence -- matching those turns the
                # list into a phantom equation.
                labels = []
                for b in run:
                    m = re.search(r"\((\d+[a-z]?)\)\s*$", norm(b["text"]))
                    if m and int(re.match(r"\d+", m.group(1)).group()) <= MAX_EQ_LABEL:
                        labels.append(m.group(1))
                if labels:
                    box = run[0]["rect"]
                    for b in run[1:]:
                        box = box | b["rect"]
                    groups.append({"page": pno, "labels": labels,
                                   "box": fitz.Rect(box.x0 - 6, box.y0 - 4,
                                                    box.x1 + 8, box.y1 + 4) & page.rect,
                                   "blocks": [blk_key(pno, b["rect"]) for b in run]})
                run = []
    return groups


def load_transcriptions(out):
    """Hand-written clean math from eqs/transcriptions.md, keyed by crop name.

    Format: `## eq13` followed by the transcription until the next `## `.
    Written by a human (or a model reading the crop), never generated, never
    overwritten -- the extractor cannot produce these, since recovering the
    structure the PDF threw away is the whole point.
    """
    path = out / "eqs" / "transcriptions.md"
    if not path.exists():
        return {}
    found, name, buf = {}, None, []
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^##\s+(\S+)\s*$", line)
        if m:
            if name and "".join(buf).strip():
                found[name] = "\n".join(buf).strip("\n")
            name, buf = m.group(1), []
        elif name is not None:
            buf.append(line)
    if name and "".join(buf).strip():
        found[name] = "\n".join(buf).strip("\n")
    return found


def eq_bases(labels):
    return {re.match(r"\d+", l).group() for l in labels}


def equation_gaps(marked, groups):
    """Equation numbers the text refers to but that no crop covers.

    The usual cause is an equation the PDF merged into a prose block, so it never
    looked like display math.  Reported rather than hidden: a silently missing
    crop sends you back to the mangled text without warning you.
    """
    cropped = eq_bases({l for g in groups for l in g["labels"]})
    seen = {b for b in eq_bases(set(re.findall(r"\((\d+[a-z]?)\)", marked)))
            if int(b) <= MAX_EQ_LABEL}
    top = max((int(n) for n in cropped | seen), default=0)
    return [str(n) for n in range(1, top + 1) if str(n) in seen and str(n) not in cropped]


def eq_name(labels):
    """eq13 for (13a)-(13c); eq19-20 when one crop happens to cover two equations.

    The span must show in the name.  Naming a two-equation crop after only its
    first label makes the second one look absent from the output.
    """
    bases = [re.match(r"\d+", l).group() for l in labels]
    if len(set(bases)) == 1:
        return f"eq{bases[0]}"
    return f"eq{bases[0]}-{bases[-1]}"


# ----------------------------------------------------------------- sections ---

HEAD_PREFIX = r"(?:[IVXL]+\.\s+|[A-Z]\.\s+|\d+(?:\.\d+)*\.?\s+)?"


def split_heading(line, titles):
    """Peel a heading off the front of a joined block: (heading|None, remainder).

    Both failure modes are real and pull opposite ways.  A heading that *wraps*
    ("VII. EXTENDING XPLACE VIA DETAILED-ROUTABILITY" / "OPTIMIZATION") needs its
    lines joined to be findable; a heading the PDF glued to the paragraph that
    follows it needs them split.  Joining everything and then peeling against the
    known outline titles handles both.
    """
    for title in titles:
        m = re.match(r"(" + HEAD_PREFIX + re.escape(title) + r")(?:\s|$)", line, re.I)
        if m:
            return m.group(1).strip(), line[m.end(1):].strip()
    return None, line


def toc_sections(doc):
    """Top-level headings from the PDF outline, matched back to body lines."""
    toc = doc.get_toc()
    if not toc:
        return [], set()
    tops = [t for lvl, t, _ in toc if lvl == 1]
    subs = {norm(t) for lvl, t, _ in toc if lvl == 2}
    return tops, subs


def locate_headings(marked, toc_tops):
    """(start, end, text) for each top-level heading found in the body."""
    hits = []
    for title in toc_tops:
        key = re.escape(norm(title))
        pat = re.compile(r"^(?:[IVXL]+\.\s+|\d+(?:\.\d+)*\.?\s+)?" + key + r"\s*$",
                         re.M | re.I)
        m = pat.search(marked)
        if m:
            hits.append((m.start(), m.end(), norm(m.group(0))))
    for extra in ("REFERENCES", "ACKNOWLEDGMENT", "ACKNOWLEDGEMENTS"):
        m = re.search(r"^" + extra + r"S?\s*$", marked, re.M | re.I)
        if m and not any(s <= m.start() < e for s, e, _ in hits):
            hits.append((m.start(), m.end(), norm(m.group(0))))
    return sorted(set(hits))


def body_font_size(doc):
    """The size most characters are set in -- the baseline a heading rises above."""
    sizes = Counter()
    for page in doc:
        for blk in page.get_text("dict")["blocks"]:
            for line in blk.get("lines", []):
                for span in line["spans"]:
                    sizes[round(span["size"], 1)] += len(span["text"])
    return sizes.most_common(1)[0][0] if sizes else 10.0


def font_headings(doc, body_size):
    """Heading candidates for papers that number nothing (Nature, Science, ...).

    Numbering is the easy signal and plenty of papers have none; typography is
    the one they all have.  A heading is short, and either larger than the body
    text or bold.
    """
    out = []
    for page in doc:
        for blk in page.get_text("dict")["blocks"]:
            if "lines" not in blk:
                continue
            spans = [sp for ln in blk["lines"] for sp in ln["spans"] if sp["text"].strip()]
            if not spans:
                continue
            text = norm("".join(sp["text"] for sp in spans))
            if not (2 < len(text) <= 80) or CAPTION_RE.match(text):
                continue
            if not re.match(r"^[A-Z0-9]", text) or text.endswith((".", ",", ";")):
                continue
            size = max(sp["size"] for sp in spans)
            bold = any("bold" in sp["font"].lower() for sp in spans)
            if size > body_size + 0.6 or (bold and size >= body_size - 0.1):
                if text not in out:
                    out.append(text)
    return out


def fallback_headings(marked):
    """No outline: take numbered or roman-numeral heading lines."""
    hits = []
    for m in re.finditer(r"^(?:[IVXL]+\.\s+[A-Z][A-Z \-]{2,60}|\d+\.\s+[A-Z].{2,60})$",
                         marked, re.M):
        hits.append((m.start(), m.end(), norm(m.group(0))))
    return hits


# ------------------------------------------------------------------ assembly ---

def dehyphenate(text):
    """Rejoin words split across a line break, using the paper as its own dictionary.

    'computation-\\nally' -> 'computationally', but 'pin-\\naccessibility' keeps its
    hyphen.  An unseen word keeps the hyphen: a wrong join destroys the token,
    a stray hyphen is merely ugly.
    """
    flat = re.sub(r"(\w+)-\n(\w+)", r"\1\2", text)
    plain = {w.lower() for w in re.findall(r"[A-Za-z]{3,}", flat)}
    hyphen = {w.lower() for w in re.findall(r"[A-Za-z]+-[A-Za-z]+", flat)}

    def fix(m):
        head, tail = m.group(1), m.group(2)
        if (head + tail).lower() in plain and (head + "-" + tail).lower() not in hyphen:
            return head + tail
        return head + "-" + tail
    return re.sub(r"(\w+)-\n(\w+)", fix, text)


def assemble(doc, layout, items, groups, titles, transcripts):
    """Whole paper as one marked-up string, ready to split on headings."""
    by_page_dead = {}
    for (kind, num), it in items.items():
        by_page_dead.setdefault(it["page"], []).append((kind, num, it))
    eq_by_block = {}
    for g in groups:
        for key in g["blocks"]:
            eq_by_block[key] = g

    parts = []
    for pno, page in enumerate(doc):
        parts.append(f"\n\n[p.{pno + 1}]\n\n")
        dead = by_page_dead.get(pno, [])
        emitted = set()
        for blk in layout.blocks(page):
            rect, text = blk["rect"], blk["text"]
            caption_of = None
            skip = False
            for kind, num, it in dead:
                if abs(rect.y0 - it["cap_rect"].y0) < 0.5 and abs(rect.x0 - it["cap_rect"].x0) < 0.5:
                    caption_of = (kind, num, it)
                elif (rect & it["box"]).get_area() > 0.5 * rect.get_area():
                    skip = True                      # label living inside the artwork
            if skip:
                continue
            if caption_of:
                kind, num, it = caption_of
                if (kind, num) in emitted:
                    continue
                emitted.add((kind, num))
                sub = "figs" if kind == "fig" else "tables"
                stem = (f"fig{num}_{slugify(caption_body(it['caption']))}" if kind == 'fig'
                        else f"table{num}")
                parts.append(f"\n@@ITEM@@{it['caption']}@@SEP@@{sub}/{stem}.png\n\n")
                continue
            key = blk_key(pno, rect)
            group = eq_by_block.get(key)
            clean = transcripts.get(eq_name(group["labels"])) if group else None
            if re.match(r"^(?:Algorithm|ALGORITHM)\s*\d|^\d+:\s", text):
                # pseudocode: line structure is the content, and consecutive
                # blocks must land in ONE fence, so no blank line after
                parts.append("@@PRE@@" + text.replace("\n", "@@NL@@") + "\n")
                continue
            if clean is None:
                # join soft wraps here, not at render time, so a heading that
                # wraps across two lines is one line when we search for it
                head, rest = split_heading(norm(text), titles)
                if head:
                    parts.append(head + "\n\n")
                if rest:
                    parts.append(rest + "\n\n")
            if group is not None and key == group["blocks"][-1]:
                labels = ", ".join("(" + l + ")" for l in group["labels"])
                img = f"eqs/{eq_name(group['labels'])}.png"
                if clean is None:
                    parts.append(f"@@EQIMG@@{img}@@SEP@@{labels}\n\n")
                else:
                    parts.append("@@EQTXT@@" + clean.replace("\n", "@@NL@@")
                                 + f"@@SEP@@{img}@@SEP@@{labels}\n\n")
    return dehyphenate("".join(parts))


def render_markdown(body, subheads):
    out = []
    for para in body.split("\n\n"):
        para = para.strip("\n")
        if not para.strip():
            continue
        if para.startswith("@@ITEM@@"):
            cap, _, img = para[8:].partition("@@SEP@@")
            out.append(f"> **{cap}**\n>\n> ![{cap}]({img})")
        elif para.startswith("@@EQTXT@@"):
            body_, _, tail = para[9:].partition("@@SEP@@")
            img, _, labels = tail.partition("@@SEP@@")
            out.append("```\n" + body_.replace("@@NL@@", "\n").strip("\n")
                       + "\n```\n*" + labels + " transcribed from the typeset "
                       + "form: ![" + labels + "](" + img + ")*")
        elif para.startswith("@@EQIMG@@"):
            img, _, labels = para[9:].partition("@@SEP@@")
            out.append(f"*[{labels} as typeset: ![{labels}]({img})]*")
        elif "@@PRE@@" in para:
            lines = [ln.replace("@@PRE@@", "").replace("@@NL@@", "\n")
                     for ln in para.split("\n")]
            out.append("```\n" + "\n".join(lines).strip("\n") + "\n```")
        elif re.fullmatch(r"\[p\.\d+\]", para.strip()):
            out.append(para.strip())
        else:
            out.append(" ".join(ln.strip() for ln in para.split("\n") if ln.strip()))
    text = "\n\n".join(out)
    for sub in sorted(subheads, key=len, reverse=True):
        text = re.sub(r"^((?:[A-Z]\.\s+)?)" + re.escape(sub) + r"\s*$",
                      lambda m: f"## {m.group(0).strip()}", text, flags=re.M)
    return text


def page_at(marked, pos):
    hits = re.findall(r"\[p\.(\d+)\]", marked[:pos])
    return int(hits[-1]) if hits else 1


def split_sections(marked, heads):
    chunks = [("00_frontmatter", "Front matter", marked[: heads[0][0]] if heads else marked)]
    for k, (start, end, title) in enumerate(heads):
        stop = heads[k + 1][0] if k + 1 < len(heads) else len(marked)
        stem = f"{k + 1:02d}_{slugify(re.sub(r'^[IVXL0-9.]+', '', title), 5)}"
        # a section starting mid-page carries no [p.N] of its own; restate it
        chunks.append((stem, title, f"\n[p.{page_at(marked, start)}]\n\n" + marked[end:stop]))
    return chunks


# ------------------------------------------------------------------- output ---

def write_all(doc, out, layout, items, groups, chunks, subheads, pdf, dpi_fig, dpi_page, gaps):
    for sub in ("figs", "tables", "eqs", "pages"):
        (out / sub).mkdir(parents=True, exist_ok=True)

    manifest = {"figs": [], "tables": [], "eqs": [], "sections": []}

    for (kind, num), it in sorted(items.items()):
        sub = "figs" if kind == "fig" else "tables"
        stem = (f"fig{num}_{slugify(caption_body(it['caption']))}" if kind == 'fig' else f"table{num}")
        box = it["box"]
        if box.width <= 0 or box.height <= 0:
            print(f"warning: degenerate box for {kind} {num} on page {it['page'] + 1} "
                  f"({box}) -- skipped, crop it by hand from pages/")
            continue
        doc[it["page"]].get_pixmap(clip=box, dpi=dpi_fig).save(str(out / sub / f"{stem}.png"))
        manifest[sub].append({"n": num, "file": f"{sub}/{stem}.png",
                              "page": it["page"] + 1, "caption": it["caption"]})

    used = set()
    for g in groups:
        name = eq_name(g["labels"])
        while name in used:                    # never let one crop clobber another
            name += "_b"
        used.add(name)
        g["name"] = name
        doc[g["page"]].get_pixmap(clip=g["box"], dpi=dpi_fig).save(str(out / "eqs" / f"{name}.png"))
        manifest["eqs"].append({"file": f"eqs/{name}.png", "page": g["page"] + 1,
                                "labels": g["labels"]})

    for pno, page in enumerate(doc):
        page.get_pixmap(dpi=dpi_page).save(str(out / "pages" / f"page{pno + 1:02d}.png"))

    for stem, title, body in chunks:
        md = render_markdown(body, subheads).strip("\n")
        pages = sorted({int(n) for n in re.findall(r"\[p\.(\d+)\]", md)})
        span = f"p.{pages[0]}" if len(pages) < 2 else f"pp.{pages[0]}-{pages[-1]}"
        note = ("*Machine-extracted from the PDF. Numbered equations appear as clean "
                "transcriptions where `eqs/transcriptions.md` supplies one, otherwise as "
                "raw mangled text; either way the typeset crop is linked — trust the crop. "
                "Unnumbered inline math is NOT recovered: read `pages/`. Table text is "
                "scrambled by column interleaving: read `tables/`.*")
        (out / f"{stem}.md").write_text(f"# {title}\n\n*PDF {span}.* {note}\n\n{md}\n",
                                        encoding="utf-8")
        manifest["sections"].append({"file": f"{stem}.md", "title": title, "pages": span})

    write_index(out, pdf, doc, manifest, gaps)
    (out / "_extract.json").write_text(json.dumps(
        {"pdf": str(pdf), "sha256": hashlib.sha256(Path(pdf).read_bytes()).hexdigest()[:16],
         "pages": doc.page_count, "extracted_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
         "columns": layout.cols, "counts": {k: len(v) for k, v in manifest.items()},
         "equations_not_cropped": gaps},
        indent=2), encoding="utf-8")
    return manifest


def write_index(out, pdf, doc, manifest, gaps):
    L = [f"# {Path(pdf).stem} — extraction index\n",
         f"Source: `{pdf}` · {doc.page_count} pages · "
         f"generated by `extract_paper.py`, do not hand-edit (see `README.md` for notes).\n",
         "## Sections\n", "| file | pages | title |", "|---|---|---|"]
    for s in manifest["sections"]:
        L.append(f"| [[{s['file']}]] | {s['pages']} | {s['title']} |")
    if manifest["figs"]:
        L += ["\n## Figures\n", "| # | p. | caption |", "|---|---|---|"]
        for f in manifest["figs"]:
            L.append(f"| [[{f['file']}]] | {f['page']} | {f['caption'][:110]} |")
    if manifest["tables"]:
        L += ["\n## Tables\n",
              "*Crops, because extracted table text is scrambled by column interleaving.*\n",
              "| # | p. | caption |", "|---|---|---|"]
        for t in manifest["tables"]:
            L.append(f"| [[{t['file']}]] | {t['page']} | {t['caption'][:110]} |")
    if manifest["eqs"]:
        L += ["\n## Equations\n",
              "*Each is also linked inline from the section text, at the point where "
              "its mangled twin appears.*\n", "| eq | p. | labels |", "|---|---|---|"]
        for e in manifest["eqs"]:
            L.append(f"| [[{e['file']}]] | {e['page']} | {', '.join(e['labels'])} |")
    if gaps:
        L += ["\n## Equations with no crop\n",
              "Referenced in the text but covered by no crop — usually an equation "
              "the PDF merged into a prose block. **Read these from `pages/`, never "
              "from the extracted text.**\n",
              "`" + "`, `".join("(" + g + ")" for g in gaps) + "`\n"]
    L.append("\n## Pages\n\n`pages/pageNN.png` — full-page renders, the last resort "
             "for anything the crops missed.\n")
    (out / "INDEX.md").write_text("\n".join(L), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pdf")
    ap.add_argument("outdir")
    ap.add_argument("--heading", action="append", default=[],
                    help="exact heading text; repeat per section. Overrides "
                         "auto-detection, for papers that number nothing.")
    ap.add_argument("--dpi-crop", type=int, default=300)
    ap.add_argument("--dpi-page", type=int, default=200)
    args = ap.parse_args()

    pdf = Path(args.pdf)
    if not pdf.exists():
        sys.exit(f"no such file: {pdf}")
    out = Path(args.outdir)
    out.mkdir(parents=True, exist_ok=True)

    doc = fitz.open(pdf)
    layout = Layout(doc)
    items = captioned_items(doc, layout)
    groups = equation_groups(doc, layout, items)

    tops, subheads = toc_sections(doc)
    if args.heading:
        tops = args.heading                       # caller knows better
    titles = sorted(set(tops) | set(subheads), key=len, reverse=True)
    transcripts = load_transcriptions(out)
    marked = assemble(doc, layout, items, groups, titles, transcripts)
    # Detectors in descending reliability. Numbered headings beat typography:
    # bold pseudocode lines ("if t ∈ V then", "end") look exactly like headings
    # to a font rule, and letting it win turned 8 good sections into 5 junk ones.
    heads = locate_headings(marked, tops) if tops else []
    how = "outline" if heads else ""
    if len(heads) < 2:
        heads, how = fallback_headings(marked), "numbering"
    if len(heads) < 2:
        heads = locate_headings(marked, font_headings(doc, body_font_size(doc)))
        how = "typography"
    if len(heads) < 2:
        print("warning: could not find section headings — the whole body goes "
              "into one file.\n         Re-run with explicit headings, e.g.\n"
              "           --heading Introduction --heading Results --heading Methods")
        how = "none"
    if not heads:
        print("warning: no section headings found; writing a single body file")
    chunks = split_sections(marked, heads)

    gaps = equation_gaps(marked, groups)
    m = write_all(doc, out, layout, items, groups, chunks, subheads, pdf,
                  args.dpi_crop, args.dpi_page, gaps)

    print(f"{pdf.name}: {doc.page_count} pages, {len(layout.cols)}-column "
          f"(margins {[round(c) for c in layout.cols]})")
    print(f"  {len(m['sections'])} sections (via {how}), {len(m['figs'])} figures, "
          f"{len(m['tables'])} tables, {len(m['eqs'])} equation crops")
    names = {eq_name(g["labels"]) for g in groups}
    todo = sorted(names - set(transcripts))
    print(f"  {len(names & set(transcripts))}/{len(names)} equations have a clean "
          f"transcription in eqs/transcriptions.md")
    if todo:
        print("    still mangled: " + ", ".join(todo))
    if gaps:
        print("  !! no crop for equation(s) " + ", ".join(gaps) + " -- see INDEX.md")
    print(f"  -> {out}/INDEX.md")
    print("\nVerify before trusting: open 2-3 figure/table crops and confirm they are "
          "whole,\nand check INDEX.md's equation count against the paper's last "
          "equation number.")


if __name__ == "__main__":
    main()
