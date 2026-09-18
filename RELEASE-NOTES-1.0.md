# Fluid Solver 1.0 — release notes

Everything here is new since **0.2**, which is the last thing that was
published. 0.3 through 0.9 are branch names in the history and nothing else —
they were never tagged, never uploaded, and never had a release page. If you
are on 0.2, this is the next release, and it is most of the program.

**The version is 1.0, and the repository is called `Fluid-Solver` now.** It was
`CFD-Solver-2D` up to 0.2, which stopped being true the moment it started
solving volumes, and would have stopped being true again when the deformation
and electrodynamic solvers arrive. GitHub redirects the old address so saved
links still work, but an existing checkout needs

```
git remote set-url origin https://github.com/MihanN1/Fluid-Solver.git
```

before it pushes. The CMake project, `CFD_REPO_NAME` (which is what the update
check asks GitHub about), the startup banner, the interactive configuration
header and the title line of every `.vtk` frame all carry the new name. The
executable itself is still `Fluid Solver` / `Fluid Solver.exe` - that never was
the repository's name and has not changed, so no archive, installer or script
that names the binary needs touching.

---

## Contents

- [What this release is](#what-this-release-is)
- [Cookbook: how to actually do the new things](#cookbook-how-to-actually-do-the-new-things)
- [The headline: it solves a volume](#the-headline-it-solves-a-volume)
  - [`nz = 1` is the plane, and it is the same answer](#nz--1-is-the-plane-and-it-is-the-same-answer)
  - [What the port had to touch](#what-the-port-had-to-touch)
  - [What a volume costs](#what-a-volume-costs)
- [Numerical solver](#numerical-solver)
  - [Gravity, in both its formulations](#gravity-in-both-its-formulations)
  - [Boundaries have names now](#boundaries-have-names-now)
  - [Cases: the cavity, the shock tube](#cases-the-cavity-the-shock-tube)
  - [Stopping when the flow settles](#stopping-when-the-flow-settles)
  - [Convection and time](#convection-and-time)
  - [Two fluids with an interface](#two-fluids-with-an-interface)
  - [Surface tension and the contact angle](#surface-tension-and-the-contact-angle)
  - [Fluids that mix](#fluids-that-mix)
  - [Flow sources](#flow-sources)
  - [Moving walls](#moving-walls)
  - [Bodies that travel](#bodies-that-travel)
  - [Turbulence, and the viscous term that was wrong before it](#turbulence-and-the-viscous-term-that-was-wrong-before-it)
  - [The compressible solver](#the-compressible-solver)
  - [Two gases](#two-gases)
  - [The sound it makes, and the microphones](#the-sound-it-makes-and-the-microphones)
  - [The stretched grid](#the-stretched-grid)
  - [Adaptive mesh refinement](#adaptive-mesh-refinement)
  - [The pressure solver](#the-pressure-solver)
  - [Extra diagnostic fields](#extra-diagnostic-fields)
- [Geometry](#geometry)
  - [Several models at once](#several-models-at-once)
- [Output and restart](#output-and-restart)
- [Tests](#tests)
- [Release and installer tooling](#release-and-installer-tooling)
- [Visualization: the desktop UI](#visualization-the-desktop-ui)
- [Fixes](#fixes)
- [If you have old files or old scripts](#if-you-have-old-files-or-old-scripts)
- [Known limits, and what is not done](#known-limits-and-what-is-not-done)
- [The 0.2 Future Work list, settled](#the-02-future-work-list-settled)

---

## What this release is

0.2 was the base solver: Chorin projection on a MAC grid, a multigrid pressure
solve with AVX2, OpenMP and a CUDA mirror, STL/OBJ geometry cut to a plane
section, and continuing a run from a frame. `include/Config.hpp` was 66 lines.
There were no boundary kinds, no gravity, no second fluid, no moving anything,
no turbulence, no compressible solver, no adaptive refinement, and no tests.

1.0 is that solver with the whole of the 0.2 *Future Work* list built on top of
it, and then ported from a plane to a volume.

Counted rather than estimated, in the two trees side by side:

| | 0.2 | 1.0 |
|---|---|---|
| `src/*.cpp` + `src/*.cu` | 5,965 lines | 22,680 lines |
| `include/Config.hpp` | 66 lines | 462 lines |
| configuration keys | 27 | 115 |
| test suites | 0 | 14, plus 10 in the UI |
| solver source files (`.cpp` + `.cu`) | 12 | 20 |

Three things are worth knowing before any of the detail.

**Nothing you already run runs differently.** The golden master is 88 frames
across ten configurations — plain, fine, odd cell counts, gravity, moving
walls, free slip, imported geometry, AVX2 off, OpenMP off and a restart —
compared field by field against the frames **0.2** wrote, at a relative
difference of exactly `0.000e+00`. Every branch since has re-measured it, the
third dimension included. Twice during the optimisation passes it was not zero,
and both times the change was reverted rather than the master updated, because
`* invNorm` is not the same float as `/ norm`.

That is about the *values*. The frame header did change and the file is not
byte-identical — see [If you have old files or old scripts](#if-you-have-old-files-or-old-scripts).

**Everything new is off by default.** `nz` defaults to 1, `gravityEnabled` to
0, `phases` to 1, `turbulence` to `none`, `regime` to `incompressible`,
`surfaceTension` to 0, `wallMotion` and `bodyMotion` and `sources` and
`profiles` to empty, `amrLevels` to 0, `gridStretch` to `off`,
`steadyTolerance` to 0. A command line written for 0.2 means exactly what it
meant, and most of the code below does not execute at all for it.

**It is checked now.** 14 solver test suites and 10 UI suites, all green, and
CI runs `ctest` before it builds a single release row. At 0.2 the release
matrix only ever checked that the thing linked. It always linked; that was
never the problem.

---

## Cookbook: how to actually do the new things

Every section further down explains what a feature *is* and why it works the
way it does. This one is the other half: the exact lines to type, in order,
with what should come back and what usually goes wrong. Everything here was
run before it was written.

`FS` below is whatever the executable is called on your machine —
`"Fluid Solver.exe"` on Windows, `./Fluid\ Solver` on Linux and macOS. Quote
it; the name has a space in it.

### 0. The one-key version

Take any 0.2 command line you already have. Add `nz=`. That is the whole
migration:

```
FS nx=128 ny=64 Lx=2 Ly=1 U0=1 nu=0.002 totalTime=2 saveInterval=25
FS nx=128 ny=64 nz=64 Lz=1 Lx=2 Ly=1 U0=1 nu=0.002 totalTime=2 saveInterval=25
```

The first is the run you have always had, down to the bits. The second is the
same thing in a box 1 m deep. Leave `nz` out and nothing about your run
changes — not the answer, not the speed, not the frames.

**Set `Lz` when you set `nz`.** It defaults to 1.0, which with `nz=64` gives
cells 1/64 m deep. If `dx` and `dy` are 1/64 too, that is a cubic cell and you
want that; if they are not, you have a grid stretched in one direction and the
pressure solve will tell you about it in V-cycles. Cubic cells are the default
you should aim at: `dx = Lx/nx`, `dy = Ly/ny`, `dz = Lz/nz`, make them equal.

**How long will it take.** A volume has `nz` times the cells. There is no way
around that and no clever setting that avoids it. Start at `nz=16` and a short
`totalTime`, look at it, then scale up. The step limit printed at startup —
*"about N steps for T s"* — is the number to watch; if it says two million,
stop and coarsen before you press on.

### 1. Flow past a real body in a duct

This is the case most people actually want. In a plane the model was cut to a
section; in a volume it is voxelised whole.

```
FS geometryFile="C:\models\wing.stl" ^
   nx=192 ny=96 nz=96 Lx=2 Ly=1 Lz=1 ^
   U0=20 nu=1.5e-5 ^
   bcLeft=inlet bcRight=outlet ^
   bcBottom=slip bcTop=slip bcFront=slip bcBack=slip ^
   profiles="wing.stl@x=0.6,y=0.5,z=0.5,size=0.4,ay=8" ^
   totalTime=0.5 saveInterval=20 extraFields=vorticity,speed
```

What each new piece does:

* `nz=96 Lz=1` — the box is now a duct 1 m deep instead of an infinitely thin
  slice.
* `bcFront` / `bcBack` are the two new faces, at `z=0` and `z=Lz`. `slip` makes
  them frictionless walls, which is what you want if you are modelling a wing
  section that continues past the box. `wall` makes them no-slip, which is what
  you want if the duct really has walls there.
* `profiles="...@x=,y=,z=,size=,ay="` places the model. `z=` is new; `ay=` is
  the third rotation angle (`ax=`, `ay=`, `az=` are `sliceAngleX/Y/Z`).
* `size=` is now the longest side of the placed bounding box, not the longer
  side of a section.

**What you should see at startup:**

```
  Number of objects = 1 (these numbers are what wallMotion takes)
    object 1: 56 cells, centre (0.75, 0.5, 0.5) m, rim 0.0963542 m
  Number of placed triangles = 0
```

(that is the fallback sphere on a coarse grid; with a real model the cell count
and the triangle count are both large, and the centre is where you placed it)

If it says `0 cells`, the model missed the box — check `x/y/z` and `size`. If
it refuses with a message about an open surface, the STL is not watertight;
run it through a repair tool, because a surface with a hole in it has no
inside and the voxeliser will not guess one.

**In the UI you do not type that line.** Pick the body in the BODIES group — or
click it in the 3D view — and `Position X/Y/Z`, `Size`, `Tilt X/Y/Z` and `Turn
in plane` are the same settings as rows. To nudge it rather than place it: set
`Move along` to the axis, type the distance into `Move by`, and it moves that
far the moment you press Enter, with the box going back to zero for the next
one. `Turn about` and `Turn by` do the same in degrees. Or do it from the
viewport without the panel at all — click the body, `G`, `x`, `0.25`, Enter.

### 2. A lid-driven cavity in a cube

The 3D benchmark, and the quickest way to convince yourself the port is real:

```
FS caseType=cavity lidSpeed=1 nu=0.01 ^
   nx=64 ny=64 nz=64 Lx=1 Ly=1 Lz=1 ^
   geometryFile=empty totalTime=30 mgIterations=6 saveInterval=50
```

`caseType=cavity` now closes all six sides and slides the top one. The lid
drags along **x** only — a face has two tangential directions and there is one
speed, so the second one is left alone deliberately. `geometryFile=empty` is
what you want whenever you do not want a body: without it the mesh falls back
to its verification sphere sitting in the middle of your domain, which is a
fine way to spend an afternoon wondering why your shock tube is not
one-dimensional.

You should get the primary vortex at roughly `(0.60, 0.77)` in the mid-depth
plane and about 5% of the lid speed going sideways. That sideways motion is the
Ekman recirculation a cube has and a plane cannot; if `|w|max` is zero,
something is wrong.

### 3. Gravity pointing anywhere

`gravityAngle` is unchanged: degrees clockwise from straight down, inside the
xy plane. `gravityTilt` is new: degrees out of that plane, towards `+z`.

```
gravityEnabled=1 gravityAccel=9.81 gravityAngle=0  gravityTilt=0     down  (-y)
gravityEnabled=1 gravityAccel=9.81 gravityAngle=90 gravityTilt=0     -x
gravityEnabled=1 gravityAccel=9.81 gravityAngle=0  gravityTilt=-90   -z
gravityEnabled=1 gravityAccel=9.81 gravityAngle=0  gravityTilt=30    down and forward
```

The startup line tells you what it resolved to, so you never have to guess:

```
Gravity: 9.81 m/s^2 at 0 deg, tilted 25 deg -> g = (0, -8.89088, 4.14589) m/s^2.
```

`gravityMode` still matters more than the direction: `reduced` (the default) is
right for one fluid and puts the hydrostatic head on the output only; `body`
puts a real force in the predictor and is what you need the moment there are
two densities.

### 4. A drop that rises, falls, or sits there

```
FS phases=2 rho1=1 rho2=1000 nu1=1e-5 nu2=1e-6 ^
   phaseInit=drop phaseLevel=0.4 phaseX=0.5 phaseY=0.5 phaseZ=0.5 ^
   gravityEnabled=1 gravityMode=body gravityAccel=9.81 ^
   surfaceTension=0.072 contactAngle=90 ^
   nx=48 ny=48 nz=48 Lx=1 Ly=1 Lz=1 ^
   caseType=cavity U0=0 geometryFile=empty ^
   totalTime=1 mgIterations=8 saveInterval=20
```

`phaseZ` is the new one: where the drop sits in depth, as a fraction of `Lz`.
At `nz>1` `phaseInit=drop` is a **sphere**, and its radius is
`0.5 * phaseLevel * min(Lx, Ly, Lz)` — the `Lz` in that `min` is new and stops
a sphere being silently clipped by a shallow box. `layer` is still a layer
normal to y and `column` still runs along x; both extend through the depth.

`caseType=cavity` here is doing duty as "close all six sides" — with
`lidSpeed` left at its default the lid does not move, so it is just a sealed
box.

**The check that catches a wrong stride:** run the same case four times with
gravity on each axis in turn (`gravityAngle=0`, `gravityAngle=90`,
`gravityTilt=-90`, `gravityTilt=90`). The drop must travel the same distance
every time. It does, to 0.0001%.

### 5. An inlet that is a window, not a whole face

`inletFrom`/`inletTo` cut the inlet along the face's **first** tangential axis,
which is what they always did. `inletFrom2`/`inletTo2` cut it along the
**second**. Which is which depends on the face:

| face | first axis (`inletFrom`/`To`) | second axis (`inletFrom2`/`To2`) |
|---|---|---|
| `bcLeft`, `bcRight` | y | z |
| `bcBottom`, `bcTop` | x | z |
| `bcFront`, `bcBack` | x | y |

A square jet through the middle quarter of the left face:

```
bcLeft=inlet inletFrom=0.375 inletTo=0.625 inletFrom2=0.375 inletTo2=0.625 ^
bcRight=outlet bcLeftSpeed=5
```

The speed of an inlet is `bc<Side>Speed`, not `inletSpeed` — there is no such
key and the run will tell you so and stop. Leave it out and the inlet runs at
`U0`.

`inletProfile` has three values now:

* `uniform` — flat.
* `parabolic` — a parabola along **both** axes of the window. Duct flow.
* `parabolicSpan` — a parabola along the first axis, flat along the second.
  A plane channel extruded in z, which is what you want if you are comparing
  against a 2D result.

All three carry the same flow rate, so switching between them changes the shape
and not the amount. At `nz=1` `parabolic` and `parabolicSpan` are the same
thing, because there is nothing to bend along the second axis.

### 6. Walls that spin about any axis

`wallMotion` is per object, and the objects are numbered by the line the mesh
prints at startup. It gained three settings:

```
wallMotion="1:rotX=45,rotY=0,rotZ=90,slideZ=0.2"
```

* `rotX=`, `rotY=`, `rotZ=` — degrees per second about each axis, right-handed,
  through the object's own centroid. `rot=` and `rotation=` are still accepted
  and both mean `rotZ=`, so every 0.2-era line still means what it meant.
* `slideX=`, `slideY=`, `slideZ=` — metres per second. `slideZ` is new.
* `slip=1` is still exclusive with all of them: a wall that carries no
  tangential stress cannot drag anything.

The surface velocity is `slide + omega × (x − centre)`, so a cylinder with
`rotX` set spins end over end rather than about its length. The startup report
tells you the rim speed it worked out:

```
  object 1 at (0.292, 0.292, 0.292) m: rot = (45, 0, 90) deg/s
      -> rim speed 0.158812 m/s, slide = (0, 0, 0.2) m/s
```

### 7. A body that tumbles

Bodies are quaternions and a real inertia tensor now, which means a free body
in 3D does what a free body in 3D does — including the tennis-racket flip about
its intermediate axis, which no amount of 2D will give you.

Prescribed motion, for when you want the body to go exactly where you say:

```
bodyMotion="1:vx=0.3,vy=0,vz=0.1,omegaX=30,omegaY=0,omegaZ=45"
```

Free motion, for when you want the flow to decide:

```
bodyMotion="1:free=1,density=2700,pinZ=0,pinRotX=0,pinRotY=0"
```

New settings: `vz=`, `omegaX=`, `omegaY=`, `pinZ=`, `pinRotX=`, `pinRotY=`,
`inertiaX=`, `inertiaY=`. The old names keep their meaning — `omega=` is
`omegaZ=`, `pinRot=` is the z one, `inertia=` is `Izz` — so a 0.2 line still
parses and still does what it did.

**You almost never need to give the inertias.** The mesh now computes the full
tensor from the voxelised body, so `density=` alone is enough. Give
`inertiaX=`/`inertiaY=`/`inertia=` only to override one of the diagonal
entries deliberately.

**Pins are how you keep a 3D run sane while you are setting it up.** A body
free in all six degrees of freedom on a coarse grid will tumble, and it should.
If you want a body that only falls, pin the rest:

```
bodyMotion="1:free=1,density=4000,pinX=1,pinY=1,pinRotX=1,pinRotY=1,pinRot=1"
```

That body drops along z at `g` and nothing else. Measured: 9.80 m/s² with
0.002 degrees of spurious spin over the whole drop.

Keyframes take the new components too, in the same syntax as before. `@<t>` is
a comma-separated token like any other setting, so there is a comma in front of
every `@` except the first:

```
bodyMotion="1:density=2700,@0,vx=0,vz=0,@0.5,vx=1,vz=0.4,interp=sine,@1.2,free=1"
```

That body starts still, eases up to 1 m/s along x and 0.4 along z by half a
second, and is let go at 1.2 s to do whatever the flow wants. The `density=` is
not optional here: the moment a `free=1` appears anywhere in the line the run
refuses to start without a `mass=` or a `density=`, because the fluid cannot
accelerate something whose weight it does not know. `interp=` shapes
the segment that *starts* at the key it is written on — `linear` is the
default, and `constant`, `bezier`, `sine`, `quad`, `cubic`, `quart`, `quint`,
`expo`, `circ`, `back`, `bounce` and `elastic` are the rest.

**A moving body needs a real model.** `bodyMotion` moves the body, which means
cutting its outline out of the grid again every step, and the fallback
verification sphere you get with `geometryFile=none` is not a model — it is a
mask. Give the run a `geometryFile=` or a `profiles=` or it will say so and
leave every body where it is:

```
!!! the mask this run started from cannot be moved: it came out of a frame rather than
    a model, and moving a body means cutting its outline again every step. Give the run
    a geometryFile or profiles= and the bodies will move.
```

`wallMotion` has no such requirement: it moves the surface velocity, not the
body, so it works on any mask.

### 8. A jet that points out of the plane

`sources` gained `z=` and `elev=`:

```
sources="x=0.5,y=0.2,z=0.3,r=0.05,rate=2,angle=90,elev=30"
```

`angle` keeps its exact meaning — degrees in the xy plane, 0 is `+x`,
counter-clockwise. `elev` tilts that direction towards `+z`, so the unit vector
is `(cos(elev)·cos(angle), cos(elev)·sin(angle), sin(elev))`. At `elev=0` you
get exactly the 2D direction back.

**Two things to know.** `z=` defaults to 0, not to `Lz/2`, so a source with no
`z=` sits on the front face — say where you want it. And the source is a sphere
now, so the same `rate` pushes a different amount of fluid than it did in a
plane; the startup line prints the total in m³/s and you should read it.

A source can still ride a body with `body=1`, and it is rotated by the body's
orientation, so a thruster on a tumbling body points where the body points.

### 9. Gas, shocks and sound in a volume

```
FS regime=compressible caseType=shockTube geometryFile=empty ^
   nx=200 ny=40 nz=40 Lx=1 Ly=0.2 Lz=0.2 ^
   bcLeft=wall bcRight=wall ^
   bcBottom=slip bcTop=slip bcFront=slip bcBack=slip ^
   CFL=0.4 totalTime=0.0008 saveInterval=50
```

Nothing about the compressible interface changed except that it has a depth
now. The conserved state carries `rhoW`, there is a z flux with the same
reconstruction and the same Riemann solver, and a one-dimensional solution
stays one-dimensional: measured 7e-7 of the density across every column, with
1.8e-5 of transverse momentum, and the mid-column matches a `nz=1` run to
3.9e-5.

Microphones take a third coordinate:

```
acousticFields=1 microphones="x=0.8,y=0.1,z=0.1;x=0.8,y=0.1" micInterval=2
```

Two numbers still parse and mean `z=0`, so old microphone lines still work.

### 10. Refinement in three dimensions

```
amrLevels=2 amrEvery=8 amrThreshold=0.15 amrCriterion=everything ^
amrMinPatch=8 amrMaxPatch=48
```

Nothing new to type. What changed is that it works: the line that used to
refuse AMR whenever `nz>1` is gone, patches are boxes, clustering splits the
longest of three axes, tagging looks at the z gradient and the full 3D curl,
and averaging down is the mean of eight cells. The report tells you what it
built:

```
Adaptive mesh: base 32 x 16 x 16 = 8192 cells
  level 1: 1 patches, 10240 cells, 2x finer
  56064 cells against 524288 for the same resolution everywhere, 10.6934% of it
```

That `8×` arithmetic per level instead of `4×` is the reason AMR is worth more
in a volume than it ever was in a plane.

### 11. Turbulence in a volume

```
turbulence=smagorinsky Cs=0.17
turbulence=komegasst turbIntensity=0.05 turbLengthScale=0.02
```

Same keys, same meaning. Underneath, the strain magnitude is summed over all
nine components and the Smagorinsky filter width is `cbrt(dx·dy·dz)` instead of
`sqrt(dx·dy)` — which is the right thing and also means a volume run with the
same `Cs` is not the same amount of eddy viscosity as the plane run was. If you
tuned `Cs` on a plane case, re-check it on the volume.

### 12. Stopping and continuing a volume

Unchanged in how you drive it:

```
FS restart=1 restartFile=output addTime=5
FS restart=1 restartFile=output/solution_240.vtk totalTime=30
```

`restartFile` takes a frame or the folder holding them. `nz` and `Lz` come out
of the frame and cannot be changed on a continuation — the field has a shape.
`gravityTilt`, boundary kinds, viscosity and everything else can be overridden
on the line as always.

Measured: a volume stopped halfway and continued lands within 5e-4 of one that
never stopped. Frames written by any earlier version still load, as a volume
one cell deep with `w` zero.

### 13. Looking at the result

**In the UI.** A volume opens in the 3D viewport, a flat frame in the old 2D
one, and `V` switches between them at any time. The input parameter panel is
not drawn on this page — it belongs to Setup, and the picture takes the width.

* **3D viewport.** Left drag turns the view, middle drag slides it, wheel
  zooms. `Camera > rotate` and `Camera > move` decide which of the first two
  the left button does — one of them is the only way to slide the view on a
  trackpad with no middle button — and holding `Shift` slides it either way.
  `F` frames everything. Numpad 1 / 3 / 7 lock to front / right / top, Ctrl
  with them gives back / left / bottom, and the six names in `Camera` do the
  same; resting on one lights that side of the box up, so the names do not have
  to be learned. Numpad 5, or `Camera > isometric`, gives parallel projection,
  which is the one to read a shock angle off. Numpad 4 / 6 / 8 / 2 turn in 15
  degree steps and Numpad 9 flips to the opposite side.
* **The bar is eight lists, not thirty buttons**: `Layers` (cloud, isosurface,
  vortices), `Slices` (X, Y, Z), `Flow` (streams, tracers), `Show` (body,
  wireframe, box, grid, microphones), `Colour`, `Camera`, `Range` and `Run`.
  Each button says what is on — `Layers: cloud+vortices` — and every entry in
  every list carries a sentence, shown beside the list while the cursor rests
  on it. `What?` pins the whole lot open at once.
* **A volume opens on the cloud with the slice planes off**, a flat frame
  opens on its one plane. Turn a slice on from `Slices` when you want a cut; it
  stays on until you load a result of the other kind.
* **Cloud** paints every cell that differs from the still air as a translucent
  coloured block and draws nothing where the field is flat, which is most of a
  box with one body in it. It is the closest thing here to a schlieren
  photograph: the shock cone, the wake and the vortices stand in clear air with
  no level to choose and no plane to position. `C` toggles it, `[` and `]` or
  its slider set how solid.
* **All three layers can be on at once.** Cloud, isosurface and vortices are
  three ticks, not one three-way switch: a faint cloud around a vortex tube is
  a picture worth having, and if it turns into fog the cloud slider is right
  there. `C`, `I` and `Q` toggle one each.
* **Colour is one field for everything drawn.** Pick `density` and the cloud,
  the isosurface and every slice are density; pick `Q` and the cloud is a cloud
  of vortices. `off` clears the picture down to the body and the box. `D` goes
  straight to density, which is what makes a shock look like a shock rather
  than a smear — a compressible run writes it into every frame with nothing
  asked for, and a compressible volume opens on it.
* **Vortices** are the Q criterion drawn as an isosurface with the vortex core
  lines through it. Turn it on, drag its slider until the structures separate
  from the noise. This is the answer to "where are the vortices" that a colour
  map never gave you. Q, for the record, is rotation squared minus shear
  squared: positive where the flow turns faster than it is being sheared.
* **Streamlines and tracers** are seeded through the fluid, integrated with
  RK4, coloured by speed; turning tracers on sends a bright head down each one.
* **Clicking selects.** Click a body and it is selected in the BODIES group.
  Click a domain face and that face's boundary row is focused, so you set
  `bcBack=inlet` by clicking the back wall instead of hunting through a list.
  The status line shows the cell indices, the position and the field value
  under the cursor.
* **The 2D view is a slice.** Pick the axis with the X / Y / Z buttons, move
  the plane with the slider or Up / Down. Everything the 2D view ever did —
  colour maps, vectors, tracers, the probe, the legend — works on that slice
  unchanged.

**In ParaView.** Frames are ordinary `STRUCTURED_POINTS` with a real third
dimension, so Slice, Contour, Stream Tracer and Volume all work with no
persuasion. `extraFields=vorticity` writes a **vector** in a volume and a
scalar in a plane, so the Glyph filter has something to point at.

### 13b. Making vortices, so the Vortices button has something to draw

Q only ever draws what is there, and a missile flying straight and level makes
almost none. The trick is incidence: hold the same body at 20 degrees nose-up
in a Mach 0.6 stream and the air cannot get round it sideways without rolling
up. A pair of vortices leaves the nose and runs the length of the body, the
fins throw four more off their tips, and the lot trails away downstream.

    profiles=missile.obj@x=0.45,y=0.5,z=0.5,size=0.45,ax=20,ay=180,az=0
    machInlet=0.6      bodyMotion=            turbulence=none
    nx=160 ny=80 nz=80 Lx=2.0 Ly=1.0 Lz=1.0
    bcLeft=inlet       everything else outlet
    totalTime=0.006    acousticFields=0

The body is held and the air blown past it rather than the body flown through
still air: no remasking, no per-step voxelisation, and the wake settles instead
of being chased down the box. `turbulence=none` on purpose - these vortices are
shed by the geometry, not grown out of a boundary layer, so they are there in
an inviscid solve and a model would only smear them.

Measured on the frame at 6 ms: 2235 vortex core segments with the slider loose,
627 with it strict. Above about Mach 0.8 the nose shock starts to dominate the
Q field and the vortices get hard to pick out; past about 35 degrees the flow
separates off the whole upper side and it stops being a tidy pair.

`vortices.cfdui` in the repository is this, written out with the rest of the
keys and a note on which buttons to press.

### 14. Saving a setup, and getting one back out of a run

In the UI: **Save configuration** writes every parameter as `key=value` lines
in exactly the form the command line takes, so the block can be pasted onto a
terminal as it is. **Load configuration** reads one back, checks every value,
applies what it recognises and tells you what it did not.

The one worth knowing about: **Load configuration also takes a `.vtk` frame**.
Every frame carries the full configuration of the run that wrote it, so you can
recover the entire setup of anything you ever ran from its own output — even if
you have long since lost the command line. **Recover setup** in the results bar
does it for the frame already on screen.

### 15. Making a body follow a curve

In the UI a body's path is a curve by default now. Place control points, the
body follows a smooth Catmull-Rom curve through them in three dimensions, and
the UI emits the `bodyMotion` string that reproduces that path for the solver.
Straight keyframes are still there — set the path row to `keys` — for anyone
who wants a body on rails.

### 16. When it is too slow

In order of how much they buy you:

1. **`nz` is a multiplier on everything.** Half it and you halve the run. There
   is no substitute for choosing the depth you actually need.
2. **Cell counts divisible by a high power of two** give the deepest multigrid
   hierarchy and the fastest pressure solve. `64×64×64` is a much better shape
   than `70×70×70`. The startup line tells you how many levels you got, and
   fewer than three on a grid above 32 is a warning.
3. **`mgIterations`** is V-cycles per step. A volume usually wants more than a
   plane did — 4 to 8 rather than 2. If the run says it ran out of cycles, that
   is what it is asking for. Watch the `mg res` column: if it sits at the
   tolerance you are fine, if it climbs you are not.
4. **`useCuda=1`** if you have a card. The GPU multigrid mirrors the CPU one
   kernel for kernel and the whole pressure solve moves over.
5. **AMR** if the interesting part of the flow is a small part of the box.
   Eight times the saving per level in a volume, against four in a plane.
6. **`saveInterval`** — a volume frame is `nz` times bigger. Writing one every
   step will make the disk the bottleneck before the solver is.

---

## The headline: it solves a volume

The solver fills a volume `Lx × Ly × Lz` with `nx × ny × nz` cells. Two keys
are new and that is the whole of the interface:

```powershell
"Fluid Solver.exe" nx=160 ny=64 nz=64 Lx=2.5 Ly=1 Lz=1 bcFront=wall bcBack=wall
```

Cell data is `nx*ny*nz` indexed `(k*ny + j)*nx + i`; `u` lives on `(nx+1)*ny*nz`
faces, `v` on `nx*(ny+1)*nz`, `w` on `nx*ny*(nz+1)`. The momentum equation has
three components, the pressure operator is seven-point, and `w` is a real
velocity that goes into the frame.

A cube cavity, which is the case everybody checks a new 3D solver against:

```powershell
"Fluid Solver.exe" caseType=cavity lidSpeed=1 nu=0.01 Lx=1 Ly=1 Lz=1 nx=64 ny=64 nz=64 steadyTolerance=1e-5 totalTime=200 "extraFields=vorticity,speed"
```

`caseType=cavity` closes all **six** sides itself. Re is `lidSpeed * Ly / nu`,
so that is Re 100, and the primary vortex lands where Ghia puts it — same
tables as the square cavity, the centre plane of a cube rather than a square.
`VolumeTests` runs it and fails if it does not.

### `nz = 1` is the plane, and it is the same answer

This is the sentence the whole port was written to be able to say. `nz`
defaults to 1, and a grid one cell deep **is** the plane: every `∂/∂z`
differences to nothing, `w` is zero everywhere, the front and back coefficients
of the pressure operator are closed, and what is left is the five-point
operator it always was.

What that is worth, and where each number comes from:

- **The frames.** The 88-frame golden master against 0.2 comes back at exactly
  `0.000e+00`. The ten configurations are the ones listed above.
- **The pressure solve.** At `nz = 1` the multigrid returns the 2D field **bit
  for bit**, with the same level count and the same cycle count. Not a similar
  answer after a similar amount of work — the same field after the same work.
- **The bodies.** At `nz = 1` the gyroscopic term in the rigid-body update is
  identically zero, the inertia tensor has one finite in-plane entry, and the
  quaternion is a rotation about z. The update reduces to exactly the scalar
  `theta`/`omega` one that was there before. Not approximately; the terms are
  not there to round.
- **Turbulence.** The strain magnitude `|S|` is the same **bit for bit** on the
  xy, xz and yz planes for the same shear on cubic cells — the same floats,
  because it is the same expression with the indices permuted.
- **The compressible solver.** A shock tube run in a volume stays
  one-dimensional to **7e-7** of the density, produces **1.8e-5** of transverse
  momentum, and reproduces the plane run's profile to **3.9e-5**. A shock tube
  has no business knowing about y or z at all, so any number the transverse
  directions produce is a number the code invented.
- **CUDA.** The device hierarchy mirrors the CPU one kernel for kernel — a 2D
  block over (i, j), one grid layer per k — and every kernel was checked against
  the CPU path off-GPU and is bit-identical to it.

`VolumeTests` holds ten checks and they are the port's own conscience: the
plane still being the plane, the cube cavity, a solid body in a volume, a
volumetric restart, gravity down each of the three axes, a closed box keeping
its mass, turbulence staying finite, walls turning about every axis, a shock
tube refusing to notice the two directions it should not, and refinement in
depth.

### What the port had to touch

Everything, which is the honest answer, but in a shape that did not cost
anything back:

- **Loop order is k, j, i, and `i` stays contiguous.** Every AVX2 kernel in the
  tree is unchanged in shape, because the thing it vectorises — a run of cells
  along `i` — is still a run of cells along `i`. A volume is a stack of the
  planes the vector code already knew how to walk.
- **OpenMP collapses (k, j).** A plane has exactly one `k`, so without the
  collapse a plane run would hand the whole grid to one thread. This is the one
  place the port cost something outside the solver: `collapse` is OpenMP 3.0,
  MSVC's classic `/openmp` is 2.0 and rejects it, so the Windows build now asks
  for `/openmp:llvm`. See the [migration section](#if-you-have-old-files-or-old-scripts).
- **The multigrid coarsens each axis independently**, so a grid deep in one
  direction and thin in another still walks down towards isotropy instead of
  stalling on the axis it cannot smooth. Restriction and prolongation are the
  3D tensor product of the 1D transfers it already had.
- **Geometry is voxelised instead of cut** at `nz > 1` — see
  [Geometry](#geometry).

### What a volume costs

**A volume is `nz` times the cells of the plane it is made of, and the pressure
solve is the expensive part.** The Poisson solve is about 90% of the runtime of
an incompressible step and it scales with the cell count, so `nz=64` is not a
setting, it is a decision to do sixty-four times as much work per step. Nothing
in the port changed that and nothing could.

What the port was careful about is that it is only `nz` times and not worse. A
V-cycle over three axes costs about `1 + 1/8 + 1/64 + … = 8/7` of a fine-grid
sweep, against the `4/3` a plane pays — the cycle gets relatively *cheaper* as
the dimension goes up, which is the one piece of good news in this section.

**There is no wall-clock number here for a volume pressure solve, and none is
going to be invented.** Every 3D measurement in this release is a correctness
measurement: bit-for-bit agreement at `nz = 1`, second order on a manufactured
3D solution, and the conservation and isotropy figures quoted throughout, plus
the 5 ms voxelisation below. The timing tables in the README are all plane runs
on a named machine. Run your own case at `nz=8` before you run it at `nz=128`;
the step count and the seconds-per-step it prints are the only honest estimate
there is.

---

## Numerical solver

### Gravity, in both its formulations

Off by default. Three keys turn it on, and a fourth tips it out of the plane:

```powershell
"Fluid Solver.exe" gravityEnabled=1 gravityAccel=9.81 gravityAngle=30
```

`gravityAngle` is degrees clockwise from straight down, inside the xy plane —
`0` is down, `90` points into the inlet, `180` is up, `270` runs along the
flow. It is an angle rather than a vector because the flow direction is the one
thing in this domain that cannot be turned: the inlet is the left edge and the
outlet the right one, so everything else turns around them.

`gravityTilt` tips the whole vector towards +z:

```
planar = gravityAccel * cos(tilt)
gx = -planar * sin(angle)
gy = -planar * cos(angle)
gz =  gravityAccel * sin(tilt)
```

`gravityTilt=0`, the default, leaves `gz` at exactly nought and `gx`, `gy`
exactly what they were. `gravityTilt=90` pulls straight along +z, and
`gravityAngle` then means nothing because there is nothing left in the plane
for it to point at. The reduced-pressure potential picks up the one term that
follows, `phi = gx*(x - Lx) + gy*(y - Ly/2) + gz*(z - Lz/2)`, with the
reference point at `(Lx, Ly/2, Lz/2)`.

```powershell
"Fluid Solver.exe" gravityEnabled=1 gravityAccel=9.81 gravityTilt=90 nz=32 Lz=1
```

**Measured.** A light drop released in a heavy fluid rises the same distance
whichever of the three axes gravity is turned to, **to 0.0001 %**. That is the
check that matters: there is nothing special about `y`, and if there were, this
is where it would show.

**Why there are two formulations.** At constant density a body force cannot
change the velocity field at all — it is exactly a pressure offset. That is not
a limitation of this implementation, it is what the equations say.

```powershell
"Fluid Solver.exe" gravityEnabled=1 gravityAccel=9.81 gravityMode=body
```

`gravityMode=reduced` is the default and the shortcut that follows: the force
never enters the solve, `p` carries only the dynamic part, and the hydrostatic
head is added back when the frame is written. At one density that is not an
approximation, it is exact, and it costs nothing. `body` puts the force in the
predictor where it physically belongs and solves for the total pressure; on a
single-density run it produces the same answer for a little more work (a fluid
at rest stays at rest to one part in a hundred thousand of the head, which
`ConservationTests` checks). It exists because the shortcut stops being exact
the moment two densities share a domain, and the multiphase work had to have
something correct underneath it. `phases=2` moves the mode to `body` on its own
and refuses to be moved back.

### Boundaries have names now

At 0.2, `applyBC()` was nine lines with "inlet on the left, walls top and
bottom, outlet on the right" hard-coded into it. Every side has a name now, and
there are six of them.

| Kind | What it does |
|---|---|
| `inlet` | fluid enters at `bc<Side>Speed`, or at `U0` when that was never given |
| `outlet` | fluid leaves; this is also where the pressure level is fixed |
| `wall` | no-slip: the fluid sticks to it and is dragged to a stop |
| `movingWall` | no-slip, but the wall itself slides at `bc<Side>Speed` |
| `slip` | free-slip: nothing crosses it, nothing rubs against it |

```powershell
"Fluid Solver.exe" bcBottom=wall bcTop=wall
"Fluid Solver.exe" bcLeft=wall bcRight=wall bcBottom=wall bcTop=movingWall bcTopSpeed=1
"Fluid Solver.exe" nz=64 Lz=1 bcBottom=wall bcTop=wall bcFront=wall bcBack=wall
```

The defaults are the channel 0.2 solved — `inlet` left, `outlet` right, `slip`
above, below, in front and behind — and at `nz = 1` the two new sides close a
volume one cell deep, which is the plane. The third line is a duct, and that is
the point: nothing about writing a case changed, there are just two more sides
to write.

**A closed box is solvable now, and was not.** With no side fixing the
pressure, the operator has the constants in its null space: the answer is
defined only up to one, and the iteration drifts along it instead of
converging. The mean now comes off the right-hand side before the solve and off
the answer after every cycle, on both the CPU and the CUDA path. That had to
exist before a cavity could.

**Inlet windows are rectangles.** `inletFrom`/`inletTo` cut the window along
the face's **first** axis, `inletFrom2`/`inletTo2` along its **second**. Which
is which depends on the face and there is no way to guess it:

| Face | first axis | second axis |
|---|---|---|
| `bcLeft`, `bcRight` | y | z |
| `bcBottom`, `bcTop` | x | z |
| `bcFront`, `bcBack` | x | y |

```powershell
"Fluid Solver.exe" nz=64 Lz=1 inletFrom=0.4 inletTo=0.6 inletFrom2=0.4 inletTo2=0.6
```

That is a square jet a fifth of the face across, in the middle of the left
side. Both pairs default to 0 and 1, so an inlet that was a full side is still
a full side and a band across a plane is now that same band running the whole
depth.

`inletProfile` takes `uniform`, `parabolicSpan` (a parabola along the first
axis, flat along the second — a plane channel extruded through z) and
`parabolic` (both axes — duct flow). At `nz = 1` the second axis has one cell
and the last two are the same thing, which is why every old command line means
what it always meant.

The outlet is never told about any of this. It has no speed of its own: the
pressure solve works out what has to leave, so a band a fifth of the height
wide arrives at the far end spread over the whole of it, and what went in comes
back out to one part in a hundred million. `InletTests` checks the inlet column
face by face — not "roughly a band in the middle", every single face — and the
same case run bottom-to-top as well, because two axes that are never both
tested are two code paths pretending to be one.

**A configuration that cannot work is refused before the run starts.** At 0.2,
an inlet with nothing open ran happily for as long as you asked, printing
`div = 0.5` on every line and producing nonsense:

```
!!! the inlets push 1.000000 m^2/s of fluid in and no side lets any of it out.
    An incompressible fluid does not compress: no pressure field can take it,
    the projection cannot remove the divergence it makes, and the run would
    report the same leftover on every single step to the end.
    Open a side with bcRight=outlet (or whichever one the flow should leave
    by), or make the inlet a wall.
```

The check adds up what the sides prescribe against what they leave open, over
fluid faces only, so an outlet buried under a body counts as buried. Two inlets
facing each other at the same speed cancel and are allowed, because they do
cancel. A cavity is allowed. A band narrower than one cell gets the same
treatment, because an inlet that lets nothing in is a run that does nothing for
however long you asked it to.

### Cases: the cavity, the shock tube

Writing five keys to get four walls and a moving lid is exactly the kind of
thing that gets typed wrong once and debugged for an hour, so there is a name
for it.

```powershell
"Fluid Solver.exe" caseType=cavity lidSpeed=1 nx=64 ny=64 Lx=1 Ly=1 nu=0.01
```

`caseType=cavity` writes all six sides itself and empties the domain, because a
cavity with the verification circle floating in the middle of it is not a
cavity. Anything after it still wins, so `caseType=cavity bcBottom=movingWall`
is a two-sided cavity and `caseType=cavity profiles=wing.obj@x=0.5,y=0.5` is a
cavity with something in it. The preset is a shortcut, not a mode.

`caseType=shockTube` is the compressible one: four walls and Sod's initial
condition scaled to your `pInf` and `T0`. `channel` is the default and is what
0.2 ran.

**`geometryFile=empty`, because `none` was a lie.** `geometryFile=none` has
never meant "nothing" — it means the verification circle, and has since the
first commit. Nobody noticed because until the cavity, every case had a model
in it. `empty` means empty; `none` still means the circle, which is a body like
any other. `CavityTests` counts the solid cells and fails if any come back.

**The cavity is where the first outside numbers came from.** Everything before
it was checked against itself: the Poisson solver reproduced a function we
chose, the channel matched a parabola we derived, the restart matched the run it
continued. All true, all circular. Ghia, Ghia and Shin published thirty-four
numbers in 1982 and every solver since has been held against them:

```
  steady at t = 17.85 s, 3250 steps, 64x64
       y   Ghia u    solver u |      x   Ghia v    solver v
  0.9766  0.84123    0.84454 |   0.9688 -0.05906   -0.06512
  ...
  worst interior miss: u 0.0065, v 0.0153
```

Re 400 was in the plan and is not here. One published `v` value does not sit on
the curve its own neighbours describe, two independently published copies of
the table agree with each other, and a test whose failure condition is "the
paper has a typo" is not a test.

### Stopping when the flow settles

A cavity has a steady state and reaching it is the whole point, but nobody
knows in advance how long that takes — the run above needed 17.85 s of
simulated time and `totalTime` was set to 200 out of cowardice, which is 182 s
of computing a field that had stopped moving.

```powershell
"Fluid Solver.exe" caseType=cavity steadyTolerance=1e-5 totalTime=200
```

Every ten steps the field is compared against the last snapshot, and the
largest velocity change per second is divided by whatever drives the case — the
lid, or `U0` on a channel, so the number means the same thing on both:

```
Step 3250, t = 17.8528 s: the field is changing at 9.49436e-06 per second
against the driving speed, under the steadyTolerance of 1e-05.
  Steady state, stopping here.
```

The last frame is written either way, so a run that stops early is continued
exactly like one that ran out of time. Zero, the default, turns it off and not
even the snapshot arrays are allocated.

### Convection and time

```powershell
"Fluid Solver.exe" convection=muscl limiter=vanLeer timeScheme=rk2
```

The solver has been telling you on every start since long before 0.2 that
upwind adds more viscosity than the fluid has. Now it does not have to:

```text
note: upwind adds 3.125x more viscosity than the fluid has.
      The run behaves like Re 9.7, not 40.
```

`convection=muscl` reads one cell further in each direction and blends the
upwind difference towards the central one by however much the limiter allows —
second order where the field is smooth, back to upwind where it is not.
`central` does no limiting at all and is the least dissipative and the least
forgiving. `limiter` takes `minmod`, `vanLeer` or `superbee` and is only read
by `muscl`.

Second order in space with one stage in time is only conditionally stable, and
not by the CFL number anybody has in mind when they set it, so anything other
than `upwind` wants `timeScheme=rk2` or `rk3` under it, and the solver says so
rather than letting it blow up on step one. Each Runge-Kutta stage is a whole
projection, so `rk2` costs twice a step and `rk3` three times; in exchange the
result is a convex combination of divergence-free fields, which is what keeps
it divergence free.

What it buys, measured on the same case by how much vorticity survives — this
is what `ConvectionTests` asserts:

| Scheme | Peak vorticity |
|---|---|
| `upwind` + `euler` | 1.20 |
| `muscl` `minmod` + `rk2` | 1.45 |
| `muscl` `vanLeer` + `rk2` | 1.75 |
| `central` + `rk3` | 2.01 |

`upwind` + `euler` stays the default, so nothing about an existing run changes,
and the predictor is templated on the scheme so upwind compiles to exactly what
it was and pays nothing for the others existing.

QUICK was in the plan and is not here: on a three-point stencil it is not
QUICK, it is a mislabelled unstable blend, and it diverged on step one when
asked. A real one needs four points. Better an honest three schemes than a
fourth that lies about what it is.

### Two fluids with an interface

```powershell
"Fluid Solver.exe" phases=2 rho1=1000 nu1=1e-6 rho2=1.225 nu2=1.5e-5 gravityEnabled=1 phaseInit=column phaseX=0.25 phaseLevel=0.75 bcLeft=wall bcRight=wall bcBottom=wall bcTop=outlet geometryFile=empty Lx=0.6 Ly=0.4 nx=96 ny=64
```

That is a dam break: a column of water three quarters of the way up a quarter
of the domain, air above it, one wall taken away at t = 0. `phases=1` is every
run written before this and none of the code below executes at all.

The domain carries a volume fraction per cell — 1 is fluid 1, 0 is fluid 2, in
between is a cell the interface passes through — and `rho` and `nu` come out of
that fraction per cell, so `ro` and `nu` stop being read the moment there are
two of everything.

**In a volume** the fraction is advected on all three axes by the same scheme
and the same limiter, and the interface normal and the curvature are the full
3D ones rather than the in-plane pair extended by zero. A sphere of radius R
comes back with curvature **2/R to within 1 %**, a plane comes back with zero,
and rotating the same interface about x, y or z gives identical numbers. The
last one is the check with teeth — an axis treated differently from the other
two shows up there and nowhere else.

- `phaseZ` places the start shape in depth, as a fraction of `Lz`, alongside
  `phaseX` and `phaseY`. Only read at `nz > 1`.
- `phaseInit=drop` is a **sphere** at `nz > 1` and the disc it always was at
  `nz = 1`.
- `initialPhaseFile` takes a whole volume, `nx*ny*nz` fractions in
  `(k*ny + j)*nx + i` order. Hand it a single plane's worth and it extrudes that
  plane through z rather than refusing it.

**Measured:** the phase volume in a closed box drifts by nothing over the run.
Not "a little" — the number comes back the number it started at.

`vofScheme` takes `upwind`, `hric` or `cicsam`. All three are algebraic: they
choose a value for the fraction at each face and carry it with the flow the
projection just produced. None reconstruct the interface as a surface the way
PLIC does, which is a large fraction of the cost and most of the complexity.
`upwind` smears it over ten cells inside a second and is there to be compared
against. `hric` and `cicsam` steer the face value downwind where the donor cell
is half full, fade that back out where the interface lies along the flow rather
than across it, and fade it out again as the Courant number climbs. The step
size is limited for the interface on its own — half a cell per step, whatever
the momentum equation would have allowed.

`MultiphaseTests` runs three cases:

```
stirred drop   started 0.125664 m^2, ended 0.125796, drift 0.105%
still layer    spurious 2.7810e-02 m/s against 2.215 m/s if it fell (1.26%)
dam break      |u|max 2.403 m/s against sqrt(2gh) = 2.426, front at 0.84 of the floor
               volume 0.044898 m^2 from 0.045000, drift 0.226%
```

The first is the thing an algebraic VOF scheme actually loses, and it loses it
quietly — the interface just gets thinner every step until it is gone. The
third needs no table and no paper: nothing in a collapsing column can be going
faster than something dropped from the top of it, and it gets to 99% of that
without passing it.

A run says what it is doing, including the part that is going to cost you:

```
Two fluids: 1 is rho 1000 kg/m^3, nu 1e-06 m^2/s; 2 is rho 1.225 kg/m^3, nu 1.5e-05 m^2/s.
  Density ratio 816.327:1, interface carried by hric, pressure solved with 1/rho on every face.
  note: past a hundred to one the pressure solve needs more V-cycles than a single fluid does.
```

and, if gravity was left off, that two fluids with nothing pulling on them are
two dyes rather than two phases.

### Surface tension and the contact angle

```powershell
"Fluid Solver.exe" phases=2 surfaceTension=0.072 contactAngle=60 rho1=1000 nu1=1e-6 rho2=100 nu2=1e-5 gravityEnabled=1 phaseInit=drop phaseLevel=0.3 geometryFile=empty Lx=0.02 Ly=0.02 nx=64 ny=64
```

`surfaceTension=0` is every run before this one and the whole path is skipped
for it: no curvature pass, no extra force, no extra limit on dt.

The force is the CSF one, `f = sigma * kappa * grad c`, added to the predictor
in the same place gravity's body force already was, **on the faces**, so it is
differenced by exactly the discretisation the pressure gradient is. Put it
anywhere else and the two disagree by a rounding error per cell, and a drop
sitting still develops a circulation out of nothing.

**The curvature is a height function.** The amount of fluid in a column of
cells is a height, and the second derivative of that height along the interface
is the curvature. The column starts at seven cells and grows until it brackets
the surface at both ends, because a fixed stack does not bracket an interface
at 45 degrees and gives a curvature somewhere between wrong and enormous. In a
volume the height is differenced along **both** directions across the column,
so what comes back is the sum of the two principal curvatures — a sphere of
radius R has curvature 2/R, not 1/R, and the jump across it is twice what a
circle of the same radius gives. At `nz = 1` the second direction has one cell,
contributes nothing, and the curvature is the `1/R` of a circle exactly as
before.

Taking the curvature off a smoothed gradient instead is the version that is
half the code, and it is a factor of ten worse: a drop that should sit still
boils on the spot. There is no simple version of this worth writing first,
which is why there is only one here.

`contactAngle` is measured inside fluid 1 at a solid wall. 90 is a wall neither
fluid prefers; below 90 fluid 1 wets it and climbs, above 90 it beads off. It
is applied by rotating the interface normal in the cells against the wall
before the curvature is taken.

**The step size will drop, and that is not a bug.**

```
dt < sqrt((rho1 + rho2) * d^3 / (4 pi sigma))
```

with `d` the smallest of dx, dy and dz in a volume and the smaller of dx and dy
in a plane — a third axis cannot make this limit looser, only tighter. It is
separate from the CFL number and from the interface Courant number, and it is
usually the one that binds: on a millimetre of water against air it is
microseconds, and it falls as `dx^1.5`, so halving the cell size costs about
three times the steps. The solver says so at the start rather than leaving you
to wonder why it is slow, with the real number, and with the sentence "It is
not the solver hanging."

`SurfaceTensionTests` runs three cases:

```
laplace     jump 14.4262 Pa against sigma/R = 14.4000 (0.18% off)
spurious    1.314e-05 m/s against 0.1200 m/s of capillary wave (0.0001 of it)
rounding    interface 0.01741 m -> 0.01610 m (7.5% less)
```

The second is measured against the capillary wave speed rather than against
zero, because zero is not achievable and a ratio says how badly. The third is
physics rather than arithmetic: a square blob of water has no business staying
square.

### Fluids that mix

```powershell
"Fluid Solver.exe" phases=2 mixing=miscible diffusivity=1e-4 rho1=1000 nu1=1e-6 rho2=1000 nu2=1e-6 geometryFile=empty
```

Oil and water have a surface. Ink and water do not: there is nothing to
compress, nothing for tension to pull on, and the composition spreads instead
of staying sharp. `mixing=miscible` says so, and then the compressive VOF
scheme is not read at all — the composition rides the same limited MUSCL
convection the momentum does — a Fickian `D * grad^2 c` term is added
explicitly, `surfaceTension` is **refused** rather than ignored, and `dt` picks
up the diffusive limit `d^2 / (4 D)`.

Refusing rather than ignoring matters: a surface tension on fluids that have no
surface is a number that would sit there looking meaningful and do nothing, and
you would never find out.

The check is the one closed-form answer diffusion gives: a step in composition
left alone spreads as an error function, and the slope at its middle is
`1/sqrt(4 pi D t)` whatever else is going on.

```
mixing      steepest 28.015 /m against 1/sqrt(4 pi D t) = 28.209 (0.7% off)
            composition 0.020000 m^2 from 0.020000
```

### Flow sources

```powershell
"Fluid Solver.exe" "sources=x=0.5,y=0.2,z=0.5,r=0.05,rate=2,angle=90,elev=0,phase=1"
```

A disc — a ball, in a volume — inside the domain that pushes fluid out of
itself, for cases where the flow does not come in through a side. `rate` is the
speed it leaves at, `angle` aims it inside the xy plane, `elev` tilts the jet
out of that plane, `z` places it in depth, and `phase` says which fluid comes
out. The divergence the projection has to produce in those cells is the rate
itself, so the pressure carries the flow away in every direction and the
momentum aims it.

`z=` defaults to the middle of the domain and `elev=0` to a jet that stays in
the plane, so every source line written before the port does the same thing it
did.

What a source adds has to leave somewhere, exactly as an inlet's does, so a
case with one and no outlet is refused before the run starts. The `div` column
in the step lines takes the intended part out, so what it reports is still the
part the projection failed to produce — without that, a working source reads as
a solver that cannot converge.

### Moving walls

A moving wall is a wall whose *surface* has a velocity while the body itself
stays exactly where it is: a spinning cylinder, a conveyor belt, the lid of a
driven cavity. Gravity is the term that cannot change the flow; this is the
opposite — it is the cheapest way there is to put circulation into one, and it
is what the Magnus effect is made of.

```powershell
"Fluid Solver.exe" "wallMotion=1:rot=720"
"Fluid Solver.exe" "wallMotion=1:rot=720;2:slideX=-1.5,slideY=0.4"
"Fluid Solver.exe" "wallMotion=1:slip=1;2:rot=-45"
"Fluid Solver.exe" nz=64 Lz=1 "wallMotion=1:rotX=360,rotY=180"
"Fluid Solver.exe" nz=64 Lz=1 "wallMotion=1:rotZ=720,slideZ=0.5;2:slip=1"
```

| Setting | Unit | Means |
|---|---|---|
| `rotZ` | deg/s, counter-clockwise | the surface turns about that body's own centroid, about z |
| `rotX` `rotY` | deg/s | the same about x and y |
| `rot` | deg/s | an alias for `rotZ`, kept because every line ever written uses it |
| `slideX` `slideY` `slideZ` | m/s | the surface is dragged in a straight line |
| `slip` | switch | free-slip instead of no-slip |

`rotX`, `rotY` and `slideZ` are what the third dimension added. A plane has one
axis a surface can turn about and two it can slide along; a volume has three of
each, and they compose as a single angular velocity vector — the field is
**v = slide + ω × (x − centre)** — rather than as three settings applied in
some order.

`slip` is exclusive with all three, and asking for both is refused rather than
half-applied. A free-slip wall carries no tangential stress *by definition*,
and tangential stress is the only thing a spinning or sliding surface has to
push the fluid with. `1:rot=90,slip=1` is not a combination, it is a
contradiction.

**Where the numbers come from.** The mask is flood-filled into numbered bodies
before the run starts, 8-connected in a plane and **26-connected** in a volume,
and the list is printed with the rest of the mesh information:

```text
  Number of objects = 2 (these numbers are what wallMotion takes)
    object 1: 76 cells, centre (0.5, 0.346628) m, rim 0.109486 m
    object 2: 41 cells, centre (1.04992, 0.679688) m, rim 0.0781845 m
```

Numbering follows the scan order of the grid — `i` inside `j` inside `k`, so
the whole of the front plane is numbered before any of the one behind it. Write
the line against those centres, not against the order you typed the models in.

The solver also does the arithmetic you would otherwise do by hand, because
degrees per second is not a number you can compare to `U0` on sight:

```text
  object 1 at (0.5, 0.346628) m: rot = 720 deg/s -> rim speed 1.37584 m/s, slide = (0, 0) m/s
    surface moves at 1.37584x the inlet velocity
```

**Measured**, 128×64, `Lx=2 Ly=1 nu=0.005 U0=1`, the verification circle, mean
pressure over the ring of fluid just outside the body:

```text
                       above     below     below - above
static cylinder      -0.15518  -0.15517       0.00001 Pa
rot = 1440 deg/s     +0.02670  -0.52826      -0.55496 Pa
```

The static body is symmetric to the fifth decimal, as it has to be. Spinning
counter-clockwise, the top surface runs against the stream and the bottom runs
with it, so the flow is slowed above and sped up below and the pressure
follows. That is the Magnus force, on the correct side, at a believable size.
Divergence over the same runs stayed at 2e-5 against 1e-5 for the static body.

**One side effect worth knowing.** The wall velocity is a velocity like any
other, so it goes into the CFL limit. On that run `|u|max` went from 1.40 to
2.16 and `dt` halved from 4.2e-3 to 2.0e-3 s. A wall spun much faster than the
flow buys its physics with step count, and the solver says so at startup.

**Free-slip, and what it is for.** No-slip is the physically right condition
for a viscous fluid on a real wall and is the default. Free-slip is what you
want when the wall is not really a wall: a symmetry plane, an interface
standing in for a free surface, or a body you want present as an *obstacle*
without the boundary layer it would grow. Measured on a flat, grid-aligned wall
(`nu=0.005`), the tangential velocity in the first five fluid cells above it:

```text
no-slip     +0.0540  +0.1250  +0.2116  +0.3121  +0.4247
slip        +0.4218  +0.4471  +0.4958  +0.5644  +0.6484
```

On a *staircase* body the effect is real but partial — the same measurement on
the crown of the circle gives `0.311 → 0.592`. The steps either side present
vertical faces, and those are no-penetration faces, which free-slip does not
and must not touch. That residual is the staircase approximating a smooth
circle, not the slip condition.

### Bodies that travel

`wallMotion` and `bodyMotion` look almost the same and do opposite things.
`wallMotion` moves the SURFACE: the wall drags the fluid past it like a
conveyor belt and the body itself never goes anywhere. `bodyMotion` moves the
BODY, and then the mask is a different mask every step. They are kept separate
because the second costs a rasterisation per step and the first costs nothing,
and nobody should pay for a feature by accident.

```powershell
"Fluid Solver.exe" "profiles=disc.obj@x=0.5,y=0.5,size=0.2" "bodyMotion=1:vx=0.4,omega=60" Lx=2 Ly=1 nx=96 ny=48 U0=0 bcLeft=wall bcRight=wall bcBottom=wall bcTop=wall
```

Empty is every run written before this and not one line of the code below
executes. **Both regimes take it** — the compressible solver used to refuse
`bodyMotion` because it cut its mask once and kept it; it re-cuts it every step
now.

**A body in a volume is a rigid body, properly.** In a plane a rigid body has
three degrees of freedom and its orientation is one number. In a volume it has
six and its orientation is not a number at all. So orientation is a **unit
quaternion**, inertia is a **3×3 tensor** taken about the body's own centroid
and computed from the voxels it is actually made of, and `integrate` carries
**angular momentum** rather than angular velocity — `L += tau dt`, then
`w = I_world^-1 L` — including the gyroscopic term, which is the one everybody
drops and the one that makes the motion right. Spin a free body about its
intermediate axis and **the tennis-racket flip appears**, unasked for, out of
the integrator.

**Measured.** Torque-free tumbling conserves `|L|` to **3.6e-5** and the
kinetic energy to **8.2e-5** over 20000 steps. A dropped body falls at
**9.80 m/s^2** with no spurious drift. The first pair catches a wrong
`I_world`, because a wrong inertia tensor conserves neither; the second catches
a sign or a factor in the force, which the first would sail straight past.

```powershell
"Fluid Solver.exe" nz=64 Lz=1 "bodyMotion=1:vx=0.2,vz=0.1,omegaX=30,omegaY=45"
"Fluid Solver.exe" nz=64 Lz=1 "bodyMotion=1:free=1,density=2700,pinZ=1,pinRotX=1,pinRotY=1"
```

| Setting | Means |
|---|---|
| `vz` | velocity along z, next to `vx` and `vy` |
| `omegaX` `omegaY` | spin about x and about y, deg/s. `omega=` is still `omegaZ=` |
| `pinZ` | hold it still in z |
| `pinRotX` `pinRotY` | hold that rotation. `pinRot=` is still `pinRotZ=` |
| `inertiaX` `inertiaY` | the two moments a plane never needed. `inertia=` is the z one |

A body given `pinZ=1,pinRotX=1,pinRotY=1` is confined to the plane it started
in — which is what a plane run is doing anyway, and is the useful thing to ask
for when you want a volume's flow around a body that is not allowed to wander
out of the mid-plane.

**The mesh stopped being a constant.** `Solver` held a `const Mesh&` to say the
mask was cut once and never touched. It does not any more: every step the
outline is transformed by the body's pose, rasterised again, flood filled
again, and handed to the multigrid. The numbering had to survive that — bodies
are numbered by scan order, so as soon as one moves past another the numbers
could swap and `wallMotion=2:rot=90` would quietly start spinning a different
object halfway through a run. Now each body remembers where it started, every
body/blob pair is scored by distance and claimed in order of distance rather
than in object order, and two bodies merging says so once, by number.

**Freshly uncovered cells, which is where this normally breaks.** A cell the
body has just left held the inside of a solid one step ago. What is in it is
not a velocity — it is the mirrored value the no-slip stencil left behind.
Leave it and the divergence of that one cell is enormous, the projection
faithfully spreads it over the whole field in a single step, and the run looks
like it exploded for no reason. What belongs there is the velocity of the
surface that just swept past. Faces the body did not vacate itself fall back to
the mean of their open neighbours, and the pressure and the phase fraction of a
freshly opened cell are filled the same way. A disc driven across a closed box
at 0.4 m/s holds a divergence of 1.9e-06, which is float noise.

**Why there are three couplings, and why the default is not the obvious one.**
Compute the force, move the body, recompute the flow, repeat. That is the
obvious scheme and it is unstable, and not in a way a smaller step fixes. A
body accelerating through a fluid has to accelerate some of the fluid with it,
and that entrained fluid pushes back in proportion to the body's own
acceleration — an added mass, comparable to the mass of fluid displaced.
Evaluate it one step late and you have a feedback loop whose gain is the ratio
of added mass to body mass. Above about one it diverges, and halving `dt` does
not reduce the gain at all: it is a property of the splitting, not of the
discretisation.

```
bodyCoupling=weak      the obvious scheme, here to be compared against
bodyCoupling=added     the added mass on the left hand side (default)
bodyCoupling=strong    force and motion iterated until they agree
```

`added` is one line — divide by `m + m_added` instead of by `m` — and costs
nothing. `strong` rewinds the flow and the bodies to the start of the step and
redoes it with the velocity the last pass predicted, until the two agree; it
costs `bodyIterations` pressure solves and is what a body lighter than the
fluid actually needs. On a disc twice as dense as the fluid, where the added
mass is half the body mass:

```
weak -0.6536, added -0.5552, strong -0.5331 m/s
```

`strong` is the reference. `added` sits 4.1% from it for the price of a
division; `weak` is 22.6% out, and that is at a density ratio of two. The test
fails if `added` drifts past 5% of `strong`, **and** it fails if `weak` gets as
close as `added` does — a case where the cheap scheme happens to be right is a
case that is not testing anything.

**Keyframes.**

```powershell
"Fluid Solver.exe" "bodyMotion=1:@0,vx=0,vz=0,@1,vx=0.5,vz=0.2,omegaY=90,@2,vx=0,vz=0,omegaY=0"
"Fluid Solver.exe" "bodyMotion=1:@0,vx=0,interp=bezier,@1,vx=0.5,@2,vx=0"
"Fluid Solver.exe" "bodyMotion=1:@0,vx=0.6,@0.15,free=1,@0.9,free=0,vx=0,density=1200"
```

`@<t>` opens a keyframe and everything after it belongs to that keyframe. Times
go forwards; between two of them the velocity is interpolated, before the first
and after the last it is held. It is sampled at the **middle** of the step,
which integrates a straight line exactly — sampled at the start it is a left
Riemann sum and loses half a step off every ramp, which is how the first
version came out at 0.1445 m instead of 0.1500. A body ramped from 0 to
0.5 m/s over 0.2 s and then held now moves 0.15001 m in 0.4 s.

`interp=` belongs to the key it is written on and governs the segment that
starts there, which is how a 3D package reads it: `constant`, `linear`
(default), `bezier`, and `sine quad cubic quart quint expo circ back bounce
elastic`, with `ease=in|out|inout|auto`. Bezier handles are **auto-clamped** —
zero tangent at a turning point — so a curve that goes up and then flattens
cannot bulge past the value it was told to reach:

```
interpolation constant 0.1979, linear 0.3000, bezier 0.3167, quad 0.3334,
              bounce 0.2622 m
```

all against a ramp whose peak, held for the whole run, would give 0.4. The test
fails if any of them passes that.

New components are settings like any other, so `vz`, `omegaX` and `omegaY`
belong to the key they are written on and interpolate the same way `vx` always
did. A component left out of a key is not zero there — it is whatever the
interpolation between the keys that do mention it says, which is why a 2D
timetable means what it always meant when you run it in a volume.

`free` is a keyframe setting too, so a body can be driven along a path and then
released, and released and then taken back. The velocity carries across the
switch either way: driven at 0.600 m/s and let go at 0.15 s, down to 0.4887 by
0.4 s, because from that moment nothing is pushing it.

**Bodies that bounce.**

```powershell
"Fluid Solver.exe" bodyCollisions=1 bodyRestitution=0.6
```

Off is the default and is what every run before this did — bodies pass straight
through each other and through the walls. That is the honest default because
turning it on changes the answer. On, contact is read straight off the mask:
each body is rasterised on its own before they are put together, which is the
only moment the grid knows who claimed what, so contact is exact for any shape
rather than a circle drawn round it. Collisions gained the z walls and the z
component of the contact impulse, so a body can bounce off the front and back
of the domain and off another body at any angle.

```
collisions    off 0.3976 m/s, on -0.8686 m/s - the second one met something
```

**Sources that ride.**

```powershell
"Fluid Solver.exe" "sources=x=-0.14,y=0,z=0,r=0.06,rate=3,angle=180,elev=20,body=1"
```

`body=<n>` puts the source in that body's own frame with the origin at its
centre, so a thruster stays on its nozzle however far the body has travelled or
turned. In a volume that turn is the body's **orientation matrix** rather than
one angle, so a thruster on a tumbling body stays bolted to the same patch of
hull through the tumble. The reaction — `rho * Q * v`, the momentum leaving per
second — goes onto the body.

A source that lands entirely inside its own body covers no fluid cell and emits
nothing, so it pushes nothing either. That is one line and it is the difference
between a rocket and a reactionless drive. (The reactionless drive shipped
first, internally, and got a body to 39.6 m/s out of a 2 m domain, which was
briefly very exciting.)

**A body whose path you set never deviates from it**, whatever the fluid does.
`bodyForceReport=1` works the force out anyway and puts it in the step line, to
be read, integrated into a drag coefficient, or looked at just before the body
is released. It is off by default because a body on rails does not need it and
it costs a pass over the surface every step.

`MovingBodyTests` runs:

```
prescribed    moved 0.200000 m against 0.200000 exact, div 1.907e-06
keyframes     moved 0.15001 m against 0.15000 from the ramp it was given
numbering     closing at 0.12 and -0.12 m, and the mask agrees
coupling      weak -0.6539, added -0.5551, strong -0.5328 m/s
neutral       0.0017 m/s after 0.3 s against 2.943 falling free (0.06%)
thrust        jet on the body 0.5148 m/s, the same jet bolted down 0.0539
carried       let go in a 1 m/s flow, reached 0.6637 m/s downstream
released      driven at 0.600, let go at 0.15 s, down to 0.4887 by 0.4 s
collisions    off 0.3976 m/s, on -0.8686 m/s
```

`carried` is the one that earns its place least obviously and caught the most:
every other force case is vertical, and vertical and horizontal faces take the
outward normal from opposite sides, so a sign error in the horizontal force
survives a falling test, a buoyancy test and a spurious-current test untouched.
`neutral` looks like nothing and is not — a body weighing exactly what it
displaces, under gravity in the solve, has to have the pressure integral over
its surface cancel its own weight to five significant figures, and a sign error
anywhere in the surface integral shows up there as a body that takes off.

### Turbulence, and the viscous term that was wrong before it

```powershell
"Fluid Solver.exe" turbulence=kOmegaSST turbIntensity=0.05 turbLengthScale=0.02 nu=1.5e-5 U0=10 Lx=2 Ly=0.2 nx=400 ny=64 bcBottom=wall bcTop=wall
```

Off by default, and off means the solver does exactly what it did before this
existed. That is right until the grid stops being able to hold the smallest
eddy that matters, which on any grid a person can afford happens somewhere
around a Reynolds number of a few thousand. Past that the run does not blow up,
it quietly lies: the wake is too long, the recirculation too strong, the drag
too low.

**The viscous term had to change first, and this is the part nobody asked
for.** Every version up to and including 0.2 wrote the viscous term as
`nu * lap(u)`. That is not the viscous term — it is what the viscous term
collapses to when `nu` is one number everywhere. The real one is

```
div(2 nu S),    S = (grad u + grad u^T) / 2
```

and expanding the divergence gives `nu*lap(u)` **plus** `grad(nu)` contracted
with the strain. The second half is exactly what a turbulence model puts there,
so dropping it is not an approximation, it is deleting the only term by which
the model reaches the flow. Writing `nuT` into an array and multiplying a
Laplacian by it would have looked like a turbulence model and been an expensive
way of doing nothing.

On the MAC grid the diagonal part of `S` lands at cell centres and the
off-diagonal part at the corners, which is why the code carries two viscosity
arrays rather than one. **The golden master came out bit for bit identical**,
which is the point: at constant `nu` the two really are the same expression,
and if they had not come out identical it would have meant the new one was
wrong. Two-fluid runs get the term for free, and should have had it all along —
a density jump makes `nu` vary just as much as a model does.

In a volume `S` has **nine** components rather than four and `|S|` is summed
over all of them. Both models are built on that number, so getting it wrong
would be quiet and total. The check is that it cannot prefer an axis: the same
shear laid on the xy, the xz and the yz plane gives the same `|S|`, **bit for
bit on cubic cells**.

**`smagorinsky`** is the large-eddy model and the smaller of the two:
`nu_t = (Cs * delta * D)^2 * |S|`. `delta` is the filter width — `cbrt(dx dy dz)`
in a volume and `sqrt(dx dy)` in a plane, which is not a tidying-up: a plane has
no third direction for an eddy to be a third of, and taking a cube root of a
depth that is not a depth would shrink the model's reach for no physical reason.

`D` is the near-wall damping, and the textbook version does not work. Capping
the mixing length at `kappa*y` compares a length against a length, and on any
grid a run of this kind can afford, the first cell centre is already further
from the wall than the filter width — the cap never binds, ever. What works is
van Driest, because it is keyed on a Reynolds number rather than a length:
`y+ = y * sqrt(|S| / nu)`, `D = 1 - exp(-y+ / 26)`, with `u_tau` from the local
strain so nothing extra is carried around. On the test channel that brings the
mixing length in the first row off the wall down to **32%** of `Cs*delta`, and
it keeps falling as the grid is refined, which is the whole signature of the
thing. `Cs = 0.17` is the isotropic value; anything with a wall in it wants
about `0.1`.

**`kOmegaSST`** is Menter's 2003 shear-stress-transport model, two more
transported fields, transported on three axes in a volume with the same scheme
on each. `F1` blends the constants between k-omega near the wall and k-epsilon
in the free stream; `F2` goes into `nu_t = a1 k / max(a1 omega, |S| F2)`, which
is the SST part and the reason the model exists. Three things are imposed
rather than solved: omega at a wall takes the analytic `60 nu / (beta1 d^2)`
(on the test channel that is 1228.8, and the field peaks at 1228.8), k at a
wall goes to zero, and both are clamped positive — an explicit step will take
`k` below zero on a coarse grid, and then `sqrt(k)` is a NaN and the run is a
very fast way of producing 400 MB of the letters "nan".

The step size gets a fourth limit alongside advection, diffusion and capillary
waves: `1/(beta* omega)`, the source term's own time scale. Near a wall omega
is large and that limit is the one that binds, so a turbulent run takes smaller
steps than a laminar one on the same grid. That is the model's cost and it is
visible in the step line rather than hidden.

The wall distance is a breadth-first sweep out of the solid cells and the
domain edges, with the diagonal step counted at its own length. In a volume it
visits all **26** neighbours rather than 8: a cell diagonally across a corner
in three directions at once is `sqrt(3)` cells away, and a sweep that cannot
step there reports it as further off than it is — which lands straight in
omega's wall value, where an error in `d` is squared. It is geometry, not flow,
so it is built once, and rebuilt when the geometry moves.

`TurbulenceTests`, four cases: the wall distance and the damping (the mixing
length is inverted straight back out of the field and checked against the
model's own definition, cell by cell, so it holds on any grid); k-omega staying
a number, with `nu_t/nu` reaching 43 so the model is doing something; a
continuation reproducing the straight-through run to 2.8e-4 in `k`; and a
backward-facing step, which is what a turbulence model is actually for — the
laminar run has -0.77 m/s of backflow, k-omega has -0.28, so the model took 64%
of it out by mixing momentum into the shear layer.

**`smagorinsky` is deliberately not in that comparison**, and this README-voice
admission is worth repeating here: on a RANS-affordable grid it gives -0.85
against the laminar -0.83. It changes nothing, because it is an LES model being
asked to run somewhere it is not for. `Cs` could have been tuned until the test
went green. It was written down instead.

`k` and `omega` go into **every** frame of a `kOmegaSST` run whether or not
anybody asked, because the frame is the restart file and a two-equation model
that comes back with the inlet values in it has thrown away everything the run
spent its time building.

### The compressible solver

```powershell
"Fluid Solver.exe" regime=compressible machInlet=2.5 "profiles=wedge.obj@x=0.75,y=0.07,size=1,attach=1" nx=240 ny=120 Lx=1.2 Ly=0.6 bcLeft=inlet bcRight=outlet bcBottom=slip bcTop=outlet
```

`regime=incompressible` is the default and is every run written before this
one, down to the last bit. `regime=compressible` is **not** an extension of it.
It is a second solver, sharing the geometry, the boundaries, the frame format
and the UI, and sharing none of the numerics.

**Why it had to be a second solver.** The projection method is built on
`div(u) = 0`. That is not a simplification sitting on top of it, it is the
assumption the whole method is derived from: the pressure exists to enforce it,
the Poisson solve computes it, and the multigrid exists to make that solve
fast. Take the constraint away and there is nothing left of the method to keep.

So there is no pressure solve here at all. `Multigrid.cpp` and
`MultigridCuda.cu` are not called once in a compressible run, and not one line
of either changed for it. The riskiest code in the project sat the whole branch
out, which is exactly why it went before the refinement work and not after.

What runs instead: the conservative variables rho, rho·u, rho·v, **rho·w** and
rho·E in the cell centres, MUSCL reconstruction of the primitives with the same
limiters `convection=muscl` uses, an HLLC flux at every face, and SSP-RK3 in
time. The step size comes from `|u| + c` rather than `|u|` alone, which is the
whole difference: a sound wave now takes a finite time to cross a cell, and the
step has to see it.

`rhoW` is what the third dimension added to the state. The x and y fluxes
gained their `w` row and a whole **z flux** appeared beside them, using the same
reconstruction, the same limiter and the same Riemann solver — there is one
HLLC in this codebase and all three axes call it.

**It is written on a `Block`, and that was deliberate.** `SolverCompressible`
never reads `cfg.nx`, `cfg.ny` or a global array. Every kernel takes a `Block`
— sizes, spacings, origin, field pointers, ghost width — and reads everything
from it. That cost about five percent more effort to write and it is the entire
reason the refinement branch did not begin by rewriting this one: a refined
patch is another `Block`, and the same kernels run on it unchanged.

The physics lives in `CompressibleKernels.hpp`, marked with a single `CFD_HD`
macro so it compiles for both host and device, and the CPU sweep and the CUDA
one call the same functions. Two hand-kept copies of a Riemann solver drift
apart; there is one here.

**Boundaries are characteristic, and that is not decoration.** An inlet imposes
density and velocity from `machInlet` and `T0` while the flow is subsonic and
holds the pressure too once it is not, because above Mach 1 nothing travels
back out of the domain to tell the boundary what the interior wants. An outlet
holds `pInf` while subsonic and says nothing once the flow leaves faster than
sound. Inside the domain, a face between a solid cell and a fluid one gets the
wall flux **written down** rather than solved for: zero mass, zero energy,
pressure in the normal momentum, nothing else. A mirrored state does give HLLC
exactly zero mass flux — but only before the reconstruction, which uses each
cell's own neighbours and breaks the symmetry the cancellation depended on.

**What it costs.** 256×128, empty domain at Mach 0.6, two cores of a 2.1 GHz
Xeon:

| | per step | steps for 4 ms of flow | total |
|---|---|---|---|
| incompressible, muscl + rk2 | 4.6 ms | 171 (for 600 ms) | - |
| compressible, one gas | 5.8 ms | 1007 | 5.8 s |
| compressible, two gases, same properties | 7.6 ms | 1007 | 7.6 s |
| compressible, air and helium | 16 ms | 3722 | 59 s |

A step costs about a quarter more than an incompressible one and the step
itself is six times smaller, because `|u| + c` is six times `|u|` at Mach 0.6.
That is not an implementation cost, it is what solving for sound means.

Carrying a fifth variable costs **31%** — the second row against the first.
Everything past that is the flow, not the code: helium carries sound 2.9 times
faster than air, so the step shrinks with it, and a real contact discontinuity
makes the limiter's extremum test unpredictable across most of the domain,
which is a branch misprediction per face. At scale the sweep settles at about
150 ns per cell-step (149.7 at 512×256, 165 at 256×128), which on two 2.1 GHz
cores is roughly 105 cycles per Riemann problem — about what seven divisions
and two square roots cost when they are latency bound. OpenMP gives 1.80× on
two cores.

**AVX2 measures no difference at all**, and these notes would rather say why
than pretend. GCC does vectorise — 31 vectorised loops and a great deal of SLP
inside the kernels, all of it the five flux components done together. What it
cannot vectorise is the part that costs: the divisions and square roots are per
**face**, one at a time, and no compiler will find eight faces to do at once
through a function call. That would need the face loop written with intrinsics.
It is a real remaining factor and it is a job to do with a profiler open rather
than on the way past.

`omega`, `smootherOmega`, `mgIterations`, `mgTolerance` and `mgMinCoarseSize`
do nothing in this regime. They are still accepted, because they are part of
the argument contract the UI has always sent and refusing them would break
every launcher for no gain.

**Bodies travel here too.** Same grammar, same keyframes, same `free=1`. The
mask is re-cut from the model every step, and two things happen that only
matter here: the solid ghosts mirror about the **body's velocity**, not about
zero, so the pressure at the wall face rises ahead of it and falls behind and a
body moving fast enough makes a wave that leaves and keeps going; and a cell
the body has just left is reseeded from its fluid neighbours. The pressure at a
wall face is the neighbouring cell's plus `rho*c*(closing speed)` — the
acoustic piston relation, and the first-order Riemann solution at a moving
wall. For a wall that does not move the term is zero and this is exactly what
the previous behaviour was. For a disc driven at Mach 0.25 through still air it
is the whole point: **101479 Pa in front against 99477 Pa behind**, compression
ahead and rarefaction in the wake, which is what a piston does.

`CompressibleTests` runs nine cases, four of them against arithmetic rather
than a table:

- **Sod's shock tube against the exact Riemann solution**, which the test
  solves itself — it iterates the star pressure and samples the fan, so every
  one of 400 cells is compared against arithmetic. Worst error 7.5% in density
  and 7.2% in pressure, both at the contact discontinuity, which is exactly
  where a limited second-order scheme smears.
- **An oblique shock on a 15 degree wedge at Mach 2.5.** theta-beta-M fixes the
  shock angle at 36.9 degrees and Rankine-Hugoniot the pressure jump at 2.468;
  the run gives 2.648, 7% high on a grid with 24 cells across the wedge.
- Two gases, both halves of the acoustics, a driven body, a wav that is
  actually a wav, a stretched grid and the refinement hierarchy — below.

### Two gases

`phases=2` in the compressible regime means two **gases** rather than two
liquids, and they always mix: there is no interface to carry. `gamma2` and `R2`
are the second gas, and the transported mass fraction sets the mixture's own
gamma and R through its heat capacities.

That is not bookkeeping — it is the answer to the question everybody asks
first. Helium at the same pressure and temperature carries sound 2.9 times
faster than air, and it comes out of the run at **2.935 against the 2.935** the
gas constants give, with nothing in the code that knows about helium.

```powershell
"Fluid Solver.exe" regime=compressible phases=2 gamma2=1.667 R2=2077 machInlet=0.6
```

`speciesMode=passive` turns that off: the fraction still rides along but gamma
and R stay frozen at the first gas. It exists to be compared against, it is
very easy to leave on by accident, and the solver therefore prints a four-line
warning about it in the configuration it echoes before starting.

### The sound it makes, and the microphones

Two independent halves, each switched on by itself, because they answer
different questions.

**`acousticFields=1`** writes the sound onto the grid. Every cell keeps a slow
running mean of its pressure — `acousticWindow` sets how slow — and the
fluctuation about it becomes `pFluct`; its RMS becomes `SPL` in decibels
against `acousticRef`; and the rate at which the fluctuation changes sign
becomes `pitch` in hertz. The mean for the level and the mean for the pitch run
at deliberately different speeds: the level wants a slow one so everything the
flow is doing counts as sound, and the pitch wants a fast one, or the sign of
the fluctuation is decided by whatever the slow mean has not caught up with yet
and the crossings get counted at the rate the mean drifts. (That was wrong
first: 45 Hz in a tube that rings at 500.)

Zero crossing is a crude pitch estimator and these notes are not going to
pretend otherwise: it reports the rate of the largest thing happening and it is
fooled by broadband noise. It is also free, it works per cell, and you can look
at it.

**`microphones=`** is the accurate half. Each point records the pressure every
`micInterval` steps and the run writes `microphones.txt` beside the frames: the
whole trace, then a level and a peak frequency for each point found by scanning
512 bins with Goertzel. Not an FFT, because the sample count is whatever the
time step happened to give and is never a power of two, and scanning fixed bins
costs bins×samples with no padding, no window artefacts and no library.

A microphone takes a **third coordinate** now. It is optional and defaults to
the middle of the depth, so a line of microphones written for a plane run still
lands where it always did — and in a volume you can put one off the mid-plane
and hear the difference, which is the whole reason anybody runs a volume.

```powershell
"Fluid Solver.exe" regime=compressible caseType=shockTube nx=340 ny=8 Lx=0.34 Ly=0.04 geometryFile=empty "microphones=x=0.05,y=0.02" micInterval=1 micAudio=1 micAudioSpeed=0.05 totalTime=0.02
```

On a closed 0.34 m tube rung by a pressure step the field reports 179 dB and
749 Hz, the microphone 177 dB and 1398 Hz, against a 500 Hz fundamental. Two
different estimators of a signal made of bouncing shocks, and neither is lying.

**And you can listen to it.** `micAudio=1` writes `microphone1.wav`,
`microphone2.wav` and so on — one mono 16-bit file per microphone. Three things
happen to the trace on the way in and each of them matters. The **mean comes
off**, because a 101325 Pa DC offset in a signed 16-bit sample is silence with
a clipped rail on it. It is **box-filtered down, not decimated** — the run
samples somewhere between 100 kHz and 10 MHz, and throwing away every sample
but the 44100th folds everything above 22 kHz back into the audible band as a
screech that was never in the flow, so each output sample is the average of
every input sample inside its own window. And it is **peak-normalised** to 0.9
of full scale, with the pascal value that ended up there printed, so the file
is audible and you can still say what it was.

`micAudioSpeed` stretches the timebase without touching the simulation.
`0.05` plays it twenty times slower and divides every frequency by twenty with
it, which is how you hear two milliseconds of shock tube and how you bring a
40 kHz whistle down to where ears are.

The wav test reads the file back byte by byte: RIFF and WAVE and fmt and data
where they belong, the RIFF length matching the file, 16-bit mono PCM, the
sample rate that was asked for, the block alignment agreeing with the format,
the frame count matching `totalTime / micAudioSpeed` to within 2%, and the peak
at 0.9 of full scale.

### The stretched grid

```powershell
"Fluid Solver.exe" regime=compressible geometryFile=wing.obj nx=400 ny=200 gridStretch=body stretchRatio=1.05 refineNear=0.3 machInlet=0.8
```

Every run before this had one `dx` and one `dy` for the whole grid. The
compressible solver does not any more: `Block` carries a width per column and a
height per row, and the far field costs what it is worth instead of what the
body needs. `gridStretch=body` keeps a band `refineNear` wide around the mask
at full resolution and grows the cells outside it by `stretchRatio` each step;
`wake` lets the fine band run to the downstream edge; `edges` puts the fine
cells against the walls, which is what a duct wants; `off` is the even grid and
is still the default. The z axis came with the port, and `gridFaceZ` goes into
the frame next to `gridFaceX` and `gridFaceY`.

**The part that is easy to get wrong.** A MUSCL reconstruction takes a slope
and extrapolates it half a cell to the face. Written the obvious way that is
`face = centre + 0.5 * limit(centre - back, forward - centre)`, and the `0.5`
is doing two jobs at once: it is half of *this* cell, and the differences it is
limiting are implicitly *per cell*. On an even grid those are the same number.
On a stretched one they are three different numbers and the scheme quietly
stops being second order. What is written instead is

```
face = centre + (width/2) * limit((centre - back) / hBack,
                                  (forward - centre) / hForward)
```

with real distances between cell centres. The limiters are all homogeneous of
degree one, so on an even grid this collapses to exactly the old expression —
the same floats in the same order, which is why turning stretching off is not
merely close to the old answer but identical to it.

**Checked against** two things, and the first has teeth. A second-order scheme
reproduces a *linear* profile exactly on any grid, so the test lays a linear
density on a 4:1 geometric grid, takes one stage, and reads the flux divergence
back out. It comes out **8.4e-4** off in the worst cell, which is the float32
noise floor for a difference of two fluxes of that size; the naive
`0.5 * limit(differences)` form gives **3.0e-3** on the same grid, and the
threshold sits between them. Second, a smooth bump on the same grid at 200 and
400 cells converges at order 1.5 — a limiter clips at a smooth peak and a 4:1
stretch costs more, so that is what working looks like.

Worth saying plainly, because it would be easy to claim more: for a *smoothly*
stretched grid the two forms differ only at higher order and both converge at
second order. The naive one bites where the cell ratio changes abruptly — at
the edge of the fine band, which is exactly where this solver puts one.

**CUDA and `gridStretch` do not go together, and the run says so rather than
pretending.** The device kernels carry one cell size per axis and the stretched
metrics live on the host, so a GPU run would quietly solve an even grid instead
of the one that was asked for. Asking for both prints four lines and stays on
the CPU.

Stretched frames come out as `RECTILINEAR_GRID` with the face coordinates
written out, which ParaView opens natively. An unstretched run still writes
`STRUCTURED_POINTS` exactly as before.

### Adaptive mesh refinement

```powershell
"Fluid Solver.exe" regime=compressible caseType=shockTube nx=128 ny=64 Lx=1 Ly=0.5 geometryFile=empty amrLevels=1 amrEvery=2 amrThreshold=0.02
```

`gridStretch` puts the small cells where you say. This puts them where the flow
says, and moves them as it moves.

Levels of rectangular patches sit over the base grid, each halving the cell
size of the level above. Cells are tagged by `amrCriterion` — a density jump
for shocks and contacts, `vorticity` for wakes and shear, `species` where two
gases meet, `body` for the cells against a solid, or `everything` — padded by
`amrBuffer`, and clustered into boxes by a signature-and-split pass that cuts a
region at the widest gap in its tag histogram, or at the sharpest bend when
there is no gap. That is Berger-Rigoutsos, and the whole point of it is that
one long thin feature becomes one long thin patch instead of a square box round
everything.

**A patch is a box, not a rectangle.** `AmrBox` gained `k0` and `nz`, and every
part of the machinery followed: clustering splits along the longest of the
**three** axes, tagging looks at the z gradient and the full 3D curl instead of
the single vorticity component a plane has, ghost cells interpolate with a z
slope, and averaging down is the mean of **eight** fine cells rather than four.
The patch frames go out as 3D `RectilinearGrid` pieces. The refusal that used
to say AMR and `nz > 1` could not both be on is gone — there was nothing wrong
with the argument at the time, and there is nothing left of it now.

A patch is a `Block`, so not one kernel needed touching: `advanceStage`,
`blockTimeStep`, `hllc`, the reconstruction and the solid fill all run on a
patch unchanged, on its own arrays with its own spacing.

**Time subcycles.** A level takes two steps for every one the level above takes,
which keeps every level at the same CFL rather than dragging the whole run down
to the finest cell. Between them the patches refill their ghosts from the level
above by limited linear interpolation — which reproduces a constant exactly —
and from their siblings where they overlap.

**What it is worth.** A 128×64 tube with one level: 9728 cells against the
32768 a uniform fine grid would need, 30% of them, and the answer on the base
grid comes out 21% closer to a 256×128 reference than the plain coarse run.
Three shock tubes in a closed box — 64 cells, 64 with one level over them, 128
— put the refined run **7.4e-05** from the 128-cell answer where the plain run
is **4.5e-03** away.

**What is not in it, and this matters.** There is **no flux correction at
coarse-fine boundaries.** The textbook Berger-Colella scheme keeps a register
of the fine fluxes along each patch edge and replaces the coarse flux with
their average, so the composite is conservative to the last bit. One was
written. It is not here, because it did not work: with the register on, the
mass drift of a frozen grid went from 1.35e-3 to 1.56e-3, and a gain sweep —
full, half, quarter, reversed — moved the number around inside the noise
without ever improving it. A correction derived twice that cannot be shown to
help is not a correction, it is a second bug hiding behind the first.

What is left instead is worth knowing exactly:

- With the patches following the flow, which is how it is meant to be run, a
  closed box conserves mass to about 1e-5 to 1e-6.
- Freeze the grid — `amrEvery` larger than the run — and let a wave sit on a
  patch edge for hundreds of steps, and it drifts to about 1e-3.
- `amrEvery=2` costs a regrid every other step and is cheap; the regrid carries
  the old fine data into the new patches wherever they overlap and only
  interpolates from the coarse where there was none. Regridding *without* that
  carry-over is worse than no refinement at all — the first version came out
  22% *worse* than the plain coarse run.

**Output.** The ordinary `.vtk` frame is still written on the base grid with
the fine levels averaged into it, so everything that could read a frame before
still can and the UI needs to know nothing. Beside it goes a `.vtm` with one
`.vtr` per patch carrying density, pressure, velocity, the mask and the level
number, which ParaView opens as a hierarchy and draws at full resolution.

`amrLevels` refuses `gridStretch` (refinement halves a cell and has nothing to
halve when every cell is already a different size), refuses `useCuda` (the
hierarchy lives on the host), and is refused for `regime=incompressible` — not
out of tidiness: the pressure solve is global, and a multigrid hierarchy over a
patch hierarchy is a different solver rather than a setting.

### The pressure solver

Not a feature you can point at, and most of what made the features above
possible.

- **Variable per-face weights.** `setGeometry` did topology and physics in one
  pass and could only run once. It is split: `setGeometry` is
  who-is-next-to-whom, `setCoefficients(faceX, faceY, faceZ)` is what each face
  weighs, and the second is cheap enough to call every step. Coarsening
  averages along a face, because fine faces stacked across a coarse one are
  parallel links and not series ones.
- **The projection stopped being a constant-coefficient problem.** It was
  `grad^2 p = div(u*)/dt` and is now `div((1/rho) grad p) = div(u*)/dt`, with
  1/rho on every face, rebuilt every step. The density on a face is the
  **harmonic** mean of the two cells, not the plain one: at a thousand to one
  the plain mean of water and air is half of water, so a face with air on one
  side would carry the pressure gradient of something five hundred times denser
  than the air actually there.
- **A Krylov iteration, and why.** A plain V-cycle hierarchy is a fine solver
  for a constant-coefficient Laplacian and a bad one across a jump of eight
  hundred to one: the coarse grids stop representing the fine problem, the
  correction that comes back up is longer than the error it was asked to
  remove, and the residual grows by a factor of four every cycle until the
  field is NaN. That is not a tuning problem; it is what bilinear interpolation
  across a discontinuity does. Two things went in, **both only when the face
  weights are not all one**, so every single-phase run takes the path it always
  took and produces the same bits: the coarse-grid correction is scaled by
  however much of it actually reduces the residual, and the whole V-cycle
  became the preconditioner of a conjugate gradient iteration. `PoissonTests`
  solves an 816:1 jump to 1e-6 in **27 iterations** and fails if it does not.
  Before this it did not converge at all, at any budget.
  *(Harmonic coarsening of the face weights was tried first and is measurably
  worse — five times slower on a water/air jump, converging somewhere else. It
  is written down so nobody has that idea again.)*
- **The CUDA backend has both**, so a two-fluid pressure solve stays on the
  card instead of coming back to the host, and one nine-slab allocation with
  halo is done once instead of `cudaMalloc` per level per solve.
- **The seven-point operator.** In a volume the operator gained `coefF`/`coefB`
  and the hierarchy coarsens each axis independently. Second-order convergent
  on a manufactured 3D solution; at `nz = 1` it returns the 2D field bit for
  bit with the same level count and the same cycle count.

### Extra diagnostic fields

```powershell
"Fluid Solver.exe" "extraFields=vorticity,speed,objectId"
```

A frame carries pressure, the solid mask and velocity, and nothing else unless
asked. `extraFields` adds any of `vorticity`, `divergence`, `speed`,
`objectId`, `density`, `source`, `curvature`, `nuT`, `wallDistance` and
`strain`, and in the compressible regime `temperature`, `mach`, `speedOfSound`,
`entropy`, `pFluct`, `SPL` and `pitch` as well. They cost four bytes a cell
each, are written for ParaView and the UI to read, and nothing in the solver
reads them back — a frame carrying them continues exactly like one that does
not. Empty is the default, so a frame stays the size it used to be.

`saveVTK` used to write three hard-coded blocks; it walks a list it knows
nothing about now, which is why five branches did not each make the same edit
in three places.

**One of them changes shape with `nz`.** `vorticity` is a scalar in a plane —
there is only one component a plane can hold — and a **vector** in a volume,
written as three floats and glyphed by ParaView like any other vector. Anything
that reads frames has to cope with both, which is one reason the frame carries
a format version. `divergence` and `speed` simply gained their z term and stay
one float.

Two fields are not on that list because they are not optional: the phase
fraction goes into every frame of a two-fluid run, and `k` and `omega` into
every frame of a `kOmegaSST` run, because the frame is also the restart file.

---

## Geometry

**At `nz > 1` a model is voxelised, not cut.** There is no section plane and no
contour. The triangle soup is placed, scaled and rotated exactly the way the
plane case places it, and then every cell centre is asked one question: is it
inside? The answer is a ray cast along +x with a crossing count — odd is
inside, even is out — and the casts are cheap because they are shared: one cast
per (j, k) scanline rather than one per cell, the triangles binned by (y, z) so
a scanline only ever meets the few that reach it, and the scanlines handed out
across OpenMP threads.

**128^3 against 50k triangles takes about 5 ms**, which is why the mask can be
cut again every step for a body that travels.

A crossing count is only meaningful on a closed surface. A model whose surface
is open has no inside, and the run **says so and stops** rather than filling
whatever the parity happened to come out as — which is the failure mode that
produces a body with holes in it and a wake nobody can explain.

**At `nz = 1` the section cut is untouched**, and produces the same mask 0.2
produced. Three things around it are new:

- `sliceAngleY` joins `sliceAngleX` and `sliceAngleZ`. The three are applied as
  **Rz·Ry·Rx**, which at `sliceAngleY = 0` is exactly the pair of rotations the
  plane case has always applied. The same three angles orient the model on the
  voxel path, where there is no plane to make a normal out of.
- **Every closed loop the plane cuts is kept**, not only the largest: two
  profiles side by side stay two profiles, and a loop inside a loop comes out
  as a hole. Slivers under a ten-thousandth of the largest loop are dropped as
  cutting noise rather than rasterized into the flow.
- Bodies are labelled and numbered on their own — **8-connected in a plane,
  26-connected in a volume** — and each one carries its **volume** and its full
  **inertia tensor about its own centroid**, worked out from the cells it is
  actually made of rather than from the shape somebody meant to put there.

The rasterizer was rewritten for the moving-body case and the rewrite is most
of why moving a body costs 19.3% rather than a multiple. `std::hypot` was a
fifth of the moving path — `distanceToSegment` called it once per cell per
segment, and it is a libm call that goes to real trouble not to overflow, while
the comparison it feeds only wants to know which side of the radius the point
is on. It went from 9.4% of the run to not appearing.

### Several models at once

```powershell
"Fluid Solver.exe" "profiles=wing.stl@x=0.6,y=0.5,size=0.3;ball.obj@x=1.6,y=0.5"
"Fluid Solver.exe" nz=64 Lz=1 "profiles=wing.stl@x=0.6,y=0.5,z=0.3,ay=15;wing.stl@x=0.6,y=0.5,z=0.7,ay=-15"
```

`geometryFile` still takes one model and centres it. `profiles` takes as many
as you like and puts each one where you say. The separator between the file and
its settings is `@` rather than `:`, because a Windows path already owns the
colon. `x`, `y` and `z` place the centre in metres, `size` is the larger side
of its section in metres, `rot` turns it in the plane, `ax`, `ay` and `az` are
slice angles for that model alone, and `invert=1` mirrors it. A file written
with no `@` keeps the old behaviour — centred in the domain at a fifth of its
smaller side.

`z=` and `ay=` are what the third dimension added. `z=` defaults to centred,
which is what a plane run wants and what it always silently did.

Anything landing on or outside the domain edge is refused before the run
starts, with the distance it missed by, because a body touching the border is a
wall rather than an obstacle and a wall nobody asked for through the middle of
a case is worse than a message:

```text
!!! profile 'cube.obj' does not fit the domain:
    past the right edge by 0.170833 m
    it spans x 1.75..2.15 and y 0.3..0.7 in a domain of 2 x 1 m.
    Move it with x= and y=, or shrink it with size=.
```

---

## Output and restart

**Frames are `DIMENSIONS nx+1 ny+1 nz+1` with a real `SPACING dz`,
`CELL_DATA nx*ny*nz`, and `VECTORS velocity` whose third component is now a
real `w`** rather than a column of zeroes kept for ParaView's benefit.

**They are about 40% smaller**, and nothing in them is stored twice. 0.2 wrote
32.3 bytes a cell; a plane now writes about 19, losslessly, with the ParaView
output unchanged:

| Array | Bytes/cell | |
|---|---|---|
| `pressure`, float, Pa | 4 | what ParaView colours |
| `velocity`, 3×float | 12 | what ParaView glyphs |
| `solid`, `unsigned_char` | 1 | the mask holds 0 or 1, and `bit` is not a type ParaView reads reliably |
| `facePack` | ~2.2 | the staggered face velocities — `u`, `v` and, in a volume, `w` |
| `configText` | ~0.1 | the run's own settings |

Where it came from: `solid` was an `int32` per cell for a mask that only holds
0 or 1. `pRaw` is gone — `SCALARS pressure` is the same field times `ro`, bit
for bit, so the reader divides it back out using the frame's own `ro` rather
than the run's. And the face velocities are stored as the **mistake** a
prediction makes rather than as the answer: along a row,
`u[i+1] = 2·u_cell[i] − u[i]`, and marching that from the inlet reproduces the
whole row to about 3e-6 m/s on a grid 256 wide — close, and not close enough,
because the error accumulates. So for each face the prediction is subtracted
from the truth and the difference written as a zigzag varint. An exact
prediction costs one zero byte, one ulp out costs one byte, a real miss costs
five; about one face in eight is predicted exactly, the block comes to 2.2
bytes a cell against 8.1 raw, and it is **lossless** — the reader adds each
delta back before predicting the next face from the corrected value.

Three things make it safe to rely on: the prediction is `2.0*cell − previous`
in **double** and nothing else (in float it overflows above 1.7e38 unless the
compiler contracts it into an FMA, and `-mfma` goes to the AVX2 build and not
the scalar one, so the two builds disagreed on exactly those values); the two
lines the march starts from are written out in full; and a 32-bit FNV-1a over
the faces goes in the block header, so a truncated file falls back to rebuilding
the faces and says so instead of silently restarting from something subtly
wrong.

**Frame format version is 3.** Version 1 is what 0.2 wrote — `uFace`, `vFace`
and `pRaw` as plain float arrays with `solid` as `int32`. Version 2 introduced
the packed face block. Version 3 is the volume: three `DIMENSIONS` tokens that
can each exceed 1, a real `dz`, `w` faces inside the packed block, and
`bodyState` as a key/value record.

**Every one of them still loads.** A version 1 or version 2 frame comes back as
a volume one cell deep with `w` zero — which is not a conversion, it is what
those frames always were — a version-2 packed block still unpacks, and the old
seven-number `bodyState` is still read. **The reverse does not hold**; see the
[migration section](#if-you-have-old-files-or-old-scripts).

**Continuing works the same way and is still exact.** Two things make it so and
both are easy to miss: the frame stores the `dt` that was live when it was
written, because `dt` is only recomputed every `dtUpdateInterval` steps and a
continuation that recomputed it immediately would shift the cadence by a
fraction of a step; and the first-solve full multigrid pass is skipped, because
a continuation is not starting from nothing — it has the converged pressure of
the step it stopped at, which is a better guess than that pass produces.
Continuing from the last frame of a plane run and letting it go further
produces **bit-identical** results to never having stopped.

**A volume continues to 5e-4** of one that never stopped. That is looser than
the bit-identical number above and it is the honest one for a volume: the plane
figure is measured on a single-phase run where nothing but the clock and the
in-flight `dt` have to be restored, and 5e-4 is what the whole machinery — `w`
faces, the body records, the phase volume — comes to when it is all put back at
once.

**A run whose bodies travel rebuilds the geometry from the model** and puts the
bodies back at the pose the frame carries, because the mask in the frame is a
rasterised copy of wherever they had got to and moving on from it means cutting
the outline again. Split a run in half and the halves end at the same pose and
the same mask, to the bit. Every other continuation takes the mask straight out
of the frame, so the STL does not have to still exist and no rasterizer change
can move a boundary in the middle of a run.

What can and cannot change on a continuation: `nx`, `ny`, `nz`, `Lx`, `Ly`,
`Lz` are fixed by the frame and changing them is refused; geometry keys are
ignored; `totalTime` must be larger than the time already reached; the output,
timestep and multigrid keys are free; `gravityEnabled`, `gravityAccel`,
`gravityAngle` and `gravityTilt` are free, because changing them shifts the
hydrostatic part of the pressure and leaves the velocity where it was;
`wallMotion` and `bodyMotion` are free.

---

## Tests

There were none at 0.2. `CMakeLists.txt` had `include(CTest)` and zero tests
under it, which is the software equivalent of buying a smoke alarm and leaving
it in the box.

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCFD_ENABLE_CUDA=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Everything except `main.cpp` now lives in a library called `cfd_core`, and both
the executable and the tests link the same objects, so what is tested is what
runs. Off by default, because the release matrix is 34 rows and none of them
needs them built.

| Test | What it would catch |
|---|---|
| `PoissonTests` | a manufactured solution the pressure operator has to reproduce to second order, face weights that scale it, a closed box defined only up to a constant, and an 816:1 jump converging |
| `ChannelTests` | the developed profile between two no-slip walls against the exact parabola, and the same channel with free-slip walls to prove the number came from the wall and not the inlet |
| `CavityTests` | the lid-driven cavity at Re 100 against Ghia, Ghia and Shin |
| `InletTests` | inlet bands face by face, a parabolic band carrying the same flow rate as a flat one, and what goes in coming back out |
| `MultiphaseTests` | a stirred drop's volume, a still layer staying still, a dam break against its own energy bound, and a step in composition spreading as an error function |
| `SurfaceTensionTests` | the Laplace jump, the spurious current, and a square blob refusing to stay square |
| `MovingBodyTests` | travel, fresh cells, numbering, the three couplings, neutral buoyancy, and thrust |
| `TurbulenceTests` | the two models against what they claim, including a continuation and a backward-facing step |
| `CompressibleTests` | Sod, an oblique shock, two gases, both halves of the acoustics, a driven body, a written wav, a stretched grid and the refinement hierarchy |
| `VolumeTests` | the whole third dimension, in ten checks — the plane still being the plane, the cube cavity, a solid body in a volume, a volumetric restart, gravity down each axis, a closed box's mass, turbulence staying finite, walls turning about every axis, a shock tube staying one-dimensional, and refinement in depth |
| `ConservationTests` | divergence after the projection, inflow against outflow, and a fluid at rest under real gravity staying at rest |
| `ConvectionTests` | every scheme on the same case, and the ordering of how much each throws away |
| `RestartTests` | a run cut in half and continued reproducing the run that was never cut |
| `BackendAgreementTests` | AVX2 on against off and many threads against one, which is the thing that quietly turns one solver into several that disagree |

`.github/workflows/build-all.yml` runs `ctest` on a Linux job before it builds
a single release row, and **every platform job waits on it**. A release that
does not compute correctly is worse than no release.

---

## Release and installer tooling

The installer sources (`installer/`), `RELEASE-GUIDE.md` and the release matrix
itself are unchanged: still one installer per system that reads the processor
and unpacks the matching build, still 34 solver rows plus 30 `-ui` rows. Three
things did change.

- **CI gates the matrix on the tests**, as above.
- **`scripts/make-release.ps1` packs `libomp140.<arch>.dll`** instead of
  `vcomp140.dll` on Windows OpenMP rows — `libomp140.x86_64.dll`,
  `libomp140.aarch64.dll`, `libomp140.i386.dll`, whichever fits the row. It
  still looks for `vcomp140.dll` afterwards, so a toolchain that fell back to
  the classic runtime still packs a working row, and a row that finds neither
  is reported rather than shipped as a binary that will not start.
- **`scripts/make-release.sh` stops eating its own flags.** `VERSION="${1:-}"; shift`
  took the first argument whatever it was, so `make-release.sh --something` lost
  both the flag and the version. Only a bare first argument is the version now.

---

## Visualization: the desktop UI

At 0.2 the desktop application was a sentence in the README and a vendored copy
of SFML in the solver's `lib/`. The sentence was true about the intent and not
about the program. The SFML copy is gone from the solver — nothing in `src/`
ever linked it — and the UI is its own repository, its own build, its own
release matrix of 30 archives, and its own test suite of 10 green suites.
`cfd_app` writes VTK frames and nothing else; the UI drives it as a child
process.

**A real 3D viewport, on OpenGL**, in the same window. `<SFML/OpenGL.hpp>`
gives OpenGL 1.1 with vertex arrays and `pushGLStates()`/`popGLStates()` is how
raw GL and SFML drawing share a window, so the new dependency list is empty.
What it draws: the domain box and its cell grid, the body's surface or its
wireframe, three slice planes moved through the volume on their own sliders,
the translucent cloud, isosurfaces by marching cubes, vortices as a Q-criterion
surface with their core lines, 3D streamlines with animated tracers, a colour
field (pressure, speed, u, v, w, vorticity, Q, and every named scalar the frame
carries — `density` among them, when the run wrote it), orthographic or
perspective, frame all, and the six axis views — all of it behind eight
buttons that open lists, described further down.

Everything is built into vertex arrays **once per frame change**, not per
redraw, and the isosurface, the vortex surface and the streamlines are built
off the redraw path entirely. Turning a layer off does not merely stop drawing
it — it stops being rebuilt.

**Clicking in it selects something.** A body selects it, the `Body` row in the
BODIES group moves to that number, and every row under it is about that body
from then on. A domain face focuses that face's boundary row and says what it
currently is. This is the thing the panel could never do: the object numbers
come out of the solver's flood fill rather than out of the order you listed the
models in, so pointing was the only reliable way to mean a particular body.

**Models are placed and turned by typing, not by editing a line of grammar.**
Where a model sits used to live only inside the `profiles` text — `wing.stl@x=
0.6,y=0.5,size=0.3` — which is fine to read and miserable to nudge. The BODIES
group now carries `Position X/Y/Z`, `Size`, `Tilt X/Y/Z` and `Turn in plane`
for whichever body is selected, and they are that line written out as rows:
typing in one edits the entry, and editing the line by hand moves the rows.

Under them is the way a 3D package does it. `Move along` picks an axis,
`Move by` takes a distance, and the moment you type one the model moves that
far and the box goes back to zero — so the same number typed twice moves it
twice as far, and there is no Apply to forget. `Turn about` and `Turn by` are
the same for degrees, with `plane` as the fourth axis for the rotation inside
the cut plane, which is the one that means anything at `nz = 1`. Angles wrap
into ±180 rather than climbing to 400.

The same thing works from inside the 3D viewport without touching the panel at
all: click the body, press `G`, press `x`, type `0.25`, press Enter. Three
ways to say the same thing — drag a slider by eye, type the absolute number
into the row, or type the offset — which is the arrangement every 3D package
settles on because each of the three is the convenient one some of the time.

Everything in the line these rows do not manage — `invert=1`, a spelling of
`angleX` rather than `ax` — is carried across untouched, and the order the
settings were written in is the order they keep. A body with no entry in
`profiles` yet gets one written from the imported model the first time it is
moved.

**The 2D view is kept whole and is now a slice through the volume.** Not
reimplemented, not ported, not "mostly the same" — the colour maps, the
vectors, the tracers, the probe readout, the legend, the zoom and the pan are
the code they always were, and on a flat frame every pixel of it is what it
used to be. On a volume it draws one plane of cells: pick the axis with `X`,
`Y` or `Z` and move the plane with `Up`/`Down`. **`V` switches between the two
views**, and it is the same key in both directions. A volume frame opens in the
3D viewport; a flat frame opens in the 2D view, because a 3D viewport showing a
slab one cell thick is a worse picture of a plane than the plane is.

**Rows for every new solver parameter, hidden while `nz = 1`** — off the panel,
not greyed out, keeping their values while they are away: `Lz`, `nz`, the front
and back boundary kinds and speeds, `Inlet from 2` / `Inlet to 2`,
`Gravity tilt`, `Start shape Z`, `Slice angle Y`, the nine new BODIES rows, and
the third `Inlet profile` option. A fresh UI on a fresh install is configuring
exactly the plane run it always configured, and the panel is the length it
always was until somebody types a 2 into `nz`.

**Save and load a configuration.** Save writes the whole setup as `key=value`
lines in the solver's own grammar, which means the file is not a description of
a command line, it *is* one — feed it to the solver and it runs. Load reads one
back; rows the file does not mention keep what they have, and rows it mentions
that this panel does not know are **listed** rather than swallowed.

**Recover setup** is the useful half: every frame the solver writes carries its
own configuration in a `configText` block, so point the UI at a folder of
frames, pick one, and the panel fills in with what that run was actually
launched with, down to `nz`, `Lz`, the six boundary kinds and the body grammar.
That closes the one gap that used to need a lab notebook — a folder of frames
from three weeks ago is now self-describing.

**Body paths are curves.** A body's path through the Layout view is a
Catmull-Rom curve through its control points by default, in three dimensions,
rather than a series of straight runs. Drop three poses in a rough arc and the
body flies the arc instead of flying to the middle one, stopping dead and
turning. The UI keeps the poses in its own row, `uiBodyTrack`, saved in the
`.cfdui` and never sent anywhere, and rewrites `bodyMotion` from it every time
it changes — because a keyframe in a user's head is a *pose* and `bodyMotion`
is *velocities*. Straight keyframes are still there and are what a linear
segment produces.

**The window is laid out the way Blender lays one out** — a header strip, the
viewport, a properties column with the parameter groups and its search, a scene
outliner above it listing the domain, its six sides and the bodies in the mask,
and a timeline along the bottom with a tick per keyframe. Not as a compliment
to Blender: it is the thing to copy when a window has to hold a viewport,
several hundred parameters and a timeline at once, because that is a problem
somebody has already solved and users already know the answer to. The orbit,
pan and zoom bindings are Blender's too, numpad views included.

**The panel changes shape with the regime.** Picking `compressible` does not
grey the pressure-solve rows out, it takes them off the panel — viscosity,
density, the two fluids, the VOF scheme, surface tension, sources, gravity, the
whole turbulence group and all five multigrid rows go, and the gas and acoustics
rows come in. Picking `incompressible` puts them all back exactly as they were.

**What goes on the command line depends on what the selected solver
understands**, because a solver exits on the first argument it does not know.
The UI looks each block's key up in the executable's own parameter table and
leaves the whole block off when it is not there — and the whole third dimension
is one block found by one key, `nz`. Point this UI at a solver that predates
the port and none of those eleven keys reach the command line.

**Frames decode faster.** The legacy binary payload is big-endian, and every
32-bit word is swapped in place — eight at a time through one AVX2 shuffle
where that is compiled in — rather than pulled through the stream a value at a
time. On a 600×300 grid that is **24.8 ms down to 2.6 ms** per frame. Frames
also decode on several threads at once, and the decoded-frame cache holds up to
256 frames within its byte budget instead of 16, so an ordinary series ends up
entirely resident after one pass.

**The preview is on the solver's grid, not on a better one.** The UI used to
compute `dx` in double where the solver computes it in float. For `Lx = 1,
nx = 50` that is 0.019999999552965164 against 0.02, and by `i = 30` the two
disagree about where the cell centre is by a part in 10^8 — which is nothing at
all right up until a face of the model lands on a cell boundary, which for
anything axis-aligned it does on purpose. Then the corner cells sit at exactly
one cell circumradius from the outline and a part in 10^8 is the whole
difference between a solid cell and an empty one. Three cells out of 2500 on a
cube. The preview divides in `float` now: being more accurate than the thing
you are previewing is not an improvement.

On a volume run there is no section adapter at all — the solver voxelises the
model, so what goes on the command line is the `.stl` or `.obj` you imported and
nothing is written in between. That is strictly better and it is also the only
thing that can be right: a section adapter is a flat outline, and handing one to
a voxeliser would describe a body with no thickness.

**A volume opens on the cloud, not on a slice plane.** A plane through a
volume is one cut out of hundreds and there is no reason for it to be the first
thing you see; on a flat frame the same plane is the entire result and there is
nothing else to show. So the two open differently — planes off and the cloud on
for a volume, `Slice Z` on and everything else off for a single plane — and the
choice is made once per kind of result rather than per frame, so switching a
plane back on by hand sticks until a result of the other kind is loaded.

**The Cloud: the whole volume at once, painted see-through.** The layers above
it each answer one question and hide the rest of the field to do it. A slice is
one plane out of hundreds. An isosurface is one *number* out of the field — a
skin drawn exactly where the value equals the level, so a shock arrives as a
crumpled sheet with holes wherever the level falls between two cells, and the
way to find out whether it is right is to drag a slider until it looks like
something. Neither is a picture of the flow.

`Cloud` draws the flow. Every cell becomes a little coloured block you can see
through, and how solid it is comes from how far that cell's value sits from the
value that fills the rest of the box. What is left is the shock cone, the wake
and the vortices standing in clear air, which is what a schlieren photograph of
the same flow looks like and is why that is the comparison to make.

Three things make it cheap enough to turn on. The **background is found, not
assumed**: the field is histogrammed and the fullest bin is taken as the still
air, because in a box with one body in it the undisturbed reading is by a wide
margin the most common one — so the same layer works for pressure in a shock
run and for a passive scalar in a plume without a number being set. Cells at
that value are not drawn at all, which for a supersonic flyby is about
ninety-six per cent of them. And what survives is kept as **cells rather than
triangles**: translucent things have to be drawn far to near or the near ones
blend into a background that has not been laid down yet, and which way that is
changes every time the camera crosses onto another axis; re-sampling the field
at that moment would cost a second, re-sorting cells that are already chosen
costs a millisecond. The sort is by counting, not comparison, because the key
is already a cell index. The camera stays smooth while it turns.

Two details that are only visible when they are missing. Seen from a corner a
line of sight crosses more of each cell than it does head on, so the same cloud
ought to look denser from there; it is corrected for, and without the
correction the picture visibly fades every time you rotate away from an axis
and brightens as you come back. And the budget is spent from the top: when more
cells deserve drawing than the two hundred thousand the layer allows, it is the
faintest that go, not the last ones the loop happened to reach.

Its slider sets how solid, from a tenth to eight on a log scale — `[` and `]`
do the same from the keyboard, `C` toggles the layer. Cells inside the body are
never painted; that is the body's job and it is drawn opaque underneath. The
strength is remembered between sessions.

**The cloud, the isosurface and the vortices are three independent ticks.**
They are three answers to the same question — what does the inside of this
volume look like — and drawing two of them through each other can be hard to
read, which is a reason to have a cloud slider and not a reason to forbid it.
Any of them, all of them, none. Each one that is on gets its own slider under
the bar and gives the room back when it goes off. `C`, `I` and `Q` toggle one
each from the keyboard.

**Density is one press away.** A compressible run writes density into every
frame — not as an `extraFields` extra, as one of the two scalars the writer
puts down before anything else — and until now the window had no route to it
that did not involve clicking `Colour` eight times. It is an entry in the
`Colour` list now, named, with a sentence saying why you want it; `D` goes
straight there from the keyboard, and a compressible volume *opens* coloured by
density. Speed shows a shock as a smear and density shows it as an edge; that
is the whole reason to want it. The flat view reaches it from its own `Field`
button, which labels itself with the name it is showing.

`Colour` is a list rather than a cycle, so the frame's own named fields are
visible rather than eight presses away: `off`, pressure, speed, then whatever
the run actually wrote, then u, v, w, vorticity and Q. The last five are
derived from the velocity vector, which is always there, so they were never the
ones that were hard to find. Whatever is picked colours **everything that is
drawn** — the cloud, the isosurface, the vortex surface and every slice — so a
cloud of Q is a cloud of vortices and an isosurface of density is a shock.

**The setup panel says how much disk the run will take** - `VTK ~45 | ~4.5 GB`
beside the frame count, worked out from the grid, the regime and the extra
fields. It is an estimate and it says so, and it is the difference between
finding out that forty frames is nine gigabytes before the run or after it.

**Runs have names.** `runName=` on the command line, a `Run name` row in the
OUTPUT group of the window. The frames go into a folder of that name instead of
`run-1758...`, the tray and the taskbar say it while the run is on, and a
second run of the same name gets a 2 after it rather than writing over the
first. Empty still gives the timestamp.

**A continuation goes into the folder it continues.** It used to get a new one,
which split a series that belongs together across two directories: it had to be
opened twice and could not be played through. The solver names a continued
frame `solution_<from>_<step>.vtk`, so nothing collides with what is already
there.

**Stopping a run asks rather than kills.** A file called `stop` in the output
folder means "finish this step, write the frame, come back" - the same clean
stop as Ctrl+C, and the only one available to a run with no console of its own.
That is what the window's Stop button writes now; pressing it a second time
kills, as it always did. From a shell it is one command:

    touch output/<run>/stop

**How far along, on the console.** Every step line the solver prints now ends
with where the run has got to - `0.0003221 / 0.0006 s (53%)` - so a wall of
step numbers tells you whether this finishes in a minute or an hour. It was
already in the tray tooltip and the taskbar; now it is in all three.

**What the compressible core is running on, in one line, at the top of every
run.** `Compressible core: GPU (CUDA).`, or `CPU, 8 threads - this build has no
CUDA in it`, and when the build does have it but the run is not using it, the
reason: no driver, `useCuda=0`, `amrLevels`, `gridStretch`. The acceleration
block above it says what the build can do; this says what this run does, which
is the question actually being asked when the task manager shows a cold card
and eight busy cores.

**The hover readout shows everything the frame holds**, not a chosen four of
it. Pressure, u, v, w, speed, and every named field the run wrote - density on
a compressible run, and whatever `extraFields` added - in the order the solver
wrote them. In the flat view and in the 3D one.

**It is clear which cell the cursor is on in 3D.** A ray that missed the body
used to report the cell it entered the box through - a cell on the far wall,
behind everything - which is why the readout looked like it was picking at
random. It stops on the first thing that is actually drawn now: the body, a
cloud block, or where it crosses a slice plane, and it says which of the three
it was. The cell is outlined in white in the picture, grown to a visible size
when the cells are smaller than a pixel. **The readout itself moved off the
bottom line** and into a small box beside the cursor, because the bottom line
is also where the triangle count lives and where a warning about the run
appears, and three things wanting one line means you read whichever won.

**The row of thirty buttons is eight lists.** `Box` `Grid` `Solid` `Wire`
`Slice X` `Slice Y` `Slice Z` `Cloud` `Iso` `Vortices` `Streams` `Tracers`
`Density` `Pressure` `Velocity` `Colour` `Ortho` `Rotate` `Move` `Frame all`
`Front` `Back` `Left` `Right` `Top` `Bottom` `Range` `Continue` `Details`
`Recover`, in a strip across the top of the picture, wrapping onto a second row
on a narrow window, in the order they happened to be written. Nothing in that
strip says which of them belong together, and a button five letters wide has
room for `Iso` and no room whatever for what `Iso` is.

They are now **Layers**, **Slices**, **Flow**, **Show**, **Colour**,
**Camera**, **Range** and **Run**, each of which opens a list under itself. The
button says what is on - `Layers: cloud+vortices`, `Colour: density` - so the
state is readable without opening anything, and a list has the room a button
never had.

**Every entry in every list carries a sentence**, shown in a box beside the
list while the cursor rests on that entry, and the same on the buttons
themselves. Including the one that had to exist: *the Q criterion is rotation
squared minus shear squared, positive where the flow turns faster than it is
being sheared, which is the usual definition of a vortex and is what the
vortices layer draws.* That is a thing you can now find out in the program, at
the moment you are wondering, rather than in a document you are not reading.

**The volume layers are three switches, not a three-way choice.** Cloud,
isosurface and vortices used to be one setting with three values, because a
cloud and a skin drawn through each other can be hard to read. They can be -
and a faint cloud around a vortex tube is also exactly the picture wanted
sometimes, and deciding that for somebody by making it impossible is not a
kindness. All three can be on together, each gets its own slider under the bar
while it is on and gives the room back when it is off, and the keys agree: `C`,
`I` and `Q` each toggle one layer and leave the others alone.

**Vortices are a field like any other.** There is no separate vortex colour, no
separate vortex range, no separate anything: `Colour` sets one field for
everything that is drawn, so a cloud of Q is a cloud of vortices, an isosurface
of Q is the same surface the vortices layer draws, and an isosurface of density
is a shock. `Density`, `Pressure` and `Velocity` are gone as buttons of their
own - they were three doors into a list that has ten entries and now shows all
ten, `off` among them.

**Ticks are drawn as ticks.** `[x]` and `[ ]` down the left of the list, and a
list whose entries are independent stays open while you set several of them,
instead of closing after each one and making you find the button again.

**`What?` pins the whole lot open.** One sentence at a time on hover is no use
when you do not know which button to hover over. The button opens every
explanation at once, grouped the way the picture is built up: what fills the
box, what else is in it, colour, the camera, this run.

**The update check moved into the window.** It runs once at startup on a thread
of its own and puts an `Update: 1.1` button in the top bar when there is
something newer; pressing it opens the release page. It does not replace the
running program behind your back - a portable build is a folder you chose where
to put, possibly on a read-only share, and a window that overwrites itself in
the middle of a two hour solve is not a feature.

**The Results page is the picture and nothing else.** The right-hand column of
input parameters is a Setup thing: on Results it is three hundred pixels of
numbers describing a run that has already happened, that nothing on the page
can change, taken off the only thing on the page anyone is looking at. It is
not drawn there, and the viewport takes the width. Switching pages relays out
the window, so the picture is the right size the moment you arrive rather than
at the next resize.

**The setup preview has a camera now.** It used to be drawn from a fixed angle
— three quarters on and a little above, which is a fine angle for a first
glance and no use at all for *is the nose really pointing the way it flies*,
which is the one question that picture exists to answer, and the one that costs
twenty minutes of solver time to get wrong. Middle drag turns it, `Shift` and
middle drag slides it, the wheel zooms, `Home` puts it back. Left and right
drag are untouched and still turn the **model**: the camera changes what you
see and the model changes what gets simulated, and both are wanted about
equally often. A muted line along the bottom of the preview says so, because a
camera nobody knows about is the same as no camera.

**`Continue run` opens the Setup page at that frame.** It used to take whatever
the `Continue: add time` row happened to hold, which is zero unless you went
looking for that row - so it ran to `Total time`, which the run had usually all
but reached, and gave you two more frames and a shrug. And typing the extra
time into Setup and pressing Generate instead started the whole thing over from
zero, because that is what Generate does. Neither is obviously wrong until it
has eaten an afternoon.

It now does the thing that was wanted in the first place: the run's own
settings go onto the Setup page, with the frame on screen as the starting
point, and everything on that page is yours to change before it runs - how far
it goes, whether a wall is open, one more microphone. The run button reads
**Continue run** while that is armed, with **Start from zero** next to it for
the other choice; whichever you press is what happens, and nothing is carried
on behind your back.

**The top bar is in groups.** `Setup` `Results` | `Open frames` `Import STL /
OBJ` | `Show output folder` `Output folder` | `Select solver` `Keys` |
`Stop simulation`, with a rule between them, so which buttons belong together
is something you can see. Stop stands apart from the rest at the end, because
it is the one button up there that interrupts work.

**Every key the window answers to, on one screen.** The `Keys` button, or `F1`,
puts the lot up: the ones that work anywhere, the setup preview's, the result
page's, the 3D viewport's and the flat view's, each under its own heading. A
shortcut nobody can find is a shortcut nobody has.

**`Shift+F5` stops the run**, from anywhere in the window, which is the thing
you most want to be able to do without hunting for a button - and is
deliberately not a key that can be hit by accident while typing a number into
the panel.

**Pressure and Velocity are gone from the flat view's bar.** They were two more
ways to reach two of the entries already in the `Shows` list, with nothing on
the bar to say that that is what they were.

**Both windows say what they are.** The UI's title bar read `CFD Mask UI 1.0.0`
- the name of the build folder, not the name of the program - and the solver's
console kept whatever title Windows gave it, which for a double-clicked console
program is the full path of the executable. They are `Fluid Solver UI` and
`Fluid Solver` now, in the title bar, on the taskbar button and in the Task
Manager.

**The mask warning says what differs.** `MASK MISMATCH: Fluid Solver VTK
differs from GUI preview. SOLVER REPORTED STDERR.` told you a comparison had
failed and nothing about which, by how much, or whether it mattered - and it
appeared after **every volume run**, because the preview mask is one plane and
a volume run voxelises the model in three dimensions, so the two could never
match. The check belongs to plane runs, where the two are the same thing and it
is exact; it now says how many cells differ and what share of the grid that is,
or that the grid itself is a different size; and the solver's error output is
quoted rather than announced.

**Opening a frame, and looking at it, got faster.** Measured on a 128^3
compressible frame - 2.1 million cells, 44 MB - rather than guessed at: reading
the file is 5.6 ms and byte-swapping all of it is 1.6, so the ninety-odd
milliseconds that remained were arithmetic. Most of it was the trimmed colour
range, two `nth_element` passes over a copy of each field, about 28 ms a field
and paid for pressure, for speed and for every named scalar. It is a histogram
now - min and max, then one increment per value into 4096 bins, then a walk
over the bins - which places the quantile to one part in four thousand of the
range, which is exactly as good at ending a colour scale, with no second copy
and every core working. 93 ms to 73 on two cores.

And the 3D view samples a field **once** rather than once per layer. Its colour
is one setting for everything drawn, so with the cloud, an isosurface, the
vortices and a slice all on and all coloured by Q, five passes were computing
the same velocity gradient tensor per cell. The viewport keeps the three most
recently sampled fields and hands the same one to everybody. Q itself, the
dearest field there is, now runs across cores as well: 80 ms to 55 on two.
Everything on at once, coloured by Q, went from 195 ms to 147 - while sampling
Q on its own is 55, which is the whole point.

A run continued that way still goes into the folder it continues, so the frames
stay one series.

**The 2D colour controls are gone while the 3D view is up.** `Shows`,
`Vectors` and `Range` colour the flat view; the 3D view colours itself from its
own `Colour` button. Leaving both on screen offered two ways to choose the same
thing, one of which did nothing.

**Navigating it is Blender's, because that is the one everybody already
knows.** Left drag turns the view, middle drag slides it, the wheel zooms.
`Rotate` and `Move` decide which of the first two the left button does, so a
laptop trackpad with no middle button can still slide the view, and holding
`Shift` slides it whatever the buttons say. Numpad 1/3/7 lock to front, right
and top, `Ctrl` with them gives back, left and bottom, and the six named
buttons do the same for a mouse. Numpad 5 is isometric — parallel projection,
no perspective, which is the one to be in when you are reading a shock angle
off the screen. Numpad 4/6/8/2 turn the view in 15 degree steps, numpad 9 flips
to the opposite side, `F` frames the whole volume.

**`G` and `R` transform the selected body from inside the viewport**, the same
three keystrokes as everywhere else: `G`, then `x`, then `0.25`, then Enter,
and that body is a quarter of a metre further along x. `R` is the same in
degrees, and `c` is a fourth axis for the rotation inside the cut plane —
which is what `R` starts on when the result is a single plane, because a plane
has no out-of-plane axis to tip into. It is modal on purpose: while it runs
every key belongs to it, so typing `5` means five rather than "numpad 5 flips
the projection", the prompt sits in the status line until it ends, and Escape
cancels. What it changes is the **setup**, not the picture: the frame on screen
was computed with the body where it was, so the body does not jump — the
`Position` and `Tilt` rows move, and the next run is the one that differs.

**Keyboard, because the window used to answer to the mouse and nothing else:**
`V` to swap views, `F` to frame, numpad 1/3/7 for front/right/top with `Ctrl`
for the other three, 5 for isometric, 4/6/8/2 to turn in steps, 9 to flip, `C`
for the cloud, `I` for the isosurface and `Q` for the vortices - one layer each,
the other two left alone - `[` and `]` for how solid the cloud is,
`D` for density, `G` and `R` to move and
turn the selected body by a typed amount, arrows and
`Home`/`End`/`Space` through the frame series, and in the setup view `Home` to
put the preview camera back,
`Ctrl+Z`/`Ctrl+Y` (one undo stack over the whole setup state, not one per
widget), `Ctrl+C`/`Ctrl+X`/`Ctrl+V` on rows or on the whole configuration,
`Ctrl+F` to filter the panel, and `Ctrl+S`/`Ctrl+O` for `.cfdui` files.

---

## Fixes

Bugs that were live in 0.2 and are not now. Several of these were shipping
silently, which is the only kind worth a section.

**The window showed no percentage at all on a compressible run.** It read the
simulated time out of the solver's own output by looking for `", t = "` - with
the comma, which is how the projection solver prints it. The compressible one
prints `"step   1230  t = ..."`, no comma, so the match never fired, the
progress bar stayed empty and the tray tooltip said only that something was
running. It looks for `"t = "` now and both solvers match.

**The window froze while a run was on.** The solver takes every core it is
given and holds them for minutes; at equal priority Windows then hands the
window a slice only when one of those threads happens to yield, and sixty
frames a second becomes about three. The solver is started below normal
priority now, which costs the run about a percent on an otherwise idle machine
and gives the window back. Stopping a run no longer blocks the window either -
it used to sit in a four second wait for the process to die, which is four
seconds of a frozen window and not long enough to finish writing a two hundred
megabyte frame.

**The vortex threshold was a share of the largest Q in the box, and the largest
Q is in one cell against the body.** On a real flyby frame the peak reads
3.3e7 while the wake is around 1e5, so the default setting of "fifteen per cent
of the peak" asked for a surface at 5e6 and there was nothing there to draw:
the vortices layer appeared to do nothing, and the slider did nothing until
the last hair of its travel. It is a share of the cells that are rotating at
all now, read as strictness - right for the strongest cores, left for every
swirl - and it moves the picture along its whole length. On the frame above:
8816 triangles at the loose end, 1760 at the strict one, where before there
were none at either.

**A compressible frame wrote its state down twice.** It carried density, the
velocity vector and pressure - and then the five conserved variables as well,
which are those same three rearranged. Twenty of the forty-one bytes a cell
cost were the second copy. They are not written any more: a restart rebuilds
them on the way in, from the fields the frame was showing all along. A frame of
a five million cell run went from 201 MB to 103 MB, and so did the time to read
one back. Checked by continuing the same run from a slim frame and from a full
one and comparing sixty-seven steps later: 1e-6 relative, which is float
round-off.

`frameState` says how much a frame writes down, and the rule behind its three
values is that **the default never gives up a guarantee**:

| | drops | compressible | incompressible |
|---|---|---|---|
| `slim` (default) | only what comes back exactly | −49% | 0% |
| `minimal` | also what needs a projection | −49% | −16% |
| `full` | nothing | 0% | 0% |

The two regimes differ because their spare data differs. A compressible frame's
conserved variables are an algebraic identity away from what it already shows,
so dropping them is free. An incompressible frame's packed face velocities are
not a second copy of anything - they are the only copy, and a frame without
them can be continued only by rebuilding the faces from the cell averages and
projecting once, which moves the state. Dropping them by default was tried,
measured at 16.4% of the file, and taken back out when the restart test refused
it; `minimal` is there for a run being written to be looked at rather than
continued from, which is most of them.

**What was measured and not done.** A frame is 21 bytes a cell for a
compressible run - pressure 4, density 4, mask 1, velocity 12 - and after the
above there is nothing in it that is not load-bearing. Everything further costs
something:

* gzip over the whole file gets 25% (9.07 MB to 6.83 MB on a real frame).
  Float mantissas are close to random and do not compress; 25% is not worth
  a file ParaView cannot open.
* Byte-shuffling the floats first, the way HDF5 and blosc do, gets much more -
  42% on the scalars, 74% on the velocity, about 41% overall - but no VTK
  format has it, and the one ParaView does read compressed, XML `.vti`, offers
  only plain zlib, which is the 25% again. A writer and a reader in two
  codebases for 25% is not a trade worth making.
* 16-bit floats halve the field data and VTK has no such type; for a pressure
  of 101325 Pa carrying fluctuations of tens of Pa they are useless anyway.
* Bit-packing the 0/1 solid mask saves 4% and costs both ParaView and the
  window the ability to read it.

**The whole state came back off the graphics card every step of any run with a
microphone in it.** A hundred and twenty megabytes over the bus, per step, to
read four cells - and it ignored `micInterval` entirely, so asking for one
sample in eight cost exactly as much as asking for all of them. It follows the
interval now. This is most of what a CUDA run was waiting on: the card sat at a
quarter load while the processor pulled the state over and ground through the
acoustics.

**The mask of a moving body was rebuilt on one core.** Three full sweeps of the
volume every step - the mask itself, the surface velocity of every solid cell,
and the refill of the cells the body has just uncovered - all serial while the
rest of the solver was on every core the machine had. They are parallel now.

**The compressible frame writer swapped bytes one float at a time.** VTK is
big-endian and x86 is not, so every float in a frame is a byte swap away from
the one in memory; the projection solver has done eight at a time since it
learned about AVX2 and this one, the one that writes the big frames, was still
doing four shifts and a memcpy per float into a 16 KB buffer. One shuffle does
eight now, into a one megabyte buffer, and the bytes that come out are
identical - checked against the old writer on a 21 MB frame.

**Every frame of every compressible run was called uncontinuable.** A frame
carries the state a run can be picked up from, and the window decides whether
`Continue run` is available by looking for it. It looked for `uFace`, `vFace`
and `pRaw` — the face velocities and raw pressure an *incompressible* run
leaves behind — and a compressible run leaves the conserved variables instead,
`stateRho` and the four with it. So the check failed on every compressible
frame, the button was greyed out, and each frame carried "RestartData state
arrays do not match the visible grid" as a warning. The solver's own reader
restarts from those arrays perfectly well and always has; only the window did
not know what it was looking at. It knows now, and the arrays are still
size-checked against the grid, so a truncated block is still refused.

**The setup preview showed a cut that a volume run never makes.** It draws the
model, a green quad for the section plane and an orange line where the two
meet, which is the whole story at `nz = 1`. Above that the solver voxelises
the model whole and cuts nothing, so the plane and the line were a picture of
something that does not happen - and the first question they raise is where
the body went. They are drawn only for a plane run now; a volume gets the
model, the slice angles still turning it, and a legend that says so.

**A run with a moving body could not be opened at all.** The frame series is
checked for consistency before it is shown — same grid, same spacing, same
data association — and the check also required the solid mask to be identical
in every frame. It was written when bodies never moved. `bodyMotion` rewrites
that mask on every step by design, so the first series with a body travelling
through it was refused whole, with "VTK frame series changes association,
grid, or solid mask" naming the one thing about it that was supposed to
change. The mask is data now, not layout; only its size still has to match the
grid. A test flies a body across three frames and fails if any of them is
refused.

**The 3D viewport killed the process on some machines and not others.** It
draws through client-side vertex arrays, where the last argument of
`glVertexPointer` is a pointer into this program's memory - unless a vertex
buffer object happens to be bound, in which case the same argument silently
becomes an OFFSET into that buffer. SFML draws through buffer objects, so
whether one was still bound when the raw GL ran after it was never this code's
to assume.

Read as an offset, the address of a `std::vector` is about two terabytes into
a buffer a few kilobytes long. The driver answered that by faulting inside the
vertex-fetch code it generates at run time, which belongs to no module: the
crash report named neither this program nor even a driver function, just an
address in the heap and a stack that was `nvoglv64.dll` from top to bottom. On
a machine whose GL does not use a buffer at that point it never happened at
all, which is why it looked like a graphics-driver bug rather than ours.

Every batch is copied into a buffer object the driver owns now rather than
named by a pointer into this program's memory, which closes that door for
good, and the client-array path remains only for a GL too old to have buffer
objects.

But the pointer this program passed was never the one that killed it. SFML's
own state reset enables `GL_TEXTURE_COORD_ARRAY` and points it at the vertex
data of whatever it drew last, and an array left enabled is still fetched:
every `glDrawArrays` after that read one texture coordinate per vertex from an
address that had belonged to a temporary several frames ago. The viewport
enables the two arrays it fills and had never thought to disable the four it
does not. Where the dead pointer still happens to be mapped - a software GL,
another machine, a different frame - nothing happens at all, which is why this
survived every test that was not run on the hardware it fails on.

It was found by making the failure describe itself: `FLUID_UI_GL_TRACE=1` puts
a `glFinish` after every drawing step and writes the step's name down first, so
the driver stops being allowed to defer the crash to a buffer swap where
nothing of ours is on the stack, and the last line of `gl-trace.txt` is the
call that did not survive. Two lines in, it was the first real draw of the
frame - not any particular layer, which is what said the fault was in the state
around the draw rather than in anything being drawn.

**A 3D viewport drawn with no depth buffer.** The window was created without
`sf::ContextSettings`, and SFML asks for zero depth bits unless it is told
otherwise. `Viewport3D` checks `depthBits > 0` and falls back to
`glDisable(GL_DEPTH_TEST)` when there are none, so every triangle was painted
in the order it happened to be submitted and nothing was ever behind anything.
A slice plane sat in front of the body or behind it depending on nothing but
that order, and swung through it as the camera turned — which reads as the
plane spinning on its own. The window asks for 24 bits now. The fallback stays
where it is: a machine that really cannot give a depth buffer still gets a
picture rather than a black rectangle.

**Every Windows OpenMP row refused to compile.** `collapse(2)` wants its loop
bounds provably invariant, and MSVC will not take a class member for one — `nz`
and `ny` reached the loop headers through `this` in `Phase`, `Multigrid`,
`Turbulence`, `Mesh` and `SolverCompressible`. Seven of the fourteen Windows
rows died on it, some with `C7720` and some with an internal compiler error at
the same line, which is the same cause wearing a different hat. All 56
`collapse` sites hoist their bounds into a local `const int` now, which is what
`Solver.cpp` was already doing in seventeen of its eighteen. GCC and Clang
never minded, so nothing about the numbers changed: same fourteen suites, same
results.

**Every Windows OpenMP row that did compile shipped a runtime it could not
use.** The release script searched the Visual Studio tree for
`libomp140.<arch>.dll` and packed whichever copy it reached first. Each
installed toolset keeps its own, a 2019 one turns up before a 2022 one, and a
collapsed loop calls `__kmpc_calc_original_ivs_rectang`, which the older copies
do not export. It linked, it zipped, and it died on the user's machine with
"entry point not found" before printing a character. The newest copy wins now,
and it is read and checked for that entry point before it goes into an archive:
if no copy on the machine has it, the build says so in yellow and lists it as a
problem instead of publishing a binary that cannot start.

**Frames written into a folder named after the bytes of the one that was
asked for.** A narrow `main()` on Windows is handed its arguments already
squeezed through the ANSI code page, and the path conversion then tried the
console page, the ANSI page and UTF-8 in turn and took the first spelling that
existed on disk. Given an output directory under `C:\Users\...\Файлы` it
picked the wrong one, `create_directories` obligingly made it, and a run wrote
every frame and every `.wav` into `C:\Users\...\╘рщы√\...` while the UI looked
in the folder it had asked for, found nothing, and reported that the solver
had produced no frames. The solver meanwhile said it had saved them, and it
had — somewhere else.

The wide command line is what Windows actually holds, so it is read directly
with `GetCommandLineW` and handed on as UTF-8, and the conversion is told to
stop guessing. Nothing is inferred from a code page any more. The guessing
loop stays for paths that come out of a file rather than off the command line,
where there is no wide original to consult.

While in there, the compressible solver resolves its output directory through
`resolveOutputDir` like the projection solver always did, so an install folder
a standard user cannot write to says so and names where the frames went
instead of failing once per frame.

**A Linux CUDA build that segfaulted before printing a character.** Not new at
0.2 — it reproduced on every CUDA Linux binary in the tree, on `--hardware`,
before any output. `-static-libstdc++` does not name an archive; it turns the
driver's `-lstdc++` static and lets `-L` decide which one. CMake puts nvcc's
host compiler directory on the link line, nvcc pins an older GCC than the one
compiling C++, and the archive that comes back is from the wrong GCC release.
It links clean, `std::ios_base::Init` never runs, `std::cout` is never
constructed, and the first `<<` walks into a null streambuf. The fix is four
lines: ask the compiler where its own `libstdc++.a` lives and put that
directory first.

**CUDA aborted the process when a toolkit was installed with no device behind
it.** `cudaMalloc` calls `abort()` in that case and `useCuda` defaults to 1.
`cudaGetDeviceCount` is asked first now and the run falls back to the CPU with
a line saying so.

**A NaN was invisible to the timestep reduction.** `maxps` returns its second
operand when either input is a NaN and `std::max` drops it the same way, so a
field that had gone to NaN left `dt` falling back to a hardcoded `1e-6` and the
run ground on writing frames of NaN until the every-tenth-step check happened
to look. There is a `_CMP_UNORD_Q` accumulator beside the max now, and the run
stops on the spot and says so. The `1e-6` fallback was never an answer and is
gone: a `dt` that comes out zero or infinite means the grid or the viscosity
are outside what a float can hold, and that stops the run and prints `dx`, `dy`
and `nu`.

**The buried face inside a body carried the wall velocity, so every body had
half the drag it should have.** The wall is the cell edge halfway between the
buried face and the fluid face, so the stencil saw `(u_fluid + u_wall)/2` at the
wall instead of `u_wall`. Buried faces hold the mirrored value
`2*u_wall - u_fluid` now, with the wall velocity taken at the wall's own
position. A wall sliding at 1 m/s drags the fluid touching it to 0.247 m/s
instead of 0.156.

**Semi-coarsening compared with `<` where it meant `<=`.** A ratio of exactly
two — `Lx=2, Ly=1` on a square cell count, which is the most common setup there
is — was treated as isotropic and carried the anisotropy down the whole
hierarchy. One level deeper now, and 6-8× lower residual on those grids:
64×64 goes 7.3e-5 → 1.3e-5, 128×128 goes 0.020 → 0.0025. Every other grid comes
out bit-identical.

**The residual on cycle 0 was computed and thrown away on every single solve.**
The tolerance check was guarded by `cycle > 0` and the final residual is
recomputed anyway. One sweep in three gone at `mgIterations=2`, on the CUDA
path too.

**The multigrid tolerance was measured against the wrong right-hand side.** The
rhs norm was taken before the boundary term was added, so the solve stopped
about 8% short of where it was asked to.

**Solid cells wrote zero pressure in body-gravity mode**, punching a visible
hole through the pressure map. They are written with the hydrostatic value they
would have had. Nothing ever reads them back; this is purely what ParaView
sees. Solid cell *velocities* get the same treatment for the same reason — a
cell centre value is the average of its two faces, and inside a body those
faces hold the mirrored value the no-slip condition needs.

**`serialize()` wrote `bcLeftSpeed=0` into every frame**, and reading that back
set the flag meaning "zero was asked for", so an inlet at `U0` came out stopped
on the first continuation. Only sides someone actually named are written now.
The golden master caught this one; nothing else would have.

**Restarting from a folder broke ties on the file timestamp alone**, and a run
short enough to write its whole output inside one second gives every frame the
same one — it picked `solution_20` over `solution_22`. The step in the name
breaks the tie now.

**Every failure path in `unpackFaceVelocities` now empties its outputs.**
`loadRestart` decides whether a frame is exact by the size of `u` and `v`, so a
truncated block used to come back full-sized, mostly zero, and flagged as an
exact restart while printing that it had fallen back.

**A run that stopped no longer reports that it finished.**

**Mass was being created at the domain walls of every compressible run**, found
while chasing conservation for the refinement work. The solid-face wall flux
had been replaced with an explicit one, and the *domain* walls were left
solving the same broken Riemann problem. They are explicit now too: a closed
shock tube bouncing off all four walls for two milliseconds conserves mass to
**7e-6**, where it used not to.

**The curvature was gated on the wrong quantity.** `computeCurvature` required
`c` strictly between 0 and 1 — true for an interface passing through a cell,
and never true for a sharp block or a painted field, so the entire surface
tension force was silently zero and nothing said so. Gating on `|grad c|`
instead took the Laplace error from 2.5% to 0.18% and cut the spurious current
by about **350×** on the 1000:1 case.

**Keyframed velocities were sampled at the start of the step**, which is a left
Riemann sum and loses exactly half a step off every ramp — 0.1445 m instead of
0.1500. Sampled at the middle now.

**HLLC returned its momentum fluxes in the Riemann problem's frame** (normal,
tangential) while the state vector is in x and y. For a horizontal face those
are not the same order. An empty domain at Mach 2.5 was perfect, because in a
uniform flow every horizontal face has the same flux and the swap cancels
exactly; put a cube in it at Mach 0.5 and it detonated in 105 steps.

**The second solid layer behind a compressible boundary was never filled.** The
reconstruction reaches two cells; one was filled and the other held `rho=1,
rhoE=1`, i.e. 0.4 Pa next to 101325 Pa.

**`advanceStage` imposed the domain's boundaries on every patch**, so each
patch became a little closed box that rang and the shock tube blew up to the
pressure floor in twenty-five steps. `SideState` carries an `interior` flag
now, and a patch that *does* touch the domain edge still gets the real
boundary at the right place, because `BlockBoundaries` also carries where the
block sits in the domain — a banded inlet is a fraction of the domain, not of
the patch.

**The CUDA backend reallocated its whole hierarchy every step** for a moving
body — `setGeometry` freed and re-`malloc`'d every level and wiped the pressure
field, which is fine once at setup and is tens of `cudaMalloc` pairs per step
for a body that moves, and it threw away the one good starting guess the solve
had. There is a keep-the-solution path now.

**`CFD_RELEASE_VERSION` is a cache variable**, so an existing build directory
kept printing the old number after a version bump. The build was fine; the
binary was lying. This is not fixed, it is written down — use a fresh build
directory or clear the cache.

---

## If you have old files or old scripts

Read this section. Most of it is small, and all of it is the kind of small that
costs an afternoon.

**Frames. The header changed, and the format version is now 3.**

- The third `DIMENSIONS` token is a real `nz+1`. A flat run wrote `1` at 0.2
  and writes `2` now, because `nz = 1` is one cell deep and one cell needs two
  planes of points. The cell count is `nx*ny` either way and ParaView does not
  care, but **anything that parsed that token by hand needs a look** — a script
  that tested `== 1` to decide a frame was 2D will now decide it is 3D.
- The third `SPACING` token is a real `dz = Lz/nz` where a flat frame used to
  write the literal `1`. At the default `Lz = 1.0` with `nz = 1` it still
  prints 1, so most old scripts survive; set `Lz` on a plane run and it will
  not.
- `VECTORS velocity`'s third component is a real `w` rather than a column of
  zeroes. It was always taking up the space; it now means something.
- `SCALARS solid` is `unsigned_char`, not `int32` — that is one byte a cell
  where 0.2 wrote four.
- `uFace`, `vFace` and `pRaw` are gone. Face velocities live in a packed
  `facePack` block, and `pRaw` was the pressure array times `ro`, stored twice.
- `bodyState` is a **key/value record** now:
  `bodyState=1:x=..,y=..,z=..,qw=..,qx=..,qy=..,qz=..,vx=..,vy=..,vz=..,omegaX=..,omegaY=..,omega=..;`
  A quaternion, a third position and three more velocity components do not fit
  in seven numbers, and neither will the next thing a body grows. **The old
  seven-number form `1:x,y,theta,vx,vy,omega` is still read**, and `theta` is
  turned into a rotation about z, so a frame written by an earlier build
  continues exactly as it used to.
- `vorticity` in `extraFields` is a **vector** at `nz > 1` and a scalar at
  `nz = 1`. Anything that reads frames has to cope with both.

**Reading old frames works. Reading new frames with an old build does not.**
A 1.0 build reads every frame at or below version 3: a version 1 or 2 frame
comes back as a volume one cell deep with `w` zero, a version-2 packed block
still unpacks, and old `bodyState` still parses. The reverse has never held and
still does not — a 0.2 build takes four bytes a cell for `solid`,
desynchronises inside the file, and cannot be rescued after the fact, because
the configuration text that carries the version sits at the *end* of the frame.
The version number exists so a reader can name the problem rather than report
whatever binary garbage it lands on. **Keep the 0.2 binary if you need to open
0.2-era output with 0.2-era tooling; do not expect 0.2 to open a 1.0 frame.**

**The golden master is about values, not bytes.** "88 frames at `0.000e+00`
against 0.2" means the fields compare identically. The files do not, for every
reason in the list above.

**Config keys: 27 became 115.** Nothing was removed and nothing was renamed, so
every 0.2 command line is still a valid 1.0 command line and means the same
thing. What changed is what happens when it is *not* valid.

**The command line is strict now.** At 0.2 a bad value was accepted as whatever
`atof` made of it, and a bad key stopped at the first one. Now every value is
checked by type, every bad argument on the line is reported in one go, and the
run does not start:

```text
2 arguments are wrong:
  not a right way to write nu=0,002: the decimal separator is a dot, write nu=0.002
  not a right way to write useCuda=yeah: useCuda is a switch, write useCuda=1 or useCuda=0 (true/false, yes/no and on/off work too)

Nothing has been started. Fix the line and run it again.
```

A misspelled key gets the nearest real one — `saveinterwal=5` answers *did you
mean saveInterval?* Values that are legal but suspicious are kept and warned
about instead: `CFL` above 1, `dtSafety` above 1, `omega` within 0.05 of 2,
`mgTolerance` above 0.1, `nu=0`. **A batch script that was quietly passing
`nu=0,002` and getting `nu=0` out of `strtof` has been wrong the whole time and
will now stop instead.** The exit code is 0 on a finished run and 1 on anything
refused, so a sweep can be stopped on the first bad line.

**On MSVC the OpenMP build wants `/openmp:llvm`, and an OpenMP row ships
`libomp140.<arch>.dll` instead of `vcomp140.dll`.** The port put `collapse(2)`
on the (k, j) loops — a plane has exactly one `k`, so without the collapse a
plane run would hand the whole grid to one thread — and `collapse` is
**OpenMP 3.0**. MSVC's classic `/openmp` is 2.0 and rejects it outright, so
`CMakeLists.txt` now asks for `/openmp:llvm`, which wants **Visual Studio 2019
16.9 or newer**; an older one fails the `find_package` check and the build says
OpenMP was not found rather than dying halfway through a kernel.
`scripts/make-release.ps1` packs `libomp140.x86_64.dll` /
`libomp140.aarch64.dll` / `libomp140.i386.dll` beside the binary accordingly,
and still falls back to `vcomp140.dll` if that is what the toolchain produced.
**If you ship these binaries yourself, ship the new DLL**; a row with the wrong
runtime beside it does not start.

The rest of the OpenMP 2.0 discipline still holds and is still worth keeping:
signed `int` loop counters everywhere, and reductions limited to
`+ * - & ^ | && ||`. A `max` or `min` reduction is OpenMP 3.1, MSVC rejects it
with C7660, and where one is wanted the loop keeps a per-thread value and folds
it in a `critical` at the end.

**Release archives.** The matrix is the same shape — 34 solver rows, 30 `-ui`
rows, one installer per system. The names carry `1.0` instead of `0.2`, and the
Windows OpenMP rows carry the new DLL. `RELEASE-GUIDE.md` is unchanged and
still names the 0.2 files; it needs regenerating for the actual upload and has
not been.

**Source-level API, if you build against the headers.** These matter only if
you were tracking the branch between 0.2 and 1.0 — at 0.2 neither type existed,
so a 0.2-era caller cannot be broken by them:

- `Mesh::SolidObject::area` is now `Mesh::SolidObject::volume`, and the struct
  also carries `cz`, `baseCz` and a `double inertia[9]`.
- `Mesh::BodyPose::theta` is now the quaternion `qw, qx, qy, qz`, and the
  struct carries `z`.
- `Mesh` gained `z`, `dz`, `nz`, `objectId` and `objects`, plus
  `voxelize()`, `labelObjects()`, `prepareMotion()`, `setPose()`, `pose()` and
  `updateSolid()`. `initCircle` takes a `cz`, and `buildSection` takes the
  `Profile` it is cutting.
- `Solver` no longer holds a `const Mesh&` — the mask changes every step when
  bodies travel.
- `Config::setParam` takes an error string and an optional warning string.
- Everything except `main.cpp` moved into a `cfd_core` library target.

**The licence changed, and this is the one that is not technical.** 0.2 shipped
under **MIT**. 1.0 ships under a *License for Free Non-Commercial Use with Paid
Modifications Allowed*: free to use, copy, modify and distribute
non-commercially; commercial use of the Software or of a non-substantial
modification is prohibited; commercial use of a *Substantial Modification* is
allowed only as a separate add-on product that contains none of this source,
requires the free original to run, and says so in its documentation and its UI.
Read `LICENSE` — it is 60 lines and it is the whole of it. **Anything you did
with 0.2 under MIT is not automatically fine under this.** (The desktop UI's
own `LICENSE` is still MIT. That is either deliberate or an oversight, and
these notes do not know which.)

---

## Known limits, and what is not done

Written down because finding them yourself three hours in is worse.

**The section cut still keeps only its largest contour — at `nz = 1`, from a
model.** The numbering machinery is correct and works on any mask with several
bodies in it, and a continuation gets such a mask out of the frame. Getting
there from a *model* through the section cut does not: a model that cuts into
two shapes loses one, and a model with a hole gets the hole filled. **At
`nz > 1` this does not arise** — the voxeliser tests every cell centre against
the whole triangle soup, so two shapes come out as two bodies and a hole comes
out as a hole. The limitation is the section cut's, not the solver's.

**There is no flux correction at AMR coarse-fine boundaries.** With the patches
following the flow a closed box conserves mass to about 1e-5 to 1e-6; freeze
the grid and let a wave sit on a patch edge for hundreds of steps and it drifts
to about 1e-3. A Berger-Colella flux register was written and removed, because
it made the number worse and a gain sweep never improved it.

**Mass is not conserved to the last bit near a moving body in the compressible
solver.** The wall flux carries no mass in the grid frame, the swept volume is
handled by re-cutting the mask and reseeding, and the piston pressure term is a
linearisation — both right to first order in the wall Mach number. A body
moving at a hundredth of the speed of sound will not show it; a piston at
Mach 0.5 will.

**A compressible run with moving bodies costs a host round trip per step on a
GPU.** The mask, the body velocities and the reseeded cells live on the host.
The run says nothing about it and it is not wrong, only slower, and only when
bodies actually move.

**Things that refuse each other**, out loud and before the run rather than
quietly: `gridStretch` with `useCuda`; `amrLevels` with `gridStretch`;
`amrLevels` with `useCuda`; `amrLevels` with `regime=incompressible`;
turbulence, gravity, surface tension, sources and `caseType=cavity` with
`regime=compressible`; acoustics with `regime=incompressible`; `surfaceTension`
with `mixing=miscible`; `slip` with `rot`/`slide` on the same object.

**No wall-clock number exists here for a volume pressure solve.** See
[What a volume costs](#what-a-volume-costs).

**AVX2 measures no difference in the compressible sweep.** The remaining factor
is real and lives in a face loop written with intrinsics so that eight Riemann
problems run side by side. It has not been written.

**`smagorinsky` on a RANS-affordable grid does nothing** — -0.85 against the
laminar -0.83 on the backward-facing step. That is the model being used outside
what it is for, not a bug in the implementation, and a test asserting otherwise
would be asserting a lie.

**`kOmegaSST` on a coarse 2D grid is worth what it is worth.** Reattachment
lands at 8.6 step heights against Armaly's 6-7 for a fully turbulent step and
up to about 8 through the transitional range — the right answer for the wrong
end of the range.

**The compressible solver has no viscous term.** At the speeds it is for,
pressure is the force anyway, so a free body's force is the pressure integral
over its own faces and nothing else.

**Two-fluid interfaces are algebraic, not reconstructed.** No PLIC. A stirred
drop loses about 0.1% of its volume over three seconds, and it loses it quietly.

**A body one cell thick can only be driven along itself.** The tangential value
lives on the faces *inside* the body, and a one-cell-tall bar has buried `u`
faces but no buried `v` faces at all, so it drags in `x` and ignores `slideY`
and very nearly ignores `rot`. A single isolated cell has neither kind and
cannot drive anything. Both are found, numbered and reported like any other
object; they just have nothing to push with.

**The UI's 3D viewport has never been looked at on a display by anyone who
wrote it.** Everything in it that is not pure drawing is covered by suites that
need no window — the volume reader, the slicing, marching cubes, the streamline
tracer, the picking arithmetic, the body track, the configuration file. What
nobody in the validation environment has done is *look* at it. The orbit
feeling right, the isosurface being the shape you expected and the colours
being legible are not things a headless container can tell you. Windows and
macOS builds of the UI were also not performed there.

**The UI does not draw AMR patches at their own resolution.** A refined run
writes the ordinary base-grid frame with the fine levels averaged in, which is
the best answer the hierarchy has rather than a placeholder, and the full
hierarchy goes to ParaView as a `.vtm`.

**The `attach=1` setting in a `profiles` entry is undocumented.** It exists, it
suppresses the does-it-fit-the-domain check for a body that is meant to touch a
wall (the oblique-shock wedge uses it), and it is not in the README's list of
`profiles` settings.

---

## The 0.2 Future Work list, settled

0.2's *Future Work* was eleven bullets. Nine of them are in this release:

| 0.2 said | 1.0 |
|---|---|
| Adaptive mesh refinement (AMR) | in, compressible only, without flux correction |
| Turbulence models | in — Smagorinsky with van Driest damping, and k-omega SST |
| Compressible flow solver | in — a second solver, HLLC, two gases, acoustics |
| Cavity flow | in — `caseType=cavity`, against Ghia, in a square and in a cube |
| Add optional gravity for fluids | in — two formulations, and a tilt out of the plane |
| Moving objects ("NO idea how to do it for now") | in — prescribed, free, keyframed, colliding, thrusting, tumbling |
| Moving WALLS | in — three rotations, three slides, free-slip, per object |
| Flow start coordinates and width of the flow | in — rectangular inlet windows and three profiles |
| Multiphase (multiple liquids/gases) | in — VOF, surface tension, contact angle, miscible mixing |
| MAY add several other solvers and merge all of them into one | still on the list |
| MAY make a full on website | still on the list, still conditional on the one above |

The *Future Work* section in the README is a real list again, and none of it is
this solver: deformation and heat on finite elements, electrodynamics on finite
differences, plasma by finite volumes, and fabrics. Three of those four are a
different solver rather than another feature of this one, which is the honest
shape of it — a fluid on a fixed grid is one problem, and those are not that
problem.
