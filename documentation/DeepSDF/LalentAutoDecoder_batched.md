# Micro-Batch GEMM Auto-Decoder — Technical Notes (for your alice2 sketch)

This doc explains the **batched** version you’re running: how the **GEMM forward**, **per-sample rank-1 updates**, and **row-batched reconstruction** work together. It’s still dependency-free and preserves the auto-decoder learning rule (per-sample latent updates), while unlocking big CPU/GPU wins later.

---

## 0) What changed vs. the minimal per-sample version

* **Forward is batched via GEMM**: for a micro-batch of size **B**, each layer computes
  `Z[l] = W[l] * A[l] + b` where `A[l]` is `[layerIn × B]` and `W[l]` is `[layerOut × layerIn]`.
* **Backward & updates stay per-sample**: we walk `j = 0..B-1`, compute that sample’s residual, **apply a rank-1 update** to `W,b` using its `a_prev`, and update **only its latent** `z_i`.
  This keeps behavior close to the strict per-sample schedule.
* **Reconstruction is batched by row**: to render, we evaluate a **whole scanline** (B = width) in one GEMM, identical math as training, just faster.

---

## 1) Data model at a glance

We learn
[
f_\theta\big(z_i,\ \phi(x,y)\big)\ \approx\ \text{SDF}_i(x,y)
]

* **Latent**: ( z_i \in \mathbb{R}^{d} ) per shape (i).
* **Encoder**: Fourier positional encoding (\phi(x,y)\in\mathbb{R}^{d_\phi}) (with raw ((x,y)) optionally included).
* **Input to MLP**: ( u = [,z_i \mid \phi(x,y),] \in \mathbb{R}^{d + d_\phi} ).
* **Decoder (MLP)**: tanh hidden layers, **linear output** (scalar SDF regression).

**Loss per sample**
[
\mathcal{L} = \tfrac12 (y - \tilde{t})^2 + \tfrac{\lambda_z}{2}|z_i|^2 \quad(+\ \tfrac{\lambda_W}{2}|W|^2\ \text{optional})
]
with (\tilde{t}=\text{clip}(\text{SDF}/\beta, -1, 1)).

---

## 2) Shapes, encoder, and target scaling

* **SDFs**: circle / box / up-triangle, analytic, negative inside, zero at boundary.
* **Encoder**: (K) Fourier frequencies on x and y:
  `enc = [x,y, sin(2^k 2π x), cos(...), sin(2^k 2π y), cos(...)]_{k=0..K-1}`
* **Target**: soft-clamp SDF by (\beta) (e.g., 0.1) into ([-1,1]). Keeps scales stable.

---

## 3) Batched forward (GEMM)

For a micro-batch of size **B**:

* Build `X = [inDim × B]` where each **column** is one sample `[z_i | φ(x,y)]`.
* For each layer `l`:

  ```
  Z[l] = W[l] * A[l]            // [out × in] * [in × B] → [out × B]
  Z[l] += b[l] (row-wise)       // add bias to each column
  A[l+1] = tanh(Z[l])           // hidden
  A[L]   = Z[L-1]               // output layer is linear
  ```

This is the same math as before, just done **for B samples at once**.

---

## 4) Per-sample backward + rank-1 updates

We keep the per-sample learning rule:

For each column `j ∈ [0..B-1]`:

1. Read `y = A[L][:, j]` (scalar). Compute residual `r = y - t_j`.
2. Form output delta. If output is linear: `delta[L-1][0] = r`.
3. **Update W,b with rank-1 outer product** using **only** sample `j`:

   ```
   dW = delta[l] * (a_prev[j])^T     // out × 1  times  1 × in  → out × in
   db = delta[l]
   W[l] -= lrW * dW
   b[l] -= lrW * db
   (optional) W[l] -= lrW * λW * W[l]  // weight decay
   ```
4. Backprop to previous layer for this sample (`W^T * delta ⊙ tanh'(z_prev)`).
5. Take **latent step** for the active shape `i_j`:

   ```
   dL/dz = dL/du[0:latentDim] + λz * z_i
   z_i  -= lrZ * dL/dz
   ```

This preserves **per-sample latent updates** and applies W,b updates **immediately** per sample (within the micro-batch).

---

## 5) Row-batched reconstruction

For visualization, we avoid `B=1` per pixel. Instead:

* For each image row `y`, pack all **W** pixels (`x=0..W-1`) into `X = [inDim × W]` by reusing the shape’s latent `z_i` and encoding each `(x,y)`.
* Call `forwardBatch(X, W)` once.
* Read back `A.back()` as the row of predictions.

This yields **identical** values to doing per-pixel forwards, but it’s much faster and matches training’s data layout.

---

## 6) Memory layout details (why your code is fast enough already)

* Matrices are stored row-major; we treat **columns as samples**.
  (`A` and `Z` are `[rows × B]` flat vectors; accessing column `j` walks with stride `B`.)
* Weight matrices `W[l]` are `[out × in]`, contiguous rows. That makes outer-product updates a simple loop over rows.

---

## 7) Hotkeys (as wired in the sketch)

* **T**: train one burst (several micro-batches)
* **B / b**: double / halve micro-batch size
* **M**: toggle mask vs. continuous SDF heatmap
* **S**: toggle soft mask vs. hard step
* **[ / ]**: adjust soft mask sharpness `τ`
* **R**: refresh reconstruction

---

## 8) Suggested hyperparameters

* Hidden: `64 × 64 × 64` tanh; **linear head**
* Fourier freqs: `K = 6` (try 4–8)
* Latent dim: `d = 16` (2D toy); try 8–32
* LRs: `lrW = 3e-4`, `lrZ = 1e-3`
* Reg: `λz = 1e-4`, `λW = 1e-6` (optional)
* Sampler: boundaryFrac `≈ 0.6`, cornerFrac `≈ 0.15`, band `≈ 0.02`
* Target scale: `β = 0.1`

---

## 9) Complexity & performance intuition

Per batch:

* Forward layer (l): (O(\text{out}_l \cdot \text{in}_l \cdot B)) via GEMM.
* Backward/updates per sample: (O(\sum_l \text{out}_l \cdot \text{in}_l)) for the rank-1 steps (plus vector ops).
* Total per batch: (O!\left(\sum_l \text{out}_l \text{in}_l \cdot B\right)) forward + (B \cdot O!\left(\sum_l \text{out}_l \text{in}_l\right)) backward.
  (The backward is inherently per-sample due to the update rule.)

Even the naive GEMM is cache-friendly and vectorizable; swapping it for a BLAS/CUDA GEMM is a straight replacement.

---

## 10) CUDA/BLAS upgrade path (unchanged math)

* Replace the tiny `gemm()` with cuBLAS `sgemm` (or your BLAS of choice) for each layer forward.
* Keep **per-sample** rank-1 updates; on GPU, you can:

  * Accumulate per-sample outer products in shared memory and apply immediately in block order (“near-equivalent” schedule), or
  * Apply updates sequentially inside a persistent kernel for strictness.
* Encode Fourier features on device; evaluate SDFs on device if you still sample analytically at runtime.
* Row-batched (or tile-batched) reconstruction becomes a simple batched forward pass.

---

## 11) Minimal API recap (used in the sketch)

* `PosEnc2D::encode(x,y,out)` → Fourier features.
* `TinyMLP::forwardBatch(X, B)` → fills `A[l], Z[l]` for the whole batch.
* `TinyMLP::backwardUpdateOneSample(j, t, lrW, λW, dL_du)` → per-sample rank-1 W,b update + returns `∂L/∂u`.
* `TinyAutoDecoder::trainMicroBatch(B, sampler, rng, lrW, lrZ, avgLoss, meanZ)` → wraps batching, forward, per-sample updates.
* `generateReconstruction()` → **row-batched** forward for fast display.

---

## 12) Sanity checks to keep around

* **Parity**: Run a tiny grid (e.g., 32×32) and compare per-pixel vs row-batched reconstruction (`max |diff|` ≈ 1e-6).
* **Learning**: Track `avgLoss` and `mean||z||`; both should behave similarly to the per-sample version (usually faster drop).
* **Crispness**: Mask view with small `τ` should align zero-contours with the analytic reference.

---

If you want, I can package this into a `README_batched.md` tailored to your repo (with file/class names matching your current sketch) and add a tiny “Quick Start” block at the top.
