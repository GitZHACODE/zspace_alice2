# Spatial tension-compression form-finding: computational and mathematical reference

Reference for paper preparation. Audited on 2026-10-06 against restored source baseline **1fffc12**, whose historical commit message is "before sparseQR". That baseline already uses Eigen SparseQR. It has forward finite differences and a newly computed QR factorization each iteration. Later analytic derivatives, cached symbolic analysis, LSMR and the solver switch are absent.

This document reconstructs the implemented mathematics. It does not establish novelty, global convergence or structural adequacy. Input, controls and export details are in [OffsetCableNetwork.md](OffsetCableNetwork.md). Literature and source links appear below.

## 1. Problem definition and scope

An oriented polygon mesh supplies a compression network. Yellow-yellow mesh edges select a cable network; selected interior yellow nodes receive offset cable copies and tension connectors. Selected boundary yellow nodes are shared by both networks. The optimizer varies free node coordinates and member force densities.

The goals are horizontal equilibrium at every node, vertical equilibrium at unsupported nodes, fixed XYZ black anchors, fixed Z red/yellow anchors, prescribed compression/tension signs, and attraction to an initial geometry.

The vertical-support target constrains reaction direction, not its sign. No inequality requires every vertical reaction to be compressive, upward, or within a bearing capacity. Such a requirement would need an additional constraint and verification.

The representation is a network of straight axial members. It has no bending moments, elastic constitutive law, member stress limits, cable-slack complementarity or stability calculation. Offset and member lengths are starting values, not maintained constraints.

The appropriate description is **spatial form-finding using signed force densities with positional preferences and vertical-only support-reaction targets**. It is related to 3D graphic statics, but constructs no reciprocal force diagrams/polyhedral force cells. It does not perform v1's subsequent fixed-plan TNA stage.

## 2. Notation

| Symbol | Meaning |
| --- | --- |
| \(N_s,N_c,N=N_s+N_c\) | Welded surface nodes, added cable copies, total nodes |
| \(E\) | Compression, connector and cable members combined |
| \(\mathcal B,\mathcal T_r,\mathcal T_y\) | Black, red and boundary-yellow support sets |
| \(\mathcal T=\mathcal T_r\cup\mathcal T_y\), \(B,T\) | Sliding support set; black/sliding counts |
| \(\mathbf X,\mathbf X^0\in\mathbb R^{N\times3}\) | Current and initial built-network coordinates |
| \(\mathbf z=\operatorname{vec}_r(\mathbf X)\in\mathbb R^{3N}\) | Node-major coordinates: node 0 XYZ, node 1 XYZ, etc. |
| \(\mathbf C\in\mathbb R^{E\times N}\) | Oriented incidence matrix |
| \(\mathbf q,\mathbf u\in\mathbb R^E\) | Signed densities and log variables |
| \(K,R\) | Free-coordinate and required equilibrium-component counts |
| \(\mathbf y\in\mathbb R^K,\ \mathbf x=[\mathbf u;\mathbf y]\) | Normalized free coordinates and complete unknown vector |
| \(\mathbf U\in\mathbb R^{3N\times K}\) | Embedding of free coordinates |
| \(\mathbf H\in\mathbb R^{R\times3N}\) | Selector of required equilibrium components |
| \(\mathbf p,\mathbf r\in\mathbb R^{3N}\), \(\mathbf g\in\mathbb R^R\) | Algebraic loads, full and selected residuals |
| \(S,P,W,\lambda,\alpha_j\) | Coordinate scale, load scale, equilibrium weight, increment damping, position weights |

For the viewer's disjoint support sets,

$$
K=3N-3B-T,\qquad R=3N-B-T,\qquad R-K=2B. \tag{1}
$$

The extra \(2B\) equations constrain horizontal force balance at fixed black nodes without releasing their positions.

## 3. Preprocessing and network geometry

### 3.1 Welding

With weld grid \(\epsilon_w=10^{-3}\), each coordinate receives key

$$
\mathbf k_i=\left(
\operatorname{round}(X_{ix}/\epsilon_w),
\operatorname{round}(X_{iy}/\epsilon_w),
\operatorname{round}(X_{iz}/\epsilon_w)\right). \tag{2}
$$

Equal keys merge. The first position is retained; neither averaging nor snapping is performed. This is grid-key welding, not a Euclidean-distance neighbour search. Marked colors override unmarked duplicates; conflicting black/red/yellow roles reject the input.

Faces are remapped, consecutive duplicates removed, and repeated closure indices removed. Collapsed faces, repeated vertices elsewhere in a face, invalid indices, nonmanifold edges and degenerate input normals are rejected. An undirected edge incident to one face marks boundary nodes, including hole boundaries. More than two incident faces is invalid.

### 3.2 Normals and offsets

For ordered face \(f=(v_0,\ldots,v_{m-1})\), indices cyclically wrapped,

$$
\mathbf n_f^*=\sum_{j=0}^{m-1}
(\mathbf X^0_{v_j}-\mathbf X^0_{v_0})
\times(\mathbf X^0_{v_{j+1}}-\mathbf X^0_{v_0}),\qquad
\mathbf n_i=\frac{\sum_{f\ni i}\mathbf n_f^*}
{\|\sum_{f\ni i}\mathbf n_f^*\|}. \tag{3}
$$

All polygon corners enter the calculation; vertex weighting uses unnormalized face vectors. Consistent winding remains necessary. A selected interior yellow vertex gets cable copy \(c(i)\):

$$
\mathbf X^0_{c(i)}=\mathbf X^0_i+d_0\mathbf n_i. \tag{4}
$$

A tension connector joins \(i,c(i)\). Selected boundary yellow nodes instead use \(c(i)=i\), with no duplicate/connector. Unselected source nodes have mapping -1. Selection requires an incident yellow-yellow face edge; an isolated interior yellow node has no offset copy. Boundary-yellow nodes remain supports even if isolated from the cable selection.

Every distinct original face edge is compression. Every yellow-yellow face edge additionally creates tension between its mapped cable nodes. Branching is allowed. Two boundary cable endpoints have a separate cable coincident with their compression edge. Member order is compression edges, connectors, then cables.

The viewer passes slideYellow=true and uses \(d_0=-0.05\). The helper default slideYellow=false is not the viewer setting. Offsets smaller than \(10^{-5}\) in magnitude are rejected. Original faces remain on original nodes; cable copies acquire no faces. Initial normals, offset separation, connector orientation and lengths are not enforced after construction.

## 4. Force-density equilibrium

Orient member \(e=(a,b)\) and define

$$
C_{ei}=\begin{cases}1&i=a\\-1&i=b\\0&\text{otherwise}\end{cases},
\qquad \mathbf d_e=\mathbf X_a-\mathbf X_b,\quad
\ell_e=\|\mathbf d_e\|,\quad q_e=F_e/\ell_e. \tag{5}
$$

The code labels positive q compression and negative q tension. Define

$$
\mathbf L(\mathbf q)=\mathbf C^\mathsf T\operatorname{diag}(\mathbf q)\mathbf C,\qquad
\mathbf A(\mathbf X)=
[\mathbf c_0\otimes\mathbf d_0\ \cdots\ \mathbf c_{E-1}\otimes\mathbf d_{E-1}],
\quad \mathbf c_e=\mathbf C_{e,:}^\mathsf T. \tag{6}
$$

Then

$$
\mathbf A(\mathbf X)\mathbf q
=\operatorname{vec}_r(\mathbf L(\mathbf q)\mathbf X)
=(\mathbf L(\mathbf q)\otimes\mathbf I_3)\mathbf z. \tag{7}
$$

Each member contributes at most six equilibrium coefficients. Orientation reversal changes both incidence/vector signs, leaving assembled nodal effects unchanged. L is a signed graph matrix, not a material stiffness matrix; mixed signs can make it indefinite or singular.

The implemented algebraic residual is

$$
\mathbf r=\mathbf p(\mathbf X)-\mathbf A(\mathbf X)\mathbf q,\qquad
\mathbf r_i=(0,0,p_i)-\sum_{e=(i,j)}q_e(\mathbf X_i-\mathbf X_j),\qquad
\mathbf g=\mathbf H\mathbf r. \tag{8}
$$

Member contributions cancel globally, and straight axial member pairs have zero resultant moment:

$$
\sum_i(\mathbf A\mathbf q)_i=\mathbf0,\qquad
\sum_i\mathbf X_i\times(\mathbf A\mathbf q)_i=\mathbf0.
\tag{8a}
$$

Consequently the algebraic residual satisfies

$$
\sum_i\mathbf r_i=\sum_i\mathbf p_i,\qquad
\sum_i\mathbf X_i\times\mathbf r_i=\sum_i\mathbf X_i\times\mathbf p_i.
\tag{8b}
$$

These identities provide independent global force/moment checks. When all required residuals vanish, the supported components carry the entire resultant and moment of the applied algebraic load. They do not establish stability or material adequacy.

H selects XY at every node and Z only at nodes with free Z, in node-major order. Black XY residuals therefore enter optimization despite fixed black coordinates. Black anchors fix XYZ; red and boundary-yellow anchors fix Z, free XY. No physical horizontal spring is added to the coupled model.

### Support-force sign audit

The reporter and exporter define

$$
R^{export}_{ia}=
\begin{cases}-r_{ia}&\text{component }(i,a)\text{ constrained}\\
0&\text{otherwise},\end{cases}
\qquad \mathbf F^{export}_{on\ support}=-\mathbf R^{export}. \tag{9}
$$

The arrow and JSON label use this definition. A sliding node's horizontal exported reaction is zero even if its raw XY residual is nonzero.

For a physical Z-up convention with downward gravity \(\mathbf f^{ext}_i=-p_i\mathbf e_z,\ p_i\ge0\), positive compression pushes node i away from its neighbour, giving internal nodal contribution \(+\mathbf A\mathbf q\). Physical balance would be

$$
\mathbf A\mathbf q-\mathbf p+\mathbf R^{physical}=\mathbf0,\qquad
\mathbf R^{physical}=\mathbf r\quad\text{on supported components}. \tag{10}
$$

Under these stated conventions, the exported reaction in (9) is opposite to the support-on-network vector in (10). The export strings call (9) a support-on-network reaction, so that naming needs reconciliation before physical vectors are used in a paper. This document records the existing behaviour and changes no computational code. Residual magnitudes are unaffected by reversing the signed reporting convention. A different chosen physical convention must also be defined explicitly.

## 5. Surface loading

For loaded original face f,

$$
\mathbf c_f=\frac1{m_f}\sum_{i\in f}\mathbf X_i,\qquad
\mathbf t_{fab}=(\mathbf X_b-\mathbf X_a)\times(\mathbf c_f-\mathbf X_a),
\qquad a_{fab}=\tfrac14\|\mathbf t_{fab}\|. \tag{11}
$$

For each polygon side (a,b), add \(a_{fab}\) to both endpoints' tributary areas. Thus each receives half of that centroid-fan triangle's conventional area:

$$
A_i(\mathbf X)=\sum_{\substack{f\ loaded,\,(a,b)\in\partial f\\i\in\{a,b\}}}a_{fab},
\qquad p_i(\mathbf X)=b_i+\rho\,t_i A_i(\mathbf X). \tag{12}
$$

Viewer defaults: \(b_i=0.001\) for original surface nodes, including supports; zero for copies; \(\rho=1,\ t_i=1\). Programmatic settings allow thickness arrays and unloaded faces. Density is already the load multiplier in this equation; there is no multiplication by gravity acceleration.

Areas depend on current 3D geometry, not XY projection. Every perturbation/trial recomputes self-weight. Cable copies have no base load or faces, and separate cable/bar material weight is absent.

Unsigned centroid-fan sums for concave, self-intersecting or nonplanar polygons need not equal an intended physical surface area. Input topology/normal validation is not a proof of that area interpretation.

## 6. Parameterization, hard constraints and scaling

Prescribed sign \(s_e\) is +1 for compression, -1 for tension:

$$
q_e=s_e(q_{\min}+\exp u_e),\qquad q_{\min}=10^{-4}. \tag{13}
$$

The implicit exponential multiplier is one model density unit. A dimensionally explicit presentation could write \(q_*e^{u_e}\) with \(q_*=1\), but the implementation has no independent q-scale parameter. Signs/minimum magnitude are enforced by parameterization, not clipping; float rounding can put stored q at its lower bound.

Let U embed free components in the complete coordinate vector. Define

$$
S=\max(1,\max_i\|\mathbf X_i^0-\mathbf X_0^0\|),\qquad
\mathbf y^0=\mathbf U^\mathsf T\mathbf z^0/S,\qquad
\mathbf z(\mathbf y)=\mathbf z^0+S\mathbf U(\mathbf y-\mathbf y^0). \tag{14}
$$

This mapping fixes constrained components exactly. Free coordinates equal \(S y_j\). References/scales include offset cable copies. Initial u=0, y=y0 gives densities approximately \(s_e(1.0001)\).

From initial loads, before the optional seed,

$$
P=\max(1,\max_i|p_i(\mathbf X^0)|). \tag{15}
$$

S and P stay fixed throughout the fit. Floors at one, fixed tolerances and the implicit exponential density scale introduce numerical/model-unit scales. The solver is not invariant under arbitrary unit changes.

## 7. Objective

Coordinate preferences are

$$
\alpha_j=\begin{cases}
\alpha_r&\text{red-support X/Y}\\
\alpha_y&\text{boundary-yellow-support X/Y}\\
\alpha_d&\text{other free coordinates}.
\end{cases}\qquad
\mathbf D_\alpha=\operatorname{diag}(\alpha_j). \tag{16}
$$

Viewer defaults are ordinary attraction 1, red weight 0, yellow weight 50. The objective at an outer iteration is

$$
\Phi_W(\mathbf x)=\frac W{P^2}\|\mathbf g(\mathbf x)\|_2^2+
(\mathbf y-\mathbf y^0)^\mathsf T\mathbf D_\alpha(\mathbf y-\mathbf y^0),
\qquad W_0=100. \tag{17}
$$

Position weights provide design preferences, not force springs or hard displacement bounds. There is no density-magnitude or member-length objective. In physical coordinates, the preference equals \(\sum_j\alpha_j(\Delta X_j/S)^2\).

Equation (17) is an increasing quadratic penalty on equilibrium, not an exact constrained solve at finite W. Physical acceptance is separately checked. With all anchors sliding, XY rigid motions may be selected by positional preferences; zero weights can leave gauge freedoms. Increment damping regularizes the numerical step but does not create a physical support.

For fixed W and a differentiable ideal model, its gradient is

$$
\nabla\Phi_W=
2W\mathbf J^\mathsf T\mathbf g/P^2+
2\mathbf E_y^\mathsf T\mathbf D_\alpha(\mathbf y-\mathbf y^0),
\qquad \mathbf E_y=[\mathbf0_{K\times E}\ \mathbf I_K].
\tag{17a}
$$

A stationary point can balance a nonzero equilibrium residual against position preferences. The current termination rules do not explicitly check this gradient. The Gauss-Newton subproblem below retains the Jacobian-product part of the local curvature and omits residual-weighted second derivatives.

## 8. First-step seed: bounded force-density fitting

The coupled solver invokes one step of the existing BestFitForceDensitySolver on its first outer step. That call can test many candidates. It is not repeated each coupled iteration.

Validation requires a graph anchor in every component. Without black anchors it temporarily uses fixed-Z anchors for connectivity validation; actual coupled XY remains free. The seed helper itself requires real black anchors and cannot initialize an all-sliding network.

### 8.1 Initial density subproblem

Let Hf select only the helper's unconstrained equations:

$$
\mathbf A_f=\mathbf H_f\mathbf A(\mathbf X^0),\qquad
\mathbf p_f=\mathbf H_f\mathbf p(\mathbf X^0),\qquad
\mathbf S_q=\operatorname{diag}(s_e).
\tag{18}
$$

Initial helper densities solve

$$
\min_{\mathbf q}\|\mathbf A_f\mathbf q-\mathbf p_f\|_2^2
\quad\text{subject to }s_e q_e\ge q_{\min}. \tag{19}
$$

Set \(\mathbf q=\mathbf S_q(q_{\min}\mathbf1+\boldsymbol\eta)\):

$$
\mathbf B=\mathbf A_f\mathbf S_q,\quad
\widehat{\mathbf b}=\mathbf p_f-q_{\min}\mathbf B\mathbf1,\quad
\min_{\boldsymbol\eta\ge0}\|\mathbf B\boldsymbol\eta-\widehat{\mathbf b}\|_2^2. \tag{20}
$$

For \(\mathbf w=\mathbf B^\mathsf T(\widehat{\mathbf b}-\mathbf B\boldsymbol\eta)\), exact KKT conditions are

$$
\boldsymbol\eta\ge0,\qquad \mathbf w\le0,\qquad \eta_e w_e=0. \tag{21}
$$

The default helper uses block principal pivots, up to 128 passive-set iterations. Passive solves use sparse QR; dependent columns use dense complete orthogonal decomposition. Failure/cycling retries the dense Lawson-Hanson-style path. These helper fallbacks remain and are unrelated to removed LSMR fallback.

The dense path has a shared decrementing loop budget initialized to max(100,10E); both entering-variable and passive-set inner work consume that budget. Its passive solves also use complete orthogonal decomposition rather than a normal-equation inverse.

Its dual tolerance is \(10^{-10}\max(1,\|\mathbf B^\mathsf T\widehat{\mathbf b}\|_\infty)\); final sparse KKT verification permits ten times this. Passive primal values use a \(10^{-12}\) feasibility threshold. Generic near-zero member constraints exist but v2 supplies none.

### 8.2 Candidate coordinate equilibrium

For fixed candidate q and coordinate axis a, partition free/constrained nodes:

$$
\mathbf L_{ff}\mathbf X_f^{(a)}
=\mathbf p_f^{(a)}-\mathbf L_{fc}\mathbf X_c^{(a)}. \tag{22}
$$

The helper uses dense full-pivot LU. X/Y share factorization and are solved once; Z is updated with self-weight for at most 100 load iterations. Noninvertible matrices, nonfinite coordinates, movement beyond 100S or unresolved helper equilibrium reject a candidate.

V2 supplies allowSlidingTensionSupports=false and slidingSupportPenalty=0, so the generic helper's optional horizontal spring treatment is inactive.

Let qLS solve (19), and

$$
\bar q=\max(q_{\min},E^{-1}\sum_e|q_e^{LS}|),\qquad
\mathbf q^{common}=\mathbf S_q\bar q\mathbf1. \tag{23}
$$

The first helper step tests up to 64 candidates:

$$
\mathbf q^{(\beta,k)}=
2^k[(1-\beta)\mathbf q^{LS}+\beta\mathbf q^{common}],
\quad k=0,\ldots,7,\quad
\beta\in\{0,0.001,0.01,0.05,0.1,0.25,0.5,1\}. \tag{24}
$$

It retains the feasible candidate with closest geometric RMS. Hf excludes supported equations; helper success does not imply horizontal balance at black anchors.

### 8.3 Seed adoption

A successful helper candidate is mapped to

$$
u_e^{seed}=\log\max(10^{-10},|q_e^{seed}|-q_{\min}),\qquad
\mathbf y^{seed}=\mathbf U^\mathsf T\mathbf z^{seed}/S. \tag{25}
$$

It is adopted only if its full coupled report is finite and its objective (17) is lower than the original coupled state, with unchanged P, S, W and preferences. Otherwise the original state is used.

## 9. Derivatives and local least-squares step

### 9.1 Implemented finite differences

For unknown c:

$$
h_c=10^{-4}\max(1,|x_c|),\qquad
\mathbf J_{:,c}\approx
\frac{\mathbf g(\mathbf x+h_c\mathbf e_c)-\mathbf g(\mathbf x)}{h_c},
\quad \mathbf J\in\mathbb R^{R\times(E+K)}. \tag{26}
$$

Each perturbed state reconstructs geometry, q, loads and residuals. Derivative entries with absolute unweighted value at most \(10^{-12}\) are omitted. The structure can change with numerical zeros and is not cached.

Jacobian coefficients/QR are double precision, but evaluated geometry, densities, areas, loads and residual vectors pass through float data. Consequently the finite difference contains quantization/subtraction error. Double QR does not make the residual or derivatives exact.

### 9.2 Exact derivatives of the ideal mathematical model

These derivatives explain the equations; **this revision does not evaluate them analytically**. Partition \(\mathbf J=[\mathbf J_u\ \mathbf J_y]\):

$$
\frac{\partial q_e}{\partial u_e}=s_e e^{u_e},\qquad
\mathbf J_u=-\mathbf H\mathbf A(\mathbf X)\operatorname{diag}(s_e e^{u_e}), \tag{27}
$$

$$
\mathbf J_y=\mathbf H\left[
\frac{\partial\mathbf p}{\partial\mathbf z}
-(\mathbf L(\mathbf q)\otimes\mathbf I_3)\right]S\mathbf U. \tag{28}
$$

For a face-side area,

$$
\delta\mathbf c_f=m_f^{-1}\sum_{i\in f}\delta\mathbf X_i,\quad
\delta\mathbf t=
(\delta\mathbf X_b-\delta\mathbf X_a)\times(\mathbf c_f-\mathbf X_a)
+(\mathbf X_b-\mathbf X_a)\times(\delta\mathbf c_f-\delta\mathbf X_a), \tag{29}
$$

$$
\delta a_{fab}=\frac14\frac{\mathbf t^\mathsf T\delta\mathbf t}{\|\mathbf t\|},
\quad \delta p_i=\rho t_i\delta A_i,\qquad \|\mathbf t\|>0. \tag{30}
$$

At a collapsed fan triangle the norm is nondifferentiable. Equation (30) needs a specified treatment there; the baseline has no enforced positive fan area.

### 9.3 Damped Gauss-Newton subproblem

Let \(\mathbf E_y=[\mathbf0_{K\times E}\ \mathbf I_K]\). The increment minimizes

$$
\frac W{P^2}\|\mathbf g+\mathbf J\boldsymbol\delta\|_2^2+
\|\mathbf D_\alpha^{1/2}(\mathbf y-\mathbf y^0+\mathbf E_y\boldsymbol\delta)\|_2^2+
\lambda\|\boldsymbol\delta\|_2^2. \tag{31}
$$

The augmented system is

$$
\mathbf J_{aug}=
\begin{bmatrix}
\sqrt W\,\mathbf J/P\\
\mathbf D_\alpha^{1/2}\mathbf E_y\\
\sqrt\lambda\,\mathbf I_{E+K}
\end{bmatrix},\qquad
\mathbf b_{aug}=
\begin{bmatrix}
-\sqrt W\,\mathbf g/P\\
-\mathbf D_\alpha^{1/2}(\mathbf y-\mathbf y^0)\\
\mathbf0
\end{bmatrix},\quad
\boldsymbol\delta=\arg\min_{\mathbf d}\|\mathbf J_{aug}\mathbf d-\mathbf b_{aug}\|_2^2.
\tag{32}
$$

Dimensions are \(R+K+(E+K)\) rows by \(E+K\) columns. Damping penalizes log/coordinate increments. It is not an absolute-state term in (17).

### 9.4 Direct sparse QR

Each iteration constructs a new sparse matrix and calls Eigen SparseQR with COLAMD ordering: compute(Jaug), then solve(baug). Orthogonal transformations and rank-revealing triangular solves recover the increment. No explicit normal-equation matrix or inverse is formed.

Symbolic and numeric factorization are both repeated. In the locally bundled Eigen header the default pivot threshold is

$$
\tau_{QR}=20(m+n)\epsilon_{double}\max_j\|(\mathbf J_{aug})_{:,j}\|_2, \tag{33}
$$

with maximum column norm replaced by one if it is zero. Here m,n are augmented dimensions. The coupled wrapper checks successful factorization and finite increments, but adds no explicit least-squares gradient check or numerical-rank acceptance test. Positive damping gives exact-arithmetic full column rank; large scale disparities can still influence numerical rank decisions.

QR is direct; it does not remove errors in (26), float evaluation, or local nonlinear ambiguity. The precise default threshold must be rechecked if Eigen changes.

## 10. Step restriction and conditional schedule

With raw direction components \(\delta_u,\delta_y\), apply common scaling

$$
d=\max(1,\|\boldsymbol\delta_u\|_\infty,\|\boldsymbol\delta_y\|_\infty/0.2),
\qquad \widehat{\boldsymbol\delta}=\boldsymbol\delta/d. \tag{34}
$$

Omit the last term if K=0. This caps each log increment at one and each normalized coordinate increment at 0.2; physical coordinate changes scale by S. One common divisor preserves direction.

Try 14 states

$$
\mathbf x_j^{trial}=\mathbf x+2^{-j}\widehat{\boldsymbol\delta},
\quad j=0,\ldots,13. \tag{35}
$$

Skip trials with \(\|\mathbf u_j^{trial}\|_\infty>25\). Accept the first finite trial satisfying strict objective decrease

$$
\Phi_W(\mathbf x_j^{trial})<\Phi_W(\mathbf x). \tag{36}
$$

There is no Armijo coefficient, Wolfe condition, trust-region ratio, residual-only acceptance or monotonic RMS requirement. If none pass, keep the current state.

The trial guard implies \(q_{\min}+e^{-25}\le|q_e|\le q_{\min}+e^{25}\) on tested states. The upper value is approximately \(7.2\times10^{10}\) density units, an exponent guard rather than an engineering bound. It does not clip the seed.

After a completed step, including rejection, increment k and update

$$
\lambda_{k+1}=
\begin{cases}\max(10^{-8},\lambda_k/2)&\text{accepted}\\
\min(10^6,10\lambda_k)&\text{rejected},
\end{cases}
\qquad\lambda_0=10^{-4}, \tag{37}
$$

$$
W_{k+1}=
\begin{cases}
\min(10^{12},10W_k)&\text{unequilibrated and (rejected or }k\bmod8=0)\\
W_k&\text{otherwise},
\end{cases}\qquad W_0=100. \tag{38}
$$

The line search uses current W; these updates affect the next iteration. Because W changes, an objective-decrease statement within one line search is not a monotonicity statement for the entire changing-weight sequence. Different linear steps can alter the realized schedule through acceptance/equilibrium decisions.

## 11. Metrics and termination

The coupled norms are

$$
H_{eq}=\left[\sum_{i=0}^{N-1}(r_{ix}^2+r_{iy}^2)\right]^{1/2},\qquad
V_{eq}=\left[\sum_{i:\,Z\ free}r_{iz}^2\right]^{1/2},\qquad
\varepsilon_{eq}=\sqrt{H_{eq}^2+V_{eq}^2}=\|\mathbf g\|_2. \tag{39}
$$

The wrapper explicitly includes black-anchor XY in H, overriding the shared reporter's free-component horizontal norm. Residual is a global force norm, not a per-node average, maximum or dimensionless relative error.

Design displacement is

$$
\mathrm{RMS}_{design}=\left[N^{-1}\sum_i\|\mathbf X_i-\mathbf X_i^0\|_2^2\right]^{1/2}. \tag{40}
$$

It includes surface nodes and cable copies. A cable copy's reference is its initial offset position. Black anchors contribute zero movement. This length metric is distinct from both force residual and weighted preference.

Success/exported equilibrated requires a finite report and residual at most \(10^{-3}\) by default. Any member length below \(10^{-6}\) invalidates the iterate. Numerical/model units and node count affect the absolute norm's interpretation.

After (37)-(38), termination tests are ordered:

1. Success and (rejection or \(\|\widehat{\delta}\|_2<10^{-5}\)): Converged.
2. Maximum outer iterations reached: IterationLimit.
3. Rejection, damping at \(10^6\), weight at \(10^{12}\): Stagnated.
4. Otherwise Running. QR failure/nonfinite increment: Failed; stop request: Cancelled.

The small-step test uses the capped direction before backtracking, not the final \(2^{-j}\widehat\delta\). IterationLimit can accompany success or failure; inspect equilibrated separately. The worker stores the current iterate, not a best-equilibrated archive. A later iterate can lose equilibrium while optimization continues.

No iteration limit, stagnation or local failure proves infeasibility. Low residual establishes the implemented equations only; it is not a buckling/stiffness/material certificate.

## 12. Analytic three-node example

Take anchors \((-1,0,0),(1,0,0)\), apex \((0,0,h)\), two compression densities a>0, anchor cable density c<0, and algebraic apex load p>0, without self-weight. Apex Z and anchor horizontal balance give

$$
2ah=p,\qquad a+2c=0,\qquad
a=p/(2h),\quad c=-p/(4h). \tag{41}
$$

The compression members have length \(\sqrt{1+h^2}\); cable length is two:

$$
F_{comp}=\frac p{2h}\sqrt{1+h^2},\qquad
F_{cable}=-\frac p{2h}. \tag{42}
$$

Different tension/compression densities are compatible with exact equilibrium. Supported residual Z is p/2 at each anchor; exported reaction Z is -p/2, illustrating (9)-(10).

The synthetic tied-arch test starts at apex (0.2,0,1), uses load two and fixed anchors, then checks horizontal support balance and fixed coordinates. It does not require a unique height/density solution.

## 13. Algorithm pseudocode

~~~text
PREPARE:
  Read colored mesh; grid-weld; validate; build compression/cable/connector graph.
  Build coordinate constraints and equilibrium selectors.
  Set u=0, y=y0, W=100, lambda=1e-4; evaluate initial loads; fix S and P.

OUTER STEP:
  Check cancellation.
  On first step only, attempt helper NNLS/FDM seed and adopt on objective decrease.
  Evaluate current geometry, densities, tributary loads, residual and objective.
  For each unknown:
    Check cancellation; perturb; reevaluate complete residual; collect derivatives.
  Append position-preference and increment-damping rows.
  Compute new sparse QR and solve increment; reject numerical failure.
  Common-scale direction.
  Try up to 14 half-step trials, recomputing all loads and residuals.
  Accept first finite strict objective improvement; otherwise keep state.
  Publish current state; increment count; update damping and weight.
  Apply ordered convergence, iteration-limit and stagnation tests.
~~~

A single background jthread runs the solve. A mutex protects the latest complete snapshot. The solver loop has no deliberate sleep or frame-rate cap; geometry updates when a completed step is published. Background execution maintains UI responsiveness without parallelizing QR.

Cancellation checks occur in seed work, Jacobian columns and line-search trials; coupled QR itself has no stop-token checkpoint. Reload can wait for QR before joining the old worker. Pressing q resets the solver and starts from initial geometry; it does not resume the displayed state.

## 14. Cost and conditioning

Let \(M=E+K\), \(F_v=\sum_fm_f\). A residual evaluation is approximately \(O(N+E+F_v)\), ignoring copying/allocation overhead. Finite differences require M perturbed evaluations plus the base. The code also scans all R rows per column, giving approximate assembly work

$$
O\big(M(N+E+F_v)+RM\big). \tag{43}
$$

Each evaluation copies mesh/result data. Line search adds up to 14 evaluations. The first step can add up to 64 helper candidate coordinate solves with up to 100 load iterations per candidate. These are nested budgets, not timing predictions.

Sparse QR cost and memory depend on fill-in, ordering and rank behaviour, not polygon count alone. Symbolic analysis is repeated. Cable copies and density variables add unknowns even for a visually low-poly surface.

Conditioning can deteriorate with mixed-sign mechanisms, nearly parallel/coincident opposite-sign members, exponential q ranges, increasing W, decreasing damping, zero position weights/gauge freedom, float finite differences and large coordinate origins. There is no dedicated column scaling, condition monitoring, gradient-based nonlinear termination or best-feasible archive.

Coincident compression/cable members can have large opposing forces with a small resultant. There is no meaningful upper-force regularizer. Compare axial force \(|q_e|\ell_e\), geometry and cancellation, not density magnitudes alone.

## 15. Reproducibility and evidence for paper drafting

### Current numerical record

| Setting | Value/rule |
| --- | --- |
| Weld grid / viewer offset | \(10^{-3}\) / -0.05 |
| Viewer outer limit | 50, slider 1-1000 |
| Base surface load / density / thickness | 0.001 / 1 / 1 |
| Ordinary/red/yellow coordinate weights | 1 / 0 / 50 |
| Minimum q / short-member rejection | \(10^{-4}\) / length \(<10^{-6}\) |
| Physical residual acceptance | Global absolute norm \(10^{-3}\) |
| Initial/min/max damping | \(10^{-4}/10^{-8}/10^6\) |
| Initial/max W | \(100/10^{12}\) |
| Difference increment / cutoff | \(10^{-4}\max(1,|x_c|)\) / \(10^{-12}\) |
| Log/normalized-coordinate increment caps | 1 / 0.2 |
| Backtracking / trial log guard | 14 powers of half / absolute value 25 |
| Small-direction test | \(10^{-5}\), before backtracking |
| Coupled solver | Eigen SparseQR, COLAMD, default rank threshold |

Input coordinate magnitude above \(10^{12}\) or nonfinite values is rejected. Input face-vector magnitude below \(10^{-8}\) and selected interior normal magnitude below \(10^{-6}\) are rejected. These are numerical thresholds tied to model units.

Archive source/dependency revisions, build flags, hardware, thread configuration, colored input OBJ, working directory, welded/member counts, all settings, H/V/full residual, RMS, final positions/q/status and timing scope. JSON currently lacks a complete load/attraction/iteration setting record and separate total residual/RMS fields; retain an external experiment record.

### Existing checks

~~~powershell
cmake --build alice2/build --config Release --parallel 4
./alice2/build/Release/checks/alice2_checks.exe --check-bestfit-tna
./alice2/build/Release/checks/alice2_checks.exe --check-offset-cable path/to/tna.obj
~~~

The synthetic suite covers the coupled tied arch, vertical-only fixed reactions, fixed XYZ/Z, independent red/yellow preferences, all-sliding anchors, cancellation, incompatible-network nonacceptance, topology, branching, shared anchors, welding and surface-only loads. It also includes separate v1/TNA regressions.

The strict OBJ check compares offsets -0.5/-1 and allows 100 coupled iterations, using both support weights 50, nodal load 0.001 and density one. These differ from viewer offset/red-penalty defaults. It requires final equilibrium, not loop completion alone.

There is no coupled benchmark command in this restored baseline. The existing benchmark-bestfit-tna command times the helper/v1 workflow. Later analytic/LSMR logs cannot substantiate performance claims about this baseline.

### Further study recommendations; not completed validation

1. Verify analytic examples and the physical support-force sign conversion.
2. Compare (26) with (27)-(30) across perturbation sizes and coordinate/load scales.
3. Record QR linear residual, least-squares gradient and rank on frozen systems.
4. Test mesh-density, units, preferences and seed sensitivity.
5. Log actual W, damping, accepted step length, objective, residual and RMS.
6. Test near-degenerate faces and cancelling tension/compression members.
7. Perform independent stiffness/buckling/material assessment if claiming structural adequacy.

The method supports a description of joint density/geometry optimization with prescribed signs and vertical-reaction targets. Reciprocal 3D graphic statics, elastic compatibility, structural stability, global best fit, minimum material use and equal-accuracy speedups require additional work/evidence.

## 16. Outputs

Node reference is the initial built-network coordinate, including a cable copy's initial offset. Position is current geometry. Member force_density is current q, potentially present before equilibrium; axial force follows (5).

Support reaction and force_on_support follow (9), with the physical interpretation issue in (10). Raw nodal_residual is required to assess unfinished sliding support balance. Inspect equilibrated rather than assuming exported values are solved.

OBJ shares current JSON positions, uses 1-based indexing, original surface faces and cable/connector lines. It stores no q/reactions. Exports are allowed from unfinished iterates.

## 17. Mathematical functions mapped to sources

| Mathematical operation | Implementation | Equations |
| --- | --- | --- |
| Grid welding and role preservation | weldOffsetCableMesh | (2) |
| Boundary, normals, offset graph | makeOffsetCableNetwork | (3)-(4) |
| Surface loads and fixed-Z lists | offsetCableSettings; verticalLoads; vertexAreas | (11)-(12) |
| Variable/scale setup | VerticalReactionFit::reset | (1), (13)-(17) |
| Density/coordinate evaluation | VerticalReactionFit::evaluate | (8)-(9), (13)-(14), (39)-(40) |
| Residual selection/objective | VerticalReactionFit::forces, objective | (8), (17) |
| Helper NNLS/coordinate seed | boundedFit; equilibriumShape; BestFitForceDensitySolver::step | (18)-(25) |
| Finite differences, QR, line search, schedule | VerticalReactionFit::step | (26), (31)-(38) |
| Status/diagnostics | VerticalReactionFit::finish, describe | Section 11 |
| Worker/export | OffsetCableNetworkSketch::start, update, exportNetwork | Sections 13, 16 |
| Regression checks | runBestFitTnaChecks, runOffsetCableCheck | Section 15 |

Source links:

- [VerticalReactionFit.h](../../alice2/src/tna/VerticalReactionFit.h)
- [OffsetCableNetwork.h](../../alice2/src/tna/OffsetCableNetwork.h)
- [BestFitForceDensitySolver.cpp](../../alice2/src/tna/BestFitForceDensitySolver.cpp)
- [BestFitForceDensitySolver.h](../../alice2/src/tna/BestFitForceDensitySolver.h)
- [TnaSolver.cpp](../../alice2/src/tna/TnaSolver.cpp)
- [TnaSolver.h](../../alice2/src/tna/TnaSolver.h)
- [sketch_bestfit_tna_v2.cpp](../../alice2/userSrc/bestfit/sketch_bestfit_tna_v2.cpp)
- [BestFitTnaChecks.cpp](../../alice2/src/tna/BestFitTnaChecks.cpp)

The bundled SparseQR threshold was checked in alice2/build/_deps/eigen-src/Eigen/src/SparseQR/SparseQR.h; that generated dependency location may not exist in a fresh checkout.

## 18. Literature and attribution boundaries

The equations here reconstruct the code. Established techniques including force densities, nonnegative least squares, finite differences, damped Gauss-Newton and QR predate this sketch. Novelty assessment requires a separate literature review.

1. H.-J. Schek (1974), *The force density method for form finding and computation of general networks*, Computer Methods in Applied Mechanics and Engineering 3(1), 115-134. [Publisher record/abstract](https://www.sciencedirect.com/science/article/pii/0045782574900450), DOI 10.1016/0045-7825(74)90045-0. Background for force-length ratios and density-based coordinate equilibrium. The publisher abstract was checked; this reference does not claim full-paper reproduction.
2. S. Bhooshan, T. Van Mele and P. Block (2015), *Discrete funicular structures with curve-crease-folded moulds*, Proceedings of the IASS Symposium 2015. [Authors' publication record](https://brg.ethz.ch/publications/106). The helper code attributes its initialization strategy to this work; v2's joint log-variable penalty formulation is described from code and does not include fabrication/projection constraints. The record/abstract was checked; section-by-section equivalence was not established.
3. M. Akbarzadeh, T. Van Mele and P. Block (2015), *On the equilibrium of funicular polyhedral frames and convex polyhedral force diagrams*, Computer-Aided Design. [Authors' publication record](https://brg.ethz.ch/publications/85). Context for reciprocal polyhedral graphic statics, not an implemented force-diagram construction here.
4. [Block Research Group COMPAS 3GS overview](https://blockresearchgroup.github.io/compas_3gs/latest/overview/00_graphic_statics.html). Primary project explanation of reciprocal form/force diagrams and polyhedral equilibrium.
5. [Eigen SparseQR documentation](https://libeigen.gitlab.io/eigen/docs-nightly/classEigen_1_1SparseQR.html). Background for the backend. The local bundled header is authoritative for this build's threshold, rather than a potentially newer online revision.

Before paper submission, read the complete primary papers relevant to the final claims, identify changes beyond baseline 1fffc12 and regenerate results for that final version.
