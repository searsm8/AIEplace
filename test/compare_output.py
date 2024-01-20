# This tool compares an AIE emulation output to a golden output file
# Golden files are created from scripts in python_impl/AIEmath and are stored in ./golden
# Since AIE kernels produce estimates of values, the comparison must have an acceptable error % from the golden
import math
import os

AIE_workspace_dir = "/home/msears/AIEplace/Vitis/"
home_dir = os.path.expanduser('~')
aie_dir = home_dir + "/AIEplace/Vitis/vck5000/aie/"
golden_hpwl_dir= aie_dir + "golden_data/partials/"
golden_fft_dir = aie_dir + "golden_data/density/"

rel_tol = 0.01

def compare_hpwl_outputs():
    AIE_output_dir = aie_dir + "aiesimulator_output/simdata/"
    if not os.path.exists(AIE_output_dir):
        AIE_output_dir = aie_dir + "x86simulator_output/simdata/"
    filenames = ["xa", "bc"]
    #filenames = ["hpwl", "partials"]
    all_match = True
    for filename in filenames:
        if compare_output(filename, AIE_output_dir, golden_hpwl_dir, rel_tol):
            all_match = False
    return all_match

def compare_dct_outputs():
    AIE_output_dir = aie_dir + "aiesimulator_output/simdata/"
    if not os.path.exists(AIE_output_dir):
        AIE_output_dir = aie_dir + "x86simulator_output/simdata/"
    filenames = ["dct_output", "dct_2d_output", "idct_output", "idct_2d_output", "idxst_output", "idxst_2d_output"]
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
    print(f"\nFile compare: \n{output_filename}\n{golden_filename}")
    line_count = 0
    diff_count = 0
    #try:
    with open(output_filename) as AIE_file, open(golden_filename) as golden_file:
        for golden_line in golden_file:
                AIE_line = AIE_file.readline()
                while 'T' in AIE_line: # if this line is a timestamp, skip it
                    AIE_line = AIE_file.readline()
                line_count += 1
                #print(f"line_count: {line_count}")
                if (not math.isclose(float(AIE_line), float(golden_line), rel_tol=rel_tol)) and \
                    abs(float(AIE_line) - float(golden_line)) > 1e-30:
                    print(f"(line #{line_count})\t***DIFF DETECTED: AIE: {float(AIE_line.strip()):.2f}\tgolden: {float(golden_line.strip()):.2f}***")
                    diff_found = True
                    diff_count += 1
                    if diff_count > 10:
                        break
    if not diff_found:
        print("\tALL OUTPUTS MATCH")
    #except:
    #    print(f"\t***Files for {filename}.dat not found***\n")
    #    diff_found = True
    return diff_found


if __name__ == "__main__":
    hpwl_match = compare_hpwl_outputs()      
    fft_match = True#compare_dct_outputs()
    if hpwl_match and fft_match:
        print(f"\nAll values match to within an error of {rel_tol*100}% or 1e-30")
    else: print(f"\n***FAIL: MISMATCH DETECTED OR FILE NOT FOUND***")
