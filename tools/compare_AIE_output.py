# This tool compares an AIE emulation output to a golden output file
# Golden files are created from scripts in python_impl/AIEmath and are stored in ./golden
# Since AIE kernels produce estimates of values, the comparison must have an acceptable error % from the golden
import math

AIE_output_dir = "/home/msears/AIEplace/Vitis/workspace/hpwl/Emulation-AIE/aiesimulator_output/data/"
golden_dir = "/home/msears/AIEplace/golden/test0/"
def compare_output(filename, rel_tol=0.01):
    diff_found = False
    AIE_output_filename = AIE_output_dir + filename + ".txt"
    golden_filename = golden_dir + filename + ".txt"
    print(f"\nFile compare: {filename}")
    try:
        with open(AIE_output_filename) as AIE_file, open(golden_filename) as golden_file:
            for line in AIE_file:
                if 'T' not in line: # if this line is a timestamp, skip it
                    golden_line = golden_file.readline()
                    if not math.isclose(float(line), float(golden_line), rel_tol=rel_tol):
                        print(f"\t***DIFF DETECTED: AIE: {line.strip()}\tgolden:{golden_line.strip()}***")
                        diff_found = True
        if not diff_found:
            print("\tMATCH")
    except:
        print(f"\t***Files for  {filename} not found***\n")
        diff_found = True
    return diff_found


if __name__ == "__main__":
    filenames = ["a_plus", "a_minus", "b_plus", "b_minus", "c_plus", "c_minus", "WA_hpwl", "partials"]
    all_match = True
    rel_tol = 0.01
    for filename in filenames:
        if compare_output(filename, rel_tol):
            all_match = False

    if all_match:
        print(f"\nAll values match to within an error of {rel_tol}")
    else: print(f"\n***FAIL: MISMATCH DETECTED OR FILE NOT FOUND***")