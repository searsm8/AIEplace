#!/usr/bin/env python3
import math

# Maximum N (number of beats) for which you want twiddles
Nmax = 1024

# Output header file
header_name = "twiddle_table.h"

with open(header_name, "w") as f:
    f.write("// Auto-generated twiddle table: e^{-j*pi*n/(2*Nmax)}\n")
    f.write("// Nmax = %d\n\n" % Nmax)
    f.write("#pragma once\n\n")
    f.write("#include <ap_int.h>\n\n")
    f.write("static const int Nmax = %d;\n\n" % Nmax)

    # Real (cos) and imaginary (-sin) parts
    f.write("static const float twiddle_re[%d] = {\n" % Nmax)
    for n in range(Nmax):
        theta = math.pi * n / (2.0 * Nmax)
        c = math.cos(theta)
        f.write("    %.10ff" % c)
        if n != Nmax - 1:
            f.write(",")
        f.write("\n")
    f.write("};\n\n")

    f.write("static const float twiddle_im[%d] = {\n" % Nmax)
    for n in range(Nmax):
        theta = math.pi * n / (2.0 * Nmax)
        s = -math.sin(theta)  # imag part of e^{-jθ}
        f.write("    %.10ff" % s)
        if n != Nmax - 1:
            f.write(",")
        f.write("\n")
    f.write("};\n")

print(f"Generated {header_name}")
