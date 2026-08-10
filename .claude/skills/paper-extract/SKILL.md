---
name: paper-extract
description: Extract a research-paper PDF into greppable per-section Markdown plus figure/table/equation image crops, then transcribe the equations and describe the figures. Use when asked to "get a paper into a form I can reference", to extract or split a PDF paper, to pull out its figures or tables, or when repeatedly citing a paper whose PDF is being re-read page by page.
---

# Extracting a paper for repeated reference

Turns a paper PDF into a directory you can grep, cite by page, and read equations
from without re-opening the PDF.

**The tool does the mechanical half. You do the half that needs judgment.** Running
the script and stopping produces something worse than useless — text with math in
it that is *plausibly misreadable*. Steps 3 and 4 are not optional polish.

## 1. Run the extractor

```bash
python3 .claude/skills/paper-extract/extract_paper.py <paper.pdf> <outdir>
```

Needs PyMuPDF (`import fitz`). Writes:

| path | what | regenerated? |
|---|---|---|
| `INDEX.md` | nav table: sections, figures, tables, equations | yes |
| `NN_<section>.md` | body text, one file per top-level section, `[p.N]` markers inline | yes |
| `figs/figN_*.png` | figure crops (300 dpi) | yes |
| `figs/figN_*.md` | **your** descriptions | **no — preserved** |
| `tables/tableN.png` | table crops | yes |
| `eqs/eqN.png` | display-equation crops | yes |
| `eqs/transcriptions.md` | **your** clean math | **no — preserved** |
| `pages/pageNN.png` | full-page renders (200 dpi) | yes |
| `README.md` | **your** orientation notes | **no — preserved** |

Re-run it freely; it only clobbers the mechanical outputs.

If it reports `could not find section headings`, the paper numbers nothing
(Nature/Science style). Read the headings off page 1 and pass them:

```bash
python3 extract_paper.py paper.pdf out --heading Introduction --heading Results --heading Methods
```

## 2. Verify before trusting

Three checks, each catching a failure the others miss:

- **Open 2–3 figure and table crops.** Confirm nothing is cut off. The most
  common defect is a lost bottom sub-panel — a figure's `(c)` row and its label
  sit in the gap between the artwork and the caption.
- **Check the equation count** in `INDEX.md` against the paper's last equation
  number. The tool reports `no crop for equation(s) N` when a number is
  referenced but uncropped; that is normal (the PDF merged it into a prose
  block), but it means *that* equation must be read from `pages/`.
- **Grep one distinctive phrase** you know is in the paper. If it comes back
  empty, hyphenation or column detection went wrong.

## 3. Transcribe the equations — the step that matters most

PDF text extraction destroys math. Not gently: fractions flatten into token
runs, summation limits detach, and **superscripts vanish**, so `2^64` extracts as
the integer `264` with nothing marking it as suspect. You will often be able to
reconstruct an equation you already know from domain knowledge — and that is
exactly the trap, because on an unfamiliar formula (the reason to read the paper)
a wrong parse is indistinguishable from a right one.

So: **read every `eqs/*.png` crop and write the equation out** in
`eqs/transcriptions.md`:

```markdown
## eq15
ω(λ) = λ·|H_D| · |H_W + λ·H_D|^(−1)                             (15a)

              λ · Σ_{i∈V} A_i
     = ----------------------------------                       (15b)
        Σ_{i∈V} |S_i| + λ · Σ_{i∈V} A_i
```

Keys must match the crop filenames exactly (`eq15`, `eq19-20`). Re-run the
extractor and each transcription replaces the mangled text in the section file,
with its crop linked underneath for verification. The script reports coverage
(`24/24 equations have a clean transcription`) — drive it to 100%.

Also transcribe **inline** math the tool cannot see (it only crops numbered
display equations). Add those under a descriptive key with a `NOT AN EQUATION`
note, so the record exists even though nothing gets injected.

## 4. Describe the figures

For each `figs/figN_*.png`, write `figs/figN_*.md` (same stem) containing:

- the **verbatim caption** and page number,
- **what is actually drawn** — boxes, arrows, axes, ranges, colours, sub-panels.
  Be concrete: a reader who cannot see the image should be able to reason about
  it. Note axis scales; "λ spans eight decades" changes how you read every later
  claim about λ.
- **why it matters for the work at hand** — and only claims you checked. If you
  say the paper's ω is the codebase's `precond_kappa`, grep both first and cite
  file:line.

## 5. Write `README.md`

The index the next session reads first. A table of which section holds what, with
equation numbers; the two or three sections that actually matter for your work;
and the known limits (which tables are unreliable, which equations lack crops).
`INDEX.md` is mechanical and complete; `README.md` is opinionated and short.

## Known limits

1. **Text-layer PDFs only.** A scan has no text to extract; OCR first.
2. **Table *text* is scrambled** by column interleaving and must never be quoted
   for numbers. Use `tables/*.png`, or transcribe the numbers by hand into a CSV
   alongside.
3. **Unnumbered inline math is not cropped.** Only numbered display equations.
4. **Hyphenation is resolved using the paper as its own dictionary** — a word
   split across lines is rejoined only if the joined form appears elsewhere.
   Rare words keep a spurious hyphen; grep both forms.
5. **Two-column layouts are the well-tested case.** Single-column works;
   three-column is untested.

## Copyright

Published papers are copyrighted. Keep extractions in an untracked directory
(this repo: `.claude/2_ARTIFACTS/`, gitignored via `[0-9]_*/`). Do not commit the
text or push it anywhere. Conclusions *drawn from* a paper belong in the project's
own notes; the extraction is a local reading aid.
