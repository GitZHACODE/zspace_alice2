#include "LatentSDF_CUDA.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <fstream>
#include <random>
#include <nlohmann/json.hpp>

#define ALICE2_USE_CUDA

#ifdef ALICE2_USE_CUDA
#define ALICE2_USE_CUDA

namespace DeepSDF {

#define CUDA_CHECK(x) do{ cudaError_t e=(x); if(e!=cudaSuccess){ \
  fprintf(stderr,"[CUDA] %s failed (%s:%d): %s\n", #x, __FILE__, __LINE__, cudaGetErrorString(e)); abort(); } }while(0)

//=========================== Device helpers ===========================
__device__ inline float dtanh_from_a(float a){ return 1.f - a*a; }

struct PosEnc2DDev {
    int   numFreqs;
    bool  includeInput;
    float twoPi;
    __device__ int encDim() const { return (includeInput?2:0)+4*numFreqs; }
};

// enc: [encDim x B]
__global__ void kEncodeXY(const float* xs,const float* ys,float* enc,int B, PosEnc2DDev cfg){
    int j = blockIdx.x*blockDim.x + threadIdx.x; if (j>=B) return;
    int d=0;
    if (cfg.includeInput){ enc[d*B + j]=xs[j]; enc[(d+1)*B + j]=ys[j]; d+=2; }
    float freq=1.f;
    for(int k=0;k<cfg.numFreqs;++k){
        float ax=freq*cfg.twoPi*xs[j], ay=freq*cfg.twoPi*ys[j];
        enc[(d+0)*B + j]=sinf(ax); enc[(d+1)*B + j]=cosf(ax);
        enc[(d+2)*B + j]=sinf(ay); enc[(d+3)*B + j]=cosf(ay);
        d+=4; freq*=2.f;
    }
}

// Assemble X = [z | enc]; X: [inDim x B]
__global__ void kAssembleZX(const float* __restrict__ dZ, int numShapes, int latentDim,
                            const int*  __restrict__ shapeIdx,
                            const float* __restrict__ enc, int encDim,
                            float* __restrict__ X, int B)
{
    int j = blockIdx.x*blockDim.x + threadIdx.x; if (j>=B) return;
    int si = shapeIdx[j]; if (si<0 || si>=numShapes) return;
    const float* zj = dZ + si*latentDim;
    for (int p=0;p<latentDim;++p) X[p*B + j] = zj[p];
    for (int p=0;p<encDim;  ++p) X[(latentDim+p)*B + j] = enc[p*B + j];
}

// C[m x n] = A[m x k] * B[k x n]
template<int TILE>
__global__ void kMatmul(const float* __restrict__ A,const float* __restrict__ B,float* __restrict__ C,int m,int n,int k){
    __shared__ float As[TILE][TILE], Bs[TILE][TILE];
    int row=blockIdx.y*TILE+threadIdx.y, col=blockIdx.x*TILE+threadIdx.x;
    float acc=0.f;
    for (int t=0;t<(k+TILE-1)/TILE;++t){
        int aCol=t*TILE+threadIdx.x, bRow=t*TILE+threadIdx.y;
        As[threadIdx.y][threadIdx.x]=(row<m && aCol<k)? A[row*k+aCol]:0.f;
        Bs[threadIdx.y][threadIdx.x]=(bRow<k && col<n)? B[bRow*n+col]:0.f;
        __syncthreads();
        #pragma unroll
        for (int p=0;p<TILE;++p) acc += As[threadIdx.y][p]*Bs[p][threadIdx.x];
        __syncthreads();
    }
    if (row<m && col<n) C[row*n+col]=acc;
}

__global__ void kAddBias(float* Z,const float* b,int m,int n){
    int row=blockIdx.y*blockDim.y+threadIdx.y, col=blockIdx.x*blockDim.x+threadIdx.x;
    if (row<m && col<n) Z[row*n+col]+=b[row];
}

__global__ void kTanhInplace(float* A,int m,int n){
    int idx=blockIdx.x*blockDim.x+threadIdx.x, N=m*n; if (idx<N) A[idx]=tanhf(A[idx]);
}

// last layer delta (linear head): dL/dA_out = y - t  (clipped)
__global__ void kOutputDelta(const float* __restrict__ y,
                             const float* __restrict__ t,
                             float* __restrict__ delta,
                             int B)
{
    int j = blockIdx.x*blockDim.x + threadIdx.x;
    if (j < B) {
        float d = y[j] - t[j];
        if (d > 3.f) d = 3.f; else if (d < -3.f) d = -3.f;
        delta[j] = d;
    }
}

// dW = A[M x K] * (B^T), where A=delta, B=Aprev [N x K]; result [M x N]
template<int TILE>
__global__ void kMatmul_ABt(const float* __restrict__ A, const float* __restrict__ B, float* __restrict__ C,
                            int M, int N, int K) {
    __shared__ float As[TILE][TILE], Bs[TILE][TILE];
    int row=blockIdx.y*TILE+threadIdx.y, col=blockIdx.x*TILE+threadIdx.x;
    float acc=0.f;
    for (int t=0;t<(K+TILE-1)/TILE;++t){
        int aCol=t*TILE+threadIdx.x; // K
        int bRow=t*TILE+threadIdx.y; // K
        As[threadIdx.y][threadIdx.x] = (row<M && aCol<K) ? A[row*K + aCol] : 0.f;       // A [M x K]
        // read B^T on the fly (B is [N x K], row-major with stride K)
        Bs[threadIdx.y][threadIdx.x] = (bRow<K && col<N) ? B[col*K + bRow] : 0.f;
        __syncthreads();
        #pragma unroll
        for (int p=0;p<TILE;++p) acc += As[threadIdx.y][p] * Bs[p][threadIdx.x];
        __syncthreads();
    }
    if (row<M && col<N) C[row*N + col] = acc;
}

__global__ void kRowSum(const float* A, float* out, int rows, int cols){
    int r = blockIdx.x*blockDim.x + threadIdx.x; if (r>=rows) return;
    float acc=0.f; for (int j=0;j<cols;++j) acc += A[r*cols + j];
    out[r] = acc;
}

// Hidden: deltaPrev = (W^T * delta) ⊙ tanh'(Aprev)
__global__ void kBackpropDelta(const float* W,const float* delta,const float* Aprev,float* deltaPrev,int outDim,int inDim,int B){
    int col=blockIdx.x*blockDim.x+threadIdx.x; int j=blockIdx.y*blockDim.y+threadIdx.y;
    if (col<inDim && j<B){
        float acc=0.f; for (int r=0;r<outDim;++r) acc += W[r*inDim + col] * delta[r*B + j];
        float a=Aprev[col*B + j]; deltaPrev[col*B + j] = acc * dtanh_from_a(a);
    }
}

// Input: deltaPrev = (W^T * delta)   (no tanh' on A0=X)
__global__ void kBackpropDeltaInput(const float* W,const float* delta,float* deltaPrev,int outDim,int inDim,int B){
    int col=blockIdx.x*blockDim.x+threadIdx.x; int j=blockIdx.y*blockDim.y+threadIdx.y;
    if (col<inDim && j<B){
        float acc=0.f; for (int r=0;r<outDim;++r) acc += W[r*inDim + col] * delta[r*B + j];
        deltaPrev[col*B + j] = acc;
    }
}

__global__ void kSgdStep(float* W,float* b,const float* dW,const float* db,int rows,int cols,float lr,float invB,float lambdaW){
    int row=blockIdx.y*blockDim.y+threadIdx.y, col=blockIdx.x*blockDim.x+threadIdx.x;
    if (row<rows && col<cols){
        float g = dW[row*cols + col]*invB + lambdaW*W[row*cols + col];
        W[row*cols + col] -= lr * g;
    }
    if (row<rows && col==0){
        float gb = db[row]*invB; b[row] -= lr*gb;
    }
}

// ==================== NEW: race-free latent update ====================

// Zero a buffer
__global__ void kZero(float* x, int n){
    int i = blockIdx.x*blockDim.x + threadIdx.x; if (i<n) x[i] = 0.f;
}

// Accumulate ∑_{j | shapeIdx[j]=s} Δ0[p,j] into dZgrad[s,p]
__global__ void kAccumulateLatentGrad_ByShape(const float* __restrict__ delta0, // [inDim x B]
                                              const int*  __restrict__ shapeIdx,// [B]
                                              int B, int numShapes, int latentDim,
                                              float* __restrict__ dZgrad)       // [numShapes x latentDim]
{
    int s = blockIdx.y;                               // shape id
    int p = blockIdx.x*blockDim.x + threadIdx.x;      // latent dim index
    if (s >= numShapes || p >= latentDim) return;

    float acc = 0.f;
    for (int j=0; j<B; ++j){
        if (shapeIdx[j] == s) acc += delta0[p*B + j];
    }
    dZgrad[s*latentDim + p] = acc;
}

// Apply Z ← Z − lrZ * (dZgrad + λ Z)
__global__ void kApplyLatentUpdate(float* __restrict__ Z, const float* __restrict__ dZgrad,
                                   int numShapes, int latentDim, float lrZ, float lambda)
{
    int i = blockIdx.x*blockDim.x + threadIdx.x;
    int n = numShapes * latentDim;
    if (i >= n) return;
    float z = Z[i];
    float g = dZgrad[i] + lambda * z;
    Z[i] = z - lrZ * g;
}

// ==================== stats kernels ====================

// Per-sample MSE (clipped residual)
__global__ void kLossMSE(const float* __restrict__ y,
                         const float* __restrict__ t,
                         float* __restrict__ perSample,
                         int B)
{
    int j = blockIdx.x*blockDim.x + threadIdx.x;
    if (j < B){
        float d = y[j] - t[j];
        if (d > 3.f) d = 3.f; else if (d < -3.f) d = -3.f;
        perSample[j] = 0.5f * d * d;
    }
}

// Accumulate running loss & sample count (device)
__global__ void kAccumulateLoss(const float* __restrict__ perSample,
                                int B, float* __restrict__ lossAcc,
                                unsigned int* __restrict__ nSamplesAcc)
{
    __shared__ float s[256];
    int tid = threadIdx.x;
    int i   = blockIdx.x*blockDim.x + tid;
    float v = (i < B) ? perSample[i] : 0.f;
    s[tid] = v;
    __syncthreads();
    for (int stride = blockDim.x>>1; stride > 0; stride >>= 1){
        if (tid < stride) s[tid] += s[tid+stride];
        __syncthreads();
    }
    if (tid == 0) atomicAdd(lossAcc, s[0]);
    if (blockIdx.x == 0 && tid == 0) atomicAdd(nSamplesAcc, (unsigned int)B);
}

// Mean ||z|| reduction (used in getter)
__global__ void kMeanZReduce(const float* __restrict__ Z, int numShapes, int latentDim,
                             float* __restrict__ meanZOut)
{
    int si = blockIdx.x * blockDim.x + threadIdx.x;
    if (si >= numShapes) return;
    const float* zi = Z + si*latentDim;
    float s = 0.f;
    for (int p=0; p<latentDim; ++p) s += zi[p]*zi[p];
    float nz = sqrtf(s + 1e-12f);
    atomicAdd(meanZOut, nz);
}

__global__ void kAssembleZXSingle(const float* __restrict__ z, int latentDim,
                                  const float* __restrict__ enc, int encDim,
                                  float* __restrict__ X, int B)
{
    int j = blockIdx.x*blockDim.x + threadIdx.x;
    if (j >= B) return;
    for (int p = 0; p < latentDim; ++p) {
        X[p*B + j] = z[p];
    }
    for (int p = 0; p < encDim; ++p) {
        X[(latentDim + p)*B + j] = enc[p*B + j];
    }
}

struct DeviceFieldRenderConfig {
    int   debugMode;
    int   softMask;
    float tau;
};

__global__ void kComputeTileMinMax(const float* __restrict__ panel,
                                   int panelWidth,
                                   int tileRes,
                                   int gap,
                                   int panelN,
                                   float* __restrict__ tileMin,
                                   float* __restrict__ tileMax)
{
    const int tileIdx = blockIdx.x;
    if (tileIdx >= panelN * panelN) return;
    const int tileX = tileIdx % panelN;
    const int tileY = tileIdx / panelN;
    const int offsetX = tileX * tileRes + tileX * gap;
    const int offsetY = tileY * tileRes + tileY * gap;
    const int total = tileRes * tileRes;

    float minVal = 1e30f;
    float maxVal = -1e30f;

    for (int idx = threadIdx.x; idx < total; idx += blockDim.x) {
        const int localX = idx % tileRes;
        const int localY = idx / tileRes;
        const float v = panel[(offsetY + localY) * panelWidth + (offsetX + localX)];
        minVal = fminf(minVal, v);
        maxVal = fmaxf(maxVal, v);
    }

    __shared__ float sMin[256];
    __shared__ float sMax[256];
    sMin[threadIdx.x] = minVal;
    sMax[threadIdx.x] = maxVal;
    __syncthreads();

    for (int stride = blockDim.x >> 1; stride > 0; stride >>= 1) {
        if (threadIdx.x < stride) {
            sMin[threadIdx.x] = fminf(sMin[threadIdx.x], sMin[threadIdx.x + stride]);
            sMax[threadIdx.x] = fmaxf(sMax[threadIdx.x], sMax[threadIdx.x + stride]);
        }
        __syncthreads();
    }

    if (threadIdx.x == 0) {
        tileMin[tileIdx] = sMin[0];
        tileMax[tileIdx] = sMax[0];
    }
}

__global__ void kPanelToRGBA(const float* __restrict__ panel,
                             uchar4* __restrict__ out,
                             int panelWidth,
                             int panelHeight,
                             int tileRes,
                             int gap,
                             int panelN,
                             DeviceFieldRenderConfig cfg,
                             const float* __restrict__ tileMin,
                             const float* __restrict__ tileMax)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= panelWidth || y >= panelHeight) return;

    const int tileSpan = tileRes + gap;
    const int tileX = x / tileSpan;
    const int tileY = y / tileSpan;
    const int outIdx = y * panelWidth + x;

    if (tileX >= panelN || tileY >= panelN) {
        out[outIdx] = make_uchar4(255, 255, 255, 255);
        return;
    }

    const int localX = x - tileX * tileSpan;
    const int localY = y - tileY * tileSpan;
    if (localX >= tileRes || localY >= tileRes) {
        out[outIdx] = make_uchar4(255, 255, 255, 255);
        return;
    }

    const int tileIdx = tileY * panelN + tileX;
    const float sdf = panel[outIdx];

    float gray = 0.5f;
    if (cfg.debugMode == 0) {
        if (cfg.softMask) {
            const float tau = fmaxf(cfg.tau, 1e-6f);
            gray = 1.0f / (1.0f + __expf(-(sdf / tau)));
        } else {
            gray = sdf < 0.0f ? 0.0f : 1.0f;
        }
    } else {
        const float vmin = tileMin[tileIdx];
        const float vmax = tileMax[tileIdx];
        const float range = (vmax - vmin == 0.0f) ? 1.0f : (vmax - vmin);
        gray = fminf(fmaxf((sdf - vmin) / range, 0.0f), 1.0f);
    }

    const unsigned char v = static_cast<unsigned char>(roundf(gray * 255.0f));
    out[outIdx] = make_uchar4(v, v, v, 255);
}

//=========================== MLP state ===========================
struct Layer {
    int inDim=0, outDim=0;
    float *W=nullptr, *b=nullptr;
    float *A=nullptr, *Z=nullptr, *Delta=nullptr;   // [rows x Bmax]
    float *dW=nullptr, *db=nullptr;
};

struct CudaMLP {
    int L=0, inDim=0, maxB=0;
    std::vector<Layer> layers;
    // shared batch buffers
    float *X=nullptr, *Enc=nullptr, *Xs=nullptr, *Ys=nullptr, *Tgt=nullptr;
    PosEnc2DDev enc;
};

static void xavierInit(std::vector<float>& w, int rows, int cols, bool last){
    std::mt19937 rng(42); std::normal_distribution<float> N(0.f,1.f);
    float s = last ? (0.5f/std::sqrt((float)cols)) : (1.0f/std::sqrt((float)cols));
    for (int i=0;i<rows*cols;i++) w[i] = s * N(rng);
}

//=========================== Impl ===========================
struct TinyAutoDecoderCUDA::Impl {
    int numShapes=0, latentDim=0, coordEncDim=0;
    float lambdaLatent=1e-4f, weightDecayW=1e-6f;

    // MLP
    int L=0, inDim=0, maxB=0;
    std::vector<Layer> layers;
    float *X=nullptr, *Enc=nullptr, *Xs=nullptr, *Ys=nullptr, *Tgt=nullptr;
    PosEnc2DDev enc;

    // host batch mirrors
    std::vector<int>   hShapeIdx;
    std::vector<float> hXs, hYs, hTgt;

    // device latent table & per-batch meta
    float* dZ = nullptr;          // [numShapes * latentDim]
    int*   dShapeIdx = nullptr;   // [B]

    // latent grad buffer (race-free path)
    float* dZgrad = nullptr;      // [numShapes * latentDim]

    // --- stats accumulators on device ---
    float*        dLossPerSample = nullptr;  // [maxBatch]
    float*        dLossAcc       = nullptr;  // scalar running sum of loss
    unsigned int* dNSamplesAcc   = nullptr;  // scalar running count of samples
    float*        dMeanZ         = nullptr;  // scalar (scratch for getter)

    // API-like helpers (member functions so we can access private fields)
    void forwardBatch(int B);
    void backwardBatchAndUpdate(int B, float lrW);

    void initMLP(int inputDim, const std::vector<int>& hidden, int outDim, int maxBatch, int numFreqs, bool includeInput){
        inDim = inputDim; maxB = maxBatch;
        L = (int)hidden.size() + 1;
        layers.resize(L);

        int prev=inputDim;
        for (int l=0;l<L;++l){
            int out = (l<(int)hidden.size())? hidden[l] : outDim;
            layers[l].inDim = prev;
            layers[l].outDim= out;

            int rows=out, cols=prev;
            std::vector<float> hW(rows*cols), hb(rows,0.f);
            xavierInit(hW, rows, cols, l==L-1);
            CUDA_CHECK(cudaMalloc(&layers[l].W, rows*cols*sizeof(float)));
            CUDA_CHECK(cudaMemcpy(layers[l].W, hW.data(), rows*cols*sizeof(float), cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMalloc(&layers[l].b, rows*sizeof(float)));
            CUDA_CHECK(cudaMemcpy(layers[l].b, hb.data(), rows*sizeof(float), cudaMemcpyHostToDevice));

            CUDA_CHECK(cudaMalloc(&layers[l].A, rows*maxBatch*sizeof(float)));
            CUDA_CHECK(cudaMalloc(&layers[l].Z, rows*maxBatch*sizeof(float)));
            CUDA_CHECK(cudaMalloc(&layers[l].Delta, rows*maxBatch*sizeof(float)));
            CUDA_CHECK(cudaMalloc(&layers[l].dW, rows*cols*sizeof(float)));
            CUDA_CHECK(cudaMalloc(&layers[l].db, rows*sizeof(float)));

            prev = out;
        }
        CUDA_CHECK(cudaMalloc(&X,   inputDim*maxBatch*sizeof(float)));
        CUDA_CHECK(cudaMalloc(&Enc, coordEncDim*maxBatch*sizeof(float)));
        CUDA_CHECK(cudaMalloc(&Xs,  maxBatch*sizeof(float)));
        CUDA_CHECK(cudaMalloc(&Ys,  maxBatch*sizeof(float)));
        CUDA_CHECK(cudaMalloc(&Tgt, maxBatch*sizeof(float)));

        enc.numFreqs=numFreqs; enc.includeInput=includeInput; enc.twoPi=6.283185307179586f;

        CUDA_CHECK(cudaMalloc(&dShapeIdx, maxBatch*sizeof(int)));

        // stats buffers
        CUDA_CHECK(cudaMalloc(&dLossPerSample, maxBatch*sizeof(float)));
        CUDA_CHECK(cudaMalloc(&dLossAcc,       sizeof(float)));
        CUDA_CHECK(cudaMalloc(&dNSamplesAcc,   sizeof(unsigned int)));
        CUDA_CHECK(cudaMalloc(&dMeanZ,         sizeof(float)));
        CUDA_CHECK(cudaMemset(dLossAcc,     0, sizeof(float)));
        CUDA_CHECK(cudaMemset(dNSamplesAcc, 0, sizeof(unsigned int)));
    }
};

// ---- Impl member defs ----
void TinyAutoDecoderCUDA::Impl::forwardBatch(int B){
    const int TILE=16;
    for (int l=0;l<L;++l){
        Layer& Lr = layers[l];
        int m=Lr.outDim, n=B, k=Lr.inDim;
        float* Aprev = (l==0)? X : layers[l-1].A;

        dim3 grid((n+TILE-1)/TILE,(m+TILE-1)/TILE), blk(TILE,TILE);
        kMatmul<TILE><<<grid,blk>>>(Lr.W, Aprev, Lr.Z, m,n,k);

        dim3 g2((n+15)/16,(m+15)/16), b2(16,16);
        kAddBias<<<g2,b2>>>(Lr.Z, Lr.b, m,n);

        CUDA_CHECK(cudaMemcpy(Lr.A, Lr.Z, m*n*sizeof(float), cudaMemcpyDeviceToDevice));
        if (l != L-1){
            int N=m*n; int tpb=256,bpg=(N+tpb-1)/tpb; kTanhInplace<<<bpg, tpb>>>(Lr.A, m,n);
        }
    }
}

void TinyAutoDecoderCUDA::Impl::backwardBatchAndUpdate(int B, float lrW){
    Layer& last = layers.back();

    // output delta
    { int tpb=256, bpg=(B+tpb-1)/tpb; kOutputDelta<<<bpg, tpb>>>(last.A, Tgt, last.Delta, B); }

    // zero grads
    for (auto& Lr : layers){
        CUDA_CHECK(cudaMemset(Lr.dW, 0, Lr.outDim * Lr.inDim * sizeof(float)));
        CUDA_CHECK(cudaMemset(Lr.db, 0, Lr.outDim * sizeof(float)));
    }

    // last layer grads
    {
        int M = last.outDim, K = B, N = last.inDim;
        float* Aprev = (L==1)? X : layers[L-2].A; // [N x K]
        dim3 blk(16,16), grd((N+15)/16,(M+15)/16);
        kMatmul_ABt<16><<<grd, blk>>>(last.Delta /*[M x K]*/, Aprev /*[N x K]*/, last.dW /*[M x N]*/, M,N,K);
        int tpb=256, bpg=(M+tpb-1)/tpb;
        kRowSum<<<bpg, tpb>>>(last.Delta, last.db, M, K);
    }

    // hidden layers
    for (int l=L-2; l>=0; --l){
        Layer& Lr  = layers[l];
        Layer& Lrn = layers[l+1];

        // delta[l]
        dim3 blkBP(16,16), grdBP((Lr.outDim+15)/16, (B+15)/16);
        if (l == 0){
            kBackpropDeltaInput<<<grdBP, blkBP>>>(Lrn.W, Lrn.Delta, Lr.Delta, Lrn.outDim, Lr.outDim, B);
        } else {
            kBackpropDelta<<<grdBP, blkBP>>>(Lrn.W, Lrn.Delta, Lr.A, Lr.Delta, Lrn.outDim, Lr.outDim, B);
        }

        // grads for layer l
        int M=Lr.outDim, N=Lr.inDim, K=B;
        float* Aprev = (l==0)? X : layers[l-1].A; // [N x K]
        dim3 blk(16,16), grd((N+15)/16,(M+15)/16);
        kMatmul_ABt<16><<<grd, blk>>>(Lr.Delta /*[M x K]*/, Aprev /*[N x K]*/, Lr.dW /*[M x N]*/, M,N,K);
        int tpb=256, bpg=(M+tpb-1)/tpb;
        kRowSum<<<bpg, tpb>>>(Lr.Delta, Lr.db, M, K);
    }

    // SGD step all layers
    for (auto& Lr : layers){
        dim3 blk(16,16), grd((Lr.inDim+15)/16,(Lr.outDim+15)/16);
        kSgdStep<<<grd, blk>>>(Lr.W, Lr.b, Lr.dW, Lr.db, Lr.outDim, Lr.inDim, lrW, 1.0f/float(B), weightDecayW);
    }
}

//=========================== public API ===========================
TinyAutoDecoderCUDA::TinyAutoDecoderCUDA() : impl_(new Impl) {}
TinyAutoDecoderCUDA::~TinyAutoDecoderCUDA(){ delete impl_; }

void TinyAutoDecoderCUDA::initialize(int numShapes,int latentDim,const std::vector<int>& hidden,
                                     unsigned seed,int maxBatch,int numFreqs,bool includeInput)
{
    numShapes_  = numShapes;
    latentDim_  = latentDim;
    coordEncDim_= (includeInput?2:0) + 4*numFreqs;

    // host latents
    Z_.assign(numShapes_, std::vector<float>(latentDim_, 0.f));
    std::mt19937 rng(seed); std::normal_distribution<float> N(0.f,0.01f);
    for (int i=0;i<numShapes_;++i) for (int j=0;j<latentDim_;++j) Z_[i][j]=N(rng);

    // impl config
    impl_->numShapes    = numShapes_;
    impl_->latentDim    = latentDim_;
    impl_->coordEncDim  = coordEncDim_;
    impl_->lambdaLatent = lambdaLatent_;
    impl_->weightDecayW = weightDecayW_;

    // device latents upload + grad buffer
    CUDA_CHECK(cudaMalloc(&impl_->dZ, numShapes_*latentDim_*sizeof(float)));
    CUDA_CHECK(cudaMalloc(&impl_->dZgrad, numShapes_*latentDim_*sizeof(float)));
    {
        std::vector<float> zFlat(numShapes_*latentDim_);
        for (int i=0;i<numShapes_;++i) std::memcpy(&zFlat[i*latentDim_], Z_[i].data(), latentDim_*sizeof(float));
        CUDA_CHECK(cudaMemcpy(impl_->dZ, zFlat.data(), zFlat.size()*sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemset(impl_->dZgrad, 0, numShapes_*latentDim_*sizeof(float)));
    }

    // init MLP (now stored directly on Impl)
    impl_->initMLP(latentDim_ + coordEncDim_, hidden, /*out*/1, maxBatch, numFreqs, includeInput);
}

void TinyAutoDecoderCUDA::syncLatentsToHost(){
    std::vector<float> zFlat(numShapes_*latentDim_);
    CUDA_CHECK(cudaMemcpy(zFlat.data(), impl_->dZ, zFlat.size()*sizeof(float), cudaMemcpyDeviceToHost));
    for (int i=0;i<numShapes_; ++i){
        Z_[i].assign(zFlat.begin() + i*latentDim_, zFlat.begin() + (i+1)*latentDim_);
    }
}

void TinyAutoDecoderCUDA::trainMicroBatchGPU(int B, Sampler& sampler, std::mt19937& rng,
                                             float lrW,float lrZ,
                                             const std::vector<std::vector<float>>& shapes)
{
    sampler.setShapes(&shapes);
    std::uniform_int_distribution<int> pick(0, numShapes_-1);
    impl_->hShapeIdx.resize(B); impl_->hXs.resize(B); impl_->hYs.resize(B); impl_->hTgt.resize(B);

    // 1) sample on host
    for (int j=0; j<B; ++j) {
        int si = pick(rng);
        auto [x,y,t] = sampler.sampleForShape(si);
        impl_->hShapeIdx[j] = si; impl_->hXs[j] = x; impl_->hYs[j] = y; impl_->hTgt[j] = t;
    }

    // 2) copy batch meta to device
    CUDA_CHECK(cudaMemcpy(impl_->Xs,  impl_->hXs.data(),  B*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(impl_->Ys,  impl_->hYs.data(),  B*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(impl_->Tgt, impl_->hTgt.data(), B*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(impl_->dShapeIdx, impl_->hShapeIdx.data(), B*sizeof(int), cudaMemcpyHostToDevice));

    // 3) encode coords on device
    { int tpb=256, bpg=(B+tpb-1)/tpb;
      kEncodeXY<<<bpg, 256>>>(impl_->Xs, impl_->Ys, impl_->Enc, B, impl_->enc); }

    // 4) assemble X=[z|enc] on device and set A0
    { int tpb=256, bpg=(B+tpb-1)/tpb;
      kAssembleZX<<<bpg, tpb>>>(impl_->dZ, impl_->numShapes, impl_->latentDim,
                                impl_->dShapeIdx, impl_->Enc, impl_->coordEncDim,
                                impl_->X, B);
      CUDA_CHECK(cudaMemcpy(impl_->layers[0].A, impl_->X,
                            (impl_->latentDim+impl_->coordEncDim)*B*sizeof(float), cudaMemcpyDeviceToDevice));
    }

    // 5) forward
    impl_->forwardBatch(B);

    // 6) backward + SGD
    impl_->backwardBatchAndUpdate(B, lrW);

    // 7) === RACE-FREE LATENT UPDATE ===
    {
        const int nZ = impl_->numShapes * impl_->latentDim;
        kZero<<<(nZ+255)/256, 256>>>(impl_->dZgrad, nZ);

        dim3 blkA(128,1);
        dim3 grdA((impl_->latentDim + blkA.x - 1)/blkA.x, impl_->numShapes);
        // use Δ0 = layers[0].Delta which is [inDim x B]; first latentDim rows are ∂L/∂z
        kAccumulateLatentGrad_ByShape<<<grdA, blkA>>>(impl_->layers[0].Delta,
                                                      impl_->dShapeIdx,
                                                      B, impl_->numShapes, impl_->latentDim,
                                                      impl_->dZgrad);

        kApplyLatentUpdate<<<(nZ+255)/256, 256>>>(impl_->dZ, impl_->dZgrad,
                                                  impl_->numShapes, impl_->latentDim,
                                                  lrZ, impl_->lambdaLatent);
    }

    // 8) device-side loss accumulation → running stats (no host copy)
    { int tpb=256, bpg=(B+tpb-1)/tpb;
      kLossMSE<<<bpg, tpb>>>(impl_->layers.back().A, impl_->Tgt,
                             impl_->dLossPerSample, B);
      kAccumulateLoss<<<(B+255)/256, 256>>>(impl_->dLossPerSample, B,
                                            impl_->dLossAcc, impl_->dNSamplesAcc);
    }
}

void TinyAutoDecoderCUDA::syncStatsToHost(double& avgLoss, double& meanZ, bool reset)
{
    // 1) compute mean||z|| on device
    CUDA_CHECK(cudaMemset(impl_->dMeanZ, 0, sizeof(float)));
    int tpb = 256, bpg = (numShapes_ + tpb - 1) / tpb;
    kMeanZReduce<<<bpg, tpb>>>(impl_->dZ, numShapes_, latentDim_, impl_->dMeanZ);

    // 2) pull accumulators
    float hLossAcc=0.f, hMeanZSum=0.f; unsigned int hN=0;
    CUDA_CHECK(cudaMemcpy(&hLossAcc, impl_->dLossAcc,     sizeof(float),        cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&hN,       impl_->dNSamplesAcc, sizeof(unsigned int), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(&hMeanZSum,impl_->dMeanZ,       sizeof(float),        cudaMemcpyDeviceToHost));

    avgLoss = (hN>0) ? double(hLossAcc)/double(hN) : -1.0;
    meanZ   = (numShapes_>0) ? double(hMeanZSum)/double(numShapes_) : -1.0;

    // 3) optionally reset running stats so the next call reports fresh averages
    if (reset){
        CUDA_CHECK(cudaMemset(impl_->dLossAcc,     0, sizeof(float)));
        CUDA_CHECK(cudaMemset(impl_->dNSamplesAcc, 0, sizeof(unsigned int)));
    }
}

void TinyAutoDecoderCUDA::saveModelJSON(const std::string& path,
                                        const FieldDomain& domain) const
{
    using nlohmann::json;
    json j;

    j["domain"] = {
        {"resX", domain.resX}, {"resY", domain.resY},
        {"xMin", domain.xMin}, {"xMax", domain.xMax},
        {"yMin", domain.yMin}, {"yMax", domain.yMax}
    };

    j["numShapes"] = numShapes_;
    j["latentDim"] = latentDim_;
    j["latents"]   = Z_;
    j["posenc"] = {
        {"numFreqs", impl_->enc.numFreqs},
        {"includeInput", impl_->enc.includeInput},
        {"coordEncDim", impl_->coordEncDim}
    };

    std::vector<int> dims;
    std::vector<std::vector<float>> hostW;
    std::vector<std::vector<float>> hostB;
    dims.reserve(impl_->layers.size() + 1);
    if (!impl_->layers.empty()) {
        dims.push_back(impl_->layers[0].inDim);
    }
    for (const auto& layer : impl_->layers) {
        const int rows = layer.outDim;
        const int cols = layer.inDim;

        std::vector<float> w(rows * cols);
        std::vector<float> b(rows);
        CUDA_CHECK(cudaMemcpy(w.data(), layer.W, rows * cols * sizeof(float), cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(b.data(), layer.b, rows * sizeof(float), cudaMemcpyDeviceToHost));

        hostW.push_back(std::move(w));
        hostB.push_back(std::move(b));
        dims.push_back(rows);
    }

    j["decoder"] = {
        {"dims", dims},
        {"W", hostW},
        {"b", hostB}
    };

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::fprintf(stderr, "[CUDA][Model] Failed to open '%s' for writing.\n", path.c_str());
        return;
    }
    ofs << j.dump(2);
    std::printf("[CUDA][Model] Saved model -> %s\n", path.c_str());
}

bool TinyAutoDecoderCUDA::loadModelJSON(const std::string& path,
                                        FieldDomain& outDomain,
                                        int maxBatch,
                                        unsigned seed)
{
    using nlohmann::json;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::fprintf(stderr, "[CUDA][Model] Cannot open '%s'\n", path.c_str());
        return false;
    }

    json j;
    try {
        ifs >> j;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[CUDA][Model] Failed to parse '%s': %s\n", path.c_str(), e.what());
        return false;
    }

    outDomain.resX = j["domain"]["resX"].get<int>();
    outDomain.resY = j["domain"]["resY"].get<int>();
    outDomain.xMin = j["domain"]["xMin"].get<float>();
    outDomain.xMax = j["domain"]["xMax"].get<float>();
    outDomain.yMin = j["domain"]["yMin"].get<float>();
    outDomain.yMax = j["domain"]["yMax"].get<float>();

    auto latents = j["latents"].get<std::vector<std::vector<float>>>();
    const int numShapes = static_cast<int>(latents.size());
    const int latentDim = j["latentDim"].get<int>();

    const auto& posenc = j["posenc"];
    const int numFreqs = posenc["numFreqs"].get<int>();
    const bool includeInput = posenc["includeInput"].get<bool>();

    const auto& jd = j["decoder"];
    auto dims = jd["dims"].get<std::vector<int>>();
    auto hostW = jd["W"].get<std::vector<std::vector<float>>>();
    auto hostB = jd["b"].get<std::vector<std::vector<float>>>();

    if (dims.size() < 2 || hostW.size() != hostB.size()) {
        std::fprintf(stderr, "[CUDA][Model] Invalid decoder layout in '%s'\n", path.c_str());
        return false;
    }

    std::vector<int> hidden;
    if (dims.size() > 2) {
        hidden.assign(dims.begin() + 1, dims.end() - 1);
    }

    initialize(numShapes, latentDim, hidden, seed, maxBatch, numFreqs, includeInput);

    Z_ = std::move(latents);
    std::vector<float> flatLatents(size_t(numShapes) * size_t(latentDim));
    for (int s = 0; s < numShapes; ++s) {
        if ((int)Z_[s].size() != latentDim) {
            std::fprintf(stderr, "[CUDA][Model] Latent dimension mismatch while loading '%s'\n", path.c_str());
            return false;
        }
        std::memcpy(flatLatents.data() + size_t(s) * size_t(latentDim),
                    Z_[s].data(),
                    size_t(latentDim) * sizeof(float));
    }
    CUDA_CHECK(cudaMemcpy(impl_->dZ, flatLatents.data(),
                          flatLatents.size() * sizeof(float),
                          cudaMemcpyHostToDevice));

    if (hostW.size() != impl_->layers.size()) {
        std::fprintf(stderr, "[CUDA][Model] Layer count mismatch while loading '%s'\n", path.c_str());
        return false;
    }

    for (size_t l = 0; l < impl_->layers.size(); ++l) {
        const int rows = impl_->layers[l].outDim;
        const int cols = impl_->layers[l].inDim;
        if ((int)hostB[l].size() != rows || (int)hostW[l].size() != rows * cols) {
            std::fprintf(stderr, "[CUDA][Model] Layer shape mismatch while loading '%s'\n", path.c_str());
            return false;
        }
        CUDA_CHECK(cudaMemcpy(impl_->layers[l].W, hostW[l].data(),
                              rows * cols * sizeof(float), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpy(impl_->layers[l].b, hostB[l].data(),
                              rows * sizeof(float), cudaMemcpyHostToDevice));
    }

    CUDA_CHECK(cudaMemset(impl_->dZgrad, 0, sizeof(float) * size_t(numShapes) * size_t(latentDim)));

    std::printf("[CUDA][Model] Loaded model <- %s\n", path.c_str());
    return true;
}

void TinyAutoDecoderCUDA::forwardRowGPU(int shapeIdx, const std::vector<float>& xs, float y,
                                        std::vector<float>& outY) const
{
    const int W = (int)xs.size();
    outY.resize(W);

    std::vector<float> ys(W, y);
    std::vector<int>   sidx(W, shapeIdx);

    CUDA_CHECK(cudaMemcpy(impl_->Xs, xs.data(), W*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(impl_->Ys, ys.data(), W*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(impl_->dShapeIdx, sidx.data(), W*sizeof(int), cudaMemcpyHostToDevice));

    { int tpb=256, bpg=(W+tpb-1)/tpb;
      kEncodeXY<<<bpg,256>>>(impl_->Xs, impl_->Ys, impl_->Enc, W, impl_->enc);
      kAssembleZX<<<bpg,256>>>(impl_->dZ, impl_->numShapes, impl_->latentDim,
                               impl_->dShapeIdx, impl_->Enc, impl_->coordEncDim,
                               impl_->X, W);
      CUDA_CHECK(cudaMemcpy(impl_->layers[0].A, impl_->X,
                            (impl_->latentDim+impl_->coordEncDim)*W*sizeof(float), cudaMemcpyDeviceToDevice));
    }
    impl_->forwardBatch(W);

    CUDA_CHECK(cudaMemcpy(outY.data(), impl_->layers.back().A, W*sizeof(float), cudaMemcpyDeviceToHost));
}

void TinyAutoDecoderCUDA::decodeLatentGridToDevice(const float* latentHost,
                                                   int resX, int resY,
                                                   float xMin, float xMax,
                                                   float yMin, float yMax,
                                                   float* dstDevice,
                                                   int dstStride,
                                                   int dstOffsetX,
                                                   int dstOffsetY,
                                                   float* scratchLatentDevice)
{
    if (!latentHost || !dstDevice || !scratchLatentDevice || !impl_) return;
    if (resX <= 0 || resY <= 0) return;

    CUDA_CHECK(cudaMemcpy(scratchLatentDevice, latentHost,
                          size_t(latentDim_) * sizeof(float),
                          cudaMemcpyHostToDevice));

    const float dx = (resX > 1) ? (xMax - xMin) / float(resX - 1) : 0.0f;
    const float dy = (resY > 1) ? (yMax - yMin) / float(resY - 1) : 0.0f;

    const int maxChunk = std::max(1, impl_->maxB);
    if ((int)impl_->hXs.size() < maxChunk) impl_->hXs.resize(maxChunk);
    if ((int)impl_->hYs.size() < maxChunk) impl_->hYs.resize(maxChunk);

    for (int y = 0; y < resY; ++y) {
        const float yy = yMin + dy * float(y);
        for (int baseX = 0; baseX < resX; baseX += maxChunk) {
            const int B = std::min(maxChunk, resX - baseX);
            for (int i = 0; i < B; ++i) {
                const int xIdx = baseX + i;
                impl_->hXs[i] = xMin + dx * float(xIdx);
                impl_->hYs[i] = yy;
            }

            CUDA_CHECK(cudaMemcpy(impl_->Xs, impl_->hXs.data(), B*sizeof(float), cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaMemcpy(impl_->Ys, impl_->hYs.data(), B*sizeof(float), cudaMemcpyHostToDevice));

            int tpb = 256;
            int bpg = (B + tpb - 1) / tpb;
            kEncodeXY<<<bpg, tpb>>>(impl_->Xs, impl_->Ys, impl_->Enc, B, impl_->enc);
            kAssembleZXSingle<<<bpg, tpb>>>(scratchLatentDevice, latentDim_,
                                            impl_->Enc, impl_->coordEncDim,
                                            impl_->X, B);
            CUDA_CHECK(cudaMemcpy(impl_->layers[0].A, impl_->X,
                                  (latentDim_ + impl_->coordEncDim)*B*sizeof(float),
                                  cudaMemcpyDeviceToDevice));
            impl_->forwardBatch(B);

            float* destRow = dstDevice + (dstOffsetY + y) * dstStride + dstOffsetX + baseX;
            CUDA_CHECK(cudaMemcpy(destRow, impl_->layers.back().A,
                                  B*sizeof(float), cudaMemcpyDeviceToDevice));
        }
    }
}

void TinyAutoDecoderCUDA::panelToRGBA(const float* panelFieldDevice,
                                      int panelWidth, int panelHeight,
                                      int tileRes, int gap, int panelN,
                                      const FieldRenderConfig& cfg,
                                      float* tileMinDevice,
                                      float* tileMaxDevice,
                                      uchar4* rgbaDevice)
{
    if (!panelFieldDevice || !tileMinDevice || !tileMaxDevice || !rgbaDevice) return;
    if (panelWidth <= 0 || panelHeight <= 0 || panelN <= 0 || !impl_) return;

    dim3 reduceBlock(256);
    dim3 reduceGrid(panelN * panelN);
    kComputeTileMinMax<<<reduceGrid, reduceBlock>>>(panelFieldDevice, panelWidth,
                                                    tileRes, gap, panelN,
                                                    tileMinDevice, tileMaxDevice);
    CUDA_CHECK(cudaGetLastError());

    DeviceFieldRenderConfig cfgDev;
    cfgDev.debugMode = cfg.debugMode;
    cfgDev.softMask  = cfg.softMask ? 1 : 0;
    cfgDev.tau       = cfg.tau;

    dim3 block(16,16);
    dim3 grid((panelWidth + block.x - 1) / block.x,
              (panelHeight + block.y - 1) / block.y);
    kPanelToRGBA<<<grid, block>>>(panelFieldDevice, rgbaDevice,
                                  panelWidth, panelHeight,
                                  tileRes, gap, panelN,
                                  cfgDev, tileMinDevice, tileMaxDevice);
    CUDA_CHECK(cudaGetLastError());
}

#endif //ALICE2_USE_CUDA

} // namescape DeepSDF

#ifdef ALICE2_USE_CUDA

#include "LatentNavigator_CUDA.h"

namespace detail {

__global__ void kAssembleZXSingle(const float* __restrict__ z, int latentDim,
                                  const float* __restrict__ enc, int encDim,
                                  float* __restrict__ X, int B)
{
    int j = blockIdx.x*blockDim.x + threadIdx.x;
    if (j >= B) return;
    for (int p = 0; p < latentDim; ++p) {
        X[p*B + j] = z[p];
    }
    for (int p = 0; p < encDim; ++p) {
        X[(latentDim + p)*B + j] = enc[p*B + j];
    }
}

struct DeviceFieldRenderConfig {
    int   debugMode;
    int   softMask;
    float tau;
};

__global__ void kComputeTileMinMax(const float* __restrict__ panel,
                                   int panelWidth,
                                   int tileRes,
                                   int gap,
                                   int panelN,
                                   float* __restrict__ tileMin,
                                   float* __restrict__ tileMax)
{
    const int tileIdx = blockIdx.x;
    if (tileIdx >= panelN * panelN) return;
    const int tileX = tileIdx % panelN;
    const int tileY = tileIdx / panelN;
    const int offsetX = tileX * tileRes + tileX * gap;
    const int offsetY = tileY * tileRes + tileY * gap;
    const int total = tileRes * tileRes;

    float minVal = 1e30f;
    float maxVal = -1e30f;

    for (int idx = threadIdx.x; idx < total; idx += blockDim.x) {
        const int localX = idx % tileRes;
        const int localY = idx / tileRes;
        const float v = panel[(offsetY + localY) * panelWidth + (offsetX + localX)];
        minVal = fminf(minVal, v);
        maxVal = fmaxf(maxVal, v);
    }

    __shared__ float sMin[256];
    __shared__ float sMax[256];
    sMin[threadIdx.x] = minVal;
    sMax[threadIdx.x] = maxVal;
    __syncthreads();

    for (int stride = blockDim.x >> 1; stride > 0; stride >>= 1) {
        if (threadIdx.x < stride) {
            sMin[threadIdx.x] = fminf(sMin[threadIdx.x], sMin[threadIdx.x + stride]);
            sMax[threadIdx.x] = fmaxf(sMax[threadIdx.x], sMax[threadIdx.x + stride]);
        }
        __syncthreads();
    }

    if (threadIdx.x == 0) {
        tileMin[tileIdx] = sMin[0];
        tileMax[tileIdx] = sMax[0];
    }
}

__global__ void kPanelToRGBA(const float* __restrict__ panel,
                             uchar4* __restrict__ out,
                             int panelWidth,
                             int panelHeight,
                             int tileRes,
                             int gap,
                             int panelN,
                             DeviceFieldRenderConfig cfg,
                             const float* __restrict__ tileMin,
                             const float* __restrict__ tileMax)
{
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= panelWidth || y >= panelHeight) return;

    const int tileSpan = tileRes + gap;
    const int tileX = x / tileSpan;
    const int tileY = y / tileSpan;
    const int outIdx = y * panelWidth + x;

    if (tileX >= panelN || tileY >= panelN) {
        out[outIdx] = make_uchar4(255, 255, 255, 255);
        return;
    }

    const int localX = x - tileX * tileSpan;
    const int localY = y - tileY * tileSpan;
    if (localX >= tileRes || localY >= tileRes) {
        out[outIdx] = make_uchar4(255, 255, 255, 255);
        return;
    }

    const int tileIdx = tileY * panelN + tileX;
    const float sdf = panel[outIdx];

    float gray = 0.5f;
    if (cfg.debugMode == 0) {
        if (cfg.softMask) {
            const float tau = fmaxf(cfg.tau, 1e-6f);
            gray = 1.0f / (1.0f + __expf(-(sdf / tau)));
        } else {
            gray = sdf < 0.0f ? 0.0f : 1.0f;
        }
    } else {
        const float vmin = tileMin[tileIdx];
        const float vmax = tileMax[tileIdx];
        const float range = (vmax - vmin == 0.0f) ? 1.0f : (vmax - vmin);
        gray = fminf(fmaxf((sdf - vmin) / range, 0.0f), 1.0f);
    }

    const unsigned char v = static_cast<unsigned char>(roundf(gray * 255.0f));
    out[outIdx] = make_uchar4(v, v, v, 255);
}

} // namespace DeepSDF

#endif //ALICE2_USE_CUDA
