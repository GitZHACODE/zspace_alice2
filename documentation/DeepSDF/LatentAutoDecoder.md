# Minimal Latent-SDF Auto-Decoder — Technical Notes

This document explains the three key pieces in your minimal pipeline — the **positional encoder**, the **MLP decoder**, and the **auto-decoder training loop** — and how they fit together to learn a family of 2D signed distance fields (SDFs) (circle/square/triangle) via **per-shape latent codes**. It also covers sampling, loss functions, visualization (crisp masks), and how to grow this design toward CUDA without changing the learning rule.

---

## 0) Big Picture

We learn a function
[
f_\theta(,z_i,; \phi(x,y),) ;\approx; \mathrm{SDF}_i(x,y)
]

* (i \in {1,\dots,N}) indexes shapes.
* (z_i \in \mathbb{R}^{d}) is a **latent code** for shape (i) (trainable parameters, one vector per shape).
* (\phi(x,y)) is a **positional encoding** of the coordinates (plain ((x,y)) or Fourier features).
* (f_\theta) is a small **MLP** with parameters (\theta).
* We train **per-sample**: for a single coordinate from a chosen shape, we update (\theta) and the active (z_i).

Why this works: the MLP learns a **decoder** that maps the latent “style” of a shape + spatial features → SDF value; the (z_i)s place each instance on that learned manifold.

---

## 1) Positional Encoder `PosEnc2D`

### Purpose

Tanh MLPs bias toward smooth/low-frequency functions, which rounds sharp corners. A simple Fourier feature encoder injects **high-frequency bases** so edges become easy to represent.

### Definition

For input ((x,y)), with (K) frequencies (e.g., (K=6)):
[
\phi(x,y) ;=; \big[, x, y,\ \sin(2^0 2\pi x), \cos(2^0 2\pi x), \dots, \sin(2^{K-1} 2\pi x), \cos(\cdot),\ \text{same for } y ,\big].
]

* `includeInput = true` keeps raw ((x,y)) for low-freq stability.
* Encoded dimension: (2 + 4K).

### Implementation Notes

* Stateless encoder that writes into a preallocated buffer.
* **No trainable params** → trivial to port to CUDA (one kernel to fill an array).

---

## 2) Decoder `TinyMLP`

### Structure

* Layers: ([\text{in} \rightarrow h_1 \rightarrow h_2 \rightarrow \dots \rightarrow \text{out}])
* Activations: **tanh** on hidden layers, **linear** on the final layer for SDF regression.
* Initialization: **Xavier/LeCun** style (\mathcal{N}(0, 1/\sqrt{\text{fan-in}})); slightly smaller scale for the last layer to avoid early divergence.

### Forward Pass

Input vector (u \in \mathbb{R}^{d + d_\phi}) (latent (z) concatenated with encoded coords (\phi(x,y))):

[
\begin{aligned}
a_0 &= u\
z_\ell &= W_\ell a_{\ell-1} + b_\ell \
a_\ell &=
\begin{cases}
\tanh(z_\ell), & \ell < L \
z_\ell,        & \ell = L \text{ (linear output)}
\end{cases}
\end{aligned}
]
Return (y = a_L \in \mathbb{R}).

### Backward Pass (per-sample)

With target (t) and squared error (\tfrac12(y-t)^2):

* Output gradient: (\partial \mathcal{L}/\partial a_L = (y-t)).
* Backprop through layers:

  * For hidden: multiply by ((1 - a_\ell^2)) (tanh′).
  * For output: derivative is 1 (linear).
* Accumulate (gW_\ell = \delta_\ell a_{\ell-1}^\top), (gb_\ell = \delta_\ell).

### Weight Decay (optional)

Add a tiny ( \lambda_W |W|^2) by **adding** ( \lambda_W W ) to (gW) before the SGD step.

### SGD Update

[
W_\ell \leftarrow W_\ell - \eta_W, gW_\ell,\quad b_\ell \leftarrow b_\ell - \eta_W, gb_\ell.
]

---

## 3) Auto-Decoder `TinyAutoDecoder`

### What it Owns

* The **decoder** MLP (\theta).
* The **latent table** (Z = [z_1,\dots,z_N]), trainable vectors (z_i\in\mathbb{R}^d).
* Hyperparameters: latent dim (d), weight/lr for (\theta) and (Z), regularization.

### Input Packing

Concatenate ([z_i ,|, \phi(x,y)]) to form the MLP input.

### Loss

We train on **SDF regression** (not discrete labels). To stabilize scale, use a soft clamp:
[
\tilde{t} ;=; \mathrm{clip}!\left(\frac{\mathrm{SDF}*i(x,y)}{\beta},, -1, 1\right)\quad (\text{e.g., }\beta=0.1)
]
Total (per sample):
[
\mathcal{L} ;=; \tfrac12 (y - \tilde{t})^2 ;+; \tfrac{\lambda_z}{2}, |z_i|^2 ;(+\ \tfrac{\lambda_W}{2}\sum*\ell|W_\ell|^2\ \text{optional})
]

### Gradients w.r.t. Latent

Backprop returns (\partial \mathcal{L}/\partial u). The first (d) entries correspond to ( \partial \mathcal{L}/\partial z_i ). Add ( \lambda_z z_i ) and step:
[
z_i \leftarrow z_i - \eta_Z \left( \frac{\partial \mathcal{L}}{\partial z_i} + \lambda_z z_i \right).
]

### Per-Sample Schedule (critical)

For shape (i) and one coordinate ((x,y)):

1. Build input ([z_i|\phi(x,y)]), forward → (y).
2. Compute loss against (\tilde{t}).
3. Backprop to get gradients.
4. **SGD update (\theta)** and **SGD update (z_i)** immediately.

This preserves the auto-decoder’s learning dynamics (what you validated on CPU/GPU parity).

---

## 4) Data & Sampling

### SDFs

We use analytic SDFs for circle, box, triangle. Negative inside, positive outside, zero on the boundary.

### Why Sampling Matters

* Most points are far from the boundary → weak training signal if sampled uniformly.
* Sharp features (corners) are hard → require targeted samples.

### Practical Strategy

* **Boundary-heavy**: e.g., 60% of samples try to hit (|\mathrm{SDF}| < \epsilon).
* **Corner-biased** (optional): 10–20% attempts around high-curvature regions.
* The rest uniform in the domain for global context.

---

## 5) Visualization (Crisp Masks)

* Render the **regressed SDF** for a gray heatmap.
* For crisp binary masks:

  * **Hard**: (m = \mathbf{1}[\text{SDF} \ge 0]) (outside white, inside black).
  * **Soft**: (m = \sigma(\text{SDF}/\tau)) (with small (\tau \in [0.02, 0.08])) — cleaner edges with less aliasing.
* Optional: draw a red dot where cells contain a **sign change** (quick contour overlay).

---

## 6) Hyperparameters (good defaults)

* **MLP**: 3×64 hidden, tanh, linear head.
* **Init**: Xavier with last-layer scale ≈ (0.5/\sqrt{\mathrm{fan_in}}).
* **Latent dim**: (d=16) (2D toy), try 8–32.
* **LR**: (\eta_W = 3!\times!10^{-4}), (\eta_Z = 1!\times!10^{-3}).
* **Reg**: (\lambda_z = 10^{-4}), (\lambda_W = 10^{-6}) (optional).
* **Fourier freqs**: (K=6) (try 4–8).
* **Boundary band**: (\epsilon \approx 0.02).
* **SDF scale**: (\beta = 0.1).

---

## 7) Training Loop (concise pseudocode)

```cpp
for epoch in 1..E:
  for step in 1..S:
    i = random_shape()
    (x,y,t_true) = sample_point_and_sdf(i)           // with boundary/corner bias
    t = clamp(t_true / beta, -1, 1)                  // target in [-1,1]

    enc_xy = PosEnc2D.encode(x,y)                    // Fourier features
    u = concat(z[i], enc_xy)                         // input vector

    y = MLP.forward(u)
    L = 0.5*(y - t)^2 + 0.5*lambda_z*||z[i]||^2

    dL_du = MLP.backward_and_accumulate(t, weightDecayW=lambda_W)
    MLP.sgd(lrW)

    dL_dz = dL_du[0:d] + lambda_z * z[i]
    z[i] -= lrZ * dL_dz
```

Stop when loss plateaus; optionally decay (\eta_W,\eta_Z) by 2× then continue.

---

## 8) Why the Pieces Interlock

* **Encoder** gives a richer, near-linear basis for sharp edges → the MLP’s tanh layers don’t have to synthesize high-frequency content by depth alone.
* **MLP** fuses **shape style** (latent) with **spatial structure** (encoded coords) into a universal local distance predictor.
* **Auto-decoder** regime pushes **all shape-specific information** into (z_i), keeping the MLP shared and scalable — exactly what we want for many shapes, and easy to deploy (one model, many codes).

---

## 9) Extension Path to CUDA (unchanged math)

1. **Keep the per-sample schedule**: for correctness parity, either:

   * Launch tiny kernels per sample (simplest, but overhead heavy), or
   * Use **micro-batches** that do *independent* per-sample updates inside the batch (requires careful atomic/add or warp-local accumulations per sample).
2. **Kernels**:

   * `encodeFourier<<<...>>>` to fill (\phi(x,y)) buffers.
   * `mlpForward<<<...>>>` per layer (gemv-like) or fuse layers for small networks.
   * `mlpBackwardAccum<<<...>>>` per layer.
   * `sgdUpdate<<<...>>>` for (W,b) and (z_i).
3. **Memory**:

   * Keep parameter tensors in **contiguous** device buffers.
   * Reuse **activation and grad workspaces** across steps.
4. **Parity tests**: unit tests that compare CPU vs GPU weights, biases, and latent updates after a fixed set of samples.

---

## 10) Common Pitfalls & Fixes

* **Rounding of corners** → add Fourier features; optionally increase `numFreqs` or add corner-biased sampling.
* **Vanishing gradients** → ensure **linear output**; don’t hard-clip targets too aggressively; check init scales.
* **Latents blow up** → increase (\lambda_z) slightly; reduce (\eta_Z).
* **No learning** → boundary sampling too rare; balance inside/outside/boundary; verify targets are SDF (not labels).
* **Flickery visualization** → use soft mask with (\tau \in [0.03,0.08]) and/or draw a contour hint.

---

## 11) Minimal Interface Recap

* `PosEnc2D::encode(x,y, out)` → fills `out` with (\phi(x,y)).
* `TinyMLP::forward(u)` → scalar SDF prediction.
* `TinyMLP::backward_and_accumulate(target, weightDecay)` → gradients into internal buffers; returns (\partial \mathcal{L}/\partial u).
* `TinyMLP::sgd(lrW)` → parameter step.
* `TinyAutoDecoder::trainSample(i,x,y,t, lrW, lrZ)` → wraps the three lines above + latent step.

This clean separation is exactly what you want for a later CUDA drop-in.

---

## 12) Optional Quality Boosters (still dependency-free)

* **Smooth-L1 (Huber) loss** on residual (y-t) for robustness.
* **Skip connections** from (\phi(x,y)) to later layers (tiny “FiLM” style) if you grow depth.
* **SIREN** (sin activations) instead of tanh — works very well with SDFs, but you’ll need careful init; Fourier features are simpler and already effective.

---

If you want, I can package this as a `README.md` (plus inline code docstrings) exactly matching your current file names and member functions — just say the word and I’ll format it to drop into your repo.
