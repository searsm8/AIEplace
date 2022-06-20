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
        Lambda = 1/math.sqrt(2) if k == 0 else 1
        for n in range(N):
            dct_result[k] += Lambda * input_X[n] * math.cos(math.pi*k*(n+0.5)/N)
        dct_result[k] *= math.sqrt(2 / N)
    return dct_result

def dct_2d(input_MxM):
    ''' Compute the 2D DCT. Perform 1D DCT on rows, then again on columns.'''
    M = len(input_MxM)
    # Compute DCT on rows
    for row in range(M):
        input_MxM[row] = dct(input_MxM[row])

    # Compute DCT on cols
    input_MxM = list(zip(*input_MxM)) # transpose
    for row in range(M):
        input_MxM[row] = dct(input_MxM[row])
    input_MxM = list(zip(*input_MxM)) # transpose

    return input_MxM

def idct(input_X):
    ''' Perform the Inverse Discrete Cosine Transform on input set x
    IDCT_k = 0.5*x_0 + sum_n=0^N-1 x_n cos(PI*n(k+1/2)/N)
    '''
    N = len(input_X)
    idct_result = [0]*N

    for k in range(N):
        idct_result[k] += input_X[0]/math.sqrt(2)
        for n in range(1, N):
            idct_result[k] += input_X[n] * math.cos(math.pi*n*(k+0.5)/N)
        idct_result[k] *= math.sqrt(2 / N)
    
    return idct_result

def idxst(input_X):
    return

def idct_2d(input_MxM):
    ''' Compute the 2D IDCT. Perform 1D IDCT on rows, then again on columns.'''
    M = len(input_MxM)
    # Compute DCT on rows
    for row in range(M):
        input_MxM[row] = idct(input_MxM[row])

    # Compute DCT on cols
    input_MxM = list(zip(*input_MxM)) # transpose
    for row in range(M):
        input_MxM[row] = idct(input_MxM[row])
    input_MxM = list(zip(*input_MxM)) # transpose

    return input_MxM