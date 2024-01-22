#!/usr/bin/python
# -*- coding: utf-8 -*-
# createGolden.py
#
# Driver code which takes an input list and writes terms to a test file
import os, shutil 
import random
import computeTerm
import numpy as np
import math

MAX_COORD = 10000
  
WINDOW_BASED = False # window (True) or stream based (False) communication

home_dir = os.path.expanduser('~')
golden_dir = home_dir + "/AIEplace/Vitis/vck5000/aie/golden_data/partials"

def cleanFiles(filepath):
    shutil.rmtree(filepath)
    os.makedirs(filepath)
    # delete old files
    #filenames = ["ctrl", "x_in", "xa", "bc", "input_nets", "a_plus", "a_minus", "b_plus", "b_minus", "c_plus", "c_minus", "hpwl", "partials"]
    #for filename in filenames:
    #    try: os.remove(filepath+f"/{filename}.dat")
    #    except: pass

def createGoldenHPWL(filepath, netsize=2, N=1, bench_num=0):
    ''' Generates a new directory and random input vectors
        @param: filepath - directory to output gold files to
        @param: netsize: the size of the nets generated and computed for
        @param: N - number of nets of this size
        @param: bench_num - benchmark specifier, appended to file names 
    '''

    gamma = 4 # same as in RePlace
    input_vectors = []
    a_plus_vec = []
    a_minus_vec = []
    b_plus_vec = []
    b_minus_vec = []
    c_plus_vec = []
    c_minus_vec = []
    WA_vec = []
    partials_vec = []

    for n in range(N+(8-N%8)): # generate N random input vectors
        min_x = random.randint(0, MAX_COORD)
        width = random.randint(4*netsize, 100*netsize)
        input_vec = [0] * netsize
        if n < N: # else, leave zeroes
            for i in range(netsize):
                input_vec[i] = random.randint(min_x, min_x + width)
        input_vec.sort()
        input_vectors.append(input_vec)

        #Compute a, b, c terms and write to file
        a_plus_vec.append(computeTerm.a_plus(input_vec, gamma))
        b_plus_vec.append(computeTerm.b_term(a_plus_vec[-1]))
        c_plus_vec.append(computeTerm.c_term(input_vec, a_plus_vec[-1]))

        a_minus_vec.append(computeTerm.a_minus(input_vec, gamma))
        b_minus_vec.append(computeTerm.b_term(a_minus_vec[-1]))
        c_minus_vec.append(computeTerm.c_term(input_vec, a_minus_vec[-1]))

        WA_vec.append(computeTerm.WA_hpwl(b_plus_vec[-1],c_plus_vec[-1],b_minus_vec[-1],c_minus_vec[-1]))
        partials = [0] * netsize
        if n < N: # else, leave zeroes
            for i in range(netsize):
                partials[i] = computeTerm.WA_partial(input_vec[i], a_plus_vec[-1][i], b_plus_vec[-1],c_plus_vec[-1],
                                                a_minus_vec[-1][i],b_minus_vec[-1],c_minus_vec[-1], gamma)
        partials_vec.append(partials)
        
    # write terms to files
    print("Writing golden data files to " + filepath)
    print(f"netsize: {netsize}\tnet_count: {N}")
    with    open(filepath+"/ctrl"+str(bench_num)+".dat", "a") as ctrl_file, \
            open(filepath+"/x_in"+str(bench_num)+".dat", "a") as x_in_file, \
            open(filepath+"/a_plus"+str(bench_num)+".dat", "a") as ap_file, \
            open(filepath+"/a_minus"+str(bench_num)+".dat", "a") as am_file, \
            open(filepath+"/b_plus"+str(bench_num)+".dat", "a") as bp_file, \
            open(filepath+"/b_minus"+str(bench_num)+".dat", "a") as bm_file, \
            open(filepath+"/c_plus"+str(bench_num)+".dat", "a") as cp_file, \
            open(filepath+"/c_minus"+str(bench_num)+".dat", "a") as cm_file, \
            open(filepath+"/partials"+str(bench_num)+".dat", "a") as partials_file, \
            open(filepath+"/xa"+str(bench_num)+".dat", "a") as xa_file, \
            open(filepath+"/bc"+str(bench_num)+".dat", "a") as bc_file:

        # write control values to input stream (netsize and num_nets)
        ctrl_file.write(f"{netsize}\n{N}\n")
        if(not WINDOW_BASED):
            x_in_file.write(f"{netsize}\n{N}\n0\n0\n")
        bc_file.write(f"{netsize}\n{N}\n0\n0\n")

        for iter in range(math.ceil(N/8)):
            # print to file max value first, min val second
            for i in [-1] + list(range(netsize-1)):
                for lane in range(8): # write input files to golden
                    x_in_file.write(f"{input_vectors[8*iter + lane][i]}\n")
                    xa_file.write(f"{input_vectors[8*iter + lane][i]}\n")
                for lane in range(8): # write a_plus terms
                    xa_file.write(f"{a_plus_vec[8*iter + lane  ][i]}\n")            
                    ap_file.write(f"{a_plus_vec[8*iter + lane][i]}\n")            
                for lane in range(8): # write a_minus terms
                    xa_file.write(f"{a_minus_vec[8*iter + lane][i]}\n")            
                    am_file.write(f"{a_minus_vec[8*iter + lane][i]}\n")            
                for lane in range(8): # write golden outputs for all HPWL partials
                    partials_file.write(f"{partials_vec[8*iter + lane][i]}\n")            
            for lane in range(8):
                bc_file.write(f"{b_plus_vec[8*iter + lane]}\n")            
                bp_file.write(f"{b_plus_vec[8*iter + lane]}\n")            
            for lane in range(8):
                bc_file.write(f"{c_plus_vec[8*iter + lane]}\n")            
                cp_file.write(f"{c_plus_vec[8*iter + lane]}\n")            
            for lane in range(8):
                bc_file.write(f"{b_minus_vec[8*iter + lane]}\n")            
                bm_file.write(f"{b_minus_vec[8*iter + lane]}\n")            
            for lane in range(8):
                bc_file.write(f"{c_minus_vec[8*iter + lane]}\n")            
                cm_file.write(f"{c_minus_vec[8*iter + lane]}\n")            
        
        # If less than 64 (BUFF_SIZE) floats are written, pad with zeroes
        if(WINDOW_BASED):
            for iter in range(64 - netsize*(N+(8-N%8 if N%8 > 0 else 0))):
                x_in_file.write("0\n")
                ap_file.write("0\n")
                am_file.write("0\n")
            for iter in range(32 - N):
                bp_file.write("0\n")
                bm_file.write("0\n")
                cp_file.write("0\n")
                cm_file.write("0\n")


if __name__ == "__main__":

    cleanFiles(golden_dir)
    #Create benchmarks for wirelength
    num_benchmarks = 4
    for i in range(num_benchmarks):
        createGoldenHPWL(golden_dir , netsize=2, N=32, bench_num=i)
    #createGoldenHPWL(golden_dir , netsize=2, N=8*4)
    #createGoldenHPWL(golden_dir , netsize=7, N=8*1)
    #createGoldenHPWL(golden_dir , netsize=8, N=8*1)

