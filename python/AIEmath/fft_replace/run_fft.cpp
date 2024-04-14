#include <cmath>
#include <iostream>
#include "fft.h"
#include <vector>

using namespace std;
using namespace replace;


int main() {
    
    cout << "FFT test..." << endl;
    int M = 4;
    int size = 16;
    replace::FFT* fft = new replace::FFT(M, M, size, size);
    vector<vector<int>> rho = 
                              {{1,1,99,99},
                               {1,1,99,99},
                               {99,99,99,99},
                               {99,99,99,99}};
                            //{{10, 17,114,121},
                            //   {3,10,17,24},
                            //   {6,13,20,27},
                            //   {9,16,23,30}};

    for(int x = 0; x < M; x++)
        for(int y = 0; y < M; y++)
            //fft->updateDensity(x, y,3*x+7*y);
            fft->updateDensity(x, y, rho[x][y]);
    
    for(int x = 0; x < M; x++) {
        for(int y = 0; y < M; y++) {
            cout << fft->getDensity(x, y) << " ";
        }
        cout << endl;
    }

    cout << "\nDensity:" << endl;
    for(int x = 0; x < M; x++) {
        for(int y = 0; y < M; y++) {
            cout << fft->getDensity(x, y) << " ";
        }
        cout << endl;
    }

    fft->doFFT();
    
    cout << "\nelectro phi:" << endl;
    for(int x = 0; x < M; x++) {
        for(int y = 0; y < M; y++) {
            cout << fft->getElectroPhi(x, y) << " ";
        }
        cout << endl;
    }
    cout << "\nelectro force X:" << endl;
    for(int x = 0; x < M; x++) {
        for(int y = 0; y < M; y++) {
            cout << fft->getElectroForce(x, y).first << " ";
        }
        cout << endl;
    }
    cout << "\nelectro force Y:" << endl;
    for(int x = 0; x < M; x++) {
        for(int y = 0; y < M; y++) {
            cout << fft->getElectroForce(x, y).second<< " ";
        }
        cout << endl;
    }


}
/*
    int M = 4;
    float** binDensity_ = new float*[M];
    std::vector<int> workArea_;
    workArea_.resize( round(sqrt(std::max(M, M))) + 2, 0 );

    std::vector<float> csTable_;
    csTable_.resize( std::max(M, M) * 3 / 2, 0 );

    for(int i = 0; i < M; i++) {
        binDensity_[i] = new float[M];
        for(int j = 0; j < M; j++){
            binDensity_[i][j] = 1.0f;
        }
    }
    for(int i = 0; i < M; i++) {
        cout << "[";
        for(int j = 0; j < M; j++) {
            cout << binDensity_[i][j] << " ";
        }
        cout << "]" << endl;
    }

    replace::ddct2d(M, M, -1, binDensity_, // -1 means DCT
      NULL, (int*) &workArea_[0], (float*)&csTable_[0]);

}
*/