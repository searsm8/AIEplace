#!/usr/bin/env python3

import argparse
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined


REALS_PER_DDR_WORD = 16


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate a density_mm2s HLS kernel."
    )

    parser.add_argument(
        "--lanes",
        type=int,
        required=True,
        help="Number of AXI stream output lanes.",
    )

    parser.add_argument(
        "--template",
        type=Path,
        default=Path("density_mm2s.cpp.j2"),
        help="Path to the Jinja template.",
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=Path("density_mm2s.cpp"),
        help="Generated C++ output file.",
    )

    parser.add_argument(
        "--memory-depth",
        type=int,
        default=1024,
        help="Value used for the HLS m_axi depth declaration.",
    )

    parser.add_argument(
        "--pipeline-ii",
        type=int,
        default=None,
        help=(
            "Requested pipeline initiation interval. By default this is "
            "ceil(lanes / 16), based on the number of 512-bit DDR reads."
        ),
    )

    return parser.parse_args()


def main():
    args = parse_args()

    if args.lanes <= 0:
        raise SystemExit("--lanes must be greater than zero")

    if args.memory_depth <= 0:
        raise SystemExit("--memory-depth must be greater than zero")

    ddr_words_per_output = (
        args.lanes + REALS_PER_DDR_WORD - 1
    ) // REALS_PER_DDR_WORD

    pipeline_ii = args.pipeline_ii

    if pipeline_ii is None:
        pipeline_ii = ddr_words_per_output

    if pipeline_ii <= 0:
        raise SystemExit("--pipeline-ii must be greater than zero")

    template_path = args.template.resolve()

    environment = Environment(
        loader=FileSystemLoader(str(template_path.parent)),
        undefined=StrictUndefined,
        trim_blocks=True,
        lstrip_blocks=True,
        keep_trailing_newline=True,
    )

    template = environment.get_template(template_path.name)

    generated_code = template.render(
        num_lanes=args.lanes,
        ddr_words_per_output=ddr_words_per_output,
        memory_depth=args.memory_depth,
        pipeline_ii=pipeline_ii,
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated_code, encoding="utf-8")

    print(f"Generated: {args.output}")
    print(f"Lanes: {args.lanes}")
    print(f"DDR words per output iteration: {ddr_words_per_output}")
    print(f"Pipeline II: {pipeline_ii}")


if __name__ == "__main__":
    main()
