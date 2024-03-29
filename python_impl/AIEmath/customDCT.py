# dct.py
# functions to compute the Discrete Cosine Transform (DCT) of a signal or dataset
import math
import numpy as np

#TODO: use numpy matrices


def dct(input_X):
    ''' Perform the Discrete Cosine Transform on input set x
    DCT_k = sum_n=0^N-1 x_n cos(PI*k(n+1/2)/N)
    '''
    N = len(input_X)
    dct_result = [0.0]*N

    for k in range(N):
        #Lambda = 1/math.sqrt(2) if k == 0 else 1
        Lambda = 1
        for n in range(N):
            dct_result[k] += Lambda * input_X[n] * math.cos(math.pi*k*(n+0.5)/N)
        # This scaling factor of 2/sqrt(N) is applied all in one step later
        #dct_result[k] *= 2*math.sqrt(1 / N) # to match fft.cpp
    #dct_result = np.array(dct_result)

    return dct_result 

def idct(input_X):
    ''' Perform the Inverse Discrete Cosine Transform on input set x
    IDCT_k = 0.5*x_0 + sum_n=0^N-1 x_n cos(PI*n(k+1/2)/N)
    '''
    N = len(input_X)
    idct_result = [0]*N

    for k in range(N):
        #idct_result[k] += input_X[0]/math.sqrt(N) # to match scipy
        idct_result[k] += input_X[0]/2 # to match fft.cpp 
        for n in range(1, N):
            idct_result[k] += input_X[n] * math.cos(math.pi*n*(k+0.5)/N)
        #idct_result[k] *= 2*math.sqrt(1 / N) # to match fft.cpp
        #idct_result[k] *= 2/N
    
    return np.array(idct_result)

def idst(input_X):
    N = len(input_X)
    idst_result = [0]*N

    for k in range(N):
        #idst_result[k] += input_X[0]#/math.sqrt(2)
        for n in range(1, N):
            #print(f"sin: {math.sin(math.pi*n*(k+0.5)/N)}")
            idst_result[k] += input_X[n] * math.sin(math.pi*n*(k+0.5)/N)
        #idst_result[k] *= 2*math.sqrt(1 / N) # to match fft.cpp
    
    return np.array(idst_result)

def idxst(input_):
    N = len(input_)
    input = [0]*N
    input[0] = input_[0]
    input[1:] = input_[-1:0:-1]
    idxst_result = [0]*N

    for k in range(N):
        idxst_result[k] += input[0]/2 # to match fft.cpp 
        for n in range(1, N):
            idxst_result[k] += input[n] * math.cos(math.pi*n*(k+0.5)/N)
        #idxst_result[k] *= 2*math.sqrt(1 / N) # to match fft.cpp
        #idxst_result[k] *= 2/N

    # SHOULD THIS BE PART OF THE AIE KERNEL???
    #for k in range(N):
        #idxst_result[k] -= input[0]/2 # to match fft.cpp 
    
    for k in range(N):
        idxst_result[k] *= (-1)**k
    return np.array(idxst_result)

def dct_2d(input_mat):
    ''' Compute the 2D DCT. Perform 1D DCT on rows, then again on columns.'''
    M = len(input_mat)
    N = len(input_mat[0])
    mat = np.zeros((M, N))
    # Compute DCT on rows
    for row in range(M):
        mat[row] = dct(input_mat[row])
    
    for row in range(M):
        for col in range(N):
            mat[row][col] = float(round(mat[row][col], 3))
            if math.isclose(mat[row][col], 0,  rel_tol=0.01):
                mat[row][col] = 0
    #print("1D DCT:"); print(mat)

    # Compute DCT on cols
    mat = np.transpose(mat)
    #breakpoint()
    for row in range(N):
        mat[row] = dct(mat[row])
    mat = np.transpose(mat)
    #print("2D DCT:"); print(mat)
    
    return np.array(mat)

def idct_2d(input_mat):
    ''' Compute the 2D IDCT. Perform 1D IDCT on rows, then again on columns.'''
    M = len(input_mat)
    N = len(input_mat[0])
    mat = np.zeros((M, N))
    #print("idct input")
    #print(input_mat)
    # Compute DCT on rows
    for row in range(M):
        mat[row] = idct(input_mat[row])
    
    #print("1D IDCT:"); print(mat)

    # Compute DCT on cols
    mat = np.transpose(mat)
    for row in range(N):
        mat[row] = idct(mat[row])
    mat = np.transpose(mat)

    #print("2D IDCT:"); print(mat)
    return np.array(mat)

def idcst_2d(input_mat):
    ''' Compute the 2D IDCST. Perform 1D IDST on rows, then IDCT on columns.'''
    M = len(input_mat)
    N = len(input_mat[0])
    mat = np.zeros((M, N))
    # Compute DCT on rows
    for row in range(M):
        mat[row] = idst(input_mat[row])

    # Compute DCT on cols
    mat = list(zip(*mat)) # transpose
    for row in range(N):
        mat[row] = idct(mat[row])
    mat = list(zip(*mat)) # transpose

    return np.array(mat)

def idsct_2d(input_mat):
    ''' Compute the 2D IDSCT. Perform 1D IDCT on rows, then IDST on columns.'''
    M = len(input_mat)
    N = len(input_mat[0])
    mat = np.zeros((M, N))
    # Compute DCT on rows
    for row in range(M):
        mat[row] = idct(input_mat[row])

    # Compute DCT on cols
    mat = list(zip(*mat)) # transpose
    for row in range(N):
        mat[row] = idst(mat[row])
    mat = list(zip(*mat)) # transpose

    return np.array(mat)