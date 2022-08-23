# This tool compares an AIE emulation output to a golden output file
# Golden files are created from scripts in python_impl/AIEmath and are stored in ./golden
# Since AIE kernels produce estimates of values, the comparison must have an acceptable error % from the golden
import math

AIE_workspace_dir = "/home/msears/AIEplace/Vitis/workspace/"
golden_dir = "/home/msears/AIEplace/golden/"
rel_tol = 0.01

def compare_hpwl_outputs():
    #filenames = ["a_plus", "a_minus", "b_plus", "b_minus", "c_plus", "c_minus", "HPWL", "partials"]
    filenames = ["hpwl", "partials"]
    AIE_output_dir = AIE_workspace_dir + "hpwl/Emulation-AIE/aiesimulator_output/data/"
    golden_hpwl_dir = golden_dir + "hpwl/test0/"
    all_match = True
    for filename in filenames:
        if compare_output(filename, AIE_output_dir, golden_hpwl_dir, rel_tol):
            all_match = False
    return all_match

def compare_fft_outputs():
    filenames = ["fft_output", "ifft_output"]
    AIE_output_dir = AIE_workspace_dir + "fft_basic/Emulation-AIE/aiesimulator_output/data/"
    golden_fft_dir = golden_dir + "density/fft/"
    all_match = True
    for filename in filenames:
        if compare_output(filename, AIE_output_dir, golden_fft_dir, rel_tol):
            all_match = False
    return all_match

def compare_output(filename, output_dir, golden_dir, rel_tol=0.01):
    diff_found = False
    output_filename = output_dir + filename + ".dat"
    golden_filename = golden_dir + filename + ".dat"
    print(f"\nFile compare: {filename}.dat")
    line_count = 0
    try:
        with open(output_filename) as AIE_file, open(golden_filename) as golden_file:
            for line in AIE_file:
                if 'T' not in line: # if this line is a timestamp, skip it
                    golden_line = golden_file.readline()
                    line_count += 1
                    if not math.isclose(float(line), float(golden_line), rel_tol=rel_tol):
                        print(f"(line #{line_count})\t***DIFF DETECTED: AIE: {float(line.strip()):.5f}\tgolden: {float(golden_line.strip()):.5f}***")
                        diff_found = True
        if not diff_found:
            print("\tALL OUTPUTS MATCH")
    except:
        print(f"\t***Files for {filename}.dat not found***\n")
        diff_found = True
    return diff_found


if __name__ == "__main__":
    hpwl_match = compare_hpwl_outputs()      
    fft_match = compare_fft_outputs()
    if hpwl_match and fft_match:
        print(f"\nAll values match to within an error of {rel_tol*100}%")
    else: print(f"\n***FAIL: MISMATCH DETECTED OR FILE NOT FOUND***")