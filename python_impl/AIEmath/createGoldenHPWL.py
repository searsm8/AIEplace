#!/usr/bin/python
# -*- coding: utf-8 -*-
# createGolden.py
#
# Driver code which takes an input list and writes terms to a test file
import os
import random
import computeTerm
import numpy as np
from customDCT import *

MAX_COORD = 10000
golden_dir    = "/home/msears/AIEplace/golden/hpwl/"
AIE_input_dir = "/home/msears/AIEplace/Vitis/workspace/hpwl/data/"

def createGoldenHPWL(filepath, N=1):
    ''' Generates a new directory and random input vectors
    '''
    if(not os.path.exists(filepath)):
        os.makedirs(filepath)
    # delete old files
    filenames = ["input", "a_plus", "a_minus", "b_plus", "b_minus", "c_plus", "c_minus", "hpwl", "partials"]
    for filename in filenames:
        try: os.remove(filepath+f"/{filename}.dat")
        except: pass
    try:
        os.remove(AIE_input_dir+f"/input.dat")
    except: pass

    gamma = 4 # same as in RePlace
    netsize = 3 #random.randint(2, 5)
    input_vectors = []
    a_plus_vec = []
    a_minus_vec = []
    b_plus_vec = []
    b_minus_vec = []
    c_plus_vec = []
    c_minus_vec = []
    WA_vec = []
    partials_vec = []

    for n in range(N): # generate N random input vectors
        min_x = random.randint(0, MAX_COORD)
        width = random.randint(netsize, 100*netsize)
        input_vec = [0] * netsize
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
        for i in range(netsize):
            partials[i] = computeTerm.WA_partial(input_vec[i], a_plus_vec[-1][i], b_plus_vec[-1],c_plus_vec[-1],
                                                a_minus_vec[-1][i],b_minus_vec[-1],c_minus_vec[-1], gamma)
        partials_vec.append(partials)
        
    # write terms to files
    for i in range(netsize-1, -1, -1):
        for n in range(N):
            # write input files to golden
            with open(filepath+"/input.dat", "a") as f:
                f.write(f"{input_vectors[n][i]}\n")

            # write input files to Vitis AIE workspace
            with open(AIE_input_dir+"/input.dat", "a") as f:
                f.write(f"{input_vectors[n][i]}\n")

            # write golden outputs for all HPWL terms
            with open(filepath+"/a_plus.dat", "a") as f:
                f.write(f"{a_plus_vec[n][i]}\n")            
            with open(filepath+"/a_minus.dat", "a") as f:
                f.write(f"{a_minus_vec[n][i]}\n")            

            # write golden outputs for all HPWL partials
            with open(filepath+"/partials.dat", "a") as f:
                f.write(f"{partials_vec[n][i]}\n")            

    for n in range(N):
        with open(filepath+"/b_plus.dat", "a") as f:
            f.write(f"{b_plus_vec[n]}\n")            
        with open(filepath+"/b_minus.dat", "a") as f:
            f.write(f"{b_minus_vec[n]}\n")            
        with open(filepath+"/c_plus.dat", "a") as f:
            f.write(f"{c_plus_vec[n]}\n")            
        with open(filepath+"/c_minus.dat", "a") as f:
            f.write(f"{c_minus_vec[n]}\n")            
        with open(filepath+"/hpwl.dat", "a") as f:
            f.write(f"{WA_vec[n]}\n")            
    
def createRandomDensitys(filepath, M=16):
    ''' Generates a random MxM density map
    '''
    #TODO: what range of values should this density map be?
    # 0 < rho < 1
    RHO_MIN = 0
    RHO_MAX = 1
    rho = np.random.rand(M, M)
    with open(filepath+"/electro_input_density_map.dat", "w") as f:
        for row in range(M):
            for col in range(M):
                f.write(str(rho[row][col]) + ' ')
            f.write('\n')

    return rho

def computeAllDCTs(filepath, rho):
    M = len(rho)
    a = dct_2d(rho) # compute 'a' terms
    with open(filepath+"/electro_a_vals.dat", "w") as f:
        for row in range(M):
            for col in range(M):
                f.write(str(a[row][col]) + ' ')
            f.write('\n')
            
    # Manipulations
    for i in range(len(a)):
        a[i][0] *= 0.5
    for i in range(len(a[0])):
        a[0][i] *= 0.5
    for i in range(len(a)):
        for j in range(len(a[0])):
            a[i][j] *= 4.0 / len(a) / len(a[0])
    electroPhi    = np.zeros((len(a), len(a[0])))
    electroForceX = np.zeros((len(a), len(a[0])))
    electroForceY = np.zeros((len(a), len(a[0])))
    # w_u and w_v factors
    for u in range(len(a)):
        for v in range(len(a[0])):
            w_u = 1*math.pi*u / len(a) # why not 2*pi?
            w_u2 = w_u*w_u
            w_v = 1*math.pi*v / len(a[0])
            w_v2 = w_v*w_v
            if u == 0 and v == 0:
                electroPhi[u][v] = 0
            else:
                electroPhi[u][v] = a[u][v] / (w_u2 + w_v2)
                electroForceX[u][v] = electroPhi[u][v] * w_u
                electroForceY[u][v] = electroPhi[u][v] * w_v
            
    electroPhi = idct_2d(electroPhi)
    electroForceX = idsct_2d(electroForceX)
    electroForceY = idcst_2d(electroForceY)

    with open(filepath+"/electroPhi.dat", "w") as f:
        for row in range(M):
            for col in range(M):
                f.write(str(electroPhi[row][col]) + ' ')
            f.write('\n')
    with open(filepath+"/electroForceX.dat", "w") as f:
        for row in range(M):
            for col in range(M):
                f.write(str(electroForceX[row][col]) + ' ')
            f.write('\n')
    with open(filepath+"/electroForceY.dat", "w") as f:
        for row in range(M):
            for col in range(M):
                f.write(str(electroForceY[row][col]) + ' ')
            f.write('\n')
    
    return

if __name__ == "__main__":
    benchmark_count = 1
    #Create benchhmarks for wirelength
    for i in range(benchmark_count):
        createGoldenHPWL(golden_dir+"test"+str(i), 8)

    #Create benchhmarks for density
    for i in range(benchmark_count):
        rho = createRandomDensitys(golden_dir+"test"+str(i), 16)
        computeAllDCTs(golden_dir+"test"+str(i), rho)
