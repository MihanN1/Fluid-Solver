(lets just forget that we pushed 12 commits just for things to build normally and files to look at least alright 'cause we're stooooopid as shi)
(from Kuzya: i dunno how it works, i have a feeling it's something alive, and changes by itself <3. TOTALLY NOT ME COMMITING 12 TIMES)
# Fluid-Solver

> **1.0 is still the version, and it is no longer the last thing that happened.**
>
> Adaptive mesh refinement was the last item on the old list and it is in: a
> stretched grid where you want the cells, patches of a finer grid where the
> flow wants them. Then the whole solver was ported from a plane to a volume.
> `nz` and `Lz` join `nx`, `ny`, `Lx` and `Ly`, every feature below gained its
> third axis, and **`nz = 1` is the plane every earlier version solved** - the
> code is written so that at `nz = 1` every z term vanishes identically and the
> answer is the old one, bit for bit. The golden master is still 88 frames at
> 0.000e+00 against 0.8. Fourteen test suites are green. *Future Work* at the
> bottom of this file is a real list again, and none of it is this solver.

**A 3D incompressible Navier‑Stokes solver for external flows around arbitrary bodies. `nz = 1` is the plane it used to solve.**

That last sentence is the whole update and it is worth reading twice. The
solver fills a volume `Lx × Ly × Lz` with `nx × ny × nz` cells. `nz` defaults to
1 and `Lz` to 1.0, and a grid one cell deep *is* the plane — same mask, same
pressure field, same frames, the same bits out of the same arguments. Nothing
you ran before runs differently, and the third dimension costs nothing at all
until you ask for it by typing `nz=`.

The repository was called `CFD-Solver-2D` up to and including 0.2, and that
name is gone with this release: it is `Fluid-Solver` now, because the next
things on the list are a deformation solver and an electrodynamic one and
neither of those is a fluid in two dimensions. GitHub redirects the old
address, so a link anyone saved still lands here, but a checkout wants
`git remote set-url origin https://github.com/MihanN1/Fluid-Solver.git`
before it pushes anywhere.

It is an educational/research project that implements a finite‑difference CFD solver for unsteady viscous incompressible flow. It uses the **Chorin projection method** on a **staggered MAC grid** with an **immersed boundary** technique to handle complex geometries. The code is written in C++17 and features:
- Interactive console parameter input with confirmation and on-the-fly editing.
- Full numerical solver with VTK output for post-processing in ParaView.
- STL/OBJ loading; a plane run cuts a section out of the model, a volume run voxelises it whole, with the same placement, rotation and mirroring either way.
- Optional gravity as a uniform body force, pointing in any direction in three dimensions.
- Optional wall behaviour: every body in the mask is found and numbered on its own, and each one can spin about any of the three axes, drag its surface, or be made frictionless, independently of the rest.
- Optional turbulence: Smagorinsky with near-wall damping, or two-equation k-omega SST, both switched on from the configuration and off by default.
- A second solver, switched on with one key: compressible Euler on the same grid and the same geometry, with HLLC fluxes, shocks, two gases that mix, and the sound the flow makes written out as fields and as microphone traces.- A separate SFML desktop application that configures runs, launches the solver and renders the frames it writes, in a real 3D viewport.

The project is designed to simulate incompressible flow around arbitrary bodies — cylinders, airfoils, valves, turbine blades, and similar engineering geometries — as a section through them, or as the whole body sitting in a volume.

---

# Future Work

Four groups, and three of them are a different solver rather than another
feature of this one. That is the honest shape of it: a fluid on a fixed grid is
one problem, and the things below are not that problem.

**Deformation and heat, on finite elements.** A second solver, because a body
that deforms is not a fixed grid, and finite differences are the wrong tool for
something whose shape is one of the unknowns. Two scales.

*Micro*, on a composite's own structure — the fibres and the matrix as they
actually sit: thermophysical properties, electromagnetic properties, elastic,
elastic-plastic, strength and viscoelastic properties of the material itself,
worked out from what it is made of rather than measured and typed in.

*Macro*, on the part: quasi-static elasticity, thermoelasticity at high
temperature, modal analysis, dynamic elasticity, dynamic viscoelasticity,
stability, quasi-static plasticity, quasi-static creep, shells quasi-static and
dynamic and their modal analysis, heat conduction, internal heat and mass
transfer, and heat conduction in shells.

Plus corrosion and cracking — where the material goes and where it opens. Plus
a region of pressure that travels across a body: animated, and then the same
travel modelled rather than drawn, so the load moves and the body answers.

**Electrodynamics, on finite differences.** The time-domain solve, and with it
static charge and triboelectricity.

**Plasma, by finite volumes.** The finite-volume method — or the three solvers
above joined together, which is what plasma mechanics actually is.

**Fabrics.** The fabric itself, reconstructed from a photograph of it.
Impregnation of the fabric with a binder, which is how a composite is really
made. Soaking up and wringing out: how much it holds, how fast, and what comes
back out. Then forming the preform, curing it, and the technological stresses
and the distortion the cure leaves behind.

And the two lines that were already here, which have not changed their minds:

- MAY add several other solvers and merge all of them into one
- MAY make a full on website where u would download it all, but only if the previous point is done

---

# Features

## Numerical solver

- ✅ Incompressible Navier–Stokes equations, in three dimensions
- ✅ `nz = 1` is the plane, and it is the old answer bit for bit: every z term
  vanishes identically there, the multigrid returns the 2D field with the same
  level count and the same cycle count, and the frames compare at exactly zero
- ✅ Chorin projection method
- ✅ Staggered (MAC) grid: `u` on `(nx+1)*ny*nz` faces, `v` on `nx*(ny+1)*nz`,
  `w` on `nx*ny*(nz+1)`, cells `nx*ny*nz` indexed `(k*ny + j)*nx + i`
- ✅ Convection: first-order upwind, or a limited second-order scheme
- ✅ Time: forward Euler, or SSP Runge-Kutta of second or third order
- ✅ Central-difference diffusion
- ✅ Dynamic CFL-based timestep
- ✅ Adaptive pressure correction
- ✅ Optimized Poisson solver
- ✅ Correct residual evaluation
- ✅ Immersed boundary method
- ✅ A seven-point pressure operator with `coefF`/`coefB`, a multigrid that
  coarsens each axis independently, and transfers that are the 3D tensor
  product of the 1D ones it already had
- ✅ The same hierarchy on CUDA, kernel for kernel: a 2D block over (i, j) and
  one grid layer per k, every kernel bit-identical to the CPU path
- ✅ Optional gravity / uniform body force, direction free in three dimensions,
  as a head added on output or as a real force inside the solve
- ✅ Named boundary conditions on each of the six sides: inlet, outlet, wall,
  moving wall, free slip
- ✅ Rectangular inlet windows: `inletFrom`/`inletTo` along the face's first
  axis, `inletFrom2`/`inletTo2` along its second
- ✅ A pressure problem with no open side at all, solved up to the constant it
  is defined up to
- ✅ Variable per-face weights in the pressure operator
- ✅ Two fluids with an interface: volume fraction advected on three axes,
  compressive transport, variable density in the momentum equation and in the
  pressure solve
- ✅ Surface tension by height-function curvature — the full 3D normal and
  curvature — with a contact angle at walls
- ✅ Two fluids that mix instead, spreading by Fickian diffusion
- ✅ Turbulence on three axes: all nine strain components, a Smagorinsky filter
  width of `cbrt(dx dy dz)`, k-omega SST transported in z, and a wall distance
  swept over 26 neighbours
- ✅ Moving walls: rotation about any of the three axes and sliding along any of
  them, set per object
- ✅ Bodies that travel through the volume, on a path you give or one the flow
  decides, with the mask cut again every step
- ✅ Rigid bodies with a quaternion orientation and a full inertia tensor: a
  free body carries angular momentum and its gyroscopic term, so it tumbles the
  way it should and the tennis-racket flip appears
- ✅ Freshly uncovered cells filled from the surface that swept past them
- ✅ Fluid-structure interaction with the added mass carried implicitly, and
  strong coupling for a body lighter than what it displaces
- ✅ Flow sources that ride a body, in its own frame, thrust and all
- ✅ Free-slip walls, set per object
- ✅ Several models at once, each placed where you put it
- ✅ Extra diagnostic fields written into the frames on request
- ✅ Restarting sim from a given save
- ✅ A test suite that checks the answers rather than the exit code

---

## Geometry

- ✅ STL import
- ✅ OBJ import
- ✅ Arbitrary slicing plane, at `nz = 1`
- ✅ Voxelisation of the whole model at `nz > 1`: every cell centre tested by a
  ray cast along +x with a crossing count, one cast per (j, k) scanline, the
  triangles binned by (y, z) and the scanlines split across threads. 128^3
  against 50k triangles takes about 5 ms
- ✅ A surface that is not closed is reported rather than producing garbage
- ✅ Three slice angles: `sliceAngleY` joins `sliceAngleX` and `sliceAngleZ`,
  applied as Rz·Ry·Rx, which at `angleY = 0` is exactly the two the plane case
  applies
- ✅ Automatic contour reconstruction
- ✅ Non-manifold diagnostics
- ✅ Automatic scaling and centering
- ✅ Rotation and mirroring
- ✅ Polygon rasterization using even-odd filling
- ✅ Automatic detection and numbering of separate bodies — 26-connected in a
  volume, 8-connected in a plane- ✅ Every closed loop the plane cuts, not only the largest: two profiles side
  by side stay two profiles, and a loop inside a loop comes out as a hole
- ✅ Slivers under a ten-thousandth of the largest loop are dropped as cutting
  noise rather than rasterized into the flow
- ✅ Each body carries its volume and its full inertia tensor about its own
  centroid
- ✅ `profiles` entries place a model in depth with `z=` and tilt it with `ay=`

---

## Visualization

Not part of the solver. `cfd_app` writes VTK frames and nothing else; SFML is
not linked into it. Everything below belongs to the separate desktop UI, which
is its own executable and drives the solver as a child process.

- ✅ A real 3D viewport on OpenGL: orbit, pan and zoom the way Blender does it,
  axis views, orthographic or perspective
- ✅ The domain box and the cell grid
- ✅ The body's surface, solid or wireframe
- ✅ Slice planes on any axis, moved through the volume
- ✅ The whole volume as a translucent cloud, every cell that differs from the
  still air painted and the rest not drawn
- ✅ Isosurfaces by marching cubes
- ✅ Vortices as a Q-criterion surface, with their core lines
- ✅ Any of those three at once, each with its own slider, all coloured by the
  one field chosen
- ✅ 3D streamlines, with animated tracers
- ✅ Click to select: a body picks it in the BODIES group, a domain face focuses
  that face's boundary row
- ✅ The old 2D view kept whole, working as a slice through the volume — pick
  the axis, move the plane, and every 2D feature is exactly as it was. `V`
  switches between the two
- ✅ Pressure rendering
- ✅ Velocity rendering
- ✅ Solid mask rendering
- ✅ Pause / Resume
- ✅ Time scrubbing
- ✅ Zoom and camera movement
- ✅ Rendering mode switching
- ✅ Save and load a configuration the solver's command line accepts verbatim,
  including one recovered straight out of a `.vtk` frame

---

## Output

- ✅ VTK export, `DIMENSIONS nx+1 ny+1 nz+1` with a real `SPACING dz` and
  `CELL_DATA nx*ny*nz`
- ✅ Physical pressure (Pa)
- ✅ Velocity vectors whose third component is now real
- ✅ Solid mask
- ✅ ParaView compatible
- ✅ Full run state embedded in every frame, `w` faces included, lossless to the
  bit
- ✅ Frame format version 3, and a frame written by any earlier version —
  version 1 included — still loads, as a volume one cell deep with `w` zero
- ✅ Continue a stopped simulation from any frame
- ✅ Compact frames, about 19 bytes a cell in a plane, with nothing in them
  stored twice — a compressible frame no longer carries the conserved
  variables beside the three fields they are made of, and `frameState` says how
  much further to go
- ✅ `runName` puts a run's frames in a folder of their own
- ✅ A `stop` file in the output folder asks a run with no console of its own
  to finish the step, write the frame and return

---

## Mathematical Model (brief)

We solve the 3D incompressible Navier–Stokes equations (kinematic pressure, ρ = 1). At `nz = 1` the front and back sides are closed, `w` is zero everywhere, every `∂/∂z` differences to nothing, and what is left on the page is the pair of equations this section used to hold:

**Momentum (X):**

![](https://latex.codecogs.com/svg.image?\frac{\partial%20u}{\partial%20t}+u\frac{\partial%20u}{\partial%20x}+v\frac{\partial%20u}{\partial%20y}+w\frac{\partial%20u}{\partial%20z}=-\frac{\partial%20p}{\partial%20x}+\nu\nabla^{2}u+g_{x})

**Momentum (Y):**

![](https://latex.codecogs.com/svg.image?\frac{\partial%20v}{\partial%20t}+u\frac{\partial%20v}{\partial%20x}+v\frac{\partial%20v}{\partial%20y}+w\frac{\partial%20v}{\partial%20z}=-\frac{\partial%20p}{\partial%20y}+\nu\nabla^{2}v+g_{y})

**Momentum (Z):**

![](https://latex.codecogs.com/svg.image?\frac{\partial%20w}{\partial%20t}+u\frac{\partial%20w}{\partial%20x}+v\frac{\partial%20w}{\partial%20y}+w\frac{\partial%20w}{\partial%20z}=-\frac{\partial%20p}{\partial%20z}+\nu\nabla^{2}w+g_{z})

where the body force **g** is zero unless gravity is enabled — and even then the solver never discretizes it, because at constant density it is exactly a pressure offset. `g_z` is zero unless `gravityTilt` is. See *Gravity* under §4.

**Continuity (incompressibility):**

![](https://latex.codecogs.com/svg.image?\frac{\partial%20u}{\partial%20x}+\frac{\partial%20v}{\partial%20y}+\frac{\partial%20w}{\partial%20z}=0)

The **Chorin projection** splits each time step into:

1. **Predictor** – compute intermediate velocities \(u^*, v^*, w^*\) without pressure.
2. **Poisson equation** – solve

![](https://latex.codecogs.com/svg.image?\nabla^{2}p=\frac{1}{\Delta%20t}\left(\frac{\partial%20u^{*}}{\partial%20x}+\frac{\partial%20v^{*}}{\partial%20y}+\frac{\partial%20w^{*}}{\partial%20z}\right))

using a multigrid V-cycle with red/black SOR as its smoother. The operator is seven-point in a volume and, with the front and back coefficients closed at `nz = 1`, the same five-point one it always was.

3. **Corrector** – update velocities with the pressure gradient.

Boundary conditions: no-slip on solid walls, constant velocity at inlet, zero-gradient at outlet, free-slip at top/bottom and — the two the port added — whatever `bcFront` and `bcBack` say at `z = 0` and `z = Lz`, which default to closing the volume so that a one-cell-deep grid is the plane it used to be. No-slip means "the fluid matches the wall", and the wall is allowed to be moving — or to be frictionless, see *Walls* below and §7. The outlet also carries the only Dirichlet condition on pressure, and it holds `p = 0` whatever gravity is doing — see *Gravity* under §4.

### Geometry section, and geometry volume

There are two paths now, and which one runs is decided by `nz` alone.

**`nz > 1`: the model is voxelised, not cut.** There is no section plane and no
contour. The triangle soup is placed, scaled and rotated exactly the way the
plane case places it, and then every cell centre is asked one question: is it
inside? The answer is a ray cast along +x with a crossing count — odd is
inside, even is out — and the casts are cheap because they are shared: one cast
per (j, k) scanline rather than one per cell, the triangles binned by (y, z) so
a scanline only ever meets the few that reach it, and the scanlines handed out
across OpenMP threads. **128^3 against 50k triangles takes about 5 ms**, which
is why the mask can be cut again every step for a body that travels.

A crossing count is only meaningful on a closed surface. A model whose surface
is open has no inside, and the run **says so and stops** rather than filling
whatever the parity happened to come out as — which is the failure mode that
produces a body with holes in it and a wake nobody can explain.

**`nz = 1`: the section cut, untouched.** Everything below this line is that
path, and it is the same code, producing the same mask, that every earlier
version produced.

The imported triangle mesh is centred at its bounding-box centre

![](https://latex.codecogs.com/svg.image?\mathbf{c})

The section plane is

![](https://latex.codecogs.com/svg.image?(\mathbf{x}-\mathbf{c})\cdot\mathbf{n}=0)

where

![](https://latex.codecogs.com/svg.image?\mathbf{n})

is constructed from `sliceAngleX` and `sliceAngleZ` — and from `sliceAngleY`,
which the port added so that a model can be turned about all three axes rather
than two. The three are applied as **Rz·Ry·Rx**, in that order, and at
`sliceAngleY = 0` that product is exactly the pair of rotations the plane case
has always applied. The same three angles orient the model on the voxel path,
where there is no plane to make a normal out of and they are simply the
model's own orientation.

Each triangle edge with signed endpoint distances

![](https://latex.codecogs.com/svg.image?d_{a})

and

![](https://latex.codecogs.com/svg.image?d_{b})

crosses the plane at

![](https://latex.codecogs.com/svg.image?\mathbf{x}_{section}=\mathbf{a}+\frac{d_{a}}{d_{a}-d_{b}}(\mathbf{b}-\mathbf{a}))

The resulting segments are connected into closed contours and the largest of them by area is kept; the rest are discarded, which is the limitation noted under *Geometry* above. Geometry consistency is validated before rasterization. The resulting contour can optionally be mirrored, rotated by `sliceRotation`, scaled to

![](https://latex.codecogs.com/svg.image?0.2\,\min(L_x,L_y))

centred in the computational domain, rasterized, and filled using the even–odd point-in-polygon rule.

---

# Numerical Optimizations

The solver contains numerous low-level optimizations while preserving numerical accuracy.

Implemented optimizations include:

- Precomputed reciprocal grid spacing
- Precomputed wall velocities on the closed faces
- Elimination of repeated divisions
- Cached row offsets for structured-grid indexing
- Reduced address arithmetic
- Cache-friendly memory traversal
- Optimized pressure residual computation
- Reduced temporary allocations
- Optimized boundary-condition application
- Optimized predictor and corrector kernels
- Parallel execution
- GPU acceleration
- Multigrid-accelerated pressure solver
- One residual sweep per pressure solve, not two
- Frames packed against their own contents instead of repeating them
- Semi-coarsening down to isotropy, including a two-to-one aspect ratio

The third dimension was written so that none of that had to be given back:

- **Loop order is k, j, i, and `i` stays contiguous.** Every AVX2 kernel in the
  tree is unchanged in shape, because the thing it vectorises — a run of cells
  along `i` — is still a run of cells along `i`. A volume is a stack of the
  planes the vector code already knew how to walk.
- **OpenMP collapses (k, j).** A plane has one k, so collapsing is what keeps
  a plane parallel over its rows while a volume parallelises over both.
- **The voxeliser bins triangles by (y, z)** and casts once per scanline rather
  than once per cell, which is the difference between 5 ms and something you
  would not do every step.
- **The multigrid coarsens each axis independently**, so a grid that is deep in
  one direction and thin in another still walks down towards isotropy instead
  of stalling on the axis it cannot smooth.
- **The CUDA hierarchy mirrors the CPU one kernel for kernel**, with a 2D block
  over (i, j) and one grid layer per k, and every kernel was checked against the
  CPU path off-GPU and is bit-identical to it.

The implementation prioritizes computational performance without changing the numerical formulation.

---

# Architecture

```text
Fluid-Solver/
├── .vscode/
├── out/
│   ├── build/
│   │   ├── x64Debug(or Release)
│   │   │   ├── bin
│   │   │   │   ├── Debug <-build here
│   │   │   │   │   ├── output/ <-vtk here
│   │   │   │   │   └── cfd_app.exe <-will be renamed to fluid_solver.exe, as there will be other solvers. other files arent really necessary to explain
├── logo/
│   ├── fluid-solver.ico            <- embedded in the Windows executable
│   ├── fluid-solver-{16..1024}.png <- Linux hicolor icons
│   ├── wizard-*.bmp                <- Inno Setup wizard images
│   └── toxic-mark-{32..1024}.png   <- the original mark
├── tests/                          <- ctest suite, built with -DBUILD_TESTING=ON
│   ├── TestHarness.hpp
│   ├── PoissonTests.cpp            <- manufactured solution, order of accuracy
│   ├── ChannelTests.cpp            <- Poiseuille against the exact parabola
│   ├── CavityTests.cpp             <- the lid driven cavity against Ghia 1982
│   ├── InletTests.cpp              <- inlet bands, profiles and mass balance
│   ├── MovingBodyTests.cpp         <- travel, fresh cells, coupling, thrust
│   ├── SurfaceTensionTests.cpp     <- Laplace jump, spurious currents, rounding
│   ├── MultiphaseTests.cpp         <- volume kept, a still layer staying still,
│   │                                  and a dam break against its energy bound
│   ├── ConservationTests.cpp       <- divergence, mass balance, hydrostatics
│   ├── ConvectionTests.cpp         <- the schemes against each other
│   ├── RestartTests.cpp            <- a continuation against a straight run
│   ├── TurbulenceTests.cpp         <- the two models against what they claim
│   ├── CompressibleTests.cpp       <- Sod, an oblique shock, two gases, the
│   │                                  acoustics, a driven body, a written wav,
│   │                                  a stretched grid, the refinement, and an
│   │                                  isentropic vortex carried across the grid
│   ├── VolumeTests.cpp             <- the plane still being the plane, the cube
│   │                                  cavity, gravity down each of the three
│   │                                  axes, a closed box's mass, a volumetric
│   │                                  restart, turbulence staying finite, walls
│   │                                  turning about every axis, a shock tube
│   │                                  staying one-dimensional, refinement in
│   │                                  depth, and a solid body in a volume
│   └── BackendAgreementTests.cpp   <- AVX2 and OpenMP on against off
├── src/
│   ├── main.cpp
│   ├── AppPaths.cpp                <- resolves paths against the executable
│   ├── Boundary.cpp                <- what each side of the domain does
│   ├── Phase.cpp                   <- the volume fraction and how it is carried
│   ├── Config.cpp
│   ├── Mesh.cpp
│   ├── Restart.cpp
│   ├── Solver.cpp
│   ├── SolverCompressible.cpp      <- the compressible solver and its run
│   ├── SolverCompressibleCuda.cu   <- the same kernels on a GPU
│   ├── AmrHierarchy.cpp            <- patches, tagging, clustering, ghosts
│   ├── AmrDriver.cpp               <- subcycling down the levels
│   ├── RigidBody.cpp               <- bodies that travel, and their collisions
│   ├── Turbulence.cpp
│   ├── Multigrid.cpp
│   ├── MultigridCuda.cu
│   ├── app.rc.in                   <- icon and version block, Windows only
│   └── tiny_obj_loader_impl.cpp
├── include/
│   ├── AppPaths.hpp
│   ├── Boundary.hpp
│   ├── Config.hpp
│   ├── Mesh.hpp
│   ├── Restart.hpp
│   ├── Solver.hpp
│   ├── Multigrid.hpp
│   ├── MultigridCuda.cuh
│   └── tiny_obj_loader.h
├── scripts/                        <- build every variant, package a release
├── installer/                      <- Inno Setup, makeself, productbuild
├── models/ <- actially not really needed, but the models could be stored here. we store them here for tests.
├── lib/
│   └── stl_reader/
├── CMakeLists.txt
├── LICENSE
└── README.md
```

---

# Requirements

- C++17 compatible compiler
- CMake 3.28+ (what `cmake_minimum_required` asks for; `scripts/make-release.sh`
  installs a newer one into `.toolchain/` when the system copy is older)
- CUDA Toolkit (optional, for the GPU pressure solver)
- OpenMP (optional, picked up automatically when present). This used to say
  that every directive in the tree stayed inside OpenMP 2.0, because that is
  all MSVC's classic `/openmp` implements. That is no longer true: the port put
  `collapse(2)` on the (k, j) loops — a plane has exactly one `k`, so without
  the collapse a plane run would hand the whole grid to one thread — and
  `collapse` is **OpenMP 3.0**. Classic `/openmp` rejects it outright, so on
  MSVC `CMakeLists.txt` now asks for `/openmp:llvm` instead, and the release
  script packs `libomp140.<arch>.dll` beside the binary where it used to pack
  `vcomp140.dll` (it still falls back to `vcomp140.dll` if that is what the
  toolchain produced). `/openmp:llvm` wants Visual Studio 2019 16.9 or newer;
  an older one fails the `find_package` check and the build says OpenMP was
  not found rather than dying halfway through a kernel. The rest of the 2.0
  discipline still holds and is still worth keeping: signed `int` loop
  counters everywhere, and reductions limited to `+ * - & ^ | && ||` - a `max`
  or `min` reduction is OpenMP 3.1, MSVC rejects it outright with C7660, and
  where one is wanted the loop keeps a per-thread value and folds it in a
  `critical` at the end instead.
- A CPU with AVX2 (optional, the vector kernels; without it every one of them
  falls back to the scalar loop it already carries)
- ParaView (optional, for looking at the output)

There are no other dependencies. `tiny_obj_loader` and `stl_reader` are
header-only and vendored, so a plain configure-and-build works out of the box.

Supported compilers:

- MSVC 2022+
- GCC 9+
- Clang 10+

---

# Build (PowerShell)

```powershell
cd "...\Fluid-Solver"

if (Test-Path build) { Remove-Item build -Recurse -Force }
if (Test-Path install) { Remove-Item install -Recurse -Force }

cmake -S . -B build -G "Visual Studio 18 2026" -A x64

cmake --build build --config Release

cmake --install build --config Release --prefix install
```

Four switches, all safe to change in any combination:

| Option | Default | Off means |
|---|---|---|
| `CFD_ENABLE_AVX2` | ON | scalar kernels instead of the vector ones |
| `CFD_ENABLE_OPENMP` | ON | single-threaded, bit-identical to the threaded build |
| `CFD_ENABLE_CUDA` | ON | CPU multigrid only, no toolkit needed |
| `CFD_STATIC` | ON | link against the shared runtime instead of embedding it |

CUDA is dropped automatically when no toolkit is found, unless
`-DCFD_ENABLE_CUDA_EXPLICIT=ON` says to treat that as an error instead. A CUDA
build started on a machine with no NVIDIA device says so and runs on the CPU.

`scripts/build-windows.ps1` and `scripts/build-linux.sh` build every combination
for their platform in one go; `scripts/make-release.ps1` and
`scripts/make-release.sh` go further and produce the installers and the release
folder as well.

---

# Run

```powershell
.\install\bin\"Fluid Solver.exe"
```

The CMake target is still `cfd_app`, but the file it produces is named
`Fluid Solver` — `CFD_APP_NAME` sets it, and there will be other solvers next
to this one.

Configure:

- Domain size, `Lx × Ly × Lz`
- Grid resolution, `nx × ny × nz` — `nz = 1` is the plane and is the default
- Flow parameters
- Reynolds number
- Gravity (optional: magnitude, direction in the plane, and tilt out of it)
- Time parameters
- Pressure solver parameters
- Geometry
- Slice orientation, or model orientation in a volume
- Walls (optional: rotation about any axis, sliding along any axis, and
  free-slip, per object)
- CUDA on or off

After confirmation the simulation starts immediately.

## Two runs that actually work

A **cube cavity**: a closed box with the lid sliding, which is the 3D case
everybody checks a new solver against.

```powershell
"Fluid Solver.exe" caseType=cavity lidSpeed=1 nu=0.01 ^
                   Lx=1 Ly=1 Lz=1 nx=64 ny=64 nz=64 ^
                   steadyTolerance=1e-5 totalTime=200 ^
                   "extraFields=vorticity,speed"
```

`caseType=cavity` closes **all six** sides itself now, not four, and empties the
domain. Re is `lidSpeed * Ly / nu`, so that line is Re 100, and the primary
vortex lands where Ghia puts it.

**Flow past a body in a duct**: an inlet at one end, an outlet at the other, and
four solid sides around it.

```powershell
"Fluid Solver.exe" nx=160 ny=64 nz=64 Lx=2.5 Ly=1 Lz=1 ^
                   U0=1 nu=2e-3 ^
                   bcLeft=inlet bcRight=outlet ^
                   bcBottom=wall bcTop=wall bcFront=wall bcBack=wall ^
                   inletProfile=parabolic ^
                   "profiles=ball.obj@x=0.7,y=0.5,z=0.5,size=0.3" ^
                   "extraFields=vorticity,speed,objectId" ^
                   totalTime=8 saveInterval=20
```

`inletProfile=parabolic` bends the inlet along **both** tangential axes, which
is what duct flow is; `parabolicSpan` bends it along one and leaves the other
flat, which is a plane channel extruded through z. At `nz = 1` they are the same
thing. `z=` in a `profiles` entry is where the model sits in depth — leave it
out and it is centred, which is usually what you want.

Both lines run on a plane by deleting `nz=` and `Lz=` and the two z-side
boundaries. That is the whole difference — in typing. It is not the whole
difference in what it costs: see *What a volume costs against a plane* under
**Performance** before setting `nz=64` on a grid that was already slow.

Every answer is read as a whole line and checked before it is accepted: a value
that does not fit is refused with the reason and the same question is asked
again, so one bad answer can no longer zero out everything after it. Pressing
Enter keeps the value shown in brackets, and `nx=256` typed straight into the
confirmation screen works too.

## Non-interactive mode (`key=value`)

Pass at least one argument and the program stops asking questions: it takes the
whole configuration from the command line, prints it once and starts. That is
the only switch there is - there is no `--batch` flag, `argc > 1` *is* the flag.

```powershell
.\install\bin\cfd_app.exe                        # interactive, as above
.\install\bin\cfd_app.exe --help                 # the keys and the rules
.\install\bin\cfd_app.exe nx=256 ny=128 nu=2e-3  # non-interactive
```

What changes in this mode:

- no questions and no confirmation screen, the run starts immediately;
- nothing is waited for at the end, the process just exits, so the whole run
  fits in a loop or a batch file;
- anything not given on the line keeps its default from `Config.hpp`;
- on a continuation a rejected value is fatal instead of being asked again.

### How to write the arguments

| Rule | Right | Wrong |
|---|---|---|
| no spaces around `=` | `nx=256` | `nx = 256`, `nx 256` |
| keys are case insensitive, leading dashes allowed | `nx=256`, `NX=256`, `--nx=256` | - |
| decimal separator is a dot | `nu=0.002`, `nu=2e-3` | `nu=0,002` |
| switches take 1 or 0 (`true/false`, `yes/no`, `on/off` also work) | `useCuda=1` | `useCuda=yeah` |
| no units inside the value | `totalTime=2` | `totalTime=2s` |
| counts are whole numbers | `saveInterval=25` | `saveInterval=25.5` |
| quote the whole token when the path has spaces | `"geometryFile=C:\my models\wing.stl"` | `geometryFile=C:\my models\wing.stl` |

Nothing is parsed leniently any more. A value that does not fit is refused by
name, every bad argument on the line is reported in one go, and the run does not
start:

```text
2 arguments are wrong:
  not a right way to write nu=0,002: the decimal separator is a dot, write nu=0.002
  not a right way to write useCuda=yeah: useCuda is a switch, write useCuda=1 or useCuda=0 (true/false, yes/no and on/off work too)

Nothing has been started. Fix the line and run it again.
```

A misspelled key gets the nearest real one: `saveinterwal=5` answers
*did you mean saveInterval?*

Values that are legal but suspicious are kept and warned about instead:
`CFL` above 1, `dtSafety` above 1, `omega` within 0.05 of 2, `mgTolerance`
above 0.1, `nu=0`.

### Keys and defaults

| Key | Type | Default | Accepted |
|---|---|---|---|
| `Lx` `Ly` | float, m | 1.0 | > 0 |
| `Lz` | float, m | 1.0 | > 0, how deep the volume is |
| `nx` `ny` | int | 50 | >= 8 |
| `nz` | int | 1 | >= 1. 1 is the plane every earlier version solved |
| `U0` | float, m/s | 1.0 | any finite |
| `nu` | float, m^2/s | 0.01 | >= 0 |
| `ro` | float, kg/m^3 | 1.225 | > 0 |
| `gravityEnabled` | switch | 0 | 1 / 0 |
| `gravityAccel` | float, m/s^2 | 9.81 | >= 0 |
| `gravityAngle` | float, deg CW from down | 0 | any finite |
| `gravityTilt` | float, deg | 0 | how far gravity is tipped out of the xy plane towards +z; 0 leaves it in the plane |
| `gravityMode` | name | `reduced` | `reduced` / `body`, see below |
| `convection` | name | `upwind` | `upwind` / `muscl` / `central` |
| `limiter` | name | `vanLeer` | `minmod` / `vanLeer` / `superbee`, only read by `muscl` |
| `timeScheme` | name | `euler` | `euler` / `rk2` / `rk3` |
| `bcLeft` `bcRight` `bcBottom` `bcTop` | name | `inlet` `outlet` `slip` `slip` | `inlet` / `outlet` / `wall` / `movingWall` / `slip` |
| `bcFront` `bcBack` | name | `slip` `slip` | the same five, at `z = 0` and `z = Lz` |
| `bcLeftSpeed` … `bcTopSpeed` | float, m/s | unset | what a `movingWall` slides at, or an inlet speed other than `U0` |
| `bcFrontSpeed` `bcBackSpeed` | float, m/s | unset | the same, for the two z sides |
| `inletFrom` `inletTo` | float | 0 / 1 | fraction of the face's **first** axis the inlet occupies |
| `inletFrom2` `inletTo2` | float | 0 / 1 | the same along the face's **second** axis; the window is a rectangle |
| `inletProfile` | name | `uniform` | `uniform` / `parabolic` / `parabolicSpan`. `parabolic` bends both axes (duct), `parabolicSpan` bends the first only (extruded channel) |
| `phases` | int | 1 | 1 or 2. At 2, `ro` and `nu` are ignored |
| `rho1` `rho2` | float, kg/m^3 | 1000 / 1.225 | the two fluids; 1 is what the start shape is made of |
| `nu1` `nu2` | float, m^2/s | 1e-6 / 1.5e-5 | their kinematic viscosities |
| `phaseInit` | name | `layer` | `layer` / `drop` / `column` / `file`. `drop` is a sphere at `nz > 1` |
| `phaseLevel` `phaseX` `phaseY` | float | 0.5 | fractions of the domain: height, width or centre |
| `phaseZ` | float | 0.5 | fraction of `Lz`: where the drop sits in depth. Only read at `nz > 1` |
| `initialPhaseFile` | path | empty | one fraction per cell, `(k*ny + j)*nx + i` order, `nx*ny*nz` of them. A single plane's worth is extruded through z |
| `vofScheme` | name | `hric` | `upwind` / `hric` / `cicsam`, only read when they do not mix |
| `mixing` | name | `immiscible` | `immiscible` / `miscible` |
| `diffusivity` | float, m^2/s | 1e-6 | how fast one spreads through the other, only read when they mix |
| `surfaceTension` | float, N/m | 0 | 0 is off and the whole curvature pass is skipped |
| `contactAngle` | float, deg | 90 | measured inside fluid 1 at a wall; < 90 means fluid 1 wets it |
| `sources` | list | empty | `x=0.5,y=0.2,z=0.5,r=0.05,rate=2,angle=90,elev=0,phase=1;...` — `z=` places it in depth, `elev=` aims it out of the xy plane |
| `caseType` | name | `channel` | `channel` / `cavity` / `shockTube`, presets that write all six sides at once |
| `lidSpeed` | float, m/s | 1.0 | how fast the cavity lid slides |
| `steadyTolerance` | float | 0 | stop early once the field stops changing; 0 runs the whole of `totalTime` |
| `CFL` | float | 0.5 | > 0, warns above 1 |
| `totalTime` | double, s | 10.0 | > 0 |
| `dtUpdateInterval` | int, steps | 5 | >= 1 |
| `dtSafety` | float | 0.9 | > 0, warns above 1 |
| `omega` | float | 1.85 | 0 < omega < 2 |
| `smootherOmega` | float | 1.15 | 0 < omega < 2 |
| `mgIterations` | int, V-cycles/step | 2 | >= 1 |
| `mgTolerance` | float, relative | 1e-4 | 0 < tol <= 1 |
| `mgMinCoarseSize` | int, cells/axis | 8 | >= 2 |
| `useCuda` | switch | 1 | 1 / 0, ignored on a CPU-only build |
| `saveInterval` | int, steps | 20 | >= 1 |
| `extraFields` | list | empty | `vorticity` (a vector in a volume, a scalar in a plane), `divergence`, `speed`, `objectId`, `density`, `source`, `curvature`, `nuT`, `wallDistance`, `strain`, comma separated |
| `frameState` | name | `slim` | how much of the run state goes into each frame: `slim` drops only what can be put back exactly (half of a compressible frame, nothing of an incompressible one), `minimal` also drops the packed face velocities (a sixth off an incompressible frame, and a continuation becomes close rather than exact), `full` writes everything down |
| `runName` | text | empty | frames go into `output/<runName>` instead of `output`; anything a folder name cannot hold is replaced. Only while `outputDir` is the default — a folder you named yourself is used as given |
| `vortices` | list | empty | `x=0.5,y=0.5,z=0.5,radius=0.05,strength=60,axis=z` — vortices put into the flow at the start, semicolon separated, see below |
| `outputDir` | path | `output` | created on the first frame, empty = current directory |
| `geometryFile` | path, `none` or `empty` | `none` | `none` is the verification circle (a sphere at `nz > 1`), `empty` is nothing at all |
| `sliceAngleX` `sliceAngleY` `sliceAngleZ` `sliceRotation` | float, deg | 0 | any finite; applied as Rz·Ry·Rx |
| `invertSection` | switch | 0 | 1 / 0 |
| `wallMotion` | list | empty | `<object>:rotZ=90,slideX=0.5,slideZ=0.2;<object>:slip=1` — also `rotX=` and `rotY=`; `rot=` is `rotZ=`. An object either moves or slips, see below |
| `amrLevels` | int | 0 | refinement levels over the base grid, compressible only, see below |
| `amrCriterion` | name | `everything` | `density` / `vorticity` / `species` / `body` / `everything` |
| `amrThreshold` | float | 0.2 | how steep a feature has to be, as a fraction of the steepest |
| `amrEvery` | int, steps | 8 | base steps between regrids |
| `amrBuffer` | int, cells | 2 | padding round the tagged region |
| `amrMinPatch` `amrMaxPatch` | int, cells | 8 / 64 | bounds on a patch side |
| `gridStretch` | name | `off` | `off` / `body` / `wake` / `edges`, compressible only, see below |
| `stretchRatio` | float | 1.05 | largest ratio between one cell and the next |
| `refineNear` | float | 0.25 | width of the fine band, as a fraction of the domain |
| `bodyMotion` | list | empty | `<object>:vx=0.2,vz=0.1,omegaY=45;<object>:free=1,mass=2` — also `omegaX=`, `pinZ=`, `pinRotX=`, `pinRotY=`, `inertiaX=`, `inertiaY=`; `omega=` is `omegaZ=` and `pinRot=` is `pinRotZ=`. Bodies that travel, in either regime, see below |
| `bodyCoupling` | name | `added` | `weak` / `added` / `strong`, only read by a free body |
| `bodyIterations` | int | 4 | most force/motion passes inside one step, only read by `strong` |
| `bodyCollisions` | switch | 0 | off, bodies pass through each other and through the walls |
| `bodyRestitution` | float | 0.2 | how much of the closing speed survives a bounce, 0 to 1 |
| `bodyForceReport` | switch | 0 | work the fluid force out for bodies whose path you set too |
| `regime` | name | `incompressible` | `incompressible` / `compressible` - which solver runs, see below |
| `gamma` | float | 1.4 | ratio of specific heats; 1.4 air, 1.667 helium, 1.3 steam |
| `R` | float, J/(kg K) | 287.05 | specific gas constant; 287 air, 2077 helium |
| `gamma2` `R2` | float | 1.667 / 2077 | the second gas, read only at `phases=2` |
| `T0` | float, K | 288.15 | reference temperature the inlet and the initial field are built at |
| `pInf` | float, Pa | 101325 | ambient pressure; what a subsonic outlet holds the flow to |
| `machInlet` | float | 0.5 | inlet speed as a multiple of the speed of sound there |
| `speciesMode` | name | `active` | `active` lets the composition set gamma and R, `passive` freezes them |
| `acousticFields` | switch | 0 | write the pressure fluctuation, SPL and pitch per cell |
| `acousticWindow` | float, s | 0.02 | how far back the running average looks |
| `acousticRef` | float, Pa | 2e-5 | the pressure that counts as 0 dB |
| `microphones` | list | empty | `x=0.5,y=0.2,z=0.5;x=1,y=0.5` - points that record p(t); `z=` is optional and is the depth |
| `micInterval` | int, steps | 1 | steps between microphone samples |
| `micAudio` | 0/1 | 0 | also write each microphone as a `.wav` |
| `micAudioRate` | int, Hz | 44100 | sample rate of those files |
| `micAudioSpeed` | float | 1 | timebase: 1 is real time, 0.05 is twenty times slower |
| `turbulence` | name | `none` | `none` / `smagorinsky` / `kOmegaSST`, see below |
| `Cs` | float | 0.17 | the Smagorinsky constant, 0 to 1; 0.1 is what a channel wants |
| `turbIntensity` | float | 0.05 | how turbulent the inlet is, as a fraction of its speed; only `kOmegaSST` |
| `turbLengthScale` | float, m | 0 | the biggest eddy coming in; 0 takes a tenth of `Ly`; only `kOmegaSST` |
| `profiles` | list | empty | `<file>@x=1,y=0.5,z=0.5,size=0.3,ay=30;<file>@x=3,y=0.5` — `z=` places it in depth, `ay=` is that model's own `sliceAngleY`. Several models at once, see below |
| `restart` | switch | 0 | 1 / 0 |
| `restartFile` | path | empty | a `.vtk` frame or the folder holding them |
| `addTime` | double, s | 0.0 | counts forward from the frame |

Duplicated keys are applied in order, so the last one on the line wins. On a
continuation every key given on the line is applied on top of the frame,
whatever its position - see *Continuing a run*.

**Everything the third dimension added, in one list**, so it can be diffed
against an older command line:

```
nz Lz bcFront bcBack bcFrontSpeed bcBackSpeed inletFrom2 inletTo2
gravityTilt phaseZ sliceAngleY
```

plus `z=` and `elev=` inside `sources`, `z=` and `ay=` inside `profiles`,
`rotX= rotY= slideZ=` inside `wallMotion`,
`vz= omegaX= omegaY= pinZ= pinRotX= pinRotY= inertiaX= inertiaY=` inside
`bodyMotion`, and a third coordinate in `microphones`. Every one of them is
optional and every default reproduces what the plane did.

### Parameter sweeps

The point of the mode: no keypress anywhere in here.

```powershell
foreach ($n in 0.01, 0.005, 0.002, 0.001) {
    .\install\bin\cfd_app.exe nx=256 ny=128 nu=$n totalTime=5 outputDir="out_nu_$n"
}
```

The exit code is 0 on a finished run and 1 on anything refused, so a sweep can
be stopped on the first bad line.

## Gravity

Off by default. Turning it on asks for two more numbers:

```text
Enable gravity? (0 = no, 1 = yes): 1
Enter gravitational acceleration (m/s^2, 9.81 on Earth): 9.81
Enter gravity direction (degrees clockwise from straight down: 0 = down, 90 = towards the inlet, 180 = up): 0
```

The direction is an angle rather than a vector for one reason: the flow
direction is the one thing in this domain that cannot be turned — the inlet is
always the left edge and the outlet the right one — so everything else turns
around it. `0` is straight down, and the angle runs clockwise from there, so
`90` points into the inlet, `180` is up, `270` runs along the flow. On the
command line it is the same three keys:

```powershell
.\install\bin\cfd_app.exe gravityEnabled=1 gravityAccel=9.81 gravityAngle=30
```

### Out of the plane: `gravityTilt`

`gravityAngle` keeps its exact meaning — degrees clockwise from down, inside
the xy plane — and a fourth key tips the whole vector out of that plane towards
+z:

```
planar = gravityAccel * cos(tilt)
gx = -planar * sin(angle)
gy = -planar * cos(angle)
gz =  gravityAccel * sin(tilt)
```

`gravityTilt=0`, the default, leaves `gz` at exactly nought and `gx`, `gy`
exactly what they were. `gravityTilt=90` pulls straight along +z and
`gravityAngle` then means nothing, because there is nothing left in the plane
for it to point at.

The reduced-pressure potential picks up the one term that follows:

```
phi(x, y, z) = gx*(x - Lx) + gy*(y - Ly/2) + gz*(z - Lz/2)
```

with the reference point at `(Lx, Ly/2, Lz/2)` — the 2D expression plus a term
that is zero whenever the tilt is.

**Measured.** A light drop released in a heavy fluid rises the same distance
whichever of the three axes gravity is turned to, **to 0.0001 %**. That is the
check that matters: there is nothing special about `y`, and if there were, this
is where it would show.

```powershell
"Fluid Solver.exe" gravityEnabled=1 gravityAccel=9.81 gravityTilt=90 nz=32 Lz=1
```

**Read the *Gravity* part of §4 before expecting it to do something.** The
short version: at constant density it cannot change the velocity field, only
the pressure. That is not a limitation of this implementation, it is what the
equations say.

### `gravityMode`, and why there are two of them

`reduced`, the default, is the shortcut that follows from the paragraph above:
the force never enters the solve at all, `p` carries only the dynamic part, and
the hydrostatic head is added back when the frame is written. At one density
that is not an approximation, it is exact, and it costs nothing.

`body` puts the force in the predictor where it physically belongs and solves
for the total pressure. On a single-density run it produces the same answer for
a little more work — a fluid at rest stays at rest to one part in a hundred
thousand of the head, which is what `ConservationTests` checks. It exists
because the shortcut stops being exact the moment two densities share a domain,
and the multiphase work has to have something correct underneath it. It also
means an open side is held at the head rather than at zero, so the outflow is
not driven by a pressure step that is not physically there.

```powershell
"Fluid Solver.exe" gravityEnabled=1 gravityAccel=9.81 gravityMode=body
```

## Boundaries

Every side of the domain has a name. There are **six** of them now: `bcFront`
at `z = 0` and `bcBack` at `z = Lz` join the four that were always there, and
they take the same five kinds and the same `bc<Side>Speed`. The defaults are
the channel every earlier version solved — `inlet` on the left, `outlet` on the
right, `slip` above, below, in front and behind — and at `nz = 1` the two new
ones close a volume one cell deep, which is the plane.

| Kind | What it does |
|---|---|
| `inlet` | fluid enters at `bc<Side>Speed`, or at `U0` when that was never given |
| `outlet` | fluid leaves; this is also where the pressure level is fixed |
| `wall` | no-slip: the fluid sticks to it and is dragged to a stop |
| `movingWall` | no-slip, but the wall itself slides at `bc<Side>Speed` |
| `slip` | free-slip: nothing crosses it, nothing rubs against it |

```powershell
"Fluid Solver.exe" bcBottom=wall bcTop=wall                  # a real channel
"Fluid Solver.exe" bcLeft=wall bcRight=wall bcBottom=wall bcTop=movingWall bcTopSpeed=1
"Fluid Solver.exe" nz=64 Lz=1 bcBottom=wall bcTop=wall bcFront=wall bcBack=wall
```

The third line is a duct: a square pipe with an inlet at one end and an outlet
at the other. It is the same channel as the first line with two more walls, and
that is the point — nothing about writing a case changed, there are just two
more sides to write.

The second line is a closed box, and a closed box has no side that fixes the
pressure. The operator then has the constants in its null space: the answer is
only defined up to one, and the solve says so and takes the mean off every
cycle instead of letting the level drift. Without that it does not converge at
all, which is why this had to exist before a cavity could.

An inlet does not have to occupy the whole side. `inletFrom` and `inletTo` cut
it down to a band measured from the low end of that side, and the rest of the
side behaves as a wall; `inletProfile=parabolic` bends the band into a parabola
carrying the same flow rate as the flat one.

```powershell
"Fluid Solver.exe" inletFrom=0.4 inletTo=0.6 inletProfile=parabolic
```

### The window is a rectangle now

A face of a volume has two tangential axes, so a band is not enough. `inletFrom`
and `inletTo` cut the window along the face's **first** axis; `inletFrom2` and
`inletTo2` cut it along the **second**. Which is which depends on the face, and
there is no way to guess it, so here it is:

| Face | first axis (`inletFrom`/`inletTo`) | second axis (`inletFrom2`/`inletTo2`) |
|---|---|---|
| `bcLeft`, `bcRight` | y | z |
| `bcBottom`, `bcTop` | x | z |
| `bcFront`, `bcBack` | x | y |

Both pairs default to 0 and 1, so an inlet that was a full side is still a full
side and an inlet that was a band across the plane is now that same band running
the whole depth.

```powershell
"Fluid Solver.exe" nz=64 Lz=1 inletFrom=0.4 inletTo=0.6 inletFrom2=0.4 inletTo2=0.6
```

That is a square jet a fifth of the face across, in the middle of the left side.

`inletProfile` has three values, and the two curved ones differ only in a
volume:

| Value | What it bends |
|---|---|
| `uniform` | nothing; flat across the window |
| `parabolicSpan` | a parabola along the first axis, flat along the second — a plane channel extruded through z |
| `parabolic` | both axes — duct flow |

At `nz = 1` the second axis has one cell and the two are the same thing, which
is why every old command line means exactly what it always meant.

The outlet is never told about any of this. It has no speed of its own: the
pressure solve works out what has to leave, so a band a fifth of the height
wide arrives at the far end spread over the whole of it, and what went in comes
back out to one part in a hundred million. What cannot work is fluid pushed
into a domain with nothing open, and that is now refused before the run starts
rather than reported as a divergence on every step to the end:

```
!!! the inlets push 1.000000 m^2/s of fluid in and no side lets any of it out.
    An incompressible fluid does not compress: no pressure field can take it,
    the projection cannot remove the divergence it makes, and the run would
    report the same leftover on every single step to the end.
    Open a side with bcRight=outlet (or whichever one the flow should leave
    by), or make the inlet a wall.
```

A band narrower than one cell gets the same treatment, because an inlet that
lets nothing in is a run that does nothing for however long you asked it to.

## Cases: the cavity, and stopping when it settles

Writing five keys to get four walls and a moving lid is exactly the kind of
thing that gets typed wrong once and debugged for an hour, so there is a name
for it:

```powershell
"Fluid Solver.exe" caseType=cavity lidSpeed=1 nx=64 ny=64 Lx=1 Ly=1 nu=0.01
```

`caseType=cavity` writes all **six** sides itself - wall everywhere and
`movingWall` on top at `lidSpeed` - and empties the domain, because a cavity
with the verification circle floating in the middle of it is not a cavity.
Anything you say after it still wins, so `caseType=cavity bcBottom=movingWall`
is a two-sided cavity and `caseType=cavity profiles=wing.obj@x=0.5,y=0.5` is a
cavity with something in it.

### The cube cavity

Add `nz` and the same preset closes the front and the back as walls too, and
what you have is the 3D benchmark:

```powershell
"Fluid Solver.exe" caseType=cavity lidSpeed=1 nu=0.01 ^
                   Lx=1 Ly=1 Lz=1 nx=64 ny=64 nz=64 ^
                   steadyTolerance=1e-5 totalTime=200
```

It lands where Ghia puts the primary vortex — same tables, same Re 100, the
centre plane of a cube rather than a square. `VolumeTests` runs it and fails if
it does not. A cube cavity is the case worth running first on a new build,
because a closed box has no open side to hide a mistake behind: nothing comes
in, nothing goes out, and every wrong sign shows up as motion that should not
be there.

`geometryFile=empty` is what does the emptying and can be asked for on its own.
`none` still means the verification circle, which is a body like any other and
was never a way of saying "nothing".

Re is `lidSpeed * Ly / nu`, so the line above is Re 100 - the case Ghia, Ghia
and Shin tabulated in 1982 and every solver since has been checked against.
`CavityTests` runs it at 64x64 and compares seventeen points down the vertical
centreline and seventeen across the horizontal one:

```
  steady at t = 17.85 s, 3250 steps, 64x64
       y   Ghia u    solver u |      x   Ghia v    solver v
  0.9766  0.84123    0.84454 |   0.9688 -0.05906   -0.06512
  ...
  worst interior miss: u 0.0065, v 0.0153
```

That is the first thing in this project with an answer that did not come out of
this project.

### `steadyTolerance`

A cavity has a steady state and reaching it is the whole point, but nobody knows
in advance how long that takes - the run above needed 17.85 s of simulated time
and `totalTime` was set to 200 to be safe.

```powershell
"Fluid Solver.exe" caseType=cavity steadyTolerance=1e-5 totalTime=200
```

Every ten steps the field is compared against the last snapshot, and the largest
velocity change per second is divided by whatever drives the case - the lid, or
`U0` on a channel. Under the tolerance, the run says so and stops:

```
Step 3250, t = 17.8528 s: the field is changing at 9.49436e-06 per second
against the driving speed, under the steadyTolerance of 1e-05.
  Steady state, stopping here.
```

The last frame is written either way, so a run that stops early is continued
exactly like one that ran out of time. Zero, the default, turns the whole thing
off and nothing about a normal run changes.

## Two fluids

    "Fluid Solver.exe" phases=2 rho1=1000 nu1=1e-6 rho2=1.225 nu2=1.5e-5 \
                       gravityEnabled=1 phaseInit=column phaseX=0.25 phaseLevel=0.75 \
                       bcLeft=wall bcRight=wall bcBottom=wall bcTop=outlet \
                       geometryFile=empty Lx=0.6 Ly=0.4 nx=96 ny=64

That is a dam break: a column of water three quarters of the way up a quarter
of the domain, air above it, one wall taken away at t = 0. `phases=1` is every
run written before this and none of the code below executes at all.

The domain carries a volume fraction per cell - 1 is fluid 1, 0 is fluid 2, and
in between is a cell the interface passes through. `rho` and `nu` come out of
that fraction per cell, so `ro` and `nu` stop being read the moment there are
two of everything.

### In a volume

The fraction is advected on all three axes, by the same scheme and the same
limiter, and the interface normal and the curvature are the full
three-dimensional ones rather than the in-plane pair extended by zero. What
that buys is checked against shapes that have an answer: a sphere of radius R
comes back with curvature **2/R to within 1%**, a plane comes back with zero,
and rotating the same interface about x, y or z gives identical numbers. The
last one is the check with teeth — an axis that is treated differently from the
other two shows up there and nowhere else.

Three smaller things follow from it:

- `phaseZ` places the start shape in depth, as a fraction of `Lz`, alongside
  `phaseX` and `phaseY`. It is only read at `nz > 1`.
- `phaseInit=drop` is a **sphere** at `nz > 1` and the disc it always was at
  `nz = 1`.
- `initialPhaseFile` takes a whole volume, `nx*ny*nz` fractions in
  `(k*ny + j)*nx + i` order. Hand it a single plane's worth and it extrudes that
  plane through z rather than refusing it, so a field painted for a 2D run is
  still a legal starting condition for a volume.

**Measured:** the phase volume in a closed box drifts by nothing over the run.
Not "a little" — the number comes back the number it started at.

### What the interface is carried by

    vofScheme=upwind|hric|cicsam

All three are algebraic: they choose a value for the fraction at each face and
carry it with the flow the projection just produced. None of them reconstruct
the interface as a line the way PLIC does, which is a large fraction of the cost
and most of the complexity.

`upwind` is what a plain convection scheme does to it, which is smear it over
ten cells inside a second - it is here to be compared against. `hric` and
`cicsam` both steer the face value downwind where the donor cell is half full,
which pushes the interface back together as fast as diffusion pulls it apart.
Both fade that back out where the interface lies along the flow rather than
across it (compressing it there tears a smooth surface into flotsam) and again
as the Courant number climbs, because neither is stable once the interface
crosses a whole cell in a step.

The step size is limited for that on its own, separately from the CFL number:
half a cell per step for the interface, whatever the momentum equation would
have allowed.

### What had to change underneath

Three things, and all three are the reason this branch is where it is in the
order rather than earlier.

**The projection stops being a constant coefficient problem.** It was
`grad^2 p = div(u*)/dt` and it is now `div((1/rho) grad p) = div(u*)/dt`, with
1/rho on every face, rebuilt every step. The multigrid has carried per-face
coefficients since the fundamentals branch for exactly this. The density on a
face is the harmonic mean of the two cells rather than the plain one: at a
thousand to one the plain mean of water and air is half of water, so a face
with air on one side of it would carry the pressure gradient of something five
hundred times denser than the air actually there.

**p stops being the kinematic pressure.** With 1/rho inside the operator, what
comes out is pascals, and the frame writes it as it is rather than multiplying
by `ro`. A continuation reads it back the same way, which is why the frame
carries `phases` in its header.

**Gravity has to be in the solve.** `gravityMode=reduced` adds the hydrostatic
head on output and never touches the velocity field, which is exact at one
density and is what the README has said since gravity went in. At two densities
it is not an approximation, it is the wrong answer: the difference in weight
between the two fluids is the only thing that moves either of them, and in
reduced mode it never enters. `phases=2` moves the mode to `body` on its own and
refuses to be moved back.

### What a run says about itself

```
Two fluids: 1 is rho 1000 kg/m^3, nu 1e-06 m^2/s; 2 is rho 1.225 kg/m^3, nu 1.5e-05 m^2/s.
  Density ratio 816.327:1, interface carried by hric, pressure solved with 1/rho on every face.
  note: past a hundred to one the pressure solve needs more V-cycles than a single fluid does.
```

and, if gravity was left off, that two fluids with nothing pulling on them are
two dyes rather than two phases.

### The pressure solve, and why it grew a Krylov iteration

A plain V-cycle hierarchy is a fine solver for a constant coefficient Laplacian
and a bad one across a jump of eight hundred to one. The coarse grids stop
representing the fine problem, the correction that comes back up is longer than
the error it was asked to remove, and the residual grows by a factor of four
every cycle until the field is NaN. That is not a tuning problem; it is what
bilinear interpolation across a discontinuity does.

Two things went in, both only when the face weights are not all one, so every
single phase run takes the path it always took and produces the same bits:

- the coarse grid correction is scaled by however much of it actually reduces
  the residual, which costs one extra operator application per level and turns
  divergence into convergence;
- the whole V-cycle became the preconditioner of a conjugate gradient
  iteration, which does not need the coarse grids to be right - only to point
  roughly the right way - and gets the length right by construction.

`PoissonTests` solves an 816:1 jump to 1e-6 in 27 iterations and fails if it
does not. Before this it did not converge at all.

The CUDA backend has both: a damped coarse correction and the same flexible CG
around the device V-cycle, so a two-fluid pressure solve stays on the card
instead of coming back to the host. Uniform coefficients still take the plain
V-cycle path they always did.

### Sources

    sources="x=0.5,y=0.2,r=0.05,rate=2,angle=90,phase=1"

A disc inside the domain that pushes fluid out of itself, for the cases where
the flow does not come in through a side. `rate` is the speed it leaves at,
`angle` is where it is aimed, `phase` is which fluid comes out. The divergence
the projection has to produce in those cells is the rate itself, so the
pressure carries the flow away in every direction and the momentum aims it.

What a source adds has to leave somewhere, exactly as an inlet's does, so a
case with one and no outlet is refused before the run starts. The `div` column
in the step lines takes the intended part out, so what it reports is still the
part the projection failed to produce.

### Checked against

`MultiphaseTests` runs three cases:

    stirred drop   started 0.125664 m^2, ended 0.125796, drift 0.105%
    still layer    spurious 2.7810e-02 m/s against 2.215 m/s if it fell (1.26%)
    dam break      |u|max 2.403 m/s against sqrt(2gh) = 2.426, front at 0.84 of the floor
                   volume 0.044898 m^2 from 0.045000, drift 0.226%

The first is the thing an algebraic VOF scheme actually loses, and it loses it
quietly - the interface just gets thinner every step until it is gone. The
second is the spurious current every two phase solver has, measured against the
speed the fluid would reach if it did fall. The third is the one number in a
dam break that needs no table: nothing in a collapsing column can be going
faster than something dropped from the top of it, and it gets to 99% of that.

## Surface tension

    "Fluid Solver.exe" phases=2 surfaceTension=0.072 contactAngle=60 \
                       rho1=1000 nu1=1e-6 rho2=100 nu2=1e-5 \
                       gravityEnabled=1 phaseInit=drop phaseLevel=0.3 \
                       geometryFile=empty Lx=0.02 Ly=0.02 nx=64 ny=64

`surfaceTension=0` is every run before this one and the whole path below is
skipped for it: no curvature pass, no extra force, no extra limit on dt.

The force is the CSF one, `f = sigma * kappa * grad c`, added to the predictor
in the same place gravity's body force already was, on the faces, so it is
differenced by exactly the discretisation the pressure gradient is - which is
what stops a drop sitting still from developing a circulation out of nothing.

### The curvature is a height function

The amount of fluid in a column of cells is a height, and the second derivative
of that height along the interface is the curvature. The column starts at seven
cells and grows until it brackets the surface at both ends, because a fixed
stack does not bracket an interface lying at 45 degrees and gives a curvature
that is somewhere between wrong and enormous.

In a volume the height is differenced along both directions across the column
rather than one, so what comes back is the sum of the two principal curvatures
— which is what `kappa` means and what the CSF force wants. That is not a
detail: a sphere of radius R has curvature **2/R**, not `1/R`, so the jump
`sigma * kappa` across it is twice what a circle of the same radius gives. The
measured curvature of a sphere is 2/R to within 1%, a plane comes back at zero,
and the answer does not change when the same interface is rotated onto x, y or
z. At `nz = 1` the second direction has one cell, contributes nothing, and the
curvature is the `1/R` of a circle exactly as before.

Taking the curvature from a smoothed gradient instead is the version that is
half the code, and it is a factor of ten worse: a drop that should sit still
boils on the spot. There is no simple version of this worth writing first,
which is why there is only one here.

Two things it needs and did not have:

- **The initial drop is supersampled.** A one-cell linear ramp around the
  circle is fine for advection and far too crude for a second difference. The
  perimeter ring is filled at 64x64 samples per cell, the interior and the
  outside by inspection, so the cost is a ring rather than a domain.
- **The gate is the gradient, not the fraction.** Cells strictly between 0 and
  1 are what an interface passes through - unless it was painted or started as
  a block, where it never is, and the whole force was silently zero. Gating on
  `|grad c|` instead found the block, took the Laplace error from 2.5% to
  0.18%, and cut the spurious current by about 350x on the 1000:1 case.

### The contact angle

`contactAngle` is measured inside fluid 1, at a solid wall. 90 is a wall
neither fluid prefers; below 90 fluid 1 wets it and climbs, above 90 it beads
off. It is applied by rotating the interface normal in the cells against the
wall before the curvature is taken, which is the cheapest thing that is still
the right boundary condition.

### The step size

Surface tension puts a wave on the interface, and the shortest one the grid can
hold has to be resolved in time or it grows:

    dt < sqrt((rho1 + rho2) * d^3 / (4 pi sigma))

with `d` the smallest of dx, dy and dz in a volume, and the smaller of dx and
dy in a plane — a third axis cannot make the limit looser, only tighter, and a
cell that is thin in z holds a shorter wave than the other two ever see. It is
a separate limit from the CFL number
and from the interface Courant number, and it is usually the one that binds -
on a millimetre of water against air it is microseconds. The solver says so at
the start rather than leaving you to wonder why it is slow:

```
  Surface tension 0.072 N/m, contact angle 60 deg. A drop a tenth of the domain
  across holds 36 Pa more inside than out.
  Shortest capillary wave the grid holds: 0.000192619 s per step.
  ...
  note: surface tension sets the step size here, not the flow and not the viscosity.
  The shortest capillary wave this grid can hold crosses a cell in 0.000192619 s,
  and that limit falls as dx^1.5: halving the cell size costs about three times
  the steps. It is not the solver hanging.
```

### Checked against

`SurfaceTensionTests` runs three cases:

    laplace     jump 14.4262 Pa against sigma/R = 14.4000 (0.18% off)
    spurious    1.314e-05 m/s against 0.1200 m/s of capillary wave (0.0001 of it)
    rounding    interface 0.01741 m -> 0.01610 m (7.5% less)

The first is the only closed-form answer two dimensions give you: the pressure
inside a circle is `sigma/R` above the outside, and nothing else. (Three
dimensions give you the matching one, `2 sigma/R` inside a sphere, and it is
the same arithmetic on the curvature checked above.) The second is
the failure mode of every CSF implementation - a drop that should be at rest
circulating because the force and the pressure gradient are differenced
differently - measured against the capillary wave speed rather than against
zero, because zero is not achievable and a ratio says how badly. The third is
the physics rather than the arithmetic: a square blob of water has no business
staying square.

## Fluids that mix

    "Fluid Solver.exe" phases=2 mixing=miscible diffusivity=1e-4 ...

Oil and water have a surface. Ink and water do not: there is nothing to
compress, nothing for tension to pull on, and the composition spreads instead
of staying sharp. `mixing=miscible` says so, and then:

- the compressive VOF scheme is not read at all - the composition is carried by
  the same limited MUSCL convection the momentum is, which is second order
  where it is smooth and does not invent a front;
- a Fickian term `D * grad^2 c` is added, explicitly, from the field as it was
  at the start of the step;
- `surfaceTension` is refused rather than ignored, because a surface tension on
  fluids that have no surface is a number that would quietly do nothing;
- `dt` picks up the diffusive limit `d^2 / (4 D)` alongside the others.

The check is the one closed-form answer diffusion gives: a step in composition
left alone spreads as an error function, and the slope at its middle is
`1/sqrt(4 pi D t)` whatever else is going on.

    mixing      steepest 28.015 /m against 1/sqrt(4 pi D t) = 28.209 (0.7% off)
                composition 0.020000 m^2 from 0.020000

## Several models at once

`geometryFile` still takes one model and centres it. `profiles` takes as many as
you like and puts each one where you say:

```powershell
"Fluid Solver.exe" "profiles=wing.stl@x=0.6,y=0.5,size=0.3;ball.obj@x=1.6,y=0.5"
```

The separator between the file and its settings is `@` rather than `:`, because
a Windows path already owns the colon. The settings are all optional: `x`, `y`
and `z` place the centre in metres, `size` is the larger side of its section in
metres, `rot` turns it in the plane, `ax`, `ay` and `az` are slice angles for
that model alone, and `invert=1` mirrors it. A file written with no `@` keeps
the old behaviour — centred in the domain at a fifth of its smaller side.

`z=` and `ay=` are what the third dimension added. `z=` is where the model sits
in depth and is centred when it is left out, which is what a plane run wants and
what it always silently did. `ay=` is that model's own `sliceAngleY`, so two
copies of the same file can sit in the same volume turned differently about all
three axes:

```powershell
"Fluid Solver.exe" nz=64 Lz=1 "profiles=wing.stl@x=0.6,y=0.5,z=0.3,ay=15;wing.stl@x=0.6,y=0.5,z=0.7,ay=-15"
```

Anything that lands on or outside the domain edge is refused before the run
starts, with the distance it missed by:

```text
!!! profile 'cube.obj' does not fit the domain:
    past the right edge by 0.170833 m
    it spans x 1.75..2.15 and y 0.3..0.7 in a domain of 2 x 1 m.
    Move it with x= and y=, or shrink it with size=.
```

A body touching the border is a wall rather than an obstacle, and a wall nobody
asked for through the middle of a case is worse than a message.

The bodies are numbered the same way they always were — flood-filled from the
mask in grid scan order — so `wallMotion` addresses them by the numbers
`printInfo` lists.

## Convection and time

`convection=upwind` with `timeScheme=euler` is what this solver has always done
and stays the default, so nothing about an existing run changes. The solver
prints how much numerical viscosity that costs on every start:

```text
note: upwind adds 3.125x more viscosity than the fluid has.
      The run behaves like Re 9.7, not 40.
```

`convection=muscl` reads one cell further in each direction and blends the
upwind difference towards the central one by however much the limiter allows.
Where the field is smooth that is second order; where it is not, the limiter
pulls it back to upwind rather than letting it overshoot. `central` does no
limiting at all and is the least dissipative and the least forgiving.

Second order in space with one stage in time is only conditionally stable, and
not by the CFL number anybody has in mind when they set it, so anything other
than `upwind` wants `timeScheme=rk2` or `rk3` under it. The solver says so
rather than letting it blow up on step one. Each Runge-Kutta stage is a whole
projection, so `rk2` costs twice a step and `rk3` three times; in exchange the
result is a convex combination of divergence-free fields, which is what keeps
it divergence free.

What it buys, measured on the same case by how much vorticity is still there at
the end — this is what `ConvectionTests` asserts:

| Scheme | Peak vorticity |
|---|---|
| `upwind` + `euler` | 1.20 |
| `muscl` `minmod` + `rk2` | 1.45 |
| `muscl` `vanLeer` + `rk2` | 1.75 |
| `central` + `rk3` | 2.01 |

## Extra fields

A frame carries pressure, the solid mask and velocity, and nothing else unless
asked. `extraFields` adds any of `vorticity`, `divergence`, `speed`,
`objectId`, `density`, `source`, `curvature`, `nuT`, `wallDistance` and
`strain` to every frame - and in the compressible regime `temperature`,
`mach`, `speedOfSound`, `entropy`, `pFluct`, `SPL` and `pitch` as well:

```powershell
"Fluid Solver.exe" "extraFields=vorticity,speed"
```

They cost four bytes a cell each and are written for ParaView and the UI to
read; nothing in the solver reads them back, and a frame carrying them still
continues exactly like one that does not.

**One of them changes shape with `nz`.** `vorticity` is a scalar in a plane —
there is only one component a plane can hold — and a **vector** in a volume,
written as three floats and glyphed by ParaView like any other vector. Anything
that reads frames has to cope with both, which is one reason the frame carries
a format version. `divergence` and `speed` simply gained their z term and stay
one float. Everything else on the list is unchanged in a volume.

Two fields are not on that list because they are not optional. The phase
fraction goes into every frame of a two-fluid run, and `k` and `omega` go into
every frame of a `kOmegaSST` run, whether or not anybody asked: the frame is
also the restart file, and a two-equation model that comes back with the inlet
values in it has thrown away everything the run spent its time building.

## Vortices put in by hand

    "Fluid Solver.exe" regime=compressible machInlet=0.3 nx=160 ny=160 \
                       "vortices=x=0.35,y=0.5,z=0,radius=0.0625,strength=40"

A vortex written into the initial field instead of waited for. Several are
separated by semicolons.

| | |
|---|---|
| `x` `y` `z` | where the axis of the tube passes through, in metres |
| `radius` | the distance from that axis at which the swirl is fastest, in metres |
| `strength` | that fastest swirl speed, in m/s; positive turns anticlockwise looking down the axis |
| `axis` | `x`, `y` or `z` — which way the tube runs. Default `z` |

`radius` and `strength` mean what they say: the swirl profile peaks at exactly
`radius` from the axis, and it peaks at exactly `strength`. Keep `radius` to at
least eight cells or the scheme smears the vortex away before it has gone
anywhere.

A compressible run gets the **isentropic vortex**, which is an exact steady
solution of the Euler equations: left alone in a uniform stream it is carried
along without changing shape. That is the point of it — how much of it survives
crossing the box is a direct measurement of the scheme's own dissipation,
against an answer that is known rather than guessed at. `CompressibleTests`
does exactly that: it refuses a vortex that has lost half its swirl or that has
somehow gained any, and it refuses one that has drifted off where the stream
should have carried it. The line it prints on the grid above is 40.3 m/s of the
40 it started with after thirty-nine cells of travel, centre within 0.3 cells.

An incompressible run gets the same velocity profile, which is divergence free
as it stands and so needs no projection to start from.

The core of a compressible vortex is colder and thinner than the air around it,
and there is a limit to how fast it can swirl before the middle would have to
have no temperature left at all. Past that the strength is reduced to the
ceiling and the run says on the console that it did.

## Bodies that travel

    "Fluid Solver.exe" "profiles=disc.obj@x=0.5,y=0.5,size=0.2" \
                       "bodyMotion=1:vx=0.4,omega=60" \
                       Lx=2 Ly=1 nx=96 ny=48 U0=0 \
                       bcLeft=wall bcRight=wall bcBottom=wall bcTop=wall

`wallMotion` and `bodyMotion` look almost the same and do opposite things.
`wallMotion` moves the SURFACE: the wall drags the fluid past it like a
conveyor belt and the body itself never goes anywhere. `bodyMotion` moves the
BODY, and then the mask is a different mask every step. Empty is every run
written before this and not one line of the code below executes.

Both regimes take it. The compressible solver used to refuse `bodyMotion` on
the grounds that it cut its mask once and kept it; it re-cuts it every step
now, the same way, and what a moving wall means to a gas with a finite speed of
sound is written up under **Bodies travel here too** in the compressible
section.

### A body in a volume is a rigid body, properly

In a plane a rigid body has three degrees of freedom and its orientation is one
number. In a volume it has six, and its orientation is not a number at all.
So:

**Orientation is a unit quaternion** and **inertia is a 3x3 tensor**, taken
about the body's own centroid and computed from the voxels it is actually made
of, alongside its volume. `integrate` carries **angular momentum** rather than
angular velocity — `L += tau dt`, then `w = I_world^-1 L` — including the
gyroscopic term, which is the one everybody drops and the one that makes the
motion right. A free body tumbling in a volume therefore does what a free body
does: spin it about its intermediate axis and **the tennis-racket flip
appears**, unasked for, out of the integrator.

At `nz = 1` the gyroscopic term is **identically zero**, the tensor has one
finite in-plane entry, the quaternion is a rotation about z and nothing else,
and the update reduces to exactly the scalar `theta`/`omega` one that was there
before. Not approximately — the terms are not there to round.

**Measured.** Torque-free tumbling conserves `|L|` to **3.6e-5** and the kinetic
energy to **8.2e-5** over 20000 steps. A dropped body falls at **9.80 m/s^2**
with no spurious drift. The first pair is the one that catches a wrong
`I_world`, because a wrong inertia tensor conserves neither; the second catches
a sign or a factor in the force, which the first would sail straight past.

### What `bodyMotion` gained

    "bodyMotion=1:vx=0.2,vz=0.1,omegaX=30,omegaY=45"
    "bodyMotion=1:free=1,density=2700,pinZ=1,pinRotX=1,pinRotY=1"

| Setting | Means |
|---|---|
| `vz` | velocity along z, next to `vx` and `vy` |
| `omegaX` `omegaY` | spin about x and about y, deg/s. `omega=` is still `omegaZ=` |
| `pinZ` | hold it still in z |
| `pinRotX` `pinRotY` | hold that rotation. `pinRot=` is still `pinRotZ=` |
| `inertiaX` `inertiaY` | the two moments of inertia the plane never needed. `inertia=` is the z one |

Everything else about the grammar is unchanged, keyframes included: `@<t>` opens
a keyframe, and a keyframe carries the new components the same way it carried
the old ones. A body given `pinZ=1,pinRotX=1,pinRotY=1` is a body confined to
the plane it started in, which is a useful thing to ask for in a volume and is
what a plane run is doing anyway.

Collisions gained the z walls and the z component of the contact impulse, so a
body can now bounce off the front and the back of the domain and off another
body at any angle, not only one in the plane.

### The mesh stops being a constant

The mask used to be cut once in the Mesh constructor and never touched again,
and `Solver` held a `const Mesh&` to say so. It does not any more. Every step
the outline is transformed by the body's pose, rasterised again, flood filled
again, and handed to the multigrid.

The numbering had to be made to survive that. Bodies are numbered by the scan
order of the flood fill, so as soon as one moves past another the numbers can
swap - and `wallMotion=2:rot=90` would quietly start spinning a different
object halfway through a run. Now each body remembers where it started, and
after every relabel the blob nearest to where a body was told to be keeps that
body's number. Every pair is scored by distance and claimed in order of
distance rather than in object order, so the first body cannot take a blob that
plainly belongs to the second. If two bodies touch and become one, the run says
so once and names the numbers that changed hands.

### Freshly uncovered cells, which is where this normally breaks

A cell the body has just left held the inside of a solid one step ago. There is
no velocity in it to carry forward, and whatever is there is not a velocity -
it is the mirrored value the no-slip stencil left behind. Leave it and the
divergence of that one cell is enormous, the projection spreads it over the
whole field in a single step, and the run looks like it exploded for no reason.

What belongs there is the velocity of the surface that just swept past, because
that is what the fluid touching it was moving at. Faces the body did not vacate
itself fall back to the mean of their open neighbours, and the pressure and the
phase fraction of a freshly opened cell are filled the same way - the pressure
because the multigrid reads it as its starting guess, the fraction because a
volume would otherwise appear out of nowhere.

The check is that it works: a disc driven across a closed box at 0.4 m/s holds
a divergence of 1.9e-06, which is float noise.

### The flux through a moving surface

A face with solid on one side is shut, and a shut face carries the wall's
velocity in `uWall`. The predictor and the corrector have always written
`mask * value + uWall` on every face, so putting the body's own velocity there
is at once the no-slip condition on a moving surface and the flux through it
that the projection has to account for. There is no separate source term: the
divergence of the cell next door picks it up because it is in `u*` already.

`wallMotion` deliberately does not take that path. There the surface slides and
the body stays put, the face is shut at zero, and every run written before this
one produces the same bits.

### The numbers are the mask's, not the order you listed the models in

Bodies are numbered by the flood fill, which walks the grid in scan order — `i`
inside `j` inside `k`. That is **not** the order `profiles=` lists them in, and
it is not left to right either: the body whose first cell the scan reaches is
found first, so two shapes side by side can come out either way round depending
on which one rasterised a cell lower — and in a volume, depending on which one
rasterised a cell nearer the front, since `k` is the outermost loop and the
scan reaches the whole of the first plane before it reaches any of the second.

The mesh prints the mapping before the run starts, and reading it is the whole
job:

```
  Number of objects = 2 (these numbers are what wallMotion takes)
    object 1: 98 cells, centre (1.29996, 0.5) m, rim 0.114603 m
    object 2: 88 cells, centre (0.5, 0.5) m, rim 0.107246 m
```

Here object 1 is the one on the **right**, even though it is the second entry
in `profiles=`. Write the line against those centres, not against the order you
typed. `MovingBodyTests` checks that the mask agrees with what each body says
about itself, because a pose that has drifted away from the cells it is made of
is the one failure a pose alone can never show.

### Letting go of it: free=1

    "bodyMotion=1:free=1,density=2700,pinX=1,pinY=1"

Give it a `mass`, or a `density` and let its own size do the arithmetic — the
area it covers in a plane, where a mass is per metre of depth, and the
**volume** it fills in a volume, where a mass is a mass. `inertia` left out is
taken as `m*r^2/2`, which is what a disc of that rim has; in a volume the
labelling pass has already worked out the body's real inertia tensor about its
own centroid from the cells it is made of, and `inertiaX` and `inertiaY` are
there to override the other two diagonal entries the way `inertia` overrides
the z one.

`pinX`, `pinY`, `pinZ`, `pinRot` and its `pinRotX`/`pinRotY` siblings each hold
one degree of freedom still, so a cylinder free to spin but not to drift is
`free=1,pinX=1,pinY=1,pinZ=1`, and a body confined to the plane it started in
is `free=1,pinZ=1,pinRotX=1,pinRotY=1` — which is what a plane run is doing
anyway, and is the useful thing to ask for when you want a volume's flow around
a body that is not allowed to wander out of the mid-plane.

The force is the pressure and the shear integrated over the surface, and the
surface of a staircase body is exactly the set of faces the mirror pass already
walks, so it is a sum over faces rather than a contour that has to be
reconstructed. With gravity in the solve the pressure integral already carries
the buoyancy and only the weight is left to add; under the reduced formulation
`p` holds no head at all and the whole submerged weight goes in by hand.

### Why there are three couplings, and why the default is not the obvious one

Compute the force, move the body, recompute the flow, repeat. That is the
obvious scheme and it is unstable, and not in a way a smaller step fixes.

A body accelerating through a fluid has to accelerate some of the fluid with
it. That entrained fluid pushes back in proportion to the body's own
acceleration - an added mass, comparable to the mass of fluid the body
displaces. Evaluate that force one step late and you have a feedback loop whose
gain is the ratio of added mass to body mass. Above about one it diverges, and
halving `dt` does not reduce the gain at all: it is a property of the splitting,
not of the discretisation.

    bodyCoupling=weak      the obvious scheme, here to be compared against
    bodyCoupling=added     the added mass on the left hand side (default)
    bodyCoupling=strong    force and motion iterated until they agree

`added` is one line: divide by `m + m_added` instead of by `m`. That is where
the gain came from, so that is where it is removed, and it costs nothing.
`strong` rewinds the flow and the bodies to the start of the step and redoes it
with the velocity the last pass predicted, until the two agree - a fixed point
of the whole step rather than of a linearisation of it. It costs `bodyIterations`
pressure solves and is what a body lighter than the fluid actually needs.

The three are measured against each other on a disc twice as dense as the
fluid, where the added mass is half the body mass:

    weak -0.6536, added -0.5552, strong -0.5331 m/s

`strong` is the reference. `added` sits 4.1% from it for the price of a
division; `weak` is 22.6% out, and that is at a density ratio of two. The test
fails if `added` drifts past 5% of `strong`, and it also fails if `weak` gets
as close as `added` does - a case where the cheap scheme happens to be right is
a case that is not testing anything.

### Letting go part way through, and taking it back

    "bodyMotion=1:@0,vx=0.6,@0.15,free=1,density=1200"

`free` is a keyframe setting like any other, so a body can be driven along a
path and then released, and released and then taken back under control:

    "bodyMotion=1:@0,vx=0.6,@0.15,free=1,@0.9,free=0,vx=0,density=1200"

The velocity carries across the switch either way, so nothing jumps: a body
driven at 0.6 m/s and let go at 0.15 s starts its free flight at 0.6 m/s and
then slows down, because from that moment nothing is pushing it.

    released      driven at 0.600, let go at 0.15 s, down to 0.4887 m/s by 0.4 s

Coming back the other way is a step change in the boundary condition rather
than a discontinuity in the state, the same as turning `wallMotion` on mid-run.

### A body whose path you set never deviates from it

That is the whole meaning of the word, and it holds whatever the fluid does.
`bodyForceReport=1` works the force out anyway and puts it in the step line:

```
    body 1 at (0.419, 0.5) m, v = (0.3, 0) m/s, fluid pushes (0.2347, 0) N/m, ignored
```

The velocity stays exactly what the timetable says. The force is there to be
read, to be integrated into a drag coefficient, or to be looked at just before
the body is released and the same force starts to matter. It is off by default
because a body on rails does not need it and it costs a pass over the surface
every step.

### Bodies that bounce

    bodyCollisions=1 bodyRestitution=0.6

Off - the default, and what every run before this branch did - bodies pass
straight through each other and through the domain walls. That is fine while
nothing can meet anything, and it is the honest default because turning it on
changes the answer.

On, contact is read straight off the mask. Each body is rasterised on its own
before they are put together, which is the only moment the grid knows who
claimed what: the flood fill that comes next merges anything touching into one
blob, and that merge is exactly where the information a contact is made of gets
lost. So contact is exact for any shape rather than a circle drawn round it.

`bodyRestitution` is how much of the closing speed survives: 0 stops dead, 1
comes back at the speed it arrived. A body whose path you set is unmovable by
construction, so a free body hitting one takes the whole impulse - which is
what "prescribed" has meant all along.

    collisions    off 0.3976 m/s, on -0.8686 m/s - the second one met something

### Keyframes

    "bodyMotion=1:@0,vx=0,@1,vx=0.5,@2,vx=0"

`@<t>` opens a keyframe at t seconds and everything after it belongs to that
keyframe. Times go forwards; between two of them the velocity is interpolated,
before the first and after the last it is held.

Sampled at the start of each step this is a left Riemann sum and loses half a
step off every ramp. It is sampled at the middle of the step instead, which
integrates a straight line exactly - and a pair of keyframes is a straight
line. A body ramped from 0 to 0.5 m/s over 0.2 s and then held moves 0.15001 m
in 0.4 s, against 0.15 exactly.

**A keyframe carries the new components too.** `vz`, `omegaX` and `omegaY` are
settings like any other, so they belong to the key they are written on and are
interpolated between keys the same way `vx` always was:

    "bodyMotion=1:@0,vx=0,vz=0,@1,vx=0.5,vz=0.2,omegaY=90,@2,vx=0,vz=0,omegaY=0"

A component left out of a key is not zero there — it is whatever the
interpolation between the keys that do mention it says, which is the rule the
plane keys already followed and the reason a 2D timetable means what it always
meant when you run it in a volume.

### How a segment is shaped

    "bodyMotion=1:@0,vx=0,interp=bezier,@1,vx=0.5,@2,vx=0"
    "bodyMotion=1:@0,vx=0,interp=elastic,ease=out,@1,vx=0.5"

`interp` belongs to the key it is written on and governs the segment that
starts there, which is how a 3D package reads it too. The set is the same one:

    constant     hold this value until the next key
    linear       a straight line, and the default
    bezier       a cubic with auto-clamped handles
    sine quad cubic quart quint expo circ back bounce elastic

`ease=in|out|inout|auto` says which end of the segment the easing happens at.
`auto` is `out` for the ordinary easings and `in` for `back`, `bounce` and
`elastic` - the three that overshoot read better starting from the key.

**Auto-clamped** is the part worth spelling out. The tangent at a key is the
slope through its two neighbours, except at a turning point, where it is zero.
Without that clamp a curve that goes up and then flattens bulges past the value
it was told to reach, and a body would travel faster than any number you wrote.
With it, it cannot:

    interpolation constant 0.1979, linear 0.3000, bezier 0.3167, quad 0.3334,
                  bounce 0.2622 m

all against a ramp whose peak, held for the whole run, would give 0.4. The test
fails if any of them passes that.

### Sources that ride

    "sources=x=-0.14,y=0,r=0.06,rate=3,angle=180,body=1"

`body=<n>` puts the source in that body's own frame with the origin at its
centre, so a thruster stays on its nozzle however far the body has travelled or
turned. The jet is turned into the domain frame every step, and the reaction -
`rho * Q * v`, the momentum leaving per second - goes onto the body.

In a volume that turn is the body's **orientation matrix** rather than one
angle, so a thruster on a tumbling body stays bolted to the same patch of hull
through the tumble. The source itself gained the two settings it needed to be
placed and aimed in three dimensions: `z=` is where it sits in depth, and
`elev=` is how far its jet is tilted out of the xy plane, in degrees, next to
the `angle=` that aims it inside the plane.

    "sources=x=-0.14,y=0,z=0,r=0.06,rate=3,angle=180,elev=20,body=1"

Both default to what a plane run means — `z=` to the middle of the domain,
`elev=0` to a jet that stays in the plane — so every source line written before
this does the same thing it did.

A prescribed body feels that force too. It is computed and reported and then
its trajectory ignores it, which is what "prescribed" means: you said where it
goes, so that is where it goes, and the arithmetic is there for whatever reads
it. Under `free=1` the same force is what drives it.

A source that lands entirely inside its own body covers no fluid cell and emits
nothing, so it pushes nothing either. That is one line and it is the difference
between a rocket and a reactionless drive.

### Continuing one

A continuation normally takes the mask straight out of the frame, which is
exact and needs no model at all. A run whose bodies travel cannot: that mask is
a rasterised copy of wherever they had got to, and moving them on from it means
cutting the outline again. So that one case rebuilds the geometry from the
model and puts the bodies back at the pose the frame carries. Split a run in
half and the halves end at the same pose and the same mask, to the bit.

That pose used to be a fixed run of seven numbers per body. It is a **key/value
record** now, because a quaternion, a third position and three more velocity
components do not fit in seven numbers and because the next thing a body grows
will not fit in whatever number replaces it. `bodyState` carries `z`, the
quaternion and the extra velocity components alongside everything it carried
before, and the reader still accepts the old seven-number form, so a frame
written by an earlier build continues exactly as it used to.

### Checked against

`MovingBodyTests` runs six cases:

    prescribed    moved 0.200000 m against 0.200000 exact, div 1.907e-06
    keyframes     moved 0.15001 m against 0.15000 from the ramp it was given
    numbering     closing at 0.12 and -0.12 m, and the mask agrees
    coupling      weak -0.6539, added -0.5551, strong -0.5328 m/s
    neutral       0.0017 m/s after 0.3 s against 2.943 falling free (0.06%)
    thrust        jet on the body 0.5148 m/s, the same jet bolted down 0.0539
    carried       let go in a 1 m/s flow, reached 0.6637 m/s downstream
    released      driven at 0.600, let go at 0.15 s, down to 0.4887 by 0.4 s
    collisions    off 0.3976 m/s, on -0.8686 m/s

`carried` is the one that earns its place least obviously and caught the most.
Every other force case here is vertical, and the vertical faces and the
horizontal ones take the outward normal from opposite sides - so a sign error
in the horizontal force survives a falling test, a buoyancy test and a
spurious-current test untouched. It showed up as a body let go in a flow
accelerating away from the flow instead of being carried by it, and as a body
released mid-run speeding up with nothing pushing it. `thrust` is measured
against the same jet bolted to the domain rather than against an estimate,
because the difference between the two runs is exactly the term the feature
adds and nothing else.

`neutral` is the one that looks like nothing and is not. A body weighing
exactly what it displaces, under gravity in the solve, has to have the pressure
integral over its surface cancel its own weight to five significant figures -
and it does, to 0.06% of what free fall would have given it. A sign error
anywhere in the surface integral or the buoyancy shows up there as a body that
takes off.

## The compressible solver

    "Fluid Solver.exe" regime=compressible machInlet=2.5 \
                       "profiles=wedge.obj@x=0.75,y=0.07,size=1,attach=1" \
                       nx=240 ny=120 Lx=1.2 Ly=0.6 \
                       bcLeft=inlet bcRight=outlet bcBottom=slip bcTop=outlet

`regime=incompressible` is the default and is every run written before this
one, down to the last bit. `regime=compressible` is not an extension of it. It
is a second solver, sharing the geometry, the boundaries, the frame format and
the UI, and sharing none of the numerics.

### Why it had to be a second solver

The projection method is built on `div(u) = 0`. That is not a simplification
sitting on top of it, it is the assumption the whole method is derived from:
the pressure exists to enforce it, the Poisson solve computes it, and the
multigrid exists to make that solve fast. Take the constraint away and there
is nothing left of the method to keep.

So there is no pressure solve here at all. `Multigrid.cpp` and
`MultigridCuda.cu` - 1450 lines, half of them CUDA - are not called once in a
compressible run, and not one line of either changed for this. The riskiest
code in the project sat this branch out entirely.

What runs instead: the conservative variables rho, rho*u, rho*v, **rho*w** and
rho*E in the cell centres, MUSCL reconstruction of the primitives with the same
limiters `convection=muscl` uses, an HLLC flux at every face, and SSP-RK3 in
time. The step size comes from `|u| + c` rather than `|u|` alone, which is the
whole difference: a sound wave now takes a finite time to cross a cell, and the
step has to see it.

`rhoW` is what the third dimension added to the state. The x and y fluxes gained
their `w` row and a whole **z flux** appeared beside them, using the same
reconstruction, the same limiter and the same Riemann solver — there is one
HLLC in this codebase and all three axes call it. Microphones take a third
coordinate. The stretched grid grew its z axis, and `gridFaceZ` goes into the
frame next to `gridFaceX` and `gridFaceY` so a continued run lands on the grid
it left rather than a regenerated one.

**Measured.** A shock tube run in a volume stays one-dimensional to **7e-7** of
the density, produces **1.8e-5** of transverse momentum, and reproduces the
plane run's profile to **3.9e-5**. A shock tube is the right test for this
because it has no business knowing about y or z at all: any number the
transverse directions produce is a number the code invented.

### It is written on a block, and that was deliberate

`SolverCompressible` never reads `cfg.nx`, `cfg.ny` or a global array. Every
kernel takes a `Block` - sizes, spacings, origin, field pointers, ghost width -
and reads everything from it. That costs about five percent more effort to
write and it is the reason branch 8 will not begin by rewriting branch 7:
a refined patch is another `Block`, and the same kernels run on it unchanged.

The physics lives in `CompressibleKernels.hpp`, marked so it compiles for both
the host and the device, and the CPU sweep and the CUDA one call the same
functions. Two hand-kept copies of a Riemann solver drift apart; there is only
one here.

The marking is a single macro, `CFD_HD`, which expands to `__host__ __device__`
under nvcc and to nothing everywhere else. It has to be carried all the way
down: a `CFD_HD` function that calls an unmarked one compiles on the host and
silently loses the device path, and nvcc says so with `#20011-D` /
`#20014-D`. Those diagnostics are warnings, not errors, so a CUDA build that
looks green can still be wrong - `GasModel::gammaOf` and
`GasModel::gasConstantOf` are marked for exactly this reason, and the ghost
kernels reach them through the free `cfd::gammaOf(gas, y)` /
`cfd::gasConstantOf(gas, y)` wrappers that take the model by reference. If the
CUDA row ever prints a `#20011-D`, something is calling host code from a
kernel, and it is worth stopping to find out what.

### Boundaries are characteristic, and that is not decoration

A wall is a mirrored ghost state, slip or no-slip. An inlet imposes the density
and the velocity from `machInlet` and `T0` while the flow is subsonic and holds
the pressure too once it is not - because above Mach 1 nothing travels back out
of the domain to tell the boundary what the interior wants. An outlet is the
mirror image: it holds `pInf` while subsonic and says nothing at all once the
flow leaves faster than sound.

Inside the domain, a face between a solid cell and a fluid one gets the wall
flux written down rather than solved for: zero mass, zero energy, pressure in
the normal momentum and nothing else. A mirrored state does give HLLC exactly
zero mass flux - but only before the reconstruction, which uses each cell's own
neighbours and breaks the symmetry the cancellation depended on. What leaks
through is small, and it is mass through a wall, which is the one error that
has nowhere to go but up.

### Two gases

`phases=2` in this regime means two gases rather than two liquids, and they
always mix: there is no interface to carry. `gamma2` and `R2` are the second
gas, and the transported mass fraction sets the mixture's own gamma and R
through its heat capacities. That is not bookkeeping - it is the answer to the
question everybody asks first. Helium at the same pressure and temperature
carries sound 2.9 times faster than air, and it comes out of the run at 2.935
against the 2.935 the gas constants give, with nothing in the code that knows
about helium.

`speciesMode=passive` turns that off: the fraction still rides along, but gamma
and R stay frozen at the first gas. It exists to be compared against, and it is
easy to leave on by accident, so the solver prints a four line warning about it
in the configuration it echoes before the run starts.

### The sound it makes

Two independent halves, each switched on by itself.

**`acousticFields=1`** writes the sound onto the grid. Every cell keeps a slow
running mean of its pressure - `acousticWindow` sets how slow - and the
fluctuation about it becomes `pFluct`; its RMS becomes `SPL`, in decibels
against `acousticRef`; and the rate at which the fluctuation changes sign
becomes `pitch`, in hertz. The mean for the level and the mean for the pitch
are deliberately different speeds: the level wants a slow one, so everything
the flow is doing counts as sound, and the pitch wants a fast one, or the sign
of the fluctuation is decided by whatever the slow mean has not caught up with
yet and the crossings get counted at the rate the mean drifts.

Zero crossing is a crude pitch estimator and this README is not going to
pretend otherwise: it reports the rate of the largest thing happening and it is
fooled by broadband noise. It is also free, it works per cell, and you can look
at it.

**`microphones=x=0.5,y=0.2,z=0.5;...`** is the accurate half. Each point records
the pressure every `micInterval` steps and the run writes `microphones.txt`
beside the frames: the whole trace, then a level and a peak frequency for each
point found by scanning 512 bins with Goertzel. Not an FFT, because the sample
count is whatever the time step happened to give and is never a power of two,
and scanning fixed bins costs bins*samples with no padding, no window artefacts
and no library.

A microphone takes a third coordinate now. It is optional and defaults to the
middle of the depth, so a line of microphones written for a plane run still
lands where it always did — and in a volume you can put one off the mid-plane
and hear the difference, which is the whole reason anybody runs a volume.

On a closed 0.34 m tube rung by a pressure step the field reports 179 dB and
749 Hz, the microphone 177 dB and 1398 Hz, against a 500 Hz fundamental. Two
different estimators of a signal made of bouncing shocks, and neither is lying.

### And you can listen to it

`micAudio=1` writes `microphone1.wav`, `microphone2.wav` and so on next to the
frames - one mono 16-bit file per microphone, the same trace `microphones.txt`
holds, in a format anything will play.

Three things happen to the trace on the way in, and each of them matters.

**The mean comes off.** What is in the file is the fluctuation, not the
absolute pressure. A 101325 Pa DC offset in a signed 16-bit sample is silence
with a clipped rail on it.

**It is box-filtered down, not decimated.** The run samples every
`micInterval` steps, which at a compressible time step is usually somewhere
between 100 kHz and 10 MHz. Throwing away every sample but the 44100th folds
everything above 22 kHz straight back down into the audible band as a screech
that was never in the flow. Each output sample is therefore the average of
every input sample inside its own window, which is a box low-pass and an
anti-aliasing filter at the same time, and costs one pass. Where a window
happens to hold no input sample - a rate above the run's own - it interpolates
between the two nearest instead.

**It is peak-normalised** to 0.9 of full scale — and to 0.9 of full scale
across the whole set, not one file at a time. Normalising each microphone to
its own peak makes the quiet one as loud as the loud one, which throws away the
only thing several microphones were put out there to measure. One gain is
worked out from the loudest of them and every file gets it, so a mic in the
shadow of the body sounds quieter than a mic in front of it, because it was.
The run prints each one's peak in pascals and in decibels, and marks the one
that set the scale:

```text
  mic 1 -> microphone1.wav, peak 412.6 Pa (146.3 dB)  <- sets the scale for all of them
  mic 2 -> microphone2.wav, peak 88.4 Pa (132.9 dB)
All files share one gain, so they can be compared by ear.
```

A `.wav` has no absolute level of its own — the speakers decide that — so that
line is the only place the real loudness lives.

`micAudioSpeed` stretches the timebase without touching the simulation. 1 is
real time. 0.05 plays it twenty times slower and divides every frequency by
twenty with it, which is how you hear two milliseconds of shock tube, and how
you bring a 40 kHz whistle down to where ears are. Two milliseconds at real
speed is a click; at 0.05 it is 40 ms and it has a pitch.

    "Fluid Solver.exe" regime=compressible caseType=shockTube ^
                       nx=340 ny=8 Lx=0.34 Ly=0.04 geometryFile=empty ^
                       "microphones=x=0.05,y=0.02" micInterval=1 ^
                       micAudio=1 micAudioSpeed=0.05 totalTime=0.02

### What it costs

On 256x128, an empty domain at Mach 0.6, two cores of a 2.1 GHz Xeon:

| | per step | steps for 4 ms of flow | total |
|---|---|---|---|
| incompressible, muscl + rk2 | 4.6 ms | 171 (for 600 ms) | - |
| compressible, one gas | 5.8 ms | 1007 | 5.8 s |
| compressible, two gases, same properties | 7.6 ms | 1007 | 7.6 s |
| compressible, air and helium | 16 ms | 3722 | 59 s |

A step costs about a quarter more than an incompressible one, and the step
itself is six times smaller, because `|u| + c` is six times `|u|` at Mach 0.6.
That is not an implementation cost, it is what solving for sound means: the
scheme has to see a wave cross a cell.

The three two-gas numbers are worth separating, because the obvious reading of
the last one is wrong. Carrying a fifth variable costs **31%** - that is the
second row against the first, two gases with identical properties. Everything
past that is the flow, not the code: helium carries sound 2.9 times faster than
air, so the step shrinks with it, and a real contact discontinuity makes the
limiter's extremum test unpredictable across most of the domain, which is a
branch misprediction per face and not something an implementation can decline
to pay.

At scale the sweep settles at about 150 ns per cell-step - 512x256 gives
149.7 ns, 256x128 gives 165 ns, and the gap is startup rather than cache. On
two 2.1 GHz cores that is roughly 105 cycles per Riemann problem, which is what
seven divisions and two square roots cost when they are latency bound.

OpenMP gives 1.80x on two cores. **AVX2 measures no difference at all**, and
this README would rather say why than pretend. GCC does vectorise: it reports
31 vectorised loops and a great deal of SLP inside the kernels, all of it the
five flux components being done together. What it cannot vectorise is the part
that costs - the divisions and the square roots are per FACE, one at a time,
and there is no way for the compiler to find eight faces to do at once through
a function call. That would need the face loop written with intrinsics so that
eight Riemann problems are solved side by side and the divisions become one
`vdivps`, which is where the remaining factor lives and is a job to do with a
profiler open rather than on the way past.

Two things were worth doing on the way past and were done. The primitives are
computed once per cell into six arrays rather than at every face - each cell is
read sixteen times by the reconstruction stencils, and deriving rho, u, v, p, y
and gamma each time cost a division and a gamma per read. And HLLC took eleven
divisions in its obvious writing, four of which were the same two reciprocals
used twice each. Together: 15% off one gas, 25% off two.

The multigrid is not called, so `omega`, `smootherOmega`, `mgIterations`,
`mgTolerance` and `mgMinCoarseSize` do nothing here. They are still accepted:
they are part of the argument contract the UI has always sent, and refusing
them would break every launcher for no gain.

### Bodies travel here too

`bodyMotion` is not incompressible-only any more, and it did not need a
different grammar: the same `<object>:vx=0.2,omega=45`, the same keyframes, the
same interpolations, the same `free=1` with a mass. What changes is what a
moving wall means when the fluid has a finite speed of sound.

The mask is re-cut from the model every step, exactly as the projection solver
does it, and then two things happen that only matter here.

**The solid ghosts mirror about the body's velocity, not about zero.** A static
wall reflects the fluid velocity; a moving one reflects the velocity *relative*
to itself and adds its own back. That single change is the whole of the
physics: the reconstruction on the fluid side sees a ghost that is moving, the
pressure at the wall face rises ahead of it and falls behind, and a body that
moves fast enough makes a wave in front of it that leaves and keeps going.
Put a microphone downstream of an accelerating body and it will hear it.

**A cell the body has just left is reseeded** from its fluid neighbours -
density, pressure and composition averaged, velocity set to the body's own,
because that is what the gas there was doing a moment ago. Without it a newly
uncovered cell holds whatever the solid fill last wrote, and a state that was
never a state is exactly the kind of hole this solver falls into.

The force on a free body is the pressure integral over its own faces, one term
per fluid-solid face, with the arm taken from the body's centre for the torque.
There is no viscous part because there is no viscous term; at the speeds this
solver is for, pressure is the force anyway.

The pressure at a wall face is no longer just the pressure of the cell next to
it. It is that pressure plus `rho*c*(closing speed)`, where the closing speed
counts the fluid moving toward the wall and the wall moving toward the fluid
together - the acoustic piston relation, and the first-order Riemann solution
at a moving wall. For a wall that does not move and a flow that is not running
into it, the term is zero and this is exactly what 0.9 already did. For a body
driven at Mach 0.25 through still air it is the whole point: the test measures
101479 Pa in front of the disc against 99477 Pa behind it, compression ahead
and rarefaction in the wake, which is what a piston does.

Two things are worth knowing before trusting a number out of this:

- **Mass is not conserved to the last bit near a moving body.** The wall flux
  still carries no mass, in the grid frame, and the volume the body sweeps is
  handled by re-cutting the mask and reseeding. That is the standard cheap
  moving-immersed-boundary arrangement, and the piston term above is a
  linearisation, so both are right to first order in the wall Mach number. A
  body moving at a hundredth of the speed of sound will not show it; a piston
  at Mach 0.5 will.
- **On a GPU it costs a round trip per step.** The mask, the body velocities
  and the reseeded cells all live on the host, so a compressible run with
  moving bodies syncs the fields down and back every step. The run says nothing
  about it and it is not wrong, only slower - and only when bodies actually
  move.

### Cells do not have to be the same size

    "Fluid Solver.exe" regime=compressible geometryFile=wing.obj ^
                       nx=400 ny=200 gridStretch=body stretchRatio=1.05 ^
                       refineNear=0.3 machInlet=0.8

Every run before this one had one `dx` and one `dy` for the whole grid. The
compressible solver does not any more: `Block` carries a width per column and a
height per row, and the far field costs what it is worth instead of what the
body needs.

`gridStretch=body` finds what the mask holds, keeps a band `refineNear` wide
around it at full resolution, and grows the cells outside it by `stretchRatio`
each step. `wake` does the same but lets the fine band run to the downstream
edge. `edges` puts the fine cells against the walls instead, which is what a
duct wants. `off` is the even grid and is still the default; nothing about a run
that does not ask for stretching changed, down to the bit.

**The part that is easy to get wrong.** A MUSCL reconstruction takes a slope
and extrapolates it half a cell to the face. Written the obvious way that is

    face = centre + 0.5 * limit(centre - back, forward - centre)

and the `0.5` is doing two jobs at once: it is half of *this* cell, and the
differences it is limiting are implicitly *per cell*. On an even grid those are
the same number. On a stretched one they are three different numbers, and the
scheme quietly stops being second order. What is written here instead is

    face = centre + (width/2) * limit((centre - back) / hBack,
                                      (forward - centre) / hForward)

with `hBack` and `hForward` the real distances between cell centres. The
limiters are all homogeneous of degree one, so on an even grid this collapses
to exactly the old expression - the same floats in the same order, which is why
turning stretching off is not merely close to 0.9 but identical to it.

**CUDA and gridStretch do not go together, and the run says so rather than
pretending.** The device kernels carry one cell size per axis and the stretched
metrics live on the host; a GPU run would quietly solve an even grid instead of
the one that was asked for. Asking for both prints four lines and stays on the
CPU.

The divergence is finite volume and needs nothing else: `(F_right - F_left) /
width` is conservative on any grid. `dt` takes the local cell, so the smallest
cell sets the step - which is the price of stretching and the reason
`stretchRatio` has a ceiling.

**Frames come out as `RECTILINEAR_GRID`** with the face coordinates written
out, which ParaView opens natively. The frame also carries `gridFaceX`,
`gridFaceY` and — since the port — `gridFaceZ` in its restart block, so a
continued run lands on the same grid rather than a regenerated one - which
matters, because with moving bodies the mask that the grid was built from is not
the mask at the end. An unstretched run still writes `STRUCTURED_POINTS` exactly
as before.

**What it is checked against.** Two things, and the first is the one with
teeth. A second order scheme reproduces a *linear* profile exactly on any grid:
the slope is exact and so is the extrapolation. So the test lays a linear
density down on a 4:1 geometric grid, takes one stage, and reads the flux
divergence back out - it should be `u * dρ/dx` and nothing else. It comes out
8.4e-4 off in the worst cell, which is the float32 noise floor for a difference
of two fluxes of that size; the obvious `0.5 * limit(differences)` form gives
3.0e-3 on the same grid, and the threshold sits between them. Second, a smooth
bump on the same grid at 200 and 400 cells converges at order 1.5 - a limiter
clips at a smooth peak and a 4:1 stretch costs more, so that is what working
looks like, and anything near 1 means it really has collapsed.

Worth saying plainly, because it would be easy to claim more: for a *smoothly*
stretched grid the two forms differ only at higher order and both converge at
second order. The naive one bites where the cell ratio changes abruptly - at
the edge of the fine band, which is exactly where this solver puts one.

### Refinement in patches

    "Fluid Solver.exe" regime=compressible caseType=shockTube ^
                       nx=128 ny=64 Lx=1 Ly=0.5 geometryFile=empty ^
                       amrLevels=1 amrEvery=2 amrThreshold=0.02

`gridStretch` puts the small cells where you say. This puts them where the flow
says, and moves them as it moves.

Levels of rectangular patches sit over the base grid, each one halving the cell
size of the level above it. Cells are tagged by `amrCriterion` - a density jump
for shocks and contacts, vorticity for wakes and shear, a composition gradient
where two gases meet, the cells against a solid, or all four - padded by
`amrBuffer`, and clustered into boxes by a signature-and-split pass that cuts a
region at the widest gap in its tag histogram, or at the sharpest bend when
there is no gap. That is Berger-Rigoutsos, and the whole point of it is that
one long thin feature becomes one long thin patch instead of a square box round
everything.

**A patch is a box, not a rectangle.** `AmrBox` gained `k0` and `nz`, and every
part of the machinery followed: clustering splits along the longest of the
**three** axes rather than the longer of two, tagging looks at the z gradient
and at the full 3D curl instead of the single vorticity component a plane has,
ghost cells interpolate with a z slope as well, and averaging down is the mean
of **eight** fine cells rather than four. The patch frames go out as 3D
`RectilinearGrid` pieces.

The refusal that used to say AMR and `nz > 1` could not both be on is gone.
There was nothing wrong with the argument at the time — a patch had no third
index to give — and there is nothing left of it now.

A patch is a `Block`. That is the whole reason branch 7 was written the way it
was: not one kernel needed touching. `advanceStage`, `blockTimeStep`, `hllc`,
the reconstruction, the solid fill - a patch runs the same code the base grid
runs, on its own arrays, with its own spacing.

**Time subcycles.** A level takes two steps for every one the level above it
takes, which is what keeps every level at the same CFL rather than dragging the
whole run down to the finest cell. Between them the patches refill their ghost
cells from the level above by limited linear interpolation - which reproduces a
constant exactly, and averages back to the coarse value it came from - and from
their siblings where they overlap. Afterwards the fine cells under each coarse
cell are averaged back down into it — four of them in a plane, eight in a
volume — so the base grid always carries the best answer the hierarchy has.

**The bug this shape of code invites**, written down because it cost an
afternoon: `advanceStage` fills the ghost cells from `BlockBoundaries` before
it does anything else. Run it on a patch and the patch gets the *domain's*
walls and inlets imposed on its own edges - all four of them in a plane, all
six in a volume, on top of the ghosts that were just interpolated for it - and
every patch becomes a little closed box that rings. The shock tube blew up to the pressure floor in twenty-five steps.
`SideState` now carries an `interior` flag, a patch marks the sides that do not
reach the edge of the domain, and `mirrorSide` returns immediately for those.
A patch that *does* touch the domain edge still gets the real boundary, and it
gets it at the right place, because `BlockBoundaries` also carries where the
block sits in the domain - a banded inlet is a fraction of the domain, not of
the patch.

**And a real bug it uncovered in the base solver.** Chasing conservation showed
mass being created at the domain walls of every compressible run ever made
here. Branch 7 replaced the wall flux with an explicit one for *solid* faces,
because a mirrored state only cancels to zero mass flux before the
reconstruction breaks the symmetry - and then left the domain walls solving the
same broken Riemann problem. They are explicit now too. A closed shock tube
bouncing off all four walls for two milliseconds conserves mass to 7e-6, where
it used not to.

**What it is worth.** A 128x64 tube with one level: 9728 cells against the
32768 a uniform fine grid would need, 30% of them, and the answer on the base
grid comes out 21% closer to a 256x128 reference than the plain coarse run.

**What is not in it, and this matters.** There is no flux correction at
coarse-fine boundaries. The textbook Berger-Colella scheme keeps a register of
the fine fluxes along each patch edge and replaces the coarse flux with their
average, so the composite is conservative to the last bit. I wrote one. It is
not here, because it did not work: with the register on, the mass drift of a
frozen grid went from 1.35e-3 to 1.56e-3, and a gain sweep - full, half,
quarter, reversed - moved the number around inside the noise without ever
improving it. A correction I derived twice and cannot show to help is not a
correction, it is a second bug hiding behind the first, so it came out.

What is left instead is worth knowing exactly:

- With the patches following the flow, which is how it is meant to be run, a
  closed box conserves mass to about 1e-5 to 1e-6.
- Freeze the grid - `amrEvery` larger than the run - and let a wave sit on a
  patch edge for hundreds of steps, and it drifts to about 1e-3.
- `amrEvery=2` costs a regrid every other step and is cheap; the regrid carries
  the old fine data into the new patches wherever they overlap and only
  interpolates from the coarse where there was none, so following the flow
  closely costs accuracy nothing. Regridding *without* that carry-over is worse
  than no refinement at all - it was, before I added it: the first version came
  out 22% *worse* than the plain coarse run, because every regrid threw the
  fine solution away and re-interpolated it from the coarse one.

**Output.** The ordinary `.vtk` frame is still written on the base grid, with
the fine levels averaged into it, so everything that could read a frame before
still can and the UI needs to know nothing. Beside it goes a `.vtm` with one
`.vtr` per patch carrying density, pressure, velocity, the mask and the level
number, which ParaView opens as a hierarchy and draws at full resolution.

`amrLevels` and `gridStretch` refuse each other: refinement halves a cell to
make a patch and has nothing to halve when every cell is already a different
size. `amrLevels` and `useCuda` refuse each other too, out loud and on the way
past rather than by quietly solving the base grid - the hierarchy lives on the
host and the device kernels know about one grid.

`amrLevels` is refused for `regime=incompressible`, and not out of tidiness:
the pressure solve is global, and a multigrid hierarchy over a patch hierarchy
is a different solver rather than a setting.

### Keys it refuses

Turbulence, gravity, surface tension, sources and the cavity
preset are all incompressible-only, and `gridStretch` and `amrLevels` are
compressible-only the
other way round - the projection solver indexes a flat array with one `dx`, and
the multigrid halves that grid to build its hierarchy; neither survives cells of
different sizes. They are all and asking for one of them alongside
`regime=compressible` stops the run before it starts with a sentence saying
which and why - rather than being quietly ignored, which is the failure mode
where you find out three hours later that gravity was off. Acoustics the other
way round: they need a finite speed of sound, and the projection method's is
infinite by construction.

### Checked against

`CompressibleTests`, ten cases, and five of them have an exact answer rather
than a measured one.

**Sod's shock tube against the exact Riemann solution.** Not a table - the
solver in the test iterates the star pressure and samples the fan, so every one
of 400 cells is compared against arithmetic. Worst error 7.5% in density and
7.2% in pressure, both of them at the contact discontinuity, which is exactly
where a limited second order scheme smears.

**An oblique shock on a 15 degree wedge at Mach 2.5.** The theta-beta-M
relation fixes the shock angle at 36.9 degrees and Rankine-Hugoniot fixes the
pressure jump at 2.468; the run gives 2.648, 7% high on a grid with 24 cells
across the wedge.

**Two gases**, above.

**Both halves of the acoustics**, above.

**A body driven through still gas.** A 0.16 m disc on rails at Mach 0.25 in a
1 x 0.6 m box. The test checks three things that can each fail on their own:
the body state in the frame says it travelled what it was told to; the solid
cells in that same frame have moved with it, so the mask followed the pose
rather than the pose drifting away from a mask that stayed put; and the gas
ahead of it is at a higher pressure than the gas behind - 101479 against
99477 Pa - which is the only one of the three that fails if the wall stops
being a moving wall and goes back to being a mirror.

**A wav that is a wav.** `micAudio=1` on a short shock tube, and then the file
is read back byte by byte: RIFF and WAVE and fmt and data where they belong,
the RIFF length matching the file, 16 bit mono PCM, the sample rate the one
that was asked for, the block alignment agreeing with the format, the frame
count matching `totalTime / micAudioSpeed` to within 2%, the peak at 0.9 of
full scale because that is what peak normalisation means, and the mean inside
1% of the peak because a wav is a fluctuation and a DC offset in a 16 bit
sample is silence with a rail on it.

**A stretched grid**, above: a straight line carried exactly, and a smooth bump
converging.

**The refinement hierarchy, on its own.** A perfectly uniform field must tag
nothing - if it tags anything the criterion is measuring round-off. A clear
density block must produce patches. And a uniform state interpolated onto two
levels of patches, ghost cells included, must come back exactly uniform, and
average back down without moving the coarse state: the test measures 0.0e+00
for both, because a constant is the one thing every interpolation has to
reproduce and the one thing that catches an index slip.

**The refinement hierarchy, running.** Three shock tubes in a closed box - 64
cells, 64 cells with one level over them, 128 cells - and the refined run has
to land closer to the 128-cell answer than the plain 64-cell run does. It lands
7.4e-05 away where the plain run is 4.5e-03 away. The test also refuses a
refined run that comes out *identical* to the fine reference, because that
means the patches covered the whole domain and no coarse-fine boundary was ever
exercised; and it checks the closed box holds its mass to better than 5e-4,
which it does at 8.6e-5.

**An isentropic vortex carried across the grid.** The one case where the right
answer is "exactly what went in": the vortex is an exact steady solution of the
Euler equations, so a stream that carries it without changing it is the
scheme's own dissipation measured against arithmetic rather than against
another run. Seeded with `vortices=` at 40 m/s of swirl on a 160×160 grid, it
comes out at 40.3 m/s after thirty-nine cells of travel, with its centre —
found by the pressure minimum, not by the peak swirl, which sits a whole radius
off — within 0.3 cells of where the stream should have put it. The test refuses
a vortex that has lost half its swirl, one that has somehow gained any, and one
that has drifted off its own line.

## Turbulence

    "Fluid Solver.exe" turbulence=kOmegaSST turbIntensity=0.05 turbLengthScale=0.02 \
                       nu=1.5e-5 U0=10 Lx=2 Ly=0.2 nx=400 ny=64 \
                       bcBottom=wall bcTop=wall

Off by default, and off means the solver does exactly what it did before this
existed: it solves what is on the grid and nothing else. That is right until
the grid stops being able to hold the smallest eddy that matters, which on any
grid a person can afford happens somewhere around a Reynolds number of a few
thousand. Past that the run does not blow up, it quietly lies: the wake is too
long, the recirculation too strong, the drag too low.

### The viscous term had to change first

Every version before this one wrote the viscous term as `nu * lap(u)`. That is
not the viscous term, it is what the viscous term collapses to when `nu` is one
number everywhere. The real one is

    div(2 nu S),    S = (grad u + grad u^T) / 2

and expanding the divergence gives `nu*lap(u)` **plus** `grad(nu)` contracted
with the strain. The second half is exactly what a turbulence model puts there,
and dropping it is not an approximation, it is deleting the only term by which
the model reaches the flow. So `div(2 nu S)` went in first, before either
model, and the golden master came out bit for bit identical because at a
constant `nu` the two really are the same expression.

Written out on the MAC grid the diagonal part of `S` lands at cell centres and
the off-diagonal part at the corners, which is why the code carries two
viscosity arrays rather than one, and why the corner one is the average of the
four cells around it. A two-fluid run gets the same term for free: a density
jump makes `nu` vary just as much as a model does, so the multiphase runs are
now solving a viscous term they were previously approximating.

In a volume `S` has **nine** components rather than four, and the strain
magnitude `|S|` is summed over all of them. That is the number both models are
built on, so getting it wrong would be quiet and total. The check is that it
cannot prefer an axis: the same shear laid on the xy, the xz and the yz plane
gives the same `|S|`, **bit for bit on cubic cells**. Not "to within a
tolerance" — the same floats, because the same expression is being evaluated
with the indices permuted and nothing else.

### `smagorinsky`

The large-eddy model, and the smaller of the two. It says the eddies below one
cell behave like extra viscosity:

    nu_t = (Cs * delta * D)^2 * |S|

`delta` is the filter width — the size of the cell the model is standing in.
That is `cbrt(dx*dy*dz)` in a volume and stays `sqrt(dx*dy)` in a plane, which
is not a tidying-up: a plane has no third direction for an eddy to be a third
of, and taking a cube root of a depth that is not a depth would shrink the
model's reach for no physical reason. `Cs` is yours, and `D` is the damping that
stops it from putting a full eddy viscosity in the one place there is no room
for an eddy - hard against a wall.

The obvious damping is to cap the mixing length at `kappa*y`, and it does not
work. It compares a length against a length, and on any grid a run of this kind
can afford the first cell centre is already further from the wall than the
filter width, so the cap never binds and the model is at full strength in the
first row. The one that does work is van Driest, because it is keyed on a
Reynolds number rather than a length:

    y+ = y * sqrt(|S| / nu),    D = 1 - exp(-y+ / 26)

with `u_tau` taken from the local strain, `tau_w = mu |S|`, so nothing extra
has to be carried around. The cap at `kappa*y` is kept underneath it for the
case where the grid IS fine enough for it to bite. On the test channel that
brings the mixing length in the first row off the wall down to 32% of
`Cs*delta`, and it keeps falling as the grid is refined, which is the whole
signature of the thing.

`Cs = 0.17` is the value it was derived at for isotropic turbulence. Anything
with a wall in it wants about `0.1`.

### `kOmegaSST`

Menter's 2003 shear-stress-transport model, two more transported fields —
transported on three axes in a volume, with the same scheme on each:

    Dk/Dt     = P - beta* k omega + div((nu + sigma_k nu_t) grad k)
    Domega/Dt = alpha S^2 - beta omega^2 + div((nu + sigma_w nu_t) grad omega)
                + 2 (1 - F1) sigma_w2 / omega * grad k . grad omega

`F1` blends the constants between k-omega near the wall and k-epsilon out in
the free stream, so it behaves like whichever of the two is right where it is.
`F2` goes into the eddy viscosity itself:

    nu_t = a1 k / max(a1 omega, |S| F2)

which is the SST part and the reason the model exists: plain k-omega
overpredicts the shear stress in an adverse pressure gradient and separates
too late, and that `max` is what limits it.

Three things are imposed rather than solved:

- **omega at a wall.** The analytic near-wall solution is `60 nu / (beta1 d^2)`
  with `d` the distance to the wall, and any cell with a solid neighbour takes
  it. On the test channel that is 1228.8, and the field peaks at 1228.8.
- **k at a wall** goes to zero, for the same reason.
- **positivity.** Both are positive quantities and an explicit step can take
  either below zero on a coarse grid, which makes `sqrt(k)` a NaN and the run
  stops being a run. They are clamped. That is not cosmetic, it is what makes
  an explicit two-equation model usable at all.

The step size gets a fourth limit alongside advection, diffusion and capillary
waves: `1/(beta* omega)`, the source term's own time scale. Near a wall omega
is large and that limit is the one that binds, so a turbulent run takes smaller
steps than a laminar one on the same grid - that is the model's cost, and it is
visible in the step line rather than hidden.

### The inlet

`kOmegaSST` needs two numbers at the inlet that nobody measures directly, so
they are given the way everybody gives them:

    k     = 1.5 * (turbIntensity * U0)^2
    omega = sqrt(k) / (Cmu^0.25 * turbLengthScale)

`turbIntensity` is 0.01 for a wind tunnel, 0.05 for a pipe, 0.1 behind
something. `turbLengthScale` is the size of the biggest eddy coming in - a
tenth of the duct is the usual guess, and 0 lets the solver take a tenth of
`Ly`. `smagorinsky` reads neither of them.

### What comes out in the frame

`nuT`, `wallDistance` and `strain` are `extraFields`, so ask for them if you
want to look at them. `k` and `omega` are not optional and go into every frame
of a `kOmegaSST` run, because the frame is the restart file - a continuation
that started them back at the inlet values would be a different run. Continuing
a k-omega run reproduces the straight-through one to 2.8e-4 relative.

The wall distance is a breadth-first sweep out of the solid cells and the
domain edges, with the diagonal step counted at its own length so a corner does
not come out further away than it is. In a volume that sweep visits all **26**
neighbours rather than 8, for exactly the same reason: a cell diagonally across
a corner in three directions at once is `sqrt(3)` cells away, and a sweep that
cannot step there has to go round and reports it as further off than it is —
which lands straight in `omega`'s wall value, `60 nu / (beta1 d^2)`, where an
error in `d` is squared. It is geometry, not flow, so it is built once - and
rebuilt when the geometry moves, because bodies that travel are allowed in the
same run.

### Checked against

`TurbulenceTests`, four cases.

**The wall distance and the damping.** The first row off a wall is half a cell
away and the middle of a clear channel is half its height, to the cell. Then
`nu_t = l^2 |S|` is inverted cell by cell - `l = sqrt(nuT/|S|)` - and every
value in the field is checked against `min(Cs*delta, kappa*y)`, which is the
model's own definition rather than a ratio between two cells, so it holds on
any grid. Nothing exceeds it, and in the first row off the wall the length is
32% of the filter width.

**k-omega stays a number.** k never negative, omega never zero, both finite
everywhere after the run; k peaks within a factor of the closed-form inlet
value; omega at the wall matches `60 nu/(beta1 d^2)` to a fifth of a percent;
`nu_t/nu` reaches 43, so the model is doing something.

**A continuation is the same run.** Straight through against split in two, k
differing by 2.8e-4 relative.

**A backward-facing step**, which is what a turbulence model is actually for.
Laminar and k-omega on the same grid, same everything: the laminar run has a
recirculation with -0.77 m/s of backflow in it, k-omega has -0.28, so the model
took 64% of it out by mixing momentum into the shear layer. On a finer grid at
Re about 6900 the reattachment lands at 8.6 step heights, against Armaly's
6-7 for a fully turbulent step and up to about 8 through the transitional
range - the right answer for the wrong end of the range, which is what a
two-equation model on a coarse 2D grid is honestly worth.

`smagorinsky` is deliberately NOT in the step comparison. It is an LES model,
it is asked there to run on a RANS-affordable grid, and on that grid it barely
changes the answer - -0.85 against the laminar -0.83. That is not a bug in the
implementation, it is the model being used outside what it is for, and a test
that asserted otherwise would be asserting a lie.

## Walls: moving and slipping

Off by default. A moving wall is a wall whose *surface* has a velocity while
the body itself stays exactly where it is: a spinning cylinder, a conveyor
belt, the lid of a driven cavity. Gravity is the term that cannot change the
flow; this is the opposite — it is the cheapest way there is to put
circulation into one, and it is what the Magnus effect is made of. A rotating
cylinder in a stream develops real lift, a rotating valve or blade drags its
boundary layer around with it, and a belt drives a shear layer with no inlet
involved at all.

Every separate body in the mask is found and numbered on its own, and each one
takes its own motion. One body is one entry, the entry opens with that body's
number and a colon, and everything the body does goes inside it:

```text
<object>:<setting>=<value>,<setting>=<value>;<object>:<setting>=<value>
```

| Setting | Unit | Means |
|---|---|---|
| `rotZ` | degrees/s, counter-clockwise | the surface turns about that body's own centroid, about the z axis |
| `rotX` `rotY` | degrees/s | the same about the x and the y axis |
| `rot` | degrees/s | an alias for `rotZ`, kept because every line ever written uses it |
| `slideX` `slideY` `slideZ` | m/s | the surface is dragged in a straight line |
| `slip` | switch | free-slip instead of no-slip: the fluid slides along the wall and the wall exerts no drag |

`rotX`, `rotY` and `slideZ` are what the third dimension added. A plane has one
axis a surface can turn about and two it can slide along; a volume has three of
each, and the three rotations compose as a single angular velocity vector rather
than as three separate settings that happen to be applied in some order.

The first two rows are one group and the third is the other. `rot` and `slide`
keep the no-slip wall and give its surface a velocity, so the wall holds the
fluid and now carries it somewhere; `slip` does the opposite and stops the wall
from holding the fluid at all. Nothing about the geometry changes either way —
a body never moves, only the velocity its surface hands to the fluid does.

The rotations and the `slide` components add up, because together they are just
the rigid-body velocity field **v = slide + ω × (x − centre)**, with **ω** now a
three-component vector made of `rotX`, `rotY` and `rotZ` and **slide** a
three-component one made of `slideX`, `slideY` and `slideZ`. Counter-clockwise
means what it looks like in ParaView: with `rot=720` the top of the body runs
towards the inlet at two turns a second.

`slip` is exclusive with all three, and asking for both is refused rather than
half-applied. A free-slip wall carries no tangential stress *by definition* —
that is what free-slip means — and tangential stress is the only thing a
spinning or sliding surface has to push the fluid with. `1:rot=90,slip=1` is
not a combination, it is a contradiction, so it stops the run and says so.
Different objects can of course do different things.

```powershell
.\install\bin\cfd_app.exe "wallMotion=1:rot=720"
.\install\bin\cfd_app.exe "wallMotion=1:rot=720;2:slideX=-1.5,slideY=0.4"
.\install\bin\cfd_app.exe "wallMotion=1:slip=1;2:rot=-45"
.\install\bin\cfd_app.exe wallMotion=1:rot=720,2:slideX=-1.5
.\install\bin\cfd_app.exe nz=64 Lz=1 "wallMotion=1:rotX=360,rotY=180"
.\install\bin\cfd_app.exe nz=64 Lz=1 "wallMotion=1:rotZ=720,slideZ=0.5;2:slip=1"
```

Objects are separated by `;` and settings by `,` — and either separator opens a
new object as long as the number and its colon come first, which is what the
third line uses. PowerShell eats a bare `;`, so the semicolon form has to be
quoted and the comma form does not. Everything else is the same as any other
key: no spaces around `=`, dots for decimals, names case insensitive.

**Where the numbers come from.** The mask is flood-filled into numbered bodies
before the run starts, and the list is printed with the rest of the mesh
information:

```text
  Number of solid cells = 117
  Number of objects = 2 (these numbers are what wallMotion takes)
    object 1: 76 cells, centre (0.5, 0.346628) m, rim 0.109486 m
    object 2: 41 cells, centre (1.04992, 0.679688) m, rim 0.0781845 m
```

Numbering follows the scan order of the grid — `i` inside `j` inside `k`, so
bottom row first, left to right, front plane before the one behind it, a body
taking its number from the first of its cells the scan reaches — so the same
mask always produces the same numbers, and *the mask comes out of
the frame on a continuation*, which means the
numbers therefore still mean the same bodies after a restart. The usual way to
use this is to let the first run print the list, then continue with the motion
you want. Two cells that touch only at a corner count as one body, since the
flow cannot squeeze through that corner either — that is 8-connected labelling
in a plane and **26-connected** in a volume, which is the same rule with the
same justification counted over the neighbours a volume actually has.

Each body also carries its **volume** and its **full inertia tensor about its
own centroid**, worked out from the cells it is made of. Nothing in
`wallMotion` reads either — a surface that moves while the body stays put has
no use for an inertia — but a body that is let go with `free=1` reads both, and
they come from the same labelling pass, so they are always the inertia of the
shape that is actually in the grid rather than of the shape somebody meant to
put there.

> **Known issue, and it is a `nz = 1` one.** All of the above is what the solver
> does with a mask that already has several bodies in it, and it is what a
> continuation gets, since the mask then comes out of the frame. Getting there
> from a *model* through the **section cut** does not work: the section keeps
> only its largest contour, so a model that cuts into two shapes loses one, and
> a model with a hole gets the hole filled.
>
> At `nz > 1` this does not arise, because there is no contour. The voxeliser
> tests every cell centre against the whole triangle soup, so a model that is
> two shapes comes out as two bodies and a model with a hole comes out with the
> hole in it, and the 26-connected labelling numbers whatever it finds. The
> limitation is the section cut's, not the solver's.

`rim` is the distance from the centroid to the farthest cell, i.e. the radius
the rim speed is computed at, because degrees per second is not a number you
can compare to `U0` on sight. The solver does that comparison for you:

```text
Walls:
  object 1 at (0.5, 0.346628) m: rot = 720 deg/s -> rim speed 1.37584 m/s, slide = (0, 0) m/s
    surface moves at 1.37584x the inlet velocity
  object 2 at (1.04992, 0.679688) m: rot = 0 deg/s -> rim speed 0 m/s, slide = (-1.5, 0.4) m/s
    surface moves at 1.55242x the inlet velocity
```

**What it does and does not do.** The tangential part of the surface velocity
drives the flow. The normal part is dropped, and it has to be: the mask does
not move, so a surface pushing into its own body would be creating mass on one
side and destroying it on the other. For rotation that costs nothing at all —
spin about the centroid is tangential everywhere by definition. For sliding it
is the entire difference between *the surface slides* and *the body moves*,
and the second one is `Moving objects`, still on the list at the top of this
file. §7 has the discrete version of the same argument.

**Measured.** 128×64, `Lx=2 Ly=1 nu=0.005 U0=1`, the verification circle,
mean pressure over the ring of fluid just outside the body:

```text
                       above     below     below - above
static cylinder      -0.15518  -0.15517       0.00001 Pa
rot = 1440 deg/s     +0.02670  -0.52826      -0.55496 Pa
```

The static body is symmetric to the fifth decimal, as it has to be. Spinning
counter-clockwise, the top surface runs against the stream and the bottom runs
with it, so the flow is slowed above and sped up below, and the pressure
follows: half a Pascal more of it on top than underneath, pressing the body
down. That is the Magnus force, on the correct side, at a believable size.
Divergence over the same runs stayed at `2·10⁻⁵` against `1·10⁻⁵` for the
static body, i.e. nothing leaked.

**One side effect worth knowing.** The wall velocity is a velocity like any
other, so it goes into the CFL limit. On that run `|u|max` went from 1.40 to
2.16, the wall now being the fastest thing in the domain, and `dt` halved from
`4.2·10⁻³` to `2.0·10⁻³` s. (2.16 is below the 2.50 m/s rim speed printed at
startup, and should be: the faces that carry the motion are the ones buried
inside the body, a cell in from the outermost one the rim is measured to.) A
wall spun much faster than the flow buys its physics with step count, and the
solver says so at startup when the surface is more than a few times `U0`.

**Free-slip, and what it is for.** No-slip is the physically right condition
for a viscous fluid on a real wall, and it is the default. Free-slip is what
you want when the wall is not really a wall: a symmetry plane, a fluid-fluid
interface standing in for a free surface, or a body you want present as an
*obstacle* without the boundary layer it would really grow. It is also the
cheap way to ask how much of a wake is displacement and how much is friction —
run the same geometry twice and difference them.

Measured on a flat, grid-aligned wall (a rectangular block, `nu=0.005`), the
tangential velocity in the first five fluid cells above it:

```text
no-slip     +0.0540  +0.1250  +0.2116  +0.3121  +0.4247
slip        +0.4218  +0.4471  +0.4958  +0.5644  +0.6484
```

No-slip pins the fluid to the wall and grows a boundary layer out of it. Free
slip lets it past at 0.42 and the profile is nearly flat across the first two
cells — what is left is the outer shear of the channel, not wall friction.

On a *staircase* body the effect is real but partial: the same measurement on
the crown of the circle gives `0.311 → 0.592`, a large reduction and not a
removal. The steps either side of the crown present vertical faces, and those
are no-penetration faces, which free-slip does not and must not touch. That
residual is the staircase approximating a smooth circle, not the slip
condition; it goes away as the mask gets finer, and it is the same error the
immersed boundary already has without slip.

A body one cell thick can only be driven along itself. The tangential value
lives on the faces *inside* the body, and a one-cell-tall bar has buried `u`
faces but no buried `v` faces at all — so it drags in `x` and ignores `slideY`
and, being flat, very nearly ignores `rot` too. A single isolated cell has
neither kind and cannot drive anything. Both are found, numbered and reported
like any other object; they just have nothing to push with. §7 explains which
face is which.
## Acceleration, and where the choice is kept

The banner says which build this is:

```
=== Fluid Solver 1.0 (avx2-omp-cuda) ===
```

AVX2, OpenMP and CUDA can each be turned off without changing which download
you are running. What that changes is speed, not what is being solved: the
velocity field comes out the same to the last digit a float holds, and the
pressure lands on a slightly different multigrid iterate — about a thousandth
of its peak — the same way the separate AVX2 and non-AVX2 downloads always have
between them.

An interactive run offers the switches before it asks anything else, and

```powershell
"Fluid Solver.exe" --settings
```

opens the same menu on its own. The answers go into `settings.ini` — beside the
executable when that folder can be written to, which is what a portable unpack
gives, and in the per-user data directory when it cannot, which is what an
install under Program Files gives — and are used by every later run.

For one run only, without touching that file:

```powershell
"Fluid Solver.exe" avx2=0 openmp=1 threads=4 useCuda=0 tray=0 nx=256 ny=128 ...
```

and the environment overrides everything: `FLUID_SOLVER_NO_AVX2`,
`FLUID_SOLVER_NO_OPENMP`, `FLUID_SOLVER_NO_CUDA`, `FLUID_SOLVER_NO_TRAY`,
`FLUID_SOLVER_NO_UPDATE_CHECK`.

A switch for something this build was not compiled with is shown as
"not in this build" rather than hidden — the menu explains why one machine is
slower than the one next to it instead of leaving it a mystery.

**And a compressible run says, in one line at the top, what it is actually
running on:**

```text
Compressible core: GPU (CUDA).
Compressible core: CPU, 8 threads - no NVIDIA driver on this machine.
```

When it is not the GPU the line says why not, and names the reason rather than
shrugging: the build has no CUDA in it, there is no driver, `useCuda=0`,
`amrLevels` keeps the run on the host, `gridStretch` keeps the run on the host,
or no device answered. The block above it says what the build *can* do; this
says what this run *does*, which is the question actually being asked when the
task manager shows a cold card and eight busy cores.

## While it runs

On Windows the solver puts an icon in the tray for the length of a run. Its
tooltip is the progress in simulated seconds — `Fluid Solver - 12.5 / 30 s
(41%)` — the same number fills the taskbar button, and the tray menu can send
the console window away and bring it back, open the output folder, or ask the
run to stop.

"Stop" there, and a first Ctrl+C anywhere, mean the same thing: finish the step
that is running, write the frame, and return. The run can then be continued
from that frame exactly as described below — which is the point of stopping
that way rather than killing the process halfway through a file. A second
Ctrl+C is left to the default handler and kills it outright.

**A run with no console of its own can be asked the same way.** A file called
`stop` in the output folder means "finish this step, write the frame, come
back", and the run looks for one twice a second:

```powershell
New-Item "output\<run>\stop"      # or: touch output/<run>/stop
```

That is the only way to reach a run started by something else — the UI's Stop
button writes exactly this file. A stop file left behind by a previous run is
removed when the next one starts, so it cannot stop a run before it has begun,
and it is removed again when the run ends.

**Every step line says how far along the run is.** `t = 0.0003221 s` on its own
is no help unless you remember what you asked for, so the line ends with
`0.0003221 / 0.0006 s (53%)` — the same number the tray tooltip and the taskbar
carry. A wall of step numbers then tells you whether this finishes in a minute
or in an hour.

**`runName` keeps runs apart.** Frames go into `output/<runName>` rather than
straight into `output`, so a series of runs does not land on top of itself;
anything a folder name cannot hold is replaced. It only does this while
`outputDir` is still the default — a folder you named yourself is a folder you
meant, and having the run put its frames somewhere underneath it instead would
be the sort of helpfulness nobody asked for. Left empty, everything goes where
it always did.

Elsewhere there is no tray a static console binary can reach without dragging
in a desktop toolkit, so the same progress goes into the terminal's title,
which is what the taskbar entry or the Dock shows for that window.

`tray=0`, or `tray` in `--settings`, turns all of it off. Ctrl+C keeps working
either way.

## New releases

At startup the solver asks GitHub once whether anything newer than this build
has been published, and offers to open the release page if so. Any version
greater than this one counts, whether it moved the major or only the minor,
and the comparison is numeric — 0.10 is newer than 0.9, not older.

The request has a short timeout and no network at all is the normal case
rather than an error: nothing is printed unless there is something newer. On
Windows it goes through WinHTTP; elsewhere through whichever of `curl` and
`wget` the machine has.

```powershell
"Fluid Solver.exe" --check-updates     # ask now, and say so either way
```

`checkForUpdates=0` in `settings.ini`, or `FLUID_SOLVER_NO_UPDATE_CHECK=1`,
stops it asking.

# Continuing a run

The first thing the configuration asks is whether this is a new simulation or a
continuation of an old one:

```text
Start a new simulation or continue an old one?
  0 = new simulation
  1 = continue from a saved .vtk
>
```

Answer `1` and give it either a frame or the folder the frames live in — a
folder means "take the newest one". Every parameter of the old run comes back
out of that file, and the usual confirmation screen opens on top of it, so
anything can still be changed before the run starts. The typical reason to
continue is precisely that: the flow looked interesting somewhere in the middle
and you want denser output from there on, so you continue with a smaller
`saveInterval` and a longer `totalTime`.

The same thing from the command line, where anything after `restartFile`
overrides what the frame remembers:

```powershell
.\install\bin\cfd_app.exe restart=1 restartFile=output totalTime=30 saveInterval=5
```

**What a continuation writes.** Frames are named after the file they were
started from, and carry their own step number, so the name says both where the
run came from and how far it got. Continuing from step 200 with
`saveInterval = 100`:

```text
output/solution_200.vtk          <- what you continued from
output/solution_200_300.vtk      <- step 300
output/solution_200_400.vtk
output/solution_200_522.vtk      <- final step
```

Nothing is ever overwritten and it stays obvious which run produced what.
Continue from `solution_200_400.vtk` and the next series is
`solution_200_400_450.vtk` and so on. A fresh run is unchanged:
`solution_<step>.vtk`.

**What can and cannot change.**

| Parameter | On a continuation |
|---|---|
| `nx`, `ny`, `nz`, `Lx`, `Ly`, `Lz` | fixed by the frame, changing them is refused |
| geometry (`geometryFile`, slice angles, `invertSection`) | ignored — the solid mask comes out of the frame, the model file is not needed any more |
| `totalTime` | must be larger than the time already reached, otherwise there is nothing to compute |
| `saveInterval`, `outputDir`, `CFL`, `dtSafety`, `dtUpdateInterval`, `omega`, `smootherOmega`, `mgIterations`, `mgTolerance`, `mgMinCoarseSize`, `useCuda` | free |
| `U0`, `nu` | allowed, but it is a discontinuity in the physics, not a continuation of the same problem |
| `ro` | free — it only scales the pressure on the way out to Pa, the frame stores the kinematic field |
| `gravityEnabled`, `gravityAccel`, `gravityAngle`, `gravityTilt` | free — changing them shifts the hydrostatic part of the pressure and leaves the velocity where it was |
| `bodyMotion` | free, and it is the one key that changes how the frame is read: a run whose bodies travel rebuilds the geometry from the model at the pose the frame carries, rather than inheriting a rasterised copy of the mask. Restart it a hundred times and it is in the same place as the run that was never stopped |
| `wallMotion` | free — the mask comes out of the frame, so the object numbers still mean the same bodies. Spinning a wall up mid-run is a step change in the boundary condition, not a discontinuity in the state |

Continuing from the last frame of a run and letting it go further produces
**bit-identical** results to never having stopped at all.

Frames written before gravity existed simply do not carry its three keys, and
the reader skips keys it does not know, so they load with gravity off. Frames
written now stay readable by builds that predate it, for the same reason.

**And a volume continues like a plane does.** A volume stopped halfway and
continued lands within **5e-4** of one that never stopped. That is a looser
number than the bit-identical one above and it is the honest one for a volume:
the plane figure is measured on a single-phase run where nothing but the clock
and the in-flight `dt` have to be restored, and 5e-4 is what the whole
machinery — `w` faces, the body records, the phase volume — comes to when it is
all put back at once.

Frames written before the third dimension existed do not carry `nz` or `Lz`,
so they load as a volume one cell deep with `w` zero, which is precisely what
they always were. See *What a frame is made of* for the format version that
makes that safe rather than lucky.

## Continuing a run, in detail

Every frame is also a checkpoint. That is less obvious than it sounds, because
what a frame shows and what the solver needs are not the same thing.

**Why the visible arrays are not enough.** The solver lives on a staggered
grid: `u` sits on `(nx+1)×ny×nz` faces, `v` on `nx×(ny+1)×nz`, and `w` on
`nx×ny×(nz+1)`. A VTK frame is cell centred, so what gets written is the
average of the two faces around each cell — `0.5*(u[i] + u[i+1])`. Averaging
throws away exactly one degree of freedom per row, per column and per column of
depth, and no amount of cleverness gets it back. Reading a frame and interpolating back onto the faces gives a field that
looks right and is not divergence free, which the projection then has to repair
with a visible kick.

So every frame carries a `FIELD RestartData` block at the very end, after the
arrays ParaView cares about:

```text
FIELD RestartData 2
configText 1 <n> char           the whole configuration as key=value lines,
                                plus restartTime, restartStep and restartDt
facePack   1 <n> unsigned_char  the raw face velocities, stored as the
                                difference from what the cell averages
                                predict — see "What a frame is made of"
```

A frame without face velocities — one written before the `RestartData` block existed, or one whose packed block fails its checksum — has them rebuilt from the cell averages and the state projected once. The reconstruction is masked before that projection: interpolation fills *every* face, the ones held shut against a wall included, and a projection of a field that flows through walls is not a projection of anything. Masking first is what the predictor does on every ordinary step.

Pointing `restartFile` at a folder takes the newest frame in it, by file timestamp, with the step in the file name breaking ties — a run short enough to write its whole output inside one second gives every frame the same timestamp.

`FIELD` is the only legacy VTK block that lets each array declare its own tuple
count, which is the whole reason it is used — `(nx+1)*ny*nz` simply does not fit
in a `CELL_DATA` section of `nx*ny*nz`. The pressure the restart needs is not in here at all:
the `SCALARS pressure` array ParaView reads is the same field times `ro`, so
the reader divides it back out rather than the frame carrying it twice. What is
left costs about 12% more file size, and it is the difference between a frame
you can look at and a frame you can resume.

The configuration is serialized in the same `key=value` form the command line
already speaks, so reading it back is `setParam()` on each line, and unknown
keys are skipped instead of rejected — old frames stay loadable and new frames
do not break old builds.

**The two things that make it exact.** Restoring `u`, `v`, `p` and the clock is
not quite enough, and both leftovers are easy to miss:

*The time step in flight.* `dt` is only recomputed every `dtUpdateInterval`
steps. A continuation that recomputes it immediately would shift that cadence
by a fraction of a step and drift away from the original trajectory, so the
frame stores the `dt` that was live when it was written and the solver takes it
back instead of recomputing.

*The nested iteration.* The first pressure solve of a run does one full
multigrid pass to build a field out of nothing. A continuation is not starting
from nothing — it has the converged pressure of the step it stopped at, which is
a better guess than that pass produces — so the pass is skipped. Leave it in and
the field gets nudged, and the two trajectories separate within a few steps.

With both pinned, continuing from frame *k* and running on gives byte-for-byte
the same frames as the uninterrupted run would have.

**Frames written before all this existed** still load. The face velocities get
rebuilt from the cell averages, the state is projected once before the first
step, and it says so loudly. It is a restart, not *the* restart: use it to
rescue an old run, not to claim continuity.

**The mask comes from the frame**, not from the model. `Mesh` takes an optional
preset mask and skips loading, slicing and rasterizing entirely when it gets
one. The STL does not have to still exist, and no rasterizer change can ever
move a boundary in the middle of a run.

---

# Visualization

A separate desktop application, built from its own branch. It configures a run,
launches the solver as a child process and renders the frames the solver writes.
The solver itself has no window.

Available display modes:

- Pressure
- Velocity magnitude
- Velocity vectors
- Solid mask
- Slice planes on any axis, a translucent cloud of the whole volume,
  isosurfaces, Q-criterion vortices with their core lines, and 3D streamlines
  with animated tracers — the volume half, in the viewport. The cloud, the
  isosurface and the vortices can be on together, and everything on screen
  takes the same colour field

Interactive controls include:

- Pause
- Resume
- Simulation speed
- Camera movement — orbit, pan and zoom, laid out the way Blender lays them out
- Zoom
- Time navigation
- Rendering mode switching
- `V` to swap between the 3D viewport and the 2D view, which is now a slice
  through the volume rather than a different picture

---

# Export

Simulation results are automatically written in VTK format.

The exported files contain:

- Pressure (Pa)
- Velocity vectors, with a real third component
- Solid mask

The files can be opened directly in ParaView for further analysis, contour generation, streamline visualization and animation.

---

# Tests

Off by default, because the release matrix is 34 rows and none of them needs
them built. One switch turns them on:

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCFD_ENABLE_CUDA=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Everything except `main.cpp` lives in a library called `cfd_core`, and both the
executable and the tests link the same objects, so what is tested is what runs.

| Test | What it would catch |
|---|---|
| `PoissonTests` | a manufactured solution the pressure operator has to reproduce to second order, plus the two things the operator gained here: face weights that scale it, and a closed box whose answer is only defined up to a constant |
| `ChannelTests` | the developed profile between two no-slip walls against the exact parabola, and the same channel with free-slip walls to prove the number came from the wall and not the inlet |
| `CavityTests` | the lid driven cavity at Re 100 against the Ghia, Ghia and Shin tables - the first numbers in this project that were not produced by this project |
| `InletTests` | an inlet cut down to a band of its side: where the open faces actually are, that a parabolic band carries the same flow rate as a flat one, and that what goes in comes back out |
| `MultiphaseTests` | the volume of fluid 1 after three seconds of being stirred, a layer under gravity that is supposed to be sitting still, a collapsing column against the speed a free fall from its own height would reach, and a step in composition spreading at the rate an error function says it should |
| `MovingBodyTests` | a body told where to go arriving exactly there with the divergence still at float noise, a ramp integrated exactly, two bodies keeping their numbers while they pass, the three couplings measured against each other, a neutrally buoyant body staying put, and a jet pushing its own body the other way |
| `SurfaceTensionTests` | the pressure inside a drop against `sigma/R`, the spurious current a drop that should be at rest develops anyway, and a square blob of water refusing to stay square |
| `ConservationTests` | divergence left after the projection, inflow against outflow, and a fluid at rest under real gravity staying at rest |
| `ConvectionTests` | every scheme run on the same case, and the ordering of how much of the field each one throws away |
| `RestartTests` | a run cut in half and continued reproducing the run that was never cut, and the frame it stopped at still carrying the face velocities that make it exact |
| `CompressibleTests` | Sod against the exact Riemann solution, an oblique shock against theta-beta-M, two gases, both halves of the acoustics, a driven body, a `.wav` that is a `.wav`, a stretched grid, the refinement hierarchy standing still and running, and an isentropic vortex that has to come out of the far side the same size it went in |
| `TurbulenceTests` | the wall distance and the damping against the model's own definition, k and omega staying finite and positive, k surviving a restart, and a backward step where the model has to take most of the laminar backflow out |
| `VolumeTests` | the whole third dimension: that a plane is still the plane, the cube cavity against Ghia, a light drop rising the same distance whichever axis gravity is turned to, a closed box keeping its mass, a volumetric restart, turbulence staying finite in a volume, walls turning about every axis, a shock tube refusing to notice the two directions it should not, refinement in depth, and a solid body that is a body rather than a stack of discs |
| `BackendAgreementTests` | AVX2 on against off and many threads against one, which is the thing that quietly turns one solver into several that disagree |

`.github/workflows/build-all.yml` runs `ctest` before it builds a single
release row, and every platform job waits on it. Before this the matrix only
ever checked that the thing linked.

---

# Performance

The solver is designed to remain numerically faithful while reducing computational overhead through algorithmic and implementation-level optimizations.

Performance improvements include:

- reduced arithmetic cost;
- improved cache locality;
- reduced indexing overhead;
- optimized memory access patterns;
- accelerated pressure solver;
- optimized boundary-condition processing.

## What a volume costs against a plane

The short version, before any of the detail: **a volume is `nz` times the cells
of the plane it is made of, and the pressure solve is the expensive part.** The
Poisson solve is about 90% of the runtime of an incompressible step and it
scales with the cell count, so `nz=64` is not a setting, it is a decision to do
sixty-four times as much work per step. Nothing in the port changed that and
nothing could: it is what solving a volume means.

What the port was careful about is that it is only `nz` times and not worse.

- **The multigrid coarsens each axis independently**, so the hierarchy over a
  volume is a hierarchy over a volume rather than a stack of 2D ones, and each
  level is eight times smaller instead of four. A V-cycle over all three axes
  costs about `1 + 1/8 + 1/64 + … = 8/7` of a fine-grid sweep, against the `4/3`
  a plane pays — the cycle gets relatively cheaper as the dimension goes up,
  which is the one piece of good news in this section.
- **At `nz = 1` the solve is the old solve**: the same level count, the same
  cycle count, and the 2D field back bit for bit. A plane pays nothing at all
  for the volume code being there.
- **The geometry rebuild is measured and it is small.** Voxelising **128^3
  against 50k triangles takes about 5 ms**, which is why a body can travel in a
  volume — the mask is cut again every step, and 5 ms against a pressure solve
  over two million cells is not the thing to worry about. On the section-cut
  path at `nz = 1` the rebuild is what it always was.
- **The AVX2 kernels and the threading did not have to be given back.** `i`
  stays contiguous so the vector kernels are unchanged in shape, and OpenMP
  collapses (k, j) so a volume parallelises over both.

**What this README does not have is a wall-clock number for a volume pressure
solve**, and it is not going to invent one. The measurements that exist for the
3D port are correctness measurements — bit-for-bit agreement at `nz = 1`, second
order on a manufactured 3D solution, the conservation and isotropy figures
quoted throughout — plus the 5 ms voxelisation above. The timing tables further
down this section are all plane runs on a named machine, and quoting them as if
they said something about a volume would be worse than having nothing. Run your
own case at `nz=8` before you run it at `nz=128`; the step count and the
seconds-per-step it prints are the only honest estimate there is.

0.7 added a cost that had never existed: the mask is cut again every step
instead of once. Best of five interleaved runs, same case, AVX2 and OpenMP on:

| | 192x96, disc, 0.4 s |
|---|---|
| geometry cut once | 0.869 s |
| geometry cut every step | 1.037 s |
| what moving it costs | 19.3% |

That is after the rasterizer was rewritten for it, and the rewrite is most of
the difference. Under callgrind the same case went from 150.6M executed
instructions to 126.2M, a sixth of the whole run:

- **std::hypot was a fifth of the moving path.** `distanceToSegment` called it
  once per cell per segment, and it is a libm call that goes to real trouble
  not to overflow. The comparison it feeds only wants to know which side of the
  radius the point is on, and squares answer that. Cells sitting within a part
  in 1e12 of the radius - none of them, ever - still go through hypot, so the
  mask cannot move. hypot went from 9.4% of the run to not appearing.
- **A segment is only tested against the rows it reaches.** Each contour is
  clipped to its own bounding box, and inside that box each row first collects
  the segments whose y-range covers it. A body's outline crosses a handful of
  rows per segment, so the per-cell loop went from the whole contour to a few.
- **The transfer tables stopped being rebuilt.** Which two coarse cells a fine
  cell interpolates from is a function of the level's shape, and the shape is
  fixed the moment the hierarchy is built. Only the weights read the mask.
- **The CUDA backend stopped reallocating.** `setGeometry` on the device freed
  and re-malloc'd the whole hierarchy and wiped the pressure field, which is
  fine once at setup and is tens of cudaMalloc pairs per step for a body that
  moves. It now has a keep-the-solution path that uploads the mask and the
  coefficients and touches nothing else.

The single-phase path is unchanged and still bit-for-bit what 0.2 wrote: 88
frames across ten configurations, relative difference exactly 0.000e+00. The
third dimension did not move that number either, which is the one result the
whole port was written to produce.

0.6 went back over everything the previous three branches added rather than
only writing the new feature. Best of three runs, same machine, AVX2 and
OpenMP on:

| case | 0.5 | 0.6 | |
|---|---|---|---|
| single phase, 256x128, muscl + rk2 | 1.832 s | 1.614 s | 12% |
| dam break, two fluids, 128x96 | 1.673 s | 1.474 s | 12% |
| lid driven cavity, 128x128 | 4.113 s | 3.443 s | 16% |
| the same single phase run writing 3 extra fields every 2 steps | 1.339 s | 1.178 s | 12% |

Where it came from, in rough order of size:

- **The transfer operators stopped recomputing their own stencils.** Which two
  coarse cells a fine cell interpolates from is a function of the level, not of
  the step, and it was being worked out per cell per level per cycle. It is a
  table now, built when the hierarchy is. That alone is most of the 43% of
  executed instructions the multigrid lost.
- **The reciprocal is taken once per cell instead of four times per face.**
  Every face wants 1/rho of both cells it sits between, and every cell has four
  faces.
- **`maxDivergence` and the null-space removal are vectorised**, with the
  divergence sum kept scalar and in order so the answer does not move.
- **`saveVTK` byte-swaps a whole field at a time** with a shuffle, rather than a
  word at a time through a branch on the buffer being full, and the restart
  block is written through a pointer instead of a capacity check per byte.
- **OpenMP reaches the coefficient rebuild and the face coarsening**, which
  moved from setup to the inner loop the moment the density stopped being one
  number.

The single phase path is still bit-for-bit what it was: 88 frames across ten
configurations compare to a relative difference of exactly zero against the
frames 0.2 wrote. Twice during this pass it did not, and both times the change
was reverted rather than the golden master updated - `* invNorm` for `/ norm`
is not the same float, and a fast path that skips a loop when nothing in it is
solid does not sum in the same order.

---

## Contributing / Feedback

This is a personal educational project, but suggestions and issues are welcome. Feel free to open an issue or pull request.

---

**Happy simulating!**  
If you have any questions, don't hesitate to open an issue. Complete guide for our solver is down here.

Alright. Building it up from the physics, because every design choice in the code falls out of one problem.

## The one-paragraph version

Pressure has no equation of its own; it's whatever makes the flow divergence-free. So each step you advance momentum ignoring pressure, measure the divergence you created, solve a Poisson equation for the pressure that cancels it, and subtract its gradient. That is three momentum components and a seven-point Poisson operator in a volume, and two components and a five-point one in a plane — the same code, with the front and back coefficients at exactly zero when `nz = 1`. The grid is staggered so pressure gradients and divergences land exactly where they're needed and the chessboard mode can't survive. The Poisson operator encodes every boundary condition in its coefficients, which guarantees it's exactly `div ∘ grad` and therefore that the projection actually projects. Multigrid solves it in `O(N)` by exploiting the fact that SOR smooths error fast but converges slowly — so you smooth on every grid size at once. Everything else is SIMD, threads, and not allocating memory in the inner loop. Gravity, if you switch it on, never enters the solve at all: at constant density it is exactly a pressure offset, so the solver works in the reduced pressure and puts the hydrostatic part back on the way out — §4 explains why that is the correct answer and not a shortcut. Wall behaviour is the mirror image: the faces buried inside a body are handed the surface velocity instead of zero — or the neighbouring fluid value, which is free-slip — and the operator does not change one coefficient either way. §7.

---

## 1. What we're actually solving

Incompressible Navier–Stokes:

```
∂u/∂t + (u·∇)u  =  −∇p + ν∇²u        momentum
∇·u = 0                                incompressibility
```

Written like that it says nothing about how many dimensions it is in, and that is the honest form: **u** has three components in a volume and two in a plane, `∇` runs over three axes or two, and every word below is the same either way.

Read the momentum equation as "F = ma for a blob of fluid": it accelerates because neighbours push it (pressure), because friction drags it (viscosity), and it carries itself along (convection).

The second equation is the troublemaker. It's not an evolution equation — there's no `∂/∂t` in it. It's a **constraint**: at every instant, everywhere, the fluid must neither pile up nor thin out. What flows into a cell must flow out.

And notice: **there is no equation for pressure anywhere.** No `∂p/∂t`. Pressure isn't a thing that evolves — it's whatever value it has to take, right now, to make the constraint hold. It's a Lagrange multiplier. That single fact determines the entire architecture of the solver.

---

## 2. The projection idea

If you can't march pressure forward in time, what do you do? You cheat, then fix it.

**Step 1 — cheat.** Advance the momentum equation and pretend pressure doesn't exist:

```
u* = uⁿ + dt·( −(u·∇)u + ν∇²u )
```

`u*` is a perfectly good velocity field except for one thing: it's not divergence-free. Mass is appearing and vanishing all over the place.

**Step 2 — measure the damage.** Compute `∇·u*` in every cell. Positive means mass is being created there, negative means destroyed.

**Step 3 — find the fix.** We want a correction that removes exactly that divergence. Write the correction as the gradient of some scalar `p` (this isn't arbitrary — the Helmholtz decomposition says any vector field splits uniquely into a divergence-free part plus a gradient, and we want to throw away the gradient part):

```
uⁿ⁺¹ = u* − dt·∇p
```

Take the divergence of both sides and demand `∇·uⁿ⁺¹ = 0`:

```
0 = ∇·u* − dt·∇²p        →        ∇²p = ∇·u* / dt
```

That's a **Poisson equation**. Solve it, and you get the pressure field whose gradient exactly cancels the divergence you created.

**Step 4 — apply it.** `uⁿ⁺¹ = u* − dt·∇p`. Now the field is divergence-free and you've advanced one step.

That's Chorin's fractional step method, and it's literally the four function calls in `Solver::run`:

```cpp
predictor();      // step 1
solvePoisson();   // steps 2 and 3
corrector();      // step 4
```

The Poisson solve is ~90% of the runtime. Everything else in this codebase is either feeding it or making it fast.

---

## 3. The grid, and why it's weird

The obvious layout — put `u`, `v`, `p` all at cell centres — is broken. Here's why.

To get `∂p/∂x` at a cell centre from centred values you'd use `(p[i+1] − p[i−1]) / 2dx`. Now imagine a pressure field that alternates `+1, −1, +1, −1` like a chessboard. Every `p[i+1] − p[i−1]` is zero. **That field produces no force at all.** Nothing in the equations damps it, so it grows from roundoff noise until your pressure plot looks like a chessboard. This is a genuine, famous failure mode.

The fix is to **stagger**: pressure at cell centres, velocities on cell faces.

```
              v(i,j+1)
                 ↑
        ┌────────┴────────┐
        │                 │
 u(i,j) →     p(i,j)      → u(i+1,j)
        │                 │
        └────────┬────────┘
                 ↑
              v(i,j)
```

Now `∂p/∂x` at the `u` face between cells `i−1` and `i` is `(p[i] − p[i−1]) / dx` — **adjacent** cells, no gap. A chessboard produces a huge gradient and gets crushed immediately. Same for divergence: it's the net flux through the four faces of a cell, which are exactly where the velocities live. Everything lands where you need it. No interpolation.

The price is that the fields have different shapes, and this is where the index arithmetic bites:

```
p   nx     × ny     × nz       idxP(i,j,k) = (k*ny     + j)*nx     + i
u   (nx+1) × ny     × nz       idxU(i,j,k) = (k*ny     + j)*(nx+1) + i   ← different!
v   nx     × (ny+1) × nz       idxV(i,j,k) = (k*(ny+1) + j)*nx     + i   ← different!
w   nx     × ny     × (nz+1)   idxW(i,j,k) = (k*ny     + j)*nx     + i
```

`u` has `nx+1` columns because a row of `nx` cells has `nx+1` vertical faces (think fence posts vs fence panels), `v` has `ny+1` rows for the same reason, and `w` has `nz+1` planes. Mixing up `j*nx` and `j*(nx+1)` is the single easiest way to destroy this solver, and it's exactly the bug that was in there; the third index gives you a second way to do it, with `k*ny` and `k*(ny+1)`.

Two things about that layout are worth saying out loud, because both were chosen rather than fallen into:

**At `k = 0` every one of those formulas is byte-for-byte the 2D one.** `(0*ny + j)*nx + i` *is* `j*nx + i`. That is not a coincidence and it is not a nicety — it is why a plane run produces the same bits. There is no separate 2D path to keep in step with the 3D one; there is one path, and a plane is the case where the outer loop runs once.

**Loop order is always `k` outermost, then `j`, then `i`, and `i` stays contiguous.** The AVX2 kernels vectorise runs of cells along `i`, and a volume is a stack of the planes those kernels already knew how to walk, so not one of them changed shape for the port. Neighbours are reached by a stride rather than by recomputing an index:

```
p: +1 (i)   +nx      (j)   +nx*ny  (k)
u: +1       +(nx+1)        +(nx+1)*ny
v: +1       +nx            +nx*(ny+1)
w: +1       +nx            +nx*ny
```

---

## 4. Walking through one time step

### `computeDt` — how big a step can we take?

The scheme is explicit, so there's a speed limit. Two of them.

**Advective (CFL):** in one step, fluid must not cross more than a fraction of a cell. If it jumped two cells, the stencil never even looked at the cell it passed through — information outran the numerics. Condition: `|u|·dt/dx + |v|·dt/dy + |w|·dt/dz ≤ 1`.

**Diffusive:** the explicit viscous term goes unstable if `dt > 1/(2ν(1/dx² + 1/dy² + 1/dz²))`. Physically, momentum must not diffuse more than about a cell per step.

Take the smaller, multiply by `CFL` (0.4) and `dtSafety` (0.9) for margin.

At `nz = 1` the front and back faces are shut, `w` is zero there and the `1/dz²` term is not summed, so both limits are the two-term ones they have always been and a plane takes exactly the steps it used to take. In a volume the third term only ever makes the limit tighter, which is the honest direction for a stability bound to move.

One refinement: the Courant number is computed **per cell** from that cell's own faces, then maxed. Taking a global `max|u|`, a global `max|v|` and a global `max|w|` and adding them assumes the worst flow in all three directions happens in the same place, which it usually doesn't — so you'd shrink `dt` for a cell that doesn't exist.

Called every 5 steps, not every step, since it's a full sweep over the grid and velocities don't change much in 5 steps.

### When the estimate is not an estimate

Both limits are *upper bounds from stability*. Neither says the step is a good idea, only that the scheme will not explode at it — and a bound can be vacuous. A slow flow in a thin fluid, `U0 = 0.001 m/s` and `ν = 1e-6 m²/s`, puts the CFL limit at 15 s and the viscous one at 97 s, so a one second run is a single step. At a Courant number of 0.03 that step is perfectly accurate; it is just not a film. The startup line prints both limits and the step count they imply, and says so when the run fits in a couple of steps — what fixes that is more `totalTime` or a finer grid, not a smaller `dt`.

Two cases are not estimates at all, and both stop the run rather than substituting a number nobody chose:

**The field is no longer finite.** `maxps` returns its second operand when either input is a NaN and `std::max` drops it the same way, so a `NaN` sails straight through a maximum. The Courant reduction carries a `_CMP_UNORD_Q` accumulator beside the max to catch it.

**`dt` comes out zero or infinite.** That means the grid spacing or the viscosity are outside what a float can express — `Lx=1e-20` overflows `1/dx²` and sends the viscous limit to zero. The run stops and prints `dx`, `dy` and `ν`, which is the actual problem.

One rule at the other end of the run: **no step is ever shorter than half of `dt`.** The last stretch up to `totalTime` rarely divides evenly, and the naive thing — take full steps, then whatever is left — can leave a final step of nanoseconds. That step is not wrong, but the Poisson right-hand side is `div/dt`, so the frame it writes has a pressure map scaled by a factor of millions, and the last frame of a run is one people open. So when the remainder is between one and two `dt`, it is split in half instead. The leftover after the final step is then set to zero outright rather than accumulated, because `currentTime` adds up single-precision steps and lands a few ulps short of `totalTime` — which used to be enough on its own to trigger one more absurd step, on roughly half of all runs.

### `predictor` — momentum without pressure

For each `u` face, evaluate the right-hand side and step forward:

```cpp
dudx = (uij > 0) ? (uij − uleft)*invDx : (uright − uij)*invDx;   // upwind
dudy = (vn  > 0) ? (uij − ubot )*invDy : (utop   − uij)*invDy;
d2x  = (uright − 2*uij + uleft)*invDx2;                          // central
d2y  = (utop   − 2*uij + ubot )*invDy2;

uStar = uij − dt*(uij*dudx + vn*dudy) + dt*ν*(d2x + d2y);
```

**Convection uses upwind** — the derivative is taken on the side the flow is *coming from*. If fluid moves right, what's arriving at this face came from the left, so ask the left neighbour. Using a centred difference here would be unstable: it lets information propagate against the flow, which is physically wrong and numerically explosive.

(Upwind is only first-order accurate and adds artificial diffusion of roughly `u·dx/2`. That's the source of the "why is there no wake" question — on a coarse grid that numerical viscosity can exceed your physical `ν`.)

**Diffusion uses centred differences** — friction genuinely acts in both directions equally, no upwinding needed.

`vn` is the vertical velocity *at the u-face*, which doesn't exist there, so it's the average of the four surrounding `v` faces. This is the one place staggering makes you interpolate — and in a volume it happens twice, because the `u` face also needs a `w` there, averaged from the four `w` faces around it the same way. The `w` equation is the same expression again with the indices rotated; there is no third case, only a third axis.

There is no gravity term in there, and that is deliberate — the next heading is
about why. What the real line does carry is the mask multiply and the wall
value that rides with it, so a closed face leaves the predictor holding exactly
its boundary velocity; §7.

Then `v` gets the mirror-image treatment.

### Gravity — and why it does nothing

This one deserves its own heading, because the honest answer is unintuitive
enough that people assume the code is broken when they see it.

**At constant density, gravity cannot change the velocity field.** Not
approximately — exactly. Gravity is uniform, so it is the gradient of a
potential:

```
g = ∇Φ        with        Φ = gx·x + gy·y + gz·z
```

which means `−∇p + g = −∇(p − Φ)`. Substitute `P = p − Φ` and the momentum
equation is *literally* the gravity-free one. So the flow is whatever it was,
and the pressure gains a hydrostatic term. This is why a swimming pool doesn't
develop currents.

**So the solver solves for `P` and never touches `g`.** That is the entire
implementation. `p` in the code *is* the reduced pressure: the predictor has no
body-force term, the Poisson operator has no gravity in it, the outlet keeps
its plain `p = 0`, and the multigrid never learns that gravity exists.
`phiCell()` adds `Φ` back in the one place that writes pressure out — the VTK
scalar, which is also what the restart reads back — and `setInitialState()`
takes it off again when a frame is read back in. `gx`, `gy` and `gz` appear
nowhere else.

The potential is measured from `(Lx, Ly/2, Lz/2)` — the outlet plane, at the
middle of the height and the middle of the depth — so the `gz` term is
`gz·(z − Lz/2)` and is identically zero at `gravityTilt = 0`, which is what
makes a plane run's pressure field the pressure field it always was.

Measured. 128×64, `nu=0.005`, `g = 9.81` straight down, 98 steps:

```text
uFace and vFace, gravity on vs off     bit-identical
p(on) − p(off) against ρΦ              max error 1.05·10⁻⁶ Pa
```

Not "small" — *identical*, the same bytes, because gravity does not enter a
single arithmetic operation of the solve. And the pressure differs by exactly
the hydrostatic head, to the last digit a float of order 6 Pa has.

**Why not the obvious way.** The first version did the obvious thing: a `dt·gx`
term in the predictor for the projection to cancel. It works, and it drags two
problems in behind it.

The outlet is the only Dirichlet condition in the operator and it holds
`p = 0` on the face, while the hydrostatic field wants `Φ` there. Those are
different demands. Left alone the outlet is pinned to a pressure the field
cannot reach, and the mismatch drives a jet of order `√(g·Ly)` — with `g = 9.81`
and `Ly = 1`, several times the inlet velocity. It doesn't look like a bug, it
looks like gravity working, which is the worst way for a bug to look:

```
|u|max, gravity off                    1.386   (steady)
|u|max, gravity on, outlet left alone  3.671   (still climbing at t = 0.38)
|u|max, gravity on, outlet corrected   1.386   (identical to gravity off)
```

Correcting it took a `2Φ/dx²` term on the right-hand side and a matching
`+2·dt·(p − Φ)/dx` in the corrector. And even corrected, `Φ` was still sitting
inside `‖rhs‖`, which is what the multigrid measures its *relative* residual
against — so the same `mgTolerance` bought a looser solve with gravity on than
without, and divergence went from `8·10⁻⁵` to `6·10⁻⁴` purely from stopping a
cycle early.

Working in `P` from the start deletes all of it. No predictor term, no outlet
special case, no inflated `‖rhs‖`, and no large number added at one end of the
step and subtracted at the other in single precision. Six sites in the
predictor and one boundary condition became one inline function called twice on
the way out, and the CUDA path never had anything to say about it either way.

**So why implement it at all?** Because what survives is the part that will
still be needed when the cancellation stops working. Gravity starts driving
flow the moment density stops being constant — a second phase, or a Boussinesq
buoyancy term from a thermal solver, both of which are on the list at the top
of this file. Then the force is `g·(ρ(x) − ρ₀)/ρ₀`, `Φ` is no longer a potential
for it, `P = p − Φ` no longer removes it, and it has to come back into the
predictor as a real body force — that time earning its place. The direction
convention, the config plumbing, the restart handling and the output path are
already right and stay put. And in the meantime the pressure field in ParaView
is a real pressure field, hydrostatic head included, rather than one with
gravity quietly left out of it.

### `solvePoisson` — build the right-hand side, then solve

The RHS is one line of physics per cell:

```cpp
div = (u*[i+1,j,k] − u*[i,j,k])·invDx
    + (v*[i,j+1,k] − v*[i,j,k])·invDy
    + (w*[i,j,k+1] − w*[i,j,k])·invDz;
rhs = div / dt;
```

Flux out the right face minus flux in the left, plus top minus bottom, plus back minus front. That's net mass creation. Divide by `dt`. At `nz = 1` both `w` faces are shut at zero, the third line is `0 − 0`, and what is summed is the two terms it always was.

Then hand it to the multigrid, which is §5.

### `corrector` — apply the fix

```cpp
u[i,j,k] = u*[i,j,k] − dt·(p[i,j,k] − p[i−1,j,k])·invDx;
v[i,j,k] = v*[i,j,k] − dt·(p[i,j,k] − p[i,j−1,k])·invDy;
w[i,j,k] = w*[i,j,k] − dt·(p[i,j,k] − p[i,j,k−1])·invDz;
```

Two adjacent pressures, subtract, scale. Done, three times. Notice how clean this is *because* of staggering — and that the third line is the first line with a different stride, which is the whole reason the third dimension was cheap to write.

The outlet face is the exception, since it is the one place with a prescribed pressure rather than a prescribed velocity — see §5, and *Gravity* above for what changes there when gravity is on.

---

## 5. The Poisson operator — the heart of the thing

This is the part worth understanding properly, because it's where correctness lives.

### The core requirement

The three operators — the **divergence** that builds the RHS, the **Laplacian** that gets solved, and the **gradient** the corrector applies — must satisfy `Laplacian = divergence ∘ gradient` **exactly, face by face**. Not approximately. Not "except at boundaries."

If they disagree at even one face, then at that cell `∇·uⁿ⁺¹ ≠ 0` no matter how perfectly you solve the linear system. And since the error persists into the next step, it accumulates. Forever.

### How boundaries are handled

The trick is that **boundary conditions live in the coefficients**, not in a separate fix-up pass. The operator for each cell is:

```
(L p)ᵢⱼₖ = cW·p(i−1,j,k) + cE·p(i+1,j,k)
         + cS·p(i,j−1,k) + cN·p(i,j+1,k)
         + cF·p(i,j,k−1) + cB·p(i,j,k+1) − diag·pᵢⱼₖ
```

Seven points in a volume, and the same five it always was in a plane: at `nz = 1` the front and back faces are shut, `coefF` and `coefB` are zero everywhere, and the two extra terms contribute nothing to the sum or to the diagonal. That is the level at which "bit for bit" is decided — a coefficient that is exactly zero costs the answer nothing, whereas one that is merely small does not.

Each coefficient answers one question: **is this face something the corrector will update?**

| Face | Is velocity there prescribed? | Coefficient |
|---|---|---|
| between two fluid cells | no, corrector owns it | `1/dx²`, `1/dy²` or `1/dz²` |
| touching a solid cell | yes — it's 0 | **0** |
| inlet, `i=0` | yes — it's `U0` | **0** |
| walls, `j=0` and `j=ny` | yes — it's 0 | **0** |
| front and back, `k=0` and `k=nz` | yes — closed, or whatever `bcFront`/`bcBack` prescribe | **0** |
| outlet, `i=nx` | no — free to adjust | Dirichlet, see below |

The logic is beautifully simple: **if the corrector can't change the velocity on a face, that face contributes nothing to the Laplacian.** A closed coefficient and a prescribed velocity are the same statement.

The outlet is the one interesting case. We want `p = 0` *on the face*, but pressure lives at cell centres, half a cell away. So use a ghost value `p_ghost = −p(nx−1,j)` — then the average of the two, which is the face value, is exactly zero. Its contribution to the Laplacian is `(p_ghost − p)/dx² = −2p/dx²`: nothing in the numerator, `2/dx²` added to the diagonal. And the corrector applies the matching gradient:

```cpp
u[nx,j] = u*[nx,j] + 2·dt·p[nx−1,j]·invDx;
```

Two payoffs: (a) with one real Dirichlet condition the matrix is non-singular, so no pinning and no drift; (b) the outlet velocity is *corrected by the pressure*, so the solver balances outflow against inflow by itself. Measured mass error: 0.00000%.

Being the only Dirichlet condition also makes it the only place a body force would have to be told anything — which is exactly why gravity is not implemented as one. See *Gravity* in §4.

### The identity

Substitute the corrector into the divergence and everything telescopes:

```
∇·uⁿ⁺¹ = ∇·u* − dt·(L p)
```

with exactly that `L`. Set `rhs = ∇·u*/dt`, solve `L p = rhs`, and you get `∇·u* − dt·(∇·u*/dt) = 0`. Guaranteed by construction.

### What's actually stored

Eight float arrays per grid: `cW, cE, cS, cN, cF, cB, diag, invDiag`. Built once in `buildCoefficients()`, because the geometry never changes during a run. The solver loop becomes:

```cpp
num  = cW[id]*p[id−1]     + cE[id]*p[id+1]
     + cS[id]*p[id−nx]    + cN[id]*p[id+nx]
     + cF[id]*p[id−nx*ny] + cB[id]*p[id+nx*ny];
pNew = (num − rhs[id]) * invDiag[id];
p[id] += omega * (pNew − p[id]);
```

`cF` and `cB` are read unconditionally, and in a plane they are all zero, so the two extra multiply-adds happen and contribute exactly nothing. That is the same trade the wall arrays in §8 make and it is made for the same reason: two more loads and two more `_mm256_fmadd_ps` per cell beat a branch, and a multiply by a hard zero is exact.

No branches. No "is this solid?" tests. No division — `invDiag` is precomputed, and division is ~20 cycles in the innermost loop of the whole program.

Cells that shouldn't be solved (solid, or fluid completely walled in) get `invDiag = 0`, so `pNew = 0` and the update does nothing. The masking handles solids for free.

---

## 6. Multigrid — why the solve is fast

### Why plain iteration is hopeless

Gauss-Seidel/SOR updates each cell from its four neighbours. Information moves **one cell per sweep**. On a 256-wide grid, a pressure change at the inlet needs 256 sweeps just to *reach* the outlet, and thousands to converge. That's `O(N²)` work per time step, and it's exactly what the old solver was doing — hence 70 ms/step.

### The insight

Watch what SOR actually does to the error. After two or three sweeps, the error is **smooth** — all the jagged, cell-to-cell wiggle is gone. What remains is a broad, gentle shape spanning the whole domain, and SOR barely touches it, because a smooth error looks locally like a constant and a constant is already "solved" for a Laplacian.

So SOR is excellent at high-frequency error and useless at low-frequency error.

But here's the thing: **smooth error is smooth relative to the grid spacing.** Put it on a grid with twice the spacing and it's no longer smooth — it's now high-frequency, and SOR eats it. Recurse.

### The V-cycle

```
at level L:
  smooth 2×                    kill the high frequencies here
  r = rhs − L·p                what's left over
  restrict r to level L+1      it's smooth, a coarser grid can hold it
  recurse                      solve L·e = r there
  p += prolongate(e)           bring the correction back up
  smooth 2×                    clean up interpolation artifacts
```

At the coarsest level the grid is tiny, so just hammer it with 50+ sweeps until it's solved.

Cost in a plane: each level is 4× smaller, and `1 + ¼ + ¹⁄₁₆ + … = ⁴⁄₃`, so a whole V-cycle costs about the same as ~3 fine-grid sweeps. In a volume, where all three axes coarsen, each level is 8× smaller and the series is `1 + ⅛ + ¹⁄₆₄ + … = ⁸⁄₇` — the cycle gets *relatively* cheaper as the dimension goes up, which does not make a volume cheap but does mean the hierarchy is not where a volume's cost goes. And each cycle cuts the error by **~10×, independent of grid size**. That's the whole point: `O(N²)` becomes `O(N)`, and the iteration count stops caring how big your grid is.

Two V-cycles per time step is usually enough, in a plane and in a volume alike — at `nz = 1` the solve returns the 2D field bit for bit, with the same level count and the same cycle count as before the port.

### Restriction and prolongation must match

Going down (**restriction**) and coming back up (**prolongation**) can't be designed independently. The requirement is `R = Pᵀ / (cells per coarse cell)`.

When that holds, the coarse-grid correction is an orthogonal projection in the energy norm — it provably **cannot make the error worse**. When it doesn't hold, you have no guarantee, and the two-grid operator can have spectral radius above 1, meaning each V-cycle *amplifies* error. This was the bug that made 100×100 explode while 128×128 worked fine.

So `P` is cell-centred bilinear (a fine cell sits ¼ of a coarse cell off-centre, giving weights ¾ and ¼ per axis), and `R` is computed as its literal transpose — implemented as a gather so OpenMP doesn't need atomics.

In a volume it is trilinear, and that word is doing less work than it sounds like: the 3D transfers are the **tensor product** of the same 1D transfer this solver already had, applied once per axis. There is no new interpolation stencil to get wrong, no new weights, and nothing that has to be kept in step with the 2D version — the 2D one *is* the 3D one with the third factor omitted, which is exactly what happens when an axis does not coarsen. The requirement `R = Pᵀ / (cells per coarse cell)` survives the product, because it holds factor by factor.

Three extra wrinkles:

- **Only coarsen even cell counts.** An odd count leaves the last coarse cell covering one fine cell instead of two, its column of `P` carries half the weight of the others, and the coarse grid gets a residual that's half as big as it should be. Inconsistent → divergence.
- **Semi-coarsening.** A point smoother only damps error in the direction it's strongly coupled to. On a grid with `dx ≪ dy` the `y` coupling (`1/dy²`) is tiny, so `y` error survives and the cycle stalls. So when the aspect ratio is worse than 2:1, only the over-resolved axis gets coarsened, driving the coarse grids toward isotropy.
- **Each axis coarsens independently**, which is the same rule as the one above and is what makes a plane fall out of it for free: `nz = 1` is odd, and it is also as anisotropic as a grid gets, so `z` never coarsens, the hierarchy is the 2D hierarchy, and the level count is the level count it always was. A volume 256×256×16 does the same thing for the same reason — it coarsens x and y for a while before z joins in.

---

## 7. The solid body

The obstacle isn't a mesh — it's a **mask**. `Mesh` rasterises the geometry into a per-cell `solid` array of 0s and 1s: a slice through an STL/OBJ at `nz = 1`, the voxelised model at `nz > 1`, or the verification circle-or-sphere when no file is given. Immersed boundary, simplest flavour, and the rest of this section does not care which of those filled the array.

From that, `buildFaceMasks()` derives which faces are open:

```cpp
uOpen[idxU(i,j,k)] = !solid[idxP(i,j,k)] && !solid[idxP(i−1,j,k)];
wOpen[idxW(i,j,k)] = !solid[idxP(i,j,k)] && !solid[idxP(i,j,k−1)];
```

A face is open only if fluid sits on both sides — and `w` faces are the same statement one stride further out, `nx*ny` instead of `1`. The corrector zeroes velocity on every closed face, which enforces no-slip, and the same mask closes the corresponding Poisson coefficient — so the operator and the boundary condition are automatically consistent. One mask, two uses, no way for them to drift apart.

On coarse multigrid levels, a cell is solid only if **all** the fine cells it covers are solid, so a partially-blocked coarse cell still carries fluid and the body stays visible at every level.

### Numbering the bodies

Nothing above cares how many obstacles there are — the mask is just cells. Wall motion does, because two bodies can spin differently, so the mask is flood-filled once into numbered components before the run starts: **8-connected in a plane, 26-connected in a volume**. Diagonal connectivity is the right choice here for the same reason the mask works at all: two cells meeting at a corner leave no face for the flow to pass through, so calling them two obstacles would be a lie about the geometry as well as a nuisance to configure. 26 is that same argument counted over the neighbours a cell in a volume actually has — the 6 across faces, the 12 across edges and the 8 across corners — and nothing about it is a new rule. The fill is an explicit stack, not recursion, because a body can be the whole grid.

Each component keeps its cell count, its centroid — the axis rotation turns about — the distance to its farthest cell, which is the radius `rot` is turned into a rim speed at, and, since the port, its **volume** and its **full inertia tensor about that centroid**. The last two are what a free body needs and they come out of the same pass, so they are always the inertia of the shape that is in the grid rather than of the shape somebody meant to put there.

### Wall behaviour, discretely

Closed faces come in two kinds, and this is the whole trick:

| Closed `u` face | Solid on | Is | Holds |
|---|---|---|---|
| `i` | one side | a vertical wall, and this face is *normal* to it | **0** |
| `i` | both sides | a face buried in the body | `2·u_wall − u_fluid` |

A face normal to a wall stays shut, exactly as before, because the body is not going anywhere. A face inside the body looks useless — no fluid cell owns it — but it is not: the fluid `u` face one row above reads it as `u_bot` in `d²u/dy²` and in the upwind term. That read *is* the no-slip condition along that stretch of horizontal wall. `v` faces mirror it: the ones inside the body are what the fluid to the left and right of a vertical wall shears against.

**And the value it has to hold is not the wall's velocity.** This is worth being careful about, because the obvious choice is wrong and it is wrong by exactly a factor of two. The wall is a *cell edge*. The buried face sits half a cell inside it, the fluid face half a cell outside, and the wall is the midpoint of the two. Whatever a stencil reading across the pair sees at the wall is therefore the **average** of the two faces. Put `u_wall` on the buried face and the wall ends up holding `(u_fluid + u_wall)/2`, which is not the boundary condition anybody asked for: for a stationary wall the fluid slides along it at half its own near-wall speed, the shear `∂u/∂y` comes out as `(u_fluid − u_wall)/dy` instead of `(u_fluid − u_wall)/(dy/2)`, and **every body in the domain has half the drag it should**. A moving wall has the same problem from the other end — it drags the fluid at half strength.

The value that puts `u_wall` on the wall is the mirrored one, `2·u_wall − u_fluid`. It is the standard ghost-cell reflection, and here it costs nothing extra: the pairing machinery already exists for free-slip (below), the no-slip version is the same list with a different expression on it, and the wall velocity is evaluated at the cell edge the wall actually is rather than at either face's own position. On a body thin enough to have fluid on both sides, one buried face serves two walls and takes the average of the two, which is the same code path with the same index twice.

It costs no stability. The mirror raises the diagonal of the viscous operator at a near-wall face from `2/dy²` to `3/dy²` and drops the corresponding off-diagonal by the same amount, so the Gershgorin bound on the spectrum — and with it the diffusive `dt` limit — does not move. The convective term picks up the same half-cell distance, which is the distance it should be differencing over.

The predictor and corrector write `uMask*(...) + uWall`, which resets the buried faces to the plain wall value every step, and the mirror is re-applied at the top of the next one from the field as it then stands. Same lifecycle as free-slip, same place in the loop.

So a horizontal stretch of wall is driven through the `u` faces inside the body, a vertical stretch through the `v` faces, a stretch lying across the depth through the `w` faces, and every staircase in between gets whichever of the three it presents. Nothing about the argument changes with the third axis: it is the same table, the same mirror, and one more kind of buried face. The predictor and corrector change by one term:

```cpp
uStar = uMask*( ... ) + uWall;     // uWall is nonzero only on the buried faces
u     = uMask*( ... ) + uWall;
```

`uWall` and `vWall` are built once, next to the masks, because neither the geometry nor the motion changes during a run.

**Why the Poisson operator does not change.** A coefficient is zero when the corrector does not own the face — and it still does not own it, whatever value that face now holds. So `L` is the same matrix, the identity `L = div ∘ grad` is untouched, the projection still projects, and the CUDA path needed no changes at all. The RHS in *fluid* cells does not move either: every face a fluid cell owns that touches solid is a face normal to a wall, and those are still zero.

**Why nothing leaks.** A rigid-body velocity is discretely divergence-free, exactly, with no truncation error. In a plane, `u = slideX − ω(y − cy)` depends only on `y`, and both vertical faces of a cell sit at the same `y`, so `∂u/∂x` differences to a hard zero; `v` depends only on `x` and the same happens vertically. In a volume the same thing holds for the same reason: **v = slide + ω × (x − centre)** has no component that depends on its own coordinate — `u` is built from `y` and `z`, `v` from `z` and `x`, `w` from `x` and `y` — so every one of the three differences to a hard zero across the faces it is measured between, and `∇·v` is exactly nought cell by cell. That is a property of a cross product, not of the discretisation, which is why it survives the extra axis untouched. Filling a body with its own motion therefore creates no mass anywhere inside it, and by the discrete divergence theorem none crosses its boundary either. This is also the discrete version of *why the normal component is dropped*: keeping it would be the statement that the mask moves, and the mask does not.

**Free-slip is the same table with one entry changed.** No-slip mirrors the buried face about the wall's own velocity. Free-slip wants the opposite — no tangential stress, `∂u_t/∂n = 0` — which discretely means the buried face has to hold *whatever the fluid face across the wall currently holds*, so the difference the viscous stencil takes across the wall is zero and the upwind term through it is zero too. It is exactly the treatment the domain's own top and bottom walls have always had (`u_bot = u_ij` at `j = 0`), applied to a body.

That value is not a constant, so it cannot live in `uWall`. What is constant is *which* face mirrors *which*, so that pairing is what gets precomputed: one list of `(buried face, the open faces beside it)` per axis, built once after the masks, refreshed at the top of every step. A buried face with fluid on one side takes it exactly; on a body thin enough to have fluid on both sides it takes the average, which is the same expression with the same index twice, so there is one code path and no branch.

The mirrored values never reach the pressure solve. The predictor writes `uMask*(...) + uWall` into `u*`, and on a buried face the mask is zero — so `u*` holds the wall value, not the mirrored one, and the divergence, the right-hand side and the operator are exactly as they were. Free-slip is purely a change to what the *viscous and convective* stencils read across a wall, which is precisely what it means physically. And no-penetration is untouched: the faces normal to a wall are still shut, because free-slip removes friction, not the wall.

**The one thing that does need cleaning.** Solid cells have a zero diagonal and are never solved, so their residual is just their right-hand side — and `‖rhs‖`, which the whole grid's relative tolerance is measured against, is summed over every cell. A boundary solid cell has one face at zero and another at the wall velocity, so its divergence is not zero, and left alone a spinning body would inflate `‖rhs‖` and quietly loosen the tolerance everywhere (the same trap gravity fell into, one section up). So the RHS is multiplied by a fluid-cell mask on the way out. Without moving walls that multiply changes nothing — the divergence in a solid cell is already exactly zero — which is why it can be there unconditionally.

---

## 8. The speed layer

Everything above is the algorithm. This part is just making it run fast without changing a single number.

**Branch-free hot loops.** Every velocity on a closed face is held at a value that does not change during the run — zero, or the wall's own velocity if that wall moves. So the stencil can be evaluated unconditionally, the worst it ever reads being a legitimate boundary value, and the result multiplied by a float mask and offset by a float wall array. No branch, no scalar fallback near the body, and no `ω × r` evaluated per face per step — the two arrays are built once, next to the masks.

That add is unconditional, which is the deliberate part: one extra load and one extra `_mm256_add_ps` per face beats a branch, and with nothing moving the array is all zeros and the arithmetic is exact. On 256×128 the cost does not come out of the run-to-run noise (0.561–0.564 s before, 0.560–0.565 s after), and the fields are identical — literally, except that a closed face now holds `+0` where it used to hold `−0`, which nothing in the solver can tell apart.

**Halo padding.** Each pressure array is allocated with `max(nx,8)` extra floats at *both* ends, with the logical pointer offset into the middle. So `p[id−1]`, `p[id+nx]` etc. are always valid memory, even for the very first and last cell. An 8-wide AVX load at the edge reads into the halo (which is zero, and whose coefficients are zero anyway) instead of segfaulting. This is what makes the branch-free vector code *safe*, not just fast.

**Red/black SOR with masked stores.** Every neighbour of a red cell is black, so all red cells can update simultaneously. The naive version strides by 2, which kills SIMD. Instead: compute the update for all 8 lanes, then store only the current colour with `_mm256_maskstore_ps`. Half the arithmetic is discarded, but every load and store stays contiguous — a big net win on a memory-bound kernel. Since vectors always start at even `i`, the lane mask is one of exactly two constants.

**OpenMP.** Rows go to different threads, and in a volume it is `(k, j)` pairs that go to different threads — the directives carry `collapse(2)`, which is the one thing in the port that costs something outside the solver itself, because `collapse` is OpenMP 3.0 and the *Requirements* section says what that means for MSVC. The reason it is there is a plane: at `nz = 1` the `k` loop has exactly one trip, so parallelising the outermost loop alone would hand the whole grid to one thread and quietly make every existing 2D run single-threaded. Collapsed, a plane parallelises over its rows exactly as it used to and a volume parallelises over both. Red/black makes this race-free with no locks. One fork/join per `smooth()` call, and levels under 32 rows run serially — they're a few hundred cells visited by every cycle, and barrier traffic there costs more than the arithmetic.

**And all of it is optional.** Every vector kernel in here already had a scalar
loop after it, for the last `nx % 8` cells of a row — so the fallback for a CPU
without AVX2 is not a second implementation, it is the same loop starting at
`i = 0` instead of where the vector one stopped. The red/black smoother is the
neat case: its remainder loop already strides by 2 from the right parity, so
skipping the vector part turns it into a plain red/black sweep with nothing
else changed. Guarded on `__AVX2__`, so `-march=native` picks the fast path up
by itself. Measured against the vector build on a moving-wall run, the two
agree to `2·10⁻⁷` relative on velocity and `6·10⁻⁶` on pressure — float
rounding from a different summation order, not a different algorithm.

**CUDA.** Same algorithm, one thread per cell. The whole hierarchy is allocated once, the stencil is uploaded once (it's a function of geometry, and geometry is static), and the pressure field **stays resident on the GPU between time steps** — which means last step's solution is a free warm start. Only the RHS crosses the bus each step. Kernels run on the default stream, which serialises them, so the chain `smooth → residual → restrict → recurse → prolongate → smooth` needs no explicit synchronisation.

The volume version mirrors the CPU one **kernel for kernel**: a 2D block over `(i, j)` and one grid layer per `k`, which keeps the coalesced reads along `i` that the 2D launch already had and adds depth in the one place that costs nothing. Every kernel was checked against the CPU path with the GPU out of the picture and is **bit-identical** to it. That is the standard this codebase holds the backends to and the reason `BackendAgreementTests` exists: two implementations of the same solver that agree to five digits are two solvers.

---

## 9. Output

A frame is written as `DIMENSIONS nx+1 ny+1 nz+1` with a real `SPACING dz` and `CELL_DATA nx*ny*nz`, and `VECTORS velocity` whose third component is now an actual `w` rather than a column of zeroes kept for ParaView's benefit. A plane writes `nz+1 == 2`, which is the same file it always wrote: one cell deep, third component zero.

`saveVTK` byte-swaps values into a 16 KB stack buffer and writes binary legacy VTK straight out — no temporary arrays, no per-cell copies. Pressure is stored internally as `p/ρ` (kinematic), so it's multiplied by `ro` on the way out to give Pascals. Btw kinematic pressure is much easier to use cause if u divide regular pressure by density u get m^2/s^2, not some kg/(m*s^2)

One cell class gets special treatment on the way out. Solid cells have a zero diagonal and never take part in the solve, so their pressure sits at a permanent zero. Without gravity that zero is somewhere in the middle of the fluid range and nobody notices. With it the fluid is offset by the hydrostatic head, and the body would punch a visible hole straight through the pressure map — so solid cells are written with the hydrostatic value they would have had. Nothing ever reads them back; this is purely what ParaView sees.

The velocity of the same cells gets the same treatment for the same reason. A cell centre value is the average of its two faces, and inside a body those faces hold the mirrored value the no-slip condition needs — an artefact of the stencil, not a speed anything moves at — while on the rim one of them is normal to the wall and held shut. Either way the average is not a velocity. Solid cells are written with the surface velocity they actually impose instead: zero for a body that does not move, and for one that does, the thing that makes a spinning body look like a spinning body and a sliding one like a belt. Again: output only, never read back.

### What a frame is made of

About 19 bytes a cell, and nothing in it is stored twice.

| Array | Bytes/cell | |
|---|---|---|
| `pressure`, float, Pa | 4 | what ParaView colours |
| `velocity`, 3×float | 12 | what ParaView glyphs; the third component is real now, and was always taking up the space |
| `solid`, `unsigned_char` | 1 | the mask holds 0 or 1, and `bit` is not a type ParaView reads reliably |
| `facePack` | ~2.2 | the staggered face velocities — `u`, `v` and, in a volume, `w` |
| `configText` | ~0.1 | the run's own settings |

The `facePack` figure is measured on a plane. A volume carries a third face array through the same predictor and the same varint, and the block stays **lossless to the bit** either way; what it comes to per cell for a given volume is a property of that flow, and there is no measured number for it here to quote.

A compressible frame carries density, the velocity vector and pressure as well, and it used to carry the five conserved variables on top of them — which are those same three rearranged. Twenty of the forty-one bytes a cell were the second copy of the first twenty-one. They are not written any more: the restart rebuilds them from what is there, exactly, and `RestartTests` checks that a run cut in half and continued still reproduces the run that was never cut. That is where `frameState` comes in. `slim`, the default, drops only what can be put back exactly — half a compressible frame, nothing of an incompressible one — so the guarantee is unchanged and the file is smaller. `minimal` also drops the packed face velocities, which is a sixth off an incompressible frame and makes a continuation *close* rather than exact, because the faces then have to be rebuilt from the cell averages and projected. `full` writes everything down, for when you want the frame to be the whole truth on disk rather than a recipe for it.

There is no pressure array for the restart to read: `SCALARS pressure` is the same field multiplied by `ro`, bit for bit, so the reader divides `ro` back out — taking the density from the frame's own configuration text rather than from the run being started, in case that changed.

The face velocities cannot be dropped the same way. The cell velocity is an average and averages do not invert. But they are *nearly* determined by it: along a row, `u_cell[i] = (u[i] + u[i+1])/2` gives `u[i+1] = 2·u_cell[i] − u[i]`, and marching that from the inlet face reproduces the whole row to about 3e-6 m/s on a grid 256 wide. Close, and not close enough — the error accumulates along the march and lands as a divergence blip of the same order as the solver's own convergence.

So the frame stores the march's **mistake** rather than the answer. For each face: predict it, subtract the prediction's bit pattern from the true one, write the difference as a zigzag varint. An exact prediction costs one zero byte, one ulp out either way costs one byte, a real miss costs five. About one face in eight is predicted exactly, the block comes out at 2.2 bytes a cell against the 8.1 the raw arrays need, and it is **lossless** — the reader adds each delta back before predicting the next face from the corrected value, so nothing accumulates and there is no drift to bound.

Three things make it safe to rely on:

- The prediction is `2.0*cell − previous` in double, and nothing else. In float it would overflow above 1.7e38 unless the compiler contracted it into an FMA, and whether it does depends on `-mfma`; doubling a float is exact in double whatever the compiler does, and the single conversion back rounds once. Writer and reader call the same inline function.
- The two lines the march starts from, `u` at `i = 0` and `v` at `j = 0`, are written out in full rather than derived. That is one column and one row, a fraction of a percent, and the reader never has to reproduce how the inlet or the bottom wall were set on the run that wrote the frame.
- A 32-bit FNV-1a over the faces goes in the block header. On a mismatch — a truncated file, a bad byte — the reader says so and falls back to rebuilding the faces from the cell averages with one projection. It cannot silently restart from something subtly wrong.

**Frames carry a `formatVersion`, currently 3.** A build reads every frame at or below its own version: version 1 spelled `uFace`, `vFace` and `pRaw` out as plain float arrays and had `solid` as `int32`; version 2 introduced the packed face block; version 3 is the volume — three `DIMENSIONS` tokens that can each exceed 1, a real `dz`, `w` faces inside the packed block, and `bodyState` as a key/value record rather than a fixed run of seven numbers.

**Every one of them still loads.** A version 1 or version 2 frame comes back as a volume one cell deep with `w` zero — which is not a conversion, it is what those frames always were — a version-2 packed block still unpacks, and the old seven-number `bodyState` is still read. That is the whole point of having spent a version number on this: an old run can be continued by a new build, and the run does not have to know which era it came from.

The reverse does not hold, and it did not before either — a version 1 reader takes four bytes a cell for `solid`, desynchronises inside the file, and cannot be rescued after the fact, because the configuration text that carries the version sits at the *end* of the frame. The version is there so that a reader can name the problem instead of reporting whatever binary garbage it lands on.
