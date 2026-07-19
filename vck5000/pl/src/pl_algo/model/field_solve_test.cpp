// Verify field_solve_pl.hpp (full PL density solve) vs the naive double golden.
#define PL_GRID 64
#include "modules/field_solve_pl.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
#include <random>

static const double PI = 3.14159265358979323846;
using V = std::vector<double>; using M = std::vector<V>;
static V DCTn(const V& in){int N=in.size();V r(N);for(int k=0;k<N;k++){double s=0;for(int n=0;n<N;n++)s+=in[n]*cos(PI/N*(n+0.5)*k);r[k]=s;}return r;}
static V IDCTn(const V& in){int N=in.size();V r(N);for(int k=0;k<N;k++){double s=0;for(int n=1;n<N;n++)s+=in[n]*cos(PI/N*(k+0.5)*n);r[k]=0.5*in[0]+s;}return r;}
static V IDXSTn(const V& in){int N=in.size();V t(N);t[0]=in[0];for(int n=1;n<N;n++)t[n]=in[N-n];t=IDCTn(t);for(int n=1;n<N;n+=2)t[n]*=-1;return t;}
static M tr(const M&A){int N=A.size();M T(N,V(N));for(int i=0;i<N;i++)for(int j=0;j<N;j++)T[i][j]=A[j][i];return T;}
static M rows(const M&A,V(*f)(const V&)){M R;for(auto&r:A)R.push_back(f(r));return R;}
int main(){
    const int N=PL_GRID; std::mt19937 rng(777); std::uniform_real_distribution<double> uni(0,1);
    M rho(N,V(N)); std::vector<float> rf(N*N);
    for(int i=0;i<N;i++)for(int j=0;j<N;j++){rho[i][j]=uni(rng); rf[i*N+j]=(float)rho[i][j];}
    // golden
    M auv=tr(rows(tr(rows(rho,DCTn)),DCTn));
    M ExG(N,V(N,0)),EyG(N,V(N,0));
    for(int u=0;u<N;u++)for(int v=0;v<N;v++){if(u==0&&v==0)continue;double wu=2*PI*u/N,wv=2*PI*v/N,d=wu*wu+wv*wv;ExG[u][v]=auv[u][v]*wu/d;EyG[u][v]=auv[u][v]*wv/d;}
    ExG=rows(ExG,IDCTn);EyG=rows(EyG,IDXSTn);ExG=tr(ExG);EyG=tr(EyG);
    ExG=rows(ExG,IDXSTn);EyG=rows(EyG,IDCTn);ExG=tr(ExG);EyG=tr(EyG);
    // PL
    std::vector<float> Ex(N*N),Ey(N*N),tA(N*N),tB(N*N);
    plalgo::field_solve_pl(rf.data(),Ex.data(),Ey.data(),tA.data(),tB.data());
    double ex_e=0,ex_n=0,ey_e=0,ey_n=0;
    for(int i=0;i<N;i++)for(int j=0;j<N;j++){double dx=Ex[i*N+j]-ExG[i][j];ex_e+=dx*dx;ex_n+=ExG[i][j]*ExG[i][j];double dy=Ey[i*N+j]-EyG[i][j];ey_e+=dy*dy;ey_n+=EyG[i][j]*EyG[i][j];}
    printf("N=%d  Ex rel_rms=%.3e  Ey rel_rms=%.3e\n",N,sqrt(ex_e/(ex_n+1e-30)),sqrt(ey_e/(ey_n+1e-30)));
    printf("PASS if ~1e-6 (float PL field solve vs double naive golden).\n");
    return 0;
}
