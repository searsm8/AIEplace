# mathtest.py
# driver program for testing math functions
from dct import *
from timeit import *

def idct_test():
    x = [8, 16, 24, 32, 40, 48, 56, 64] # array input numbers
    print("Input: ", x)
    x = [round(res) for res in dct(x)]
    print("DCT: ", x)

    # truncate the high-frequency components from N/2 < n < N
    #for k in range(round(len(x)/2), len(x)):
    #    x[k] = 0
    #print("DCT: ", x)

    x = [round(res) for res in idct(x)]
    print("IDCT: ", x)


def dct_2d_test():
    x = np.array([[1,3,5],
         [2,6,7],
         [8,4,5]])
    print("Input:", x)
    x = dct_2d(x)
    print("2D DCT:", x)
    x = idct_2d(x)
    print("Output:", x)

if __name__ == "__main__":
    dct_2d_test()
    #idct_test()