# mathtest.py
# driver program for testing math functions
from timeit import *
from scipy.fftpack import dct, fft
import customDCT
import computeTerm
import numpy as np

def idct_test():
    x = [8, 16, 24, 32, 40, 48, 56, 64] # array input numbers
    print("Input: ", x)
    x = [round(res) for res in customDCT.dct(x)]
    print("DCT: ", x)

    # truncate the high-frequency components from N/2 < n < N
    #for k in range(round(len(x)/2), len(x)):
    #    x[k] = 0
    #print("DCT: ", x)

    x = [round(res) for res in customDCT.idct(x)]
    print("IDCT: ", x)


def dct_2d_test():
    x = np.array([[1,3,5],
         [2,6,7],
         [8,4,5]])
    print("Input:", x)
    x = customDCT.dct_2d(x)
    print("2D DCT:", x)
    x = customDCT.idct_2d(x)
    print("Output:", x)

def dct_1d_compare():
    x = np.array([[1,3,5],
         [2,6,7],
         [8,4,5]])
    print("x:")
    print(x)
    index = 2
    my_dct = customDCT.dct(x[index])
    #scipy_dct = dct.dct(x)
    scipy_dct = dct(x[index], 2, norm='ortho')
    print("my_dct: ")
    print(my_dct )

    print("scipy_dct: ")
    print(scipy_dct)

    my_idct = customDCT.idct(my_dct)
    print("my_idct: ")
    print(my_idct )
    
    scipy_idct = dct(scipy_dct, 3, norm='ortho')
    print("scipy_idct: ")
    print(scipy_idct)

def dct_2d_compare():
    x = np.array([[1,3,5],
         [2,6,7],
         [8,4,5]])

    my_dct = customDCT.dct_2d(x)
    #scipy_dct = dct.dct(x)
    scipy_dct = dct(x, 2, norm='ortho')
    scipy_dct = dct(scipy_dct.transpose(), 2, norm='ortho')
    scipy_dct = scipy_dct.transpose()
    print("x:")
    print(x)
    print("my_dct: ")
    print(my_dct )

    print("scipy_dct: ")
    print(scipy_dct)

    my_idct = customDCT.idct_2d(my_dct)
    print("my_idct: ")
    print(my_idct )
    
    scipy_idct = dct(scipy_dct, 3, norm='ortho') # Type III is IDCT
    scipy_idct = dct(scipy_idct.transpose(), 3, norm='ortho')
    scipy_idct = scipy_idct.transpose()
    print("scipy_idct: ")
    print(scipy_idct)

def alpha_test():
    rho = np.array([[1,3,5,6],
                    [2,6,7,6],
                    [8,4,5,6],
                    [7,6,4,1]])

    print("rho:")
    print(rho)
    my_dct = customDCT.dct_2d(rho)
    my_idct = customDCT.idct_2d(my_dct)
    print("my_dct: ")
    print(my_dct )
    alpha = np.zeros((len(rho), len(rho)))
    for u in range(len(rho)):
        for v in range(len(rho)):
            alpha[u][v] = computeTerm.alpha(u, v, rho)
            #print(f"a[{u}][{v}] = " + str(a))
    print("alpha:")
    print(alpha)

    print("my_idct: ")
    print(my_idct )
    phi = np.zeros((len(rho), len(rho)))
    for u in range(len(rho)):
        for v in range(len(rho)):
            phi[u][v] = computeTerm.phi(u, v, alpha)
    print("phi:")
    print(phi)
         
if __name__ == "__main__":
    #dct_2d_compare()
    #dct_2d_test()
    #idct_test()
    alpha_test()

    