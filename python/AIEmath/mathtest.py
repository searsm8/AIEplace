# mathtest.py
# driver program for testing math functions
from timeit import *
from scipy.fftpack import dct, fft
import customDCT
from copy import copy
import computeTerm
import numpy as np
import math
import cmath

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
    index = 0
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

def electro_test(rho):

    print("rho:")
    print(rho)

    my_dct = customDCT.dct_2d(rho)
    for i in range(len(my_dct)):
        for j in range(len(my_dct[0])):
            my_dct[i][j] = round(my_dct[i][j], 2)
    print("my_dct: ")
    print(my_dct )
    for i in range(len(my_dct)):
        my_dct[i][0] *= 0.5 # why cut in half? boundary conditions?
    for i in range(len(my_dct[0])):
        my_dct[0][i] *= 0.5
    for i in range(len(my_dct)):
        for j in range(len(my_dct[0])):
            my_dct[i][j] *= 4.0 / len(my_dct) / len(my_dct[0])

    print("alpha: ")
    print(my_dct )

    electroPhi    = np.zeros((len(my_dct), len(my_dct[0])))
    electroForceX = np.zeros((len(my_dct), len(my_dct[0])))
    electroForceY = np.zeros((len(my_dct), len(my_dct[0])))
    # w_u and w_v factors
    for u in range(len(my_dct)):
        for v in range(len(my_dct[0])):
            w_u = 1*math.pi*u / len(my_dct) # why not 2*pi?
            w_u2 = w_u*w_u
            w_v = 1*math.pi*v / len(my_dct[0])
            w_v2 = w_v*w_v
            if u == 0 and v == 0:
                electroPhi[u][v] = 0
            else:
                electroPhi[u][v] = my_dct[u][v] / (w_u2 + w_v2)
                electroForceX[u][v] = electroPhi[u][v] * w_u
                electroForceY[u][v] = electroPhi[u][v] * w_v


    #density = np.zeros((len(rho), len(rho)))
    #for u in range(len(rho)):
    #    for v in range(len(rho)):
    #        density[u][v] = computeTerm.density(u, v, rho)
    #        #print(f"a[{u}][{v}] = " + str(a))
    #print("density:")
    #print(density)

    print("After Manip:"); print(electroPhi)
    #print("electroForceX: "); print(electroForceX)
    #print("electroForceY: "); print(electroForceY)

    electroPhi = customDCT.idct_2d(electroPhi)
    print("electro_phi: "); print(electroPhi)

    return
    electroForceX = customDCT.idsct_2d(electroForceX)
    print("electroForceX: "); print(electroForceX)

    electroForceY = customDCT.idcst_2d(electroForceY)
    print("electroForceY: "); print(electroForceY)
    #phi = np.zeros((len(rho), len(rho)))
    #for u in range(len(rho)):
    #    for v in range(len(rho)):
    #        phi[u][v] = computeTerm.phi(u, v, density)
    #print("phi:")
    #print(phi)
         

def DCT_IDCT_test(rho):
    print("rho"); print(rho)
    dct_result = customDCT.dct(rho)
    print("dct"); print(dct_result)
    idct_result = customDCT.idct(dct_result)
    print("idct"); print(idct_result)


# DCT type II, unscaled
def transform(vector):
	temp = vector[ : : 2] + vector[-1 - len(vector) % 2 : : -2]
	temp = np.fft.fft(temp)
	factor = -1j * cmath.pi / (len(vector) * 2)
	return [(val * cmath.exp(i * factor)).real for (i, val) in enumerate(temp)]


def inverse_transform(vector):
	n = len(vector)
	factor = -1j * cmath.pi / (len(vector) * 2)
	temp = [(val if i > 0 else val / 2) * cmath.exp(i * factor)
		for (i, val) in enumerate(vector)]
	temp = np.fft.fft(temp)
	
	temp = [val.real for val in temp]
	result = [None] * n
	result[ : : 2] = temp[ : (n + 1) // 2]
	result[-1 - len(vector) % 2 : : -2] = temp[(n + 1) // 2 : ]
	return result

def idxst_test(rho):
    for i in range(4):
        print()
        print(rho[i])
        mat = customDCT.idxst(rho[i])
        print("After IDXST:")
        print(mat)

        golden = customDCT.idst(rho[i])
        print("golden:")
        print(golden)

def idct_idxst_test(rho):
    N = len(rho)
    mat = np.zeros((N, N))
    mat2 = np.zeros((N, N))
    for row in range(len(mat)):
        mat[row] = customDCT.idxst(rho[row]) 
        print(mat[row])
    mat_t = np.transpose(mat)
    for row in range(len(mat)):
        mat2[row] = customDCT.idct(mat_t[row]) 
    res = np.transpose(mat2)

    print("result:")
    print(res)

    golden = customDCT.idcst_2d(rho)
    print("golden:")
    print(golden)

    

if __name__ == "__main__":
    #dct_1d_compare()
    #dct_2d_compare()
    #dct_2d_test()
    #idct_test()
    size = 4
    rho = np.random.rand(size, size) * 100
    
    #idxst_test(rho)
    idct_idxst_test(rho)
