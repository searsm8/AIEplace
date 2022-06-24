# mathtest.py
# driver program for testing math functions
from timeit import *
from scipy.fftpack import dct, fft
import customDCT
from copy import copy
import computeTerm
import numpy as np
import math

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

def electro_test():
    rho = np.array([[0, 7,14,21],
                    [3,10,17,24],
                    [6,13,20,27],
                    [9,16,23,30]])

    #rho = np.array([[0, 7,14,21],
    #                [3,10,17,424],
    #                [6,213,20,27],
    #                [9,16,123,30]])

    #rho = np.array([[1,1,1,1],
    #                [1,1,1,1],
    #                [1,1,1,1],
    #                [1,1,1,1]])
    print("rho:")
    print(rho)

    my_dct = customDCT.dct_2d(rho)
    for i in range(len(my_dct)):
        for j in range(len(my_dct[0])):
            my_dct[i][j] = round(my_dct[i][j], 2)
    print("my_dct: ")
    print(my_dct )
    for i in range(len(my_dct)):
        my_dct[i][0] *= 0.5
    for i in range(len(my_dct[0])):
        my_dct[0][i] *= 0.5
    for i in range(len(my_dct)):
        for j in range(len(my_dct[0])):
            my_dct[i][j] *= 4.0 / len(my_dct) / len(my_dct[0])

    print("alpha: ")
    print(my_dct )

    electroPhi = copy(my_dct)
    electroForceX = copy(my_dct)
    electroForceY = copy(my_dct)
    # w_u and w_v factors
    for u in range(len(my_dct)):
        w_u = 1*math.pi*u / len(my_dct)
        w_u2 = w_u*w_u
    for v in range(len(my_dct[0])):
        w_v = 1*math.pi*v / len(my_dct[0])
        w_v2 = w_v*w_v
    
    for u in range(len(my_dct)):
        for v in range(len(my_dct[0])):
            if u == 0 and v == 0:
                electroPhi[u][v] = 0
            else:
                electroPhi[u][v] /= w_u2 + w_v2
                electroForceX[u][v] = electroPhi[u][v] * w_u
                electroForceY[u][v] = electroPhi[u][v] * w_v


    #density = np.zeros((len(rho), len(rho)))
    #for u in range(len(rho)):
    #    for v in range(len(rho)):
    #        density[u][v] = computeTerm.density(u, v, rho)
    #        #print(f"a[{u}][{v}] = " + str(a))
    #print("density:")
    #print(density)

    print("electro_phi: "); print(electroPhi)
    #print("electroForceX: "); print(electroForceX)
    #print("electroForceY: "); print(electroForceY)

    electroPhi = customDCT.idct_2d(electroPhi)
    print("electro_phi: "); print(electroPhi)

    #electroForceX = customDCT.idsct_2d(electroForceX)
    #print("electroForceX: "); print(electroForceX)

    #phi = np.zeros((len(rho), len(rho)))
    #for u in range(len(rho)):
    #    for v in range(len(rho)):
    #        phi[u][v] = computeTerm.phi(u, v, density)
    #print("phi:")
    #print(phi)
         
if __name__ == "__main__":
    #dct_2d_compare()
    #dct_2d_test()
    #idct_test()
    electro_test()

    