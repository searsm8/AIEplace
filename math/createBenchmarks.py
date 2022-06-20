# createTest.py
#
# Driver code which takes an input list and writes terms to a test file
import os
import random
import computeTerm
import numpy as np
from dct import *

def createRandomInput(filepath, N=100):
    ''' Generates a new directory and random input vectors
    '''
    if(not os.path.exists(filepath)):
        os.makedirs(filepath)
    for n in range(N): # generate N random input vectors
        netsize = random.randint(2, 5)
        min_x = random.randint(0, 10000)
        width = random.randint(netsize, 100*netsize)
        input_vec = [0] * netsize
        for i in range(netsize):
            input_vec[i] = random.randint(min_x, min_x + width)
        input_vec.sort()
        input_vec = [str(i) for i in input_vec]
        with open(filepath+"/input.txt", "a") as f:
            f.write(','.join(input_vec) + "\n")
    return

def computeAllTerms(filepath):
    ''' Computes a±, b±, c± and writes to file
    '''
    gamma = 4
    # delete old result files
    try:
        os.remove(filepath+"/a_plus.txt")
        os.remove(filepath+"/b_plus.txt")
        os.remove(filepath+"/c_plus.txt")
        os.remove(filepath+"/a_minus.txt")
        os.remove(filepath+"/b_minus.txt")
        os.remove(filepath+"/c_minus.txt")
        os.remove(filepath+"/WA_hpwl.txt")
        os.remove(filepath+"/partials.txt")
    except:
        pass

    # open "input.txt" at the specified filepath
    with open(filepath+"/input.txt", "r") as f:
        for line in f:
            input = line.strip().split(",")
            input = [int(s) for s in input]

            input.sort()
            a_plus_results = computeTerm.a_plus(input, gamma)
            a_minus_results = computeTerm.a_minus(input, gamma)
            b_plus_result = computeTerm.b_term(a_plus_results)
            b_minus_result = computeTerm.b_term(a_minus_results)
            c_plus_result = computeTerm.c_term(input, a_plus_results)
            c_minus_result = computeTerm.c_term(input, a_minus_results)

            WA = computeTerm.WA_hpwl(b_plus_result,c_plus_result,b_minus_result,c_minus_result)
            partials = [0] * len(a_plus_results)
            for i in range(len(a_plus_results)):
                partials[i] = computeTerm.WA_partial(input[i], a_plus_results[i], b_plus_result,c_plus_result,
                                                    a_minus_results[i],b_minus_result,c_minus_result, gamma)
        
            # write terms to output files
            with open(filepath+"/a_plus.txt", "a") as f:
                f.write(','.join([str(i) for i in a_plus_results]) + '\n')
            with open(filepath+"/a_minus.txt", "a") as f:
                f.write(','.join([str(i) for i in a_minus_results]) + '\n')
            with open(filepath+"/b_plus.txt", "a") as f:
                f.write(str(b_plus_result) + '\n')
            with open(filepath+"/b_minus.txt", "a") as f:
                f.write(str(b_minus_result) + '\n')
            with open(filepath+"/c_plus.txt", "a") as f:
                f.write(str(c_plus_result) + '\n')
            with open(filepath+"/c_minus.txt", "a") as f:
                f.write(str(c_minus_result) + '\n')
            with open(filepath+"/WA_hpwl.txt", "a") as f:
                f.write(str(WA) + '\n')
            with open(filepath+"/partials.txt", "a") as f:
                f.write(','.join([str(i) for i in partials]) + '\n')
    
def createRandomDensitys(filepath, M=16):
    ''' Generates a random MxM density map
    '''
    #TODO: what range of values should this density map be?
    # 0 < rho < 1
    RHO_MIN = 0
    RHO_MAX = 1
    rho = np.random.rand(M, M)
    with open(filepath+"/density_map.txt", "w") as f:
        for row in range(M):
            for col in range(M):
                f.write(str(rho[row][col]) + ' ')
            f.write('\n')

    return rho

def computeAllDCTs(filepath, rho):
    M = len(rho)
    a = dct_2d(rho) # compute 'a' terms
    with open(filepath+"/a_vals.txt", "w") as f:
        for row in range(M):
            for col in range(M):
                f.write(str(a[row][col]) + ' ')
            f.write('\n')
            
    psi = idct_2d(a) # compute 'electric potential'
    with open(filepath+"/psi.txt", "w") as f:
        for row in range(M):
            for col in range(M):
                f.write(str(psi[row][col]) + ' ')
            f.write('\n')
    
    return

if __name__ == "__main__":
    output_dir = "/home/msears/AIEplace/benchmarks/"
    #Create benchmarks for wirelength
    #for i in range(1, 10):
    #    createRandomInput(output_dir+"test"+str(i), 1)
    #    computeAllTerms(output_dir+"test"+str(i))

    #Create benchmarks for density
    for i in range(1, 10):
        rho = createRandomDensitys(output_dir+"test"+str(i), 16)
        computeAllDCTs(output_dir+"test"+str(i), rho)

