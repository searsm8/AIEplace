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
AIE_input_dir = "/home/msears/AIEplace/Vitis/AIEplace/data/"
fft_filepath = golden_dir + "fft/"
fft_size = 16
alpha = -1*cmath.pi/2/fft_size # constant term used in fast DCT computation

def shuffle_input(input, axis=1):
    shuffled = np.ndarray((fft_size, fft_size), dtype=complex)
    if axis == 1:
        for row in range(fft_size):
            for col in range(fft_size):
                k = 2*col if col < fft_size/2 else 2*(fft_size-col) - 1 
                shuffled[row][col] = input[row][k]
    else: # axis == 0
        for row in range(fft_size):
            if row < fft_size/2:
                shuffled[row] = input[2*row]
            else: shuffled[row] = input[2*(fft_size-row) - 1]
    return shuffled


def unshuffle_input(input, axis=1):
    unshuffled = np.ndarray((fft_size, fft_size), dtype=complex)
    if axis==1:
        for row in range(fft_size):
            for col in range(fft_size):
                if col < fft_size / 2:
                    unshuffled[row][2*col] = input[row][col]
                else:
                    unshuffled[row][2*(fft_size-col) - 1] = input[row][col]
    else: # axis==0
        for row in range(fft_size):
            if row < fft_size / 2:
                unshuffled[2*row] = input[row]
            else:
                unshuffled[2*(fft_size-row) - 1] = input[row]
    return unshuffled

def fft_to_dct(fft_output, alpha, axis=1):
    ''' Perform manipulation to convert a 2N FFT result to a DCT
        DCT = Re[FFT]*cos(alpha) - Im[FFT]*sin(alpha)
    '''
    dct_output = np.ndarray((fft_size, fft_size), dtype=complex)
    if axis==1:
        for row in range(fft_size):
            for col in range(fft_size):
                dct_output[row][col] = fft_output[row][col].real * cmath.cos(alpha*col) - \
                    fft_output[row][col].imag * cmath.sin(alpha*col) 
    else: # axis = 0
        for col in range(fft_size):
            for row in range(fft_size):
                dct_output[row][col] = fft_output[row][col].real * cmath.cos(alpha*row) - \
                    fft_output[row][col].imag * cmath.sin(alpha*row) 
    return dct_output
    

def fast_dct(input, axis=1):
    # compute the fft (shuffled) along all rows
    fft_input = shuffle_input(input, axis=axis)
    fft_output = np.fft.fft(fft_input, axis=axis)
    
    # modify FFT outputs to get DCT as result
    # DCT = Re[e^{-i pi k / 2N} * FFT*]
    # alpha = -1*cmath.pi/2/size
    return fft_to_dct(fft_output, alpha, axis=axis)


def idct_preprocess(input, axis=1):
    # compute V(k) = e^pi*i*k/2N (C_k - jC_N-k)
    #alpha = -1*cmath.pi/2/size
    output = np.ndarray((fft_size, fft_size), dtype=complex)
    if axis==1:
        for row in range(fft_size):
            input[row][0] /= 2 #cmath.sqrt(2)
            for col in range(fft_size):
                    output[row][col] = input[row][col] * cmath.exp(1j*alpha*col)
                    #ifft_input[row][col] *= cmath.sqrt(2*size)
    else: 
        for col in range(fft_size):
            input[0][col] /= 2 #cmath.sqrt(2)
            for row in range(fft_size):
                    output[row][col] = input[row][col] * cmath.exp(1j*alpha*row)
    return output

def fast_idct(input, axis=1):
    ifft_input = idct_preprocess(input, axis=axis) 
    ifft_output = np.fft.fft(ifft_input, n=1*fft_size, axis=axis)
    idct_output = unshuffle_input(ifft_output, axis=axis)
    return idct_output.real

def createGoldenFFT(filepath, size):
    if(not os.path.exists(filepath)):
        os.makedirs(filepath)
    if(not os.path.exists(AIE_input_dir)):
        os.makedirs(AIE_input_dir)
    
    print(f"Generating random input for a 2D-FFT to {fft_filepath}")
    fft_input = np.array([[0, 1, 2,3,4, 5, 6, 7],
                        [0,1, 2,3,4, 5, 6, 7],
                        [0,1, 2,3,4, 5, 6, 7],
                        [0,1, 2,3,4, 5, 6, 7],
                        [0,1, 2,3,4, 5, 6, 7],
                        [0,1, 2,3,4, 5, 6, 7],
                        [0,1, 2,3,4, 5, 6, 7],
                        [0,1, 2,3,4, 5, 6, 7] ])
    #fft_input = np.array([  [11,12,13,24],
    #                        [35,26,27,28],
    #                        [19,210,111,312],
    #                        [213,314,15,216]])
    #fft_input = np.array([  [11,12],[13,24] ])
    fft_input = np.random.rand(size, size) * 100

    #mathtest.electro_test(fft_input)
    print("#"*50)
    print()
    # write random inputs to file (with padding)
    with open(filepath+"input.dat", "w") as file, \
            open(AIE_input_dir+"input.dat", "w") as AIE_file:
        for row in range(len(fft_input)):
            for col in range(len(fft_input[row])):
                file.write(f"{fft_input[row][col].real}\n{fft_input[row][col].imag}\n")
                AIE_file.write(f"{fft_input[row][col].real}\n{fft_input[row][col].imag}\n")

    #fft_output = np.fft.fft(shuffle_input(np.copy(fft_input)))
    #ifft_output = np.fft.ifft(fft_output)

    # Compute the DCT along rows
    dct_output = fast_dct(np.copy(fft_input), axis=1)
    #print("After 1D-DCT"); print(dct_output.real)
    with open(filepath+"dct_output.dat", "w") as file:
        for i in range(size):
            for j in range(size):
                file.write(f"{dct_output[i][j].real:.2f}\n{dct_output[i][j].imag:.2f}\n")

    idct_output = fast_idct(dct_output, axis=1)
    with open(filepath+"idct_output.dat", "w") as file:
        for i in range(size):
            for j in range(size):
                file.write(f"{idct_output[i][j].real:.2f}\n{idct_output[i][j].imag:.2f}\n")


    #print("\n***gold_dct_output:")
    #gold_dct_output = []
    #for i in range(16):
    #    gold_dct_output.append( customDCT.dct(np.copy(fft_input[i])) )
    #    print("\n[ ", end="")
    #    for j in range(16):
    #        print(f"{gold_dct_output[i][j]:.2f}", end=", ")
    #    print("]")

    #print("\n***gold_idct_output:")
    #gold_idct_output = []
    #for i in range(16):
    #    gold_idct_output.append( customDCT.idct(np.copy(gold_dct_output[i])) )
    #    print("\n[ ", end="")
    #    for j in range(16):
    #        print(f"{gold_idct_output[i][j]:.2f}", end=", ")
    #    print("]")
    #print()

    print("\n***gold_idxst_output:")
    gold_idxst_output = []
    for i in range(16):
        gold_idxst_output.append( customDCT.idxst(np.copy(fft_input[i])) )
        print(f"\n[{i}] [ ", end="")
        for j in range(16):
            print(f" {gold_idxst_output[i][j]:.2f}", end=", ")
        print("]")

    with open(filepath+"idxst_output.dat", "w") as file:
        for i in range(size):
            for j in range(size):
                file.write(f"{gold_idxst_output[i][j].real:.2f}\n{gold_idxst_output[i][j].imag:.2f}\n")
    return

    # Compute the DCT along cols
    dct_output = fast_dct(dct_output, axis=0)
    #print("After 2D-DCT: "); print(dct_output.real)

    with open(filepath+"2d_dct_output.dat", "w") as file:
        for i in range(size):
            for j in range(size):
                file.write(f"{dct_output[i][j].real:.2f}\n")

    # adjustment
    # perform all scaling factors in one step instead of after each DCT or IDCT
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

    # generate w_u and w_v factors
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
    idct_input = np.copy(electroPhi)

    # Ref golden output
    idct_golden = np.zeros((size, size), dtype=complex)
    #print("new idct input"); print(idct_input)
    for i in range(size):
        idct_golden[i] = customDCT.idct(idct_input[i])
    #print("idct_golden "); print(idct_golden.real)

    # Compute the DCT along rows
    idct_output = fast_idct(idct_input, axis=1)
    #print("After 1D-IDCT"); print(idct_output)

    # Compute the DCT along cols
    idct_output = fast_idct(idct_output, axis=0)
    #print("After 2D-IDCT"); print(idct_output)

    # modify IFFT outputs to get IDCT as result
    # DCT = Re[e^{-i pi k / 2N} * FFT*]

    # compute the 2N ifft (padded) along all cols

    #electroPhi = idct_output

    # perform 2D-IDSCT to obtain electroX

    # perform 2D-IDCST to obtain electroY


   
    # write golden output
    with open(filepath+"fft_output.dat", "w") as file:
        for i in range(len(fft_output)):
            for j in range(size):
                file.write(f"{fft_output[i][j].real:.2f}\n{fft_output[i][j].imag:.2f}\n")
    with open(filepath+"ifft_output.dat", "w") as file:
        for i in range(len(ifft_output)):
            for j in range(size):
                file.write(f"{ifft_output[i][j].real:.2f}\n{ifft_output[i][j].imag:.2f}\n")
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