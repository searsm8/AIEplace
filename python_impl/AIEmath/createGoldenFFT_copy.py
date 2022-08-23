# fft_test.py
# generates a random input for fft functions
# computes the correct output and writes to golden file
import os, sys
import random
import numpy as np
import cmath
import mathtest
import customDCT

# Directory names
golden_dir    = "/home/msears/AIEplace/golden/density/"
AIE_input_dir = "/home/msears/AIEplace/Vitis/workspace/fft_basic/data/"
fft_filepath = golden_dir + "fft/"
fft_size = 4

def shuffle_input(input, axis=1):
    shuffled = np.ndarray((fft_size, fft_size), dtype=complex)
    for row in range(fft_size):
        for col in range(fft_size):
            temp = col if axis==1 else row
            k = 2*temp if temp < fft_size/2 else fft_size - temp 
            shuffled[row][col] = input[row if axis==1 else k][k if axis==1 else col]
    return shuffled


def unshuffle(input, axis=1):
    unshuffled = np.ndarray((fft_size, fft_size), dtype=complex)
    for row in range(fft_size):
        for col in range(fft_size):
            k = 2*col if col < fft_size/2 else fft_size - 2*col - 1
            unshuffled[row][k] = input[row if axis==1 else col][col if axis ==1 else row]
    return unshuffled


def fft_to_dct(fft_output, alpha, axis=0):
    ''' Perform manipulation to convert a 2N FFT result to a DCT
        DCT = Re[FFT]*cos(alpha) - Im[FFT]*sin(alpha)
    '''
    dct_output = np.ndarray((fft_size, fft_size), dtype=complex)
    if axis==1:
        for outer in range(fft_size):
            for inner in range(fft_size):
                i = outer if axis==1 else inner
                j = inner if axis==1 else outer
                dct_output[i][j] = fft_output[i][j].real * cmath.cos(alpha*inner) - \
                    fft_output[i][j].imag * cmath.sin(alpha*inner) 
    else:
        for col in range(fft_size):
            for row in range(fft_size):
                dct_output[row][col] = fft_output[row][col].real * cmath.cos(alpha*row) - \
                    fft_output[row][col].imag * cmath.sin(alpha*row) 
    return dct_output
    

def idct_preprocess(input, axis=1):
    output = [[0]*fft_size]*fft_size
    alpha = cmath.pi/2/fft_size
    for outer in range(fft_size):
        for inner in range(int(fft_size/2)):
            row = outer if axis==1 else inner
            col = inner if axis==1 else outer
            output[row][col] = (cmath.cos(alpha*col) + 1j*cmath.sin(alpha*col)) * \
                                    (input[row][col] - 1j*input[row][fft_size-col-1])
            output[row][fft_size-col-1] = output[row][col]
    return np.array(output)

def createGoldenFFT(filepath, size):
    if(not os.path.exists(filepath)):
        os.makedirs(filepath)
    if(not os.path.exists(AIE_input_dir)):
        os.makedirs(AIE_input_dir)
    
    print(f"Generating random input for a 2D-FFT to {fft_filepath}")
    fft_input = np.array([[1,1,99,99],
                    [1,1,99,99],
                    [99,99,99,99],
                    [99,9,99,99]])
    #fft_input= np.array([[10, 17,114,121],
    #                [3,10,17,24],
    #                [6,13,20,27],
    #                [9,16,23,30]])
    #fft_input = np.random.rand(size, size) * 100
    mathtest.electro_test(fft_input)
    print("#"*50)
    print()
    # write random inputs to file (with padding)
    with open(filepath+"fft_input.dat", "w") as file, \
            open(AIE_input_dir+"fft_input.dat", "w") as AIE_file:
        for row in range(len(fft_input)):
            for col in range(len(fft_input[row])):
                file.write(f"{fft_input[row][col].real}\n{fft_input[row][col].imag}\n")
                AIE_file.write(f"{fft_input[row][col].real}\n{fft_input[row][col].imag}\n")

    # compute the fft (shuffled) along all rows
    #fft_input = shuffle_input(fft_input, axis=1)
    fft_output = np.fft.fft(fft_input, n=2*size, axis=1)
    
    # modify FFT outputs to get DCT as result
    # DCT = Re[e^{-i pi k / 2N} * FFT*]
    alpha = -1*cmath.pi/2/fft_size
    dct_output = fft_to_dct(fft_output, alpha, axis=1)
    #dct_output = np.ndarray((size,size))

   # for row in range(size):
   #     dct_output[row] = customDCT.dct(fft_input[row])

   # for row in range(size):
   #     for col in range(size):
   #         dct_output[row][col] = float(round(dct_output[row][col], 3))
    print("After 1D-DCT"); print(dct_output.real)

    for row in range(size):
        for col in range(size):
            dct_output[row][col] = round(dct_output[row][col], 3)
    # compute the fft (shuffled) along all cols
    fft_input = np.copy(np.transpose(dct_output)) # transpose doesn't make a copy!
    print("transpose: "); print(fft_input)

    #fft_input = shuffle_input(dct_output, axis=1)
    #fft_output = np.fft.fft(fft_input, n=1*size, axis=1)
    #breakpoint()
    for row in range(size):
        dct_input = fft_input[row]
        dct_output[row] = customDCT.dct(dct_input)

    # modify FFT outputs to get 2D-DCT as result
    #dct_output = fft_to_dct(fft_output, alpha, axis=1)
    dct_output = np.transpose(dct_output)
    print("After 2D-DCT: "); print(dct_output.real)

    # adjustment
    #dct_output *= 4/size
    #dct_output *= 16/(size*size*size)

    for i in range(size):
        dct_output[i][0] *= 0.5 # why cut in half? boundary conditions?
    for i in range(size):
        dct_output[0][i] *= 0.5
    for i in range(size):
        for j in range(size):
            dct_output[i][j] *= 4.0 / size /size 

    electroPhi    = np.zeros((size, size), dtype=complex)
    electroForceX = np.zeros((size, size), dtype=complex)
    electroForceY = np.zeros((size, size), dtype=complex)
    # w_u and w_v factors
    for u in range(size):
        for v in range(size):
            w_u = 1*cmath.pi*u / size # why not 2*pi?
            w_u2 = w_u*w_u
            w_v = 1*cmath.pi*v / size
            w_v2 = w_v*w_v
            if u == 0 and v == 0:
                electroPhi[u][v] = 0
            else:
                electroPhi[u][v] = dct_output[u][v] / (w_u2 + w_v2)
                electroForceX[u][v] = electroPhi[u][v] * w_u
                electroForceY[u][v] = electroPhi[u][v] * w_v
    
    # perform 2D-IDCT to obtain phi
    idct_golden = np.zeros((size, size), dtype=complex)

    #print("new idct input"); print(electroPhi)

    for i in range(size):
        idct_golden[i] = customDCT.idct(electroPhi[i])

    #print("idct_golden "); print(idct_golden.real)

    # compute V(k) = e^pi*i*k/2N (C_k - jC_N-k)
    #ifft_input = idct_preprocess(electroPhi) 
    # compute the ifft along all rows
    alpha = 1j*cmath.pi/(2*size)
    for row in range(size):
        for col in range(size):
                electroPhi[row][col] *= cmath.exp(alpha*col)
                electroPhi[row][col] *= cmath.sqrt(2*size)
    for row in range(size):
        electroPhi[row][0] /= cmath.sqrt(2)
    ifft_output = np.fft.ifft(electroPhi, n=1*size, axis=1)
    ifft_output = unshuffle(ifft_output, axis=1)
    ifft_output = ifft_output.real

    #print("ifft_output "); print(ifft_output)



    # modify IFFT outputs to get IDCT as result
    # DCT = Re[e^{-i pi k / 2N} * FFT*]

    # compute the 2N ifft (padded) along all cols

    #electroPhi = idct_output

    # perform 2D-IDSCT to obtain electroX

    # perform 2D-IDCST to obtain electroY

    # write golden output
    with open(filepath+"fft_output.dat", "w") as file:
        for i in range(size):
            for j in range(size):
                #file.write(f"{fft_output[i][j].real:.2f}\n{fft_output[i][j].imag:.2f}\n")
                pass
    with open(filepath+"dct_output.dat", "w") as file:
        for i in range(size):
            for j in range(size):
                file.write(f"{dct_output[i][j].real:.2f}\n")
    with open(filepath+"electroPhi.dat", "w") as file:
        for i in range(size):
            for j in range(size):
                file.write(f"{electroPhi[i][j]:.2f}\n")
    with open(filepath+"electroX.dat", "w") as file:
        for i in range(size):
            for j in range(size):
                file.write(f"{electroForceX[i][j]:.2f}\n")
    with open(filepath+"electroY.dat", "w") as file:
        for i in range(size):
            for j in range(size):
                file.write(f"{electroForceY[i][j]:.2f}\n")


if __name__ == "__main__":
    createGoldenFFT(fft_filepath, fft_size)