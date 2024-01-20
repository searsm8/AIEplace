#!/usr/bin/python
# -*- coding: utf-8 -*-
# createGolden.py
#
# Driver code which takes an input list and writes terms to a test file
import os
import random
import computeTerm
import numpy as np
import math
from customDCT import *

MAX_COORD = 10000
  
WINDOW_BASED = False # window (True) or stream based (False) communication

home_dir = os.path.expanduser('~')
golden_dir = home_dir + "/AIEplace/Vitis/vck5000/aie/golden_data/partials"

def cleanFiles(filepath):
    if(not os.path.exists(filepath)):
        os.makedirs(filepath)
    # delete old files
    filenames = ["ctrl", "x_in", "xa", "bc", "input_nets", "a_plus", "a_minus", "b_plus", "b_minus", "c_plus", "c_minus", "hpwl", "partials"]
    for filename in filenames:
        try: os.remove(filepath+f"/{filename}.dat")
        except: pass

def createGoldenHPWL(filepath, netsize=2, N=1):
    ''' Generates a new directory and random input vectors
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
    with    open(filepath+"/ctrl.dat", "a") as ctrl_file, \
            open(filepath+"/x_in.dat", "a") as x_in_file, \
            open(filepath+"/a_plus.dat", "a") as ap_file, \
            open(filepath+"/a_minus.dat", "a") as am_file, \
            open(filepath+"/b_plus.dat", "a") as bp_file, \
            open(filepath+"/b_minus.dat", "a") as bm_file, \
            open(filepath+"/c_plus.dat", "a") as cp_file, \
            open(filepath+"/c_minus.dat", "a") as cm_file, \
            open(filepath+"/partials.dat", "a") as partials_file, \
            open(filepath+"/xa.dat", "a") as xa_file, \
            open(filepath+"/bc.dat", "a") as bc_file:

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

    #with    open(filepath+"/input_nets.dat", "w") as golden_input_file, \
    #        open(filepath+"/partials.dat", "w") as partials_file, \
    #        open(filepath+"/a_plus.dat", "w") as a_plus_file, \
    #        open(filepath+"/a_minus.dat", "w") as a_minus_file:

    #with    open(filepath+"/input_nets.dat", "w") as golden_input_file, \
    #        open(filepath+"/partials.dat", "w") as partials_file, \
    #        open(filepath+"/a_plus.dat", "w") as a_plus_file, \
    #        open(filepath+"/a_minus.dat", "w") as a_minus_file:
    #    # write control values to input stream (netsize and num_nets)
    #    golden_input_file.write(f"{netsize}\n{N}\n")
    #    a_plus_file.write(f"{netsize}\n{N}\n")

    #    for iter in range(int(N/8)):
    #        # print to file max value first, min val second
    #        for i in [-1] + list(range(netsize-1)):
    #            for lane in range(8):
    #                n = 8*iter + lane
    #                # write input files to golden
    #                golden_input_file.write(f"{input_vectors[n][i]}\n")
    #                # write a^pm terms
    #                a_plus_file.write(f"{a_plus_vec[n][i]}\n")            
    #                a_minus_file.write(f"{a_minus_vec[n][i]}\n")            
    #                # write golden outputs for all HPWL partials
    #                partials_file.write(f"{partials_vec[n][i]}\n")            

    ## write golden outputs for all HPWL intermediate terms
    #with open(filepath+"/hpwl.dat", "w") as hpwl_file, \
    #     open(filepath+"/b_plus.dat", "w") as b_plus_file, \
    #     open(filepath+"/b_minus.dat", "w") as b_minus_file, \
    #     open(filepath+"/c_plus.dat", "w") as c_plus_file, \
    #     open(filepath+"/c_minus.dat", "w") as c_minus_file:
    #    for n in range(N):
    #        hpwl_file.write(f"{WA_vec[n]}\n")            
    #        b_plus_file.write(f"{b_plus_vec[n]}\n")            
    #        b_minus_file.write(f"{b_minus_vec[n]}\n")            
    #        c_plus_file.write(f"{c_plus_vec[n]}\n")            
    #        c_minus_file.write(f"{c_minus_vec[n]}\n")            
#def createRandomDensitys(filepath, M=16):
#    ''' Generates a random MxM density map
#    '''
#    rho = np.random.rand(M, M)
#    with open(filepath+"/electro_input_density_map.dat", "w") as f:
#        for row in range(M):
#            for col in range(M):
#                f.write(str(rho[row][col]) + ' ')
#            f.write('\n')
#
#    return rho
#
#def computeAllDCTs(filepath, rho):
#    M = len(rho)
#    a = dct_2d(rho) # compute 'a' terms
#    with open(filepath+"/electro_a_vals.dat", "w") as f:
#        for row in range(M):
#            for col in range(M):
#                f.write(str(a[row][col]) + ' ')
#            f.write('\n')
#            
#    # Manipulations
#    for i in range(len(a)):
#        a[i][0] *= 0.5
#    for i in range(len(a[0])):
#        a[0][i] *= 0.5
#    for i in range(len(a)):
#        for j in range(len(a[0])):
#            a[i][j] *= 4.0 / len(a) / len(a[0])
#    electroPhi    = np.zeros((len(a), len(a[0])))
#    electroForceX = np.zeros((len(a), len(a[0])))
#    electroForceY = np.zeros((len(a), len(a[0])))
#    # w_u and w_v factors
#    for u in range(len(a)):
#        for v in range(len(a[0])):
#            w_u = 1*math.pi*u / len(a) # why not 2*pi?
#            w_u2 = w_u*w_u
#            w_v = 1*math.pi*v / len(a[0])
#            w_v2 = w_v*w_v
#            if u == 0 and v == 0:
#                electroPhi[u][v] = 0
#            else:
#                electroPhi[u][v] = a[u][v] / (w_u2 + w_v2)
#                electroForceX[u][v] = electroPhi[u][v] * w_u
#                electroForceY[u][v] = electroPhi[u][v] * w_v
#            
#    electroPhi = idct_2d(electroPhi)
#    electroForceX = idsct_2d(electroForceX)
#    electroForceY = idcst_2d(electroForceY)
#
#    with open(filepath+"/electroPhi.dat", "w") as f:
#        for row in range(M):
#            for col in range(M):
#                f.write(str(electroPhi[row][col]) + ' ')
#            f.write('\n')
#    with open(filepath+"/electroForceX.dat", "w") as f:
#        for row in range(M):
#            for col in range(M):
#                f.write(str(electroForceX[row][col]) + ' ')
#            f.write('\n')
#    with open(filepath+"/electroForceY.dat", "w") as f:
#        for row in range(M):
#            for col in range(M):
#                f.write(str(electroForceY[row][col]) + ' ')
#            f.write('\n')
#    
#    return

if __name__ == "__main__":

    cleanFiles(golden_dir)
    #Create benchhmarks for wirelength
    #createGoldenHPWL(golden_dir , netsize=2, N=8*4)
    createGoldenHPWL(golden_dir , netsize=3, N=17)
    #createGoldenHPWL(golden_dir , netsize=7, N=8*1)
    #createGoldenHPWL(golden_dir , netsize=8, N=8*1)

    #Create benchhmarks for density
    #rho = createRandomDensitys(golden_dir+"test"+str(i), 16)
    #computeAllDCTs(golden_dir+"test"+str(i), rho)

