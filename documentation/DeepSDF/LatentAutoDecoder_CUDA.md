# Latent SDF Auto-Decoder — CUDA Edition (Detailed Notes)

This README explains the minimal, dependency-free **auto-decoder** for small sets of 2D SDFs (circle, box, triangle) with a CUDA trainer and visualization in an alice2 sketch. It focuses on the **GPU path you’re running now**: micro-batch training, race-free latent updates, device-side statistics, and how to tune it.

---

## 1) What we’re learning

**Goal:** Learn an implicit function ( f_\theta(z_i, \mathbf{x}) \to \hat{s} \in [-1,1] ) per shape (i), where

* ( \mathbf{x}=(x,y) ) is a 2D coordinate,
* ( z_i \in \mathbb{R}^D ) is a learned latent code for shape (i),
* ( \theta ) are shared MLP weights,
* target ( s ) is a **clamped SDF** (scaled by (\beta)) so magnitudes are bounded (helps stability and lets tanh output match).

This is the “auto-decoder” idea: **no encoder during training**, just optimize latents ( {z_i} ) and shared MLP jointly from random init, à la DeepSDF.

---

## 2) Data: tiny analytic SDFs

We synthesize samples at runtime from three analytic SDFs:

* **Circle**: (\sqrt{x^2+y^2}-r)
* **Axis-aligned box** (signed distance with corner rounding outside)
* **Upward triangle** (assembled from line half-spaces)

We **clamp** distances into ([-1,1]) using ( \text{clampSDF}(d,\beta)) with (\beta\approx 0.1). This preserves sign (inside negative, outside positive) and bounds targets for tanh heads.

Sampling is **boundary-biased** (more points near the isocontour) + a small “corner” bias for polygonal sharpness. You can still visualize the full field on a grid for side-by-side comparison.

---

## 3) Model

### 3.1 Inputs

* **Latent** (z_i \in \mathbb{R}^{D}) (shared per sample in a batch column for shape (i)).
* **Coordinates** ((x,y)) with **positional encoding** (Fourier features):
  [
  \gamma(x,y) = [x,y, \sin(2^k \omega x), \cos(2^k \omega x), \sin(2^k \omega y), \cos(2^k \omega y)]_{k=0}^{F-1}
  ]
  EncDim = ((\text{includeInput? }2:0) + 4F).

The MLP input is **concat** ([z \mid \gamma(x,y)] \in \mathbb{R}^{D+\text{EncDim}}).

### 3.2 Network

* Minimal, fully-connected **tanh** stack, e.g. `in → 64 → 64 → 1`.
* Last layer is **linear** (no activation).
* Initialization: Xavier-style (scaled normal), last layer slightly smaller variance.

### 3.3 Loss

Per-sample **MSE** with optional residual clipping:
[
\mathcal{L} = \tfrac12 (\hat{s} - s)^2 + \tfrac{\lambda}{2} \lVert z_i \rVert^2
]
The (L_2) penalty on latents keeps codes bounded and prevents trivial solutions.

---

## 4) Auto-decoder training

We jointly update:

* **Weights** (\theta): standard backprop with SGD (and tiny weight decay).
* **Latents** (z_i): gradient descent using (\partial \mathcal{L}/\partial z) **from the first layer’s input gradient**.

### Race-free latent update (GPU)

Batches contain many samples but **few shapes** (3–6). A naïve per-sample update makes many threads write to the **same** (z_i), causing races.

We fix this by:

1. Computing (\Delta_0 = \frac{\partial \mathcal{L}}{\partial u}) at the **network input** (u=[z|\gamma(x,y)]). The first (D) rows of (\Delta_0) correspond to (\partial \mathcal{L} / \partial z) per sample.
2. **Per-shape reduction**: for each shape (s) and latent dim (p),
   [
   g_{s,p} = \sum_{j \in \text{batch},, \text{shapeIdx}[j]=s} \Delta_0[p,j]
   ]
   We compute (g) into a device buffer `dZgrad[numShapes, latentDim]` (no atomics).
3. **Single apply**:
   [
   z_{s,p} \leftarrow z_{s,p} - \eta_z \big(g_{s,p} + \lambda , z_{s,p}\big)
   ]
   This is **deterministic and contention-free**, and scales smoothly with batch size.

---

## 5) CUDA trainer: what runs where

### 5.1 On device (GPU)

* Positional encoding for the current batch (`kEncodeXY`)
* Assemble ([z|\text{enc}]) (`kAssembleZX`)
* Forward pass (tiled GEMMs, add bias, tanh)
* Backward pass (output delta, backprop deltas, dW/db with ABᵀ, reductions)
* SGD step (weights & biases) with per-batch averaging and weight decay
* **Latent gradient reduction by shape** and single apply (race-free)
* **Loss accumulation** to device scalars (running sum & sample count)

### 5.2 On host (CPU)

* (Current) Batch sampling (xs, ys, targets, shape indices) then H2D memcpy
  *(can be moved to device later to kill PCIe cost)*
* Occasional **stats pull** via `syncStatsToHost(avgLoss, meanZ, reset)`
* Occasional **latents pull** for visualization, `syncLatentsToHost()`
* Grid reconstruction for display calls `forwardRowGPU(...)` (device forward, one row at a time)

---

## 6) Device-side statistics

To avoid sync stalls each micro-batch:

* Compute **per-sample loss** on device.
* Reduce to a **running device scalar** (loss sum) and sample **counter**.
* Expose `syncStatsToHost(avgLoss, meanZ, reset)`:

  * does a device reduction for **mean‖z‖**,
  * copies back **one float** (loss sum) and **one uint** (count),
  * returns averages; optionally **resets** accumulators for the next reporting window.

This keeps training loops **fully on device**, and you control when to print.

---

## 7) Files & API (what you have)

* **`LatentSDF.h`**

  * Analytic SDFs, `Sampler`, `clampSDF`, grid building helpers.
  * `TinyAutoDecoderCUDA` public API:

    * `initialize(numShapes, latentDim, hidden, seed, maxBatch, numFreqs, includeInput)`
    * `trainMicroBatchGPU(B, sampler, rng, lrW, lrZ)`
    * `forwardRowGPU(shapeIdx, xs, y, outY)`
    * `syncLatentsToHost()`
    * `syncStatsToHost(avgLoss, meanZ, reset)`
    * setters: `setLambdaLatent(v)`, `setWeightDecayW(v)`
* **`LatentSDF.cu`**

  * All CUDA kernels (encoding, matmul, backprop, race-free Z, loss/stats).
  * The MLP buffers & layers inside a private `Impl`.
  * `Impl::forwardBatch` / `Impl::backwardBatchAndUpdate` as **member functions**.
* **`sketch_latentSDF_CUDA.cpp`** (alice2)

  * Sets up shapes, creates the auto-decoder, runs **train bursts** from hotkeys,
  * Visualizes original vs reconstruction,
  * Calls `syncStatsToHost` only when printing/logging,
  * Pulls latents only when actually rendering (not every micro-batch).

---

## 8) Typical hyper-parameters

* **Latent dim**: `D = 16` (works well for simple shapes; 8–32 is fine)
* **Hidden**: `64, 64` or `128, 128` (wider helps GPU more than deeper)
* **Pos enc**: `numFreqs = 6`, `includeInput = true` → EncDim = 2 + 24 = 26
* **Learning rates**: `lrW = 1e-2`, `lrZ = 1e-3`
* **Latent L2**: `lambdaLatent = 1e-4`
* **Weight decay**: `weightDecayW = 1e-6`
* **Batch size**: start `B=512–2048`. For real GPU acceleration, push to `B=8k–32k` *after* moving batch generation to device.
* **Reporting**: call `syncStatsToHost()` every 25–100 micro-batches, **reset=true**

---

## 9) Performance guidance

* **Why increasing B didn’t help (yet):** with host batch sampling, H2D memcpy grows with B; small nets don’t provide enough FLOPs to amortize launch/PCIe.
  **Fix:** move batch generation to device (I can supply a tiny kernel), then ramp B.
* **GPU wins big on reconstruction**: evaluate the whole image grid on device (e.g., 1024×1024 per shape) with pre-computed encodings — that’s embarrassingly parallel.
* **Wider layers help more than deeper** for GPU throughput, e.g., 128/128 or 256/256.
* Build in **Release**, add NVCC `-use_fast_math`.
* Print less often; avoid device→host traffic inside the hot loop.

---

## 10) Kernel cheat-sheet

* `kEncodeXY` — positional encoding over batch
* `kAssembleZX` — concat latent table rows with encodings into `[inDim × B]`
* `kMatmul<T>` / `kMatmul_ABt<T>` — tiled GEMMs (forward / dW)
* `kAddBias` — add layer bias
* `kTanhInplace` — elementwise activation
* `kOutputDelta` — output layer residuals
* `kBackpropDelta{,Input}` — hidden/input backprop (with/without tanh’)
* `kRowSum` — reduce `Delta` to bias grads
* `kSgdStep` — weight/bias update with average + L2
* **Race-free Z**

  * `kAccumulateLatentGrad_ByShape` → per-shape reduce of (\Delta_0) rows
  * `kApplyLatentUpdate` → single SGD apply on `Z`
* **Stats**

  * `kLossMSE` → per-sample loss buffer
  * `kAccumulateLoss` → running loss & count atomics (once per block)
  * `kMeanZReduce` → on-demand mean‖z‖ reduction

---

## 11) Common pitfalls & fixes

* **Loss “stuck” near ~0.1**: usually because you were averaging **globally** without reset, or pulling/stalling every batch. Use `syncStatsToHost(reset=true)` every N steps; keep training device-only otherwise.
* **Latents explode / don’t converge**: set `lambdaLatent=1e-4`, `lrZ=1e-3` (or smaller), ensure you’re using the **race-free** per-shape update.
* **GPU not faster than CPU**: too much PCIe, too little math. Move batch gen to device, increase `B`, or widen layers. Use fewer host syncs and prints.
* **Switch-case compile error on local variables**: wrap case body in `{}` (C++ rule).

---

## 12) Minimal usage sketch (pseudo)

```cpp
TinyAutoDecoderCUDA ad;
ad.initialize(/*numShapes*/3, /*latentDim*/16, /*hidden*/{64,64},
              /*seed*/1234, /*maxBatch*/8192, /*numFreqs*/6, /*includeInput*/true);
ad.setLambdaLatent(1e-4f);
ad.setWeightDecayW(1e-6f);

Sampler samp(777);
std::mt19937 rng(2025);
const int B = 2048;
const float lrW=1e-2f, lrZ=1e-3f;

for (int epoch=0; epoch<50; ++epoch) {
  for (int mb=0; mb<200; ++mb) {
    ad.trainMicroBatchGPU(B, samp, rng, lrW, lrZ);
  }
  double avgLoss=0, meanZ=0;
  ad.syncStatsToHost(avgLoss, meanZ, /*reset=*/true);
  printf("[CUDA] epoch=%d  avgLoss=%.6f  mean||z||=%.6f  (B=%d)\n", epoch, avgLoss, meanZ, B);

  // refresh viz occasionally
  ad.syncLatentsToHost();
  // ... generateReconstruction();
}
```

---

## 13) Where to extend next

* **On-device batch generator** (kill remaining H2D)
  → enables **B=8k–32k** safely, true GPU speedups.
* **GPU reconstruction kernel** over the full grid (pre-encode XY grid).
* **Optional cuBLAS/CUTLASS path** for matmuls (Tensor Cores), keep current kernels as fallback.
* **Mixed precision** (FP16/BF16 activations/weights, FP32 accumulations).
* **Residual MLP** blocks + softplus head for smoother fits (optional).

---

### TL;DR

You now have a compact CUDA auto-decoder that:

* Trains **fully on device** (no per-step host sync),
* Uses a **race-free** latent update that scales with batch size,
* Lets you **pull stats on demand** and visualize when you choose.

For the “GPU feel,” the next step is **device batch generation** + **large B** and/or **GPU reconstruction**. If you want, I can drop in a tiny `kMakeBatch(...)` and a grid forward kernel to complete the loop.
