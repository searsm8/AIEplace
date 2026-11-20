#!/usr/bin/env python3

import math
import struct

MATRIX_DIMENSION = 1024
OUTPUT_FILE = "omega_table.h"


def to_float32(value):
    """Round a Python float to IEEE-754 binary32."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def float_literal(value):
    value = to_float32(value)

    if value == 0.0:
        return "0.0f"

    return f"{value:.10e}f"


omega = []
omega_squared = []

for i in range(MATRIX_DIMENSION):
    w = 2.0 * math.pi * i / MATRIX_DIMENSION
    omega.append(to_float32(w))
    omega_squared.append(to_float32(w * w))


with open(OUTPUT_FILE, "w") as f:
    f.write("// Auto-generated frequency table.\n")
    f.write("// omega[i] = 2*pi*i/M\n")
    f.write(f"// M = {MATRIX_DIMENSION}\n\n")

    f.write("#pragma once\n\n")
    f.write(
        f"static constexpr unsigned OMEGA_TABLE_SIZE = "
        f"{MATRIX_DIMENSION};\n\n"
    )

    f.write(
        f"static const float omega_table[{MATRIX_DIMENSION}] = {{\n"
    )

    for i, value in enumerate(omega):
        f.write(f"    {float_literal(value)}")
        if i != MATRIX_DIMENSION - 1:
            f.write(",")
        f.write("\n")

    f.write("};\n\n")

    f.write(
        f"static const float omega_squared_table"
        f"[{MATRIX_DIMENSION}] = {{\n"
    )

    for i, value in enumerate(omega_squared):
        f.write(f"    {float_literal(value)}")
        if i != MATRIX_DIMENSION - 1:
            f.write(",")
        f.write("\n")

    f.write("};\n")

print(f"Generated {OUTPUT_FILE}")
