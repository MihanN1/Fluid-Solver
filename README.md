# CFD Mask UI Optimized

The solver it drives solves a volume now, and `nz = 1` is the plane it used to
solve. So does this. There is a real 3D viewport, the old 2D view is still here
and is a slice through that volume, and every row the third dimension added is
on the panel — hidden while `nz = 1`, because a plane run should look exactly
like it always did and not like a 3D tool with half its controls greyed out.

Everything this UI did before it does now, unchanged, on a flat frame.

This package builds the GUI independently of the CFD solver source tree.
A checkout of the solver (`Fluid-Solver`, called `CFD-Solver-2D` before 1.0) is
not a build dependency and must not be modified for GUI work.

## Requirements

- CMake 3.28 or newer.
- Visual Studio 2022 or newer with **Desktop development with C++**.
- Windows SDK.

On Linux: `build-essential`, `libx11-dev`, `libxrandr-dev`, `libxcursor-dev`,
`libxi-dev`, `libudev-dev`, `libgl1-mesa-dev`. On macOS: the Xcode command line
tools, plus `brew install libomp` for the OpenMP rows.

The package includes the GUI's direct source dependencies:

- `third_party/tinyobjloader/tiny_obj_loader.h`
- `third_party/sfml/` (SFML source)

Therefore `CFD_ROOT_DIR` is no longer required and the first configure does not
need a solver checkout. With the release-default static dependency policy, SFML
may download its pinned FreeType/HarfBuzz/SheenBidi sources on a clean build.

## Build with CMake GUI

1. Extract the ZIP.
2. Open **CMake GUI**.
3. **Where is the source code:** select the extracted `Source` folder.
4. **Where to build the binaries:** select a separate sibling folder, for example
   `CFD-Mask-UI-Optimized_build`.
5. If you previously configured an older package in that build folder, use
   **File -> Delete Cache** first.
6. Press **Configure**.
7. Generator: **Visual Studio 17 2022** (or newer); platform: **x64**.
8. Leave `CFD_SOLVER_EXE` empty unless you explicitly want CMake to copy a built
   `Fluid Solver.exe` beside the GUI.
9. Press **Configure** again until there are no red unresolved entries.
10. Press **Generate**.
11. Press **Open Project** and build `cfd_mask_ui_optimized` in **Release | x64**.

Typical executable location for a Visual Studio multi-config build:

```text
<build-folder>/Release/Fluid Solver UI.exe
```

`CFD_UI_STATIC_RUNTIME=ON` is the default. On Windows this selects the static
MSVC CRT (`/MT`). SFML and its bundled FreeType/HarfBuzz/SheenBidi dependencies
are also built static. Linux additionally links `libgcc`/`libstdc++` statically
when GCC is used; X11/OpenGL/OS libraries remain dynamic because they are system
interfaces. macOS system runtimes/frameworks remain dynamic by platform design.


## Release matrix

The UI now has its own release builder and GitHub Actions workflow:

```text
scripts/build-ui-release.py
.github/workflows/build-ui-all.yml
```

The matrix matches the finished Fluid Solver 0.1 binary matrix exactly: 30 UI
archives across Windows/Linux/macOS, x64/x86/arm64, AVX2/non-AVX2, and the same
OpenMP/CUDA feature-name combinations. Every archive name is the matching solver
archive name with `-ui` appended before `.zip`, for example:

```text
Fluid Solver 0.1 windows-x64 cuda-ui.zip
Fluid Solver 0.1 windows-x64 avx2-omp-cuda-ui.zip
Fluid Solver 0.1 macos-arm64 plain-ui.zip
```

List the exact 30 names without building:

```text
python scripts/build-ui-release.py --version 0.1 --list
```

Build all Windows rows locally:

```text
python scripts/build-ui-release.py --version 0.1 --arch x64 --arch x86
```

On Linux the same command builds x64/x86; the 32-bit development libraries must
be installed. On macOS, CI builds arm64 and x64 on native runners. The workflow
merges all four native-platform jobs, verifies that all 30 expected archives are
present, creates `Fluid-Solver-UI-Source-Code.zip`, and writes `SHA256SUMS.txt`.

AVX2 and OpenMP are both real compile-time switches for this UI: AVX2 vectorises
the byte swap that decodes a VTK frame, OpenMP spreads that decode and the colour
map across every core. CUDA is not, and deliberately so - decoding a frame is a
read plus a byte swap, and the colour map has to land in host memory for the
texture upload anyway, so a round trip to the GPU would cost more in transfers
than the arithmetic is worth. The `-cuda` suffix therefore selects a name and not
a build, which is what lets 22 builds cover the 30 archive names the solver
release pairs against.

A row whose name promises OpenMP is configured with
`CFD_UI_ENABLE_OPENMP_EXPLICIT=ON`, so a build machine without an OpenMP runtime
fails the row instead of quietly publishing a single-threaded binary under a name
that says otherwise.

On Windows an OpenMP row needs the OpenMP runtime DLL beside it, because MSVC
has no static one. Since the solver moved its own loops onto `collapse`, which
classic `/openmp` does not have, a solver `omp` archive now ships
`libomp140.<arch>.dll` rather than `vcomp140.dll`; a `-ui` archive is that
archive plus the UI binary, so the pairing supplies whichever of the two the
solver was built against. The UI itself has no `collapse` in it and builds
under either.

Each binary archive contains one top-level folder with the same stem,
`Fluid Solver UI[.exe]`, `README-UI.md`, `BUILD_INFO-UI.md`, and an empty
`output/` folder. The documentation is renamed on the way in on purpose: these
archives are merged by hand into the solver's `-ui` archive, which has a
`README.md` and a `LICENSE` of its own, and a plain copy would replace them.

**To assemble a `-ui` release archive:** take the solver's archive for that row,
and add `Fluid Solver UI[.exe]` from the UI archive of the same name. Nothing
else from the UI archive is needed - the binary is fully static apart from the
system graphics stack.

## Solver integration

The solver is optional at GUI build time. At runtime the UI looks for the solver
beside its own executable first - `Fluid Solver.exe` on Windows, `Fluid Solver`
with no extension on Linux and macOS - and **Select solver** points it somewhere
else. Selecting or replacing the solver does not require rebuilding the UI.

For a fresh UI with no saved preference, the output root is `output` **beside the
UI executable**, which is where the solver writes its own frames and where the
installers create the folder. It is not the process working directory: a Start
Menu shortcut, a desktop icon or a drag-and-drop each hand the process some other
directory, and frames used to land wherever that happened to be. A saved
`ui-preferences.txt` or a loaded `.cfdui` configuration still overrides it.

What goes on the command line depends on what the selected executable
understands, because a solver exits on the first argument it does not know.
`restart`, `restartFile` and `addTime` need 0.1.1 or newer; `avx2`, `openmp`,
`threads` and `tray` need 0.2. Everything added after 0.2 was published has no
version number to separate it, so the UI looks for the key name in the
executable's own parameter table instead and leaves the whole block off when it
is not there:

| Block | Found by |
|---|---|
| `gravityEnabled` `gravityAccel` `gravityAngle` | `gravityEnabled` |
| `wallMotion` | `wallMotion` |
| `bodyMotion` `bodyCoupling` | `bodyMotion` |
| `profiles` | `profiles` |
| `extraFields` | `extraFields` |
| `convection` `limiter` `timeScheme` `gravityMode` | `timeScheme` |
| `bcLeft` … `inletProfile` | `bcBottom` |
| `caseType` `lidSpeed` `steadyTolerance` | `caseType` |
| `phases` `rho1` `nu1` `rho2` `nu2` `phaseInit` … `sources` | `vofScheme` |
| `mixing` `diffusivity` `surfaceTension` `contactAngle` | `surfaceTension` |
| `turbulence` `Cs` `turbIntensity` `turbLengthScale` | `turbLengthScale` |
| `regime` `gamma` `R` `T0` `pInf` `machInlet` … `micInterval` `micAudio` `micAudioRate` `micAudioSpeed` | `machInlet` |
| `nz` `Lz` `bcFront` `bcBack` `bcFrontSpeed` `bcBackSpeed` `inletFrom2` `inletTo2` `gravityTilt` `phaseZ` `sliceAngleY` | `nz` |

The last row is the whole third dimension and it is found by one key, `nz`,
for the same reason as every block above it: point this UI at a solver that
predates the port and none of those eleven keys reach the command line, `nz`
is not on the panel to be set in the first place, and what gets launched is the
plane run that solver knows how to run. Sending `nz=1` to a solver that has
never heard of it would not be harmless — it exits on the first argument it
does not know.

The settings that live *inside* an existing key are not separate blocks and
cannot be: `z=` and `elev=` in `sources`, `z=` and `ay=` in `profiles`,
`rotX= rotY= slideZ=` in `wallMotion`, the eight new `bodyMotion` components,
and the third coordinate in `microphones` are all text this UI hands over
untouched. It writes them only when `nz` is on the panel, which is the same
test, done once.

`bc<Side>Speed` is only written when that side is a `movingWall` or the speed is
not zero. Writing `bcLeftSpeed=0` for an inlet would tell the solver a standstill
was asked for, and an inlet meant to run at `U0` would come out stopped.

A continuation sends no `wallMotion` and no `profiles` while those two boxes are
empty, so whatever the frame was run with survives.

**Case** is a preset rather than another parameter. `cavity` makes the solver
write all four sides itself - three walls and a lid sliding at **Lid speed** -
so the UI leaves the whole BOUNDARIES block off the command line when it is
picked. Sending both would run the preset and then overwrite it with whatever
the boundary rows happened to say, which is not what picking a preset means.
`channel` sends the rows exactly as before.

**Stop when steady** is `steadyTolerance`: the run ends early once the largest
velocity change per second, measured against whatever drives the case, drops
under it. Zero, the default, runs the whole of **Total time**.

=== BODIES ===

A body selector and a set of rows that edit whichever body it is pointing at,
rather than one row per body per setting - a table of eight settings across
however many bodies the mask happens to have does not fit on a screen, and the
number is not known until the mask is generated.

    BODIES   Body               which one the rows below are about
             Behaviour          static | drag | slip | travel | free
             Surface spin       deg/s      \
             Surface spin X     deg/s       |
             Surface spin Y     deg/s       > drag: the surface moves, the
             Surface slide X    m/s         |  body does not
             Surface slide Y    m/s         |
             Surface slide Z    m/s        /
             Body velocity X    m/s        \
             Body velocity Y    m/s         |
             Body velocity Z    m/s         > travel: the body itself moves.
             Body spin          deg/s       |  Under free, what it starts with
             Body spin X        deg/s       |
             Body spin Y        deg/s      /
             Body mass          kg/m       \
             Body density       kg/m3       |
             Inertia X          kg m2       > free only
             Inertia Y          kg m2      /
             Pinned             which degrees of freedom are held
             Pin Z              hold it still in depth
             Pin rot X          hold that rotation
             Pin rot Y          hold that one
             Body motion        text, the solver's grammar
             Body path          text, the poses the Layout view drew
             Coupling           weak | added | strong
             Collisions         off, bodies pass through each other
             Bounciness         how much of the closing speed survives
             Report forces      work the force out for set paths too

The nine rows the third dimension added — two more surface spins, a third
surface slide, a third body velocity, two more body spins, two more inertias
and three more pins — **are not on the panel while `nz` is 1**. A plane has one
axis a body can spin about and two it can slide along, and a row for a degree
of freedom that does not exist is a row somebody will set and then wonder about.
They appear the moment `nz` goes above 1 and they keep their values when it
goes back, exactly the way the regime rows already come and go.

`Surface spin` is `rotZ` and `Body spin` is `omegaZ`; the solver keeps `rot=`
and `omega=` as the names for those, so an old line still reads back into the
old rows.

The two text rows are the truth and the rows above them are a way of writing
into one entry of each - exactly as the brush is a way of writing into the
sources row. Pick a body and the rows read that body's entry back out of the
text; change a row and it writes that entry back. Editing the text by hand does
the same thing, and `@<seconds>` keyframes can only be written there, because a
timetable is not a slider.

**Collisions** is off by default and that is deliberate: turning it on changes
the answer, and every run written before this branch had bodies passing
through each other. On, a body that would run into another one or into the
domain edge bounces instead, and **Bounciness** is how much of the closing
speed comes back - 0 stops dead, 1 rebounds at the speed it arrived.

**Report forces** works the fluid force out for bodies whose path you set as
well. It never changes where they go; a set path is a set path. It only puts
the force in the step line so you can read what the fluid was doing to the
body - useful just before you release it, and for a drag coefficient. Free
bodies always have it computed, because that is the thing that moves them.

=== TURBULENCE ===

    TURBULENCE  Turbulence model        none | smagorinsky | kOmegaSST
                Smagorinsky Cs          the one constant smagorinsky has
                Turbulence intensity    fraction of the inlet speed
                Turbulence length       m, the biggest eddy coming in

Four rows, and three of them are read by one model each. `Cs` is only sent for
`smagorinsky`; the two inlet rows are only sent for `kOmegaSST`; `none` sends
the model name and nothing else, so a run with the model off puts exactly one
extra key on the command line and changes nothing about what the solver does.

The whole group is held back on finding `turbLengthScale` in the executable,
like every block since 0.2. Point it at a 0.7 solver and none of these four
rows reach the command line.

Refused before the run rather than after: a `Cs` of zero or above one, an
intensity above one - an inlet more turbulent than it is moving - and a
negative length scale.

`nuT`, `wallDistance` and `strain` are `extraFields` like any other and show up
in the **Field** button once the solver has been asked to write them. `k` and
`omega` arrive on their own in every frame of a `kOmegaSST` run, because the
solver needs them back to continue.

One trap the UI cannot fix for you: **object numbers come from the mask, not
from the order the models are listed in.** The flood fill walks the grid in
scan order, so of two shapes side by side either can end up as number 1. The
solver prints the mapping with the mesh, before anything runs, and that is what
the Body row is pointing at.

**Behaviour** is the one row that decides which of the two strings an entry
goes into. `drag` and `slip` are `wallMotion` - the surface moves and the body
stays put. `travel` and `free` are `bodyMotion` - the body itself goes
somewhere, and the solver cuts its outline again every step.

Held back on finding `bodyMotion` in the executable, like every block since
0.2. Point it at a 0.6 solver and the whole group is left off the command line.

The validator refuses a body told to travel through an empty domain, because
moving a body means cutting its outline again and an empty domain has no
outline to cut. Better a sentence now than a run that starts, prints a refusal
of its own and then sits perfectly still for ten minutes.

**Phases** turns the FLUIDS group into two fluids with their own densities and
viscosities, and the setup view into something you can paint on. `phases=1`
sends none of it.

**Surface tension** (in mN/m, because that is how everybody quotes it - water
against air is 72) and **Contact angle** are the last two rows of the FLUIDS
group, held back on the solver having the keys at all - a 0.5 solver is asked
exactly what it was asked before. Zero tension is off. The angle is measured
inside fluid 1 at a wall: under 90 it wets the wall and climbs, over 90 it
beads off.

**Mixing** decides whether there is a surface at all. `immiscible` is oil and
water and is what every run so far did. `miscible` is ink and water: no
interface, the interface scheme is not read, the composition spreads by
**Diffusivity** instead - and a surface tension on top of that is refused
rather than quietly ignored, because there is nothing for it to pull on.

Surface tension is not free in wall clock either. The step size it forces is
`sqrt((rho1+rho2)*d^3/(4*pi*sigma))`, it usually binds well before the CFL
number does, and it falls as `d^1.5`, so halving the cell size costs about
three times the steps. The solver says so on the first step rather than
appearing to hang.

**Paint** replaces the 3D preview with the solver's own grid, one pixel per
cell, and paints the initial volume fraction straight onto it. Left button
lays down fluid 1, right button takes it back to fluid 2, the wheel resizes the
brush, and Fill, Clear and Undo do what they say. What is painted is written as
`initial-phase.txt` beside the run and passed as `initialPhaseFile`, so the
folder holding the frames also holds the thing they started from - and a
painted field overrides whichever of layer, drop and column the Start shape row
happens to be showing, because the point of painting one is that it is none of
those.

The brush also places **sources**, and it does it by writing a line in the
solver's own grammar into the Flow sources row rather than inventing a second
way of saying the same thing. Drop one, then edit the row for rate and angle.

Fluid 1 and Fluid 2 are which of the two the left button lays down; the right
button always lays down the other, so a stroke can be taken back without
reaching for anything.

Changing nx or ny throws the painting away rather than stretching it into
something nobody drew. Changing nz does not, and that is not an oversight —
see below.

**The brush paints a plane, which is what a brush is.** `initial-phase.txt` is
`nx*ny` fractions whatever `nz` says, and the solver extrudes a single plane's
worth through the depth when that is what it is handed. So a painted start
shape in a volume is a prism: the stroke you drew, all the way through. That is
honest about what was actually drawn. Painting a volume needs a tool nobody has
written, and treating a 2D stroke as though it meant a sphere would be
inventing an intention nobody had. When a sphere is what you want,
`phaseInit=drop` with **Start shape Z** is the row that makes one.

**Run simulation** is no longer greyed out without a model. That was the last
place where "the profile is optional" was not actually true: an empty domain is
a case in its own right and a painted phase field is a whole initial condition,
and the button stayed dead through both.

In the results view the `phase` field gets a colour map of its own, fixed to
0..1 rather than to whatever turned up in this frame: a domain of pure water
should look like pure water and not like half of it. Everything else the solver
writes still goes through the general ramp.

When nothing has been drawn, the UI now sends `geometryFile=empty` instead of a
section adapter with no contours in it. An empty adapter used to make the solver
fall back to its verification circle, which is how a lid driven cavity ended up
with a cylinder sitting in the middle of it.

## Uneven grids

The compressible solver can now put its cells where they are needed, and its
frames come out as `RECTILINEAR_GRID` with a list of face positions per axis
instead of one `SPACING`. The reader takes both: `VtkFrame` grows a `faceX`,
a `faceY` and — since the volume port, because the solver's stretched grid grew
its z axis too — a `faceZ`, each empty on an evenly spaced frame, and
everything that used to multiply by `spacingX` asks the frame for the cell
instead. `spacingZ` is a real spacing now rather than a placeholder, so the
same question has the same answer on all three axes.

Drawing it needed one change and it is worth naming, because getting it wrong
looks fine. The result view builds one texture pixel per cell and stretches it
over the viewport - which on a stretched grid draws a cell a tenth the size of
its neighbour at the same width, and the picture is then a lie about where
everything is. So a stretched frame is **resampled onto an even image of the
same size**: pixel to metres, metres to cell, nearest cell wins. Hover does the
same lookup in reverse, so the readout under the cursor is the cell that is
actually under the cursor. On an evenly spaced frame the lookup is the identity
and the branch is hoisted out of the loop, so nothing about an old frame
changed.

`VtkFrameTests` covers it: a 10:1 stretched frame parsed back with the right
cell widths, a physical coordinate landing in the cell that contains it,
coordinates outside the domain clamping to the end cells, and an evenly spaced
frame still reporting itself as even and still resolving cells the way it
always did. Non-increasing coordinates are refused - a cell of zero width is
not a cell.

Three rows drive it, in the GAS / COMPRESSIBLE group: `Grid stretch`
(off / body / wake / edges), `Stretch ratio` and `Fine band`, the last two
appearing only once stretching is asked for, and `Fine band` staying away for
`edges`, which has no band.

## Refinement

The compressible solver can put patches of a finer grid where the flow needs
them. Four rows in the GAS / COMPRESSIBLE group drive it - `Refinement levels`,
`Refine on`, `Refine above` and `Regrid every` - with the last three appearing
only once a level is asked for.

Patches are volumes now — the solver's `AmrBox` carries a `k0` and an `nz`, the
clustering splits along the longest of three axes, and averaging down is the
mean of eight cells rather than four — and none of that reaches this UI either,
for the same reason as everything below.

The UI needs nothing else, and that is deliberate on the solver's side: a
refined run still writes the ordinary `.vtk` frame on the base grid, with the
fine levels averaged into it, so every frame this UI could read before it stays
readable. The full hierarchy goes into a `.vtm` next to it, one `.vtr` per
patch, for ParaView. Showing the patches at their own resolution in this window
is a later job; averaging them into the base grid is not a placeholder, it is
what makes the base frame the best answer the run has rather than the coarse
one it started from.

`FluidSolverRunTests` covers the settings: the four keys reaching the command
line together, a criterion nobody has heard of being refused, a threshold of
zero being refused (it refines the whole domain), rebuilding every zero steps
being refused, and the three detail keys staying off the command line when
refinement is off.

## The 3D viewport

A real one, on OpenGL, in the same window. `<SFML/OpenGL.hpp>` gives OpenGL 1.1
with vertex arrays, `pushGLStates()` / `popGLStates()` is how raw GL and SFML
drawing share a window, and that is the whole of the new dependency list —
which is to say there isn't one.

What it draws sits behind eight buttons along the viewport, each of which opens
a list rather than doing something on its own. A list has room for a name and a
sentence saying what the name means; a button five letters wide has room for
`Iso`, and `Iso` has never taught anybody anything:

| | |
|---|---|
| **Layers** | what fills the box: **cloud**, **isosurface**, **vortices**. Ticks, not a choice — any of the three, all three at once, none. Each one that is on gets its own slider under the bar |
| **Slices** | **slice X / Y / Z**, ticks as well, each a plane of cells coloured by the current field and moved on its own slider |
| **Flow** | **streams**, the path a weightless speck would take, and **tracers**, dots running along them |
| **Show** | **body**, **wireframe**, **box**, **grid**, **microphones** |
| **Colour** | one field for everything that is drawn: **off**, pressure, speed, u, v, w, vorticity, Q, and every scalar the frame carries. A cloud of Q is a cloud of vortices; an isosurface of density is a shock |
| **Camera** | **rotate** / **move** for what left-drag does, **ortho**, **frame all**, and the six axis views |
| **Range** | which values the colour scale spans: the series or this frame, trimmed or whole |
| **Run** | **continue this run**, **run details**, **recover the setup** |

Every entry carries a sentence, shown in a box beside the list while the cursor
rests on it — including the one that says what Q is. A tick is drawn `[x]` or
`[ ]`, the button itself says what is on (`Layers: cloud+vortices`), and the
lists whose entries are independent stay open while you tick several.

The keys do the same thing: `C` cloud, `I` isosurface, `Q` vortices, `[` and `]`
for how solid the cloud is, and each of them toggles that one layer and leaves
the others alone.

Everything above is built into vertex arrays **once per frame change**, not per
redraw, and the isosurface, the vortex surface and the streamlines are built
off the redraw path entirely. That is what keeps a 128^3 volume interactive
while you drag it around. Turning a layer off does not merely stop drawing it —
it stops being rebuilt.

### Clicking in it selects something

Left-click without dragging is a pick, and the viewport answers with what is
under the cursor:

- **a body** selects it — the `Body` row in the BODIES group moves to that
  number, and every row under it is about that body from then on. This is the
  thing the panel could never do: the BODIES group could always *describe* a
  body's motion and could never *point* at one, and the object numbers come out
  of the solver's flood fill rather than out of the order you listed the models
  in, so pointing was the only reliable way to mean a particular body.
- **a domain face** focuses that face's boundary row, and says in the status
  line what it currently is and what its speed row holds. Six faces, six rows,
  and no counting which one `bcFront` is.

Hovering reads out the cell under the cursor without selecting anything, in a
small box that follows the cursor. It used to go on the bottom line, where it
shared the room with the triangle count and with whatever warning the run had
produced, and the three took turns.

## The 2D view is a slice through the volume

The old view is kept whole. Not reimplemented, not ported, not "mostly the
same" — the colour maps, the vectors, the tracers, the probe readout, the
legend, the zoom and the pan are the code they always were, and on a flat frame
every pixel of it is what it used to be.

What changed is where its data comes from. On a volume it draws a **slice**:
pick the axis with **Axis X**, **Axis Y** or **Axis Z**, and move the plane
along that axis with the track beside it. The frame underneath is the volume;
what the 2D view gets handed is one plane of cells out of it.

**`V` switches between the two views**, and it is the same key in both
directions. A frame that is a volume opens in the 3D viewport; a frame that is
flat opens in the 2D view, because a 3D viewport showing a slab one cell thick
is a worse picture of a plane than the plane is.

## Result fields

A frame carries pressure, the solid mask and velocity. Anything else the solver
was asked to write - `vorticity`, `divergence`, `speed`, `objectId`, `phase`,
`nuT`, `k`, `omega`, `wallDistance`, `strain` - is read into a registry keyed by
the name the frame used, and the **Field** button walks whatever turned up and
back round to pressure. Nothing in the UI has a list of which fields exist, so a
field the solver learns to write later shows up without this project changing.

That last sentence earned itself back during the port. `vorticity` is a scalar
in a plane and a **vector** in a volume, and the registry did not have to be
told: a vector array arrives under the same name with three components instead
of one, and the viewport colours by its magnitude the way it colours by any
other. `divergence` and `speed` simply gained their z term and are still one
number.

### Reading a volume frame

`DIMENSIONS nx+1 ny+1 nz+1`, a real `SPACING dz`, `CELL_DATA nx*ny*nz` in
`(k*ny + j)*nx + i` order, and `VECTORS velocity` as `3*nx*ny*nz` interleaved
`(u, v, w)` in that same order. A plane run writes `nz+1 == 2` and is read as a
volume one cell deep.

A frame written by **any earlier version** — a third `DIMENSIONS` token of `1`,
no `w` at all — still loads, as a volume one cell deep with `w` zero. That is
not a compatibility shim bolted on the side; it is the same reader taking `nz`
as 1, which is what those frames have always described. `VolumeFrameTests`
holds the whole of it, and rather more: the new layout parsed back with the
right cell in the right place, an old flat frame still reporting itself flat, a
rectilinear volume with a face list per axis, slices out of a volume matching
the cells they were cut from, marching cubes, the Q and vorticity criteria, the
vortex core lines, the streamline tracer, the picking arithmetic, and the
viewport's vertex-array builds themselves. None of it needs a window, which is
the only reason any of it could be checked here at all.

Where a scalar sits in the file is the writer's business, and it took a while to
admit it. The reader used to refuse any array it did not recognise until it had
seen all three of pressure, mask and velocity - and the solver writes the phase
fraction between the pressure and the mask, with `k` and `omega` beside it. So
every two-fluid frame ever written was rejected on the way in with *Unknown
scalar array before required arrays: phase*. The order check is gone; a frame
that never provides the three is still rejected, at the end of the parse where
that can actually be known.

The three that were always there stay named members rather than map entries:
they are read on every pixel of every redraw and have no business going through
a hash lookup to get there.

### The panel changes shape with the regime

    GAS / COMPRESSIBLE  Regime              incompressible | compressible
                        gamma               \
                        Gas constant R       > the gas
                        Temperature T0      /
                        Ambient pressure
                        Inlet Mach          not m/s: the ratio is the physics
                        gamma, gas 2        \  two phases only
                        R, gas 2             > two gases rather than
                        Species mode        /   two liquids
    ACOUSTICS           Acoustic fields     SPL, pitch and p' on the grid
                        Acoustic window     how far back the mean looks
                        0 dB reference      2e-5 Pa is the usual one
                        Microphones         x=0.5,y=0.2,z=0.5;... - the accurate half
                        Mic interval        steps between samples
                        Write .wav          one file per microphone
                        Audio rate          44100 is what everything plays
                        Audio speed         0.05 plays it twenty times slower

The body rows stay on the panel in either regime. They used to be refused
alongside `regime=compressible` because the solver cut its mask once and kept
it; it re-cuts it every step now, so a body travels through a compressible run
the same way it travels through an incompressible one and the UI stopped
arguing about it.

Picking `compressible` does not grey the pressure-solve rows out, it takes them
off the panel: viscosity, density, the two fluids, the VOF scheme, surface
tension, sources, gravity, the whole turbulence group and all five multigrid
rows go, and the gas rows come in. Picking `incompressible` puts them all back
exactly as they were - a hidden row keeps its value, it is only not shown and
not sent.

Rows inside a group come and go the same way. `gamma, gas 2` and `Species mode`
appear at two phases and not before; `Acoustic window` and `0 dB reference`
appear once there is something listening; `Mic interval` and `Write .wav`
appear once there is a microphone, and `Audio rate` and `Audio speed` only
once the wav is actually asked for. And `Lid speed`, which has been sitting there since 0.4 doing
nothing for every case that is not a cavity, now appears only for the cavity.

The mechanics: the panel is one list laid out by walking it, so hiding a row is
a matter of not advancing the cursor for it and parking it off screen, and a
group whose rows are all hidden loses its header with them rather than leaving
a title standing over nothing. Which rows are hidden is a function of a handful
of other rows, so rather than hang a relayout off every path that can change one
of them, the signature of those rows is read once a frame and the layout redone
when it moves.

### The rows the third dimension added

`nz` is one of the rows the layout signature watches, and every row below is
**off the panel while it is 1**. Not greyed out — off, the same way the
incompressible rows go off under `regime=compressible`, and keeping its value
while it is away.

    GRID         Depth Lz                m, how deep the volume is
                 nz                      cells through the depth. 1 is a plane
    BOUNDARIES   Front                   the z = 0 side, same five kinds
                 Back                    the z = Lz side
                 Front speed             what a movingWall there slides at
                 Back speed              the same, at the back
                 Inlet from 2            the window along the face's second axis
                 Inlet to 2              the other end of it
    FLOW         Gravity tilt            deg out of the xy plane towards +z
    FLUIDS       Start shape Z           where the drop sits in depth
    GEOMETRY     Slice angle Y           the third model rotation

Plus the nine BODIES rows listed above, and `Inlet profile`, which is on the
panel either way and grows a third option, `parabolicSpan`, once there is a
second axis for it to mean something along. `parabolic` bends both axes and is
duct flow; `parabolicSpan` bends the first and leaves the second flat, which is
a plane channel extruded through the depth. At `nz = 1` they are the same thing,
which is why the option only appears when it is not.

`nz` itself sits next to `nx` and `ny` and defaults to 1, so a fresh UI on a
fresh install is configuring exactly the plane run it always configured, and
the panel is the length it always was until somebody types a 2 into it.

Every one of these rows has its own hover hint like every other row, and
`ParameterInfoTests` fails the build if one of them does not — which is how
they came to have them, rather than by anybody remembering.

### The preview is on the solver's grid, not on a better one

On a plane run the UI cuts the section itself, writes it out as
`section-adapter.obj`, and then compares the mask the solver rasterised out of
that file against the one it drew for the preview. They used to disagree, by
three cells out of 2500, on a cube.

**On a volume run there is no adapter at all.** The solver voxelises the whole
model rather than cutting a contour out of it, so what goes on the command line
is the model file itself — the `.stl` or `.obj` you imported — and nothing is
written in between. That is strictly better and it is also the only thing that
can be right: a section adapter is a flat outline, and handing one to a
voxeliser would describe a body with no thickness. The section-cutting code
below still runs for the preview and for the plane case, and is untouched.

The solver keeps its grid spacing in `float`: `dx = float(Lx)/nx`. The preview
computed it in `double`. For `Lx = 1, nx = 50` that is 0.019999999552965164
against 0.02, and by `i = 30` the two disagree about where the cell centre is
by a part in 10^8.

That is nothing at all right up until a face of the model lands on a cell
boundary, which for anything axis aligned it does on purpose. Then the four
corner cells of the rectangle sit at exactly one cell circumradius from the
outline - the distance at which the rasteriser decides a cell is on the
boundary - and a part in 10^8 is the whole difference between a solid cell and
an empty one.

So the preview now divides in `float` too. Being more accurate than the thing
you are previewing is not an improvement.

The answer this produces is not symmetrical: `float(1)/50` is a hair UNDER
0.02, so a cell centre drifts towards the origin as its index grows, and the
far corners land just inside the circumradius while the near ones land just
outside. Three corners solid, one not. It looks like a bug and it is the
solver's own answer, which is the only answer a preview is allowed to have.
GeometryProcessorTests pins all four.

## Frame loading

A VTK frame is read in one go and decoded from memory. The legacy binary payload
is big endian, so every 32-bit word is swapped in place - eight at a time through
one AVX2 shuffle where that is compiled in - rather than pulled through the
stream a value at a time, which is what the reader used to do. On a 600x300 grid
that is 24.8 ms down to 2.6 ms per frame.

Frames also decode on several threads at once, one per spare core up to eight.
There used to be a single loader, and a request while it was busy simply waited,
which is why flipping quickly past the few cached steps stalled on every one. The
decoded-frame cache holds up to 256 frames within its byte budget instead of 16,
so an ordinary series ends up entirely resident after one pass.

The colour map that turns a frame into the displayed texture runs across cores
too, and its buffer is kept between frames rather than reallocated per step.

## Tests

Enable `BUILD_TESTING` in CMake GUI if wanted, then build the test targets and run
CTest. `SolverCompatibilityTests` is added only when `CFD_SOLVER_EXE` points to an
existing solver executable.

## Every row explains itself

Hovering a parameter pops a small box under the cursor with one sentence about
what that row does. Not the manual - one sentence, written so that somebody who
has never opened a CFD tool gets a real answer and somebody who has still gets
the unit and a number to aim at:

    Viscosity nu     How syrupy the fluid is. Low means it swirls and sheds
                     vortices, high means it flows smoothly. Water 1e-6,
                     air 1.5e-5 m2/s.

    Inlet Mach       Inlet speed as a multiple of the speed of sound, not in
                     m/s. Under 1 is subsonic, over 1 is supersonic.

    CFL              How far the flow may cross a cell in one step. Under 1
                     keeps it stable; lower is safer and slower.

Every one of the 133 rows has one. That is not a claim, it is a test:
`ParameterInfoTests` walks the whole list and fails if a row has no hint, if a
hint is under twenty characters (a label, not an explanation), over two hundred
(that belongs in the long help), does not end in a full stop, or has stray
whitespace in it. It also checks that every row has a unique solver key, that
the groups are in ascending order, and that every group is reachable from
exactly one tab. Add a parameter and forget its hint and the build tells you.

The longer paragraph that was already there did not go anywhere - it still
appears in the info box in the top right corner while hovering, and it is
word-wrapped now instead of being clipped after one line, which is what it had
been doing since it was added.

The tables behind all of this - the parameter enum, the keys, the hints, the
long help, the groups and the tabs - moved out of `Application.cpp` into
`ParameterInfo.{hpp,cpp}`, which is what makes them testable without a window
and takes six hundred lines out of a file that had nine thousand.

## Saving and loading a configuration

**Save** writes the whole setup as `key=value` lines, one per row, in the
solver's own grammar — which means the file is not a description of a command
line, it *is* one. Feed it to the solver and it runs: the keys are the keys the
solver takes, the spelling is the spelling it takes, and nothing in the file
needs translating on the way out. A handful of UI-only lines ride along
(`format`, `model`, `outputRoot`, `solver`, `invertSection`, and the `ui`-
prefixed rows such as the body track), and the solver skips keys it does not
know, which is the same rule that lets an old frame load into a new build.

**Load** reads one back. Rows the file does not mention keep what they have;
rows it mentions that this panel does not know are listed rather than
swallowed, so a configuration written by a newer build tells you what it
brought that could not be shown.

### Recovering one out of a `.vtk` frame

**Recover setup**, next to the result view's other buttons, is the useful half.
Every frame the solver writes carries its own configuration in a `configText`
block at the end — that is how a continuation works — so the settings of any
run can be read straight back out of any frame it produced:

- point the UI at a folder of frames, pick one, press **Recover setup**;
- the panel fills in with what that run was actually launched with, down to
  `nz`, `Lz`, the six boundary kinds and the body grammar;
- the view switches back to Setup, and the status line names the solver step it
  came from.

That closes the one gap that used to need a lab notebook: a folder of frames
from three weeks ago is now self-describing, and a run can be reproduced,
tweaked and relaunched without anybody remembering what was typed. A frame with
no `configText` — one written before the block existed — says so instead of
loading half a configuration.

## Body paths are curves

A body's path through the Layout view is a **Catmull-Rom curve** through its
control points by default, in three dimensions, rather than a series of
straight runs between them. Drop three poses in a rough arc and the body flies
the arc, instead of flying to the middle one, stopping dead and turning.

Straight keyframes have not gone anywhere. They are what the curve is made of
and what it is written out as — see *Layout* below for the conversion — and a
track set to interpolate linearly behaves exactly as it did before this, which
is what every existing `.cfdui` gets.

The curve carries `z` alongside `x`, `y` and the rotation, so a path can leave
the plane it started in. On a flat run every control point has the same `z`, the
z term of the curve is a constant, and the path is the 2D path it always was.

`BodyTrackTests` covers the curve as it covers the rest: the round trip, the
sort, a keyframe replaced at the same instant, the interpolation surviving the
trip into `bodyMotion`, the clamp outside the track, and two poses at the same
instant not turning into a division by zero.

## The window is laid out the way Blender lays one out

Not as a compliment to Blender — as the thing to copy when a window has to hold
a viewport, several hundred parameters and a timeline at once, because that is
a problem somebody has already solved and users already know the answer to.

- a **header strip** across the top;
- the **viewport** filling the middle — the 3D one, or the 2D slice view, or
  the setup preview, depending on what you are doing;
- a **properties column** down the right, holding the parameter groups, with
  its tab strip and its search;
- a **scene outliner** above that column, listing the domain, its six sides and
  the bodies in the mask — click a row and the panel goes to it, the same way
  clicking the thing itself in the viewport does;
- a **timeline** along the bottom, carrying the frame range, the playback
  cursor, and a tick per keyframe for the selected body.

The orbit, pan and zoom bindings below are Blender's too, numpad views
included, for exactly the same reason.

## Keyboard and mouse

The window used to answer to the mouse and nothing else, which is fine until
you have typed a number in and want it back.

**In the 3D viewport:**

| | |
|---|---|
| left-drag | orbit |
| middle-drag | pan |
| wheel | zoom |
| left-click, without dragging | pick: a body selects it, a domain face focuses that face's boundary row |
| hover | read out the cell under the cursor |
| `V` | switch between the 3D viewport and the 2D view |
| `F` | frame the whole volume |
| `Numpad 1` / `Numpad 3` / `Numpad 7` | front, right and top views; hold `Ctrl` for the opposite side |
| `Numpad 5` | orthographic or perspective |

**In the 2D view, on a volume:**

| | |
|---|---|
| `X`, `Y`, `Z` | which axis the slice is cut along |
| `Up`, `Down` | move the slice plane one cell |
| wheel | zoom, as it always did |
| `V` | back to the 3D viewport |

**Anywhere in the result view:**

| | |
|---|---|
| `Left`, `Right` | previous and next frame |
| `Home`, `End` | first and last frame |
| `Space` | play and pause |

**In the setup view:**

| | |
|---|---|
| `Ctrl+Z` | undo |
| `Ctrl+Y`, `Ctrl+Shift+Z` | redo |
| `Ctrl+C` | copy the focused row's value; with nothing focused, the whole configuration |
| `Ctrl+X` | copy it and put the row back to its default |
| `Ctrl+V` | paste into the focused row; a whole configuration if that is what the clipboard holds |
| `Ctrl+F` | find a parameter |
| `Ctrl+S`, `Ctrl+O` | save and load a `.cfdui` |

Inside a value box `Ctrl+C`, `Ctrl+X` and `Ctrl+V` work on the text being
typed rather than on the row, and `Ctrl+A` clears it.

**Undo is one stack over the whole setup state** - every slider, every text
row, the painted field and the invert flag - rather than one stack per widget.
A snapshot goes on before anything that changes a value: grabbing a slider,
committing a typed number, a step button, a paint stroke, a paste, Reset
defaults, loading a file, and every keyframe the Layout view drops. The paint
Undo button is the same stack, so undoing a brush stroke and undoing a slider
are the same key. Consecutive identical states collapse, so holding a slider
still still costs one entry, and the stack is capped at 64.

**`Ctrl+F` filters rather than jumps.** Typing hides every row whose label and
whose solver key both fail to contain what you typed, across all groups and
regardless of the tab, so `mach`, `omp` or `wav` leaves the panel holding two
or three rows. Enter keeps the filter and focuses the first match; Escape
clears it. While a filter is up the tab strip is replaced by the search box,
because a tab and a filter fighting over which rows are visible is a bug
waiting to be reported.

## Parameter panel

The setup parameter panel is one scrollable list. It contains all current
solver-facing numerical controls rather than hiding the multigrid/timestep
controls on a separate Basic/Advanced page.

**A strip of tabs sits above it**: All, Flow, Shape, Grid, Run. They are not a
different panel - they are the same list with the groups that do not belong to
the tab hidden, exactly the mechanism the regime already uses - so a row keeps
its value, its scroll position and its place in the `.cfdui` file when it is
not on screen. `All` is the old behaviour and is still the default.

Rows are also easier to read down: they alternate a faint band, the row the
keyboard is acting on is filled and has an accent edge down its left side, and
clicking anywhere on a row focuses it.

- Mouse wheel over the left parameter panel scrolls the controls.
- The visible scrollbar can be dragged or clicked.
- Mouse wheel over the 3D preview still controls preview zoom.
- A row is a number, a choice or a line of text. The panel is one list and
  everything about it works by index, so a dropdown and a text box are kinds of
  row rather than separate widgets: the layout, the scrolling, the group
  headers and the `.cfdui` file did not have to learn they exist.
- A choice row is dragged or clicked across its options like any other slider,
  and double-clicking its value lets the name be typed instead.
- A text row holds the solver's own grammar and is handed over untouched. The
  UI only refuses a line break; what a line means is the solver's opinion, and
  having two opinions about the same string is how they end up disagreeing.
- The WALLS group is one such text row, so every body can be addressed
  individually - `1:rot=90,slideX=0.5;2:slip=1` - rather than one setting being
  applied to all of them.
- GEOMETRY has a second text row for `profiles`, which places several models at
  once, and BOUNDARIES has a choice row per side under a **Case** preset.
- `outputDir`, `geometryFile`, transformed slice arguments, and `invertSection`
  are generated from the GUI workflow rather than exposed as ordinary sliders.


## Layout: picking bodies off the screen

The BODIES group could always describe a body's motion; it could never point
at one. **Layout**, next to Paint in the setup viewport, draws the domain as
the solver will see it - the geometry rasterised onto the run's own grid, flood
filled into the same objects the solver numbers, in the same scan order, so the
number under the cursor is the number `bodyMotion` means.

Same objects means same connectivity: **8-connected on a plane, 26-connected in
a volume**, matching the solver exactly. Getting that wrong would be the worst
kind of wrong — two cells meeting only at a corner counted as one body here and
two bodies there, and every number after the disagreement pointing at something
else. Same scan order too, `i` inside `j` inside `k`, which in a volume means
the whole of the front plane is numbered before any of the one behind it.

In the result view the 3D viewport picks bodies the same way and means the same
numbers; Layout is where you do it before there is a run to look at.

- **Click a body** to select it. That sets the Body row in the panel too, so
  the existing mass, pins and coupling controls follow the selection.
- **Drag it** to place it, **right-drag** or **Q**/**E** to turn it.
- **K** drops a keyframe at the time cursor. **Delete** removes the one under
  the cursor. **Tab** cycles bodies, **,** and **.** step the cursor, **I**
  cycles the interpolation.
- The timeline under the canvas carries a tick per keyframe for the selected
  body, and the canvas draws the path it will take with a dot at each pose.

What it writes is the interesting part. A keyframe in a user's head is a
*pose*: this body, here, at this moment. `bodyMotion` is *velocities*. So the
UI keeps the poses in its own row, `uiBodyTrack` - `@t=..,x=..,y=..,z=..,rot=..`
per keyframe, per object, saved in the `.cfdui` and never sent anywhere - and
every time it changes, `bodyMotion` is rewritten from it.

By default the poses are read as a **Catmull-Rom curve** through the control
points rather than as straight runs between them, and the curve is sampled to
produce the velocities that carry the body along it, plus a final keyframe of
zeroes so it stops rather than sailing on. A curve through three points is what
somebody dropping three points meant; a polyline through them is a body that
flies to the middle one, stops dead and turns. Straight keyframes are still
there and are what a linear segment produces, so a track written before this
behaves the way it did. `interp=` and `ease=` ride along on the pose that opens
each segment.

The curve runs in three dimensions: `z` is interpolated exactly as `x` and `y`
are, so a body can be told to swing out of the plane it started in. On a flat
run every pose has the same `z` and the z term is a constant, which is the
2D path unchanged.

Editing `bodyMotion` by hand still works and still wins - it is what is sent.
It just means the Layout view no longer knows where the body is supposed to be,
because the poses it was drawing are no longer the ones the solver will follow.

The conversion is its own translation unit, `BodyTrack.cpp`, so it is testable
without a window: `BodyTrackTests` covers the round trip, the sort, the
replacement of a keyframe at the same instant, the interpolation surviving the
trip to `bodyMotion`, the clamp outside the track, two poses at the same
instant not turning into a division by zero, and the curve — eight poses round
a circle staying on the circle between the poses, the curve measuring longer
than the straight legs through the same points, a track that leaves the plane
writing a `vz`, enough keyframes emitted to be a curve rather than a polyline,
each emitted key landing where the curve says, and the last one ending where
the curve ends.

## Current UI revision — 2026-08-21

This source revision is UI-only. It does not modify the Fluid Solver numerical
code or either finished solver distribution.

Implemented UI behavior:

- warns before launching when the slice contains multiple disconnected contours;
  the user may cancel or explicitly reduce the run to the largest contour, and
  the preview/adapter are updated to match that choice;
- identifies the selected `Fluid Solver.exe` and displays its version/build;
- distinguishes CPU-only and CUDA-capable solver builds and disables CUDA requests
  when the selected solver cannot provide CUDA;
- renames the process-stop action to **Stop simulation** and confirms before
  closing the application while a simulation is active;
- keeps all numerical controls visible in one scrollable panel, grouped by
  physical, geometry, grid, timestep, multigrid, output, and backend purpose;
- provides integer +/- adjustment, inline invalid-field highlighting, and
  parameter help text;
- displays derived grid/runtime information including `dx`, `dy`, `dz`, cell
  count, Reynolds number, approximate timestep, approximate VTK count, and
  estimated multigrid levels;
- persists the output-root preference and supports Save/Load of `.cfdui`
  configuration files, and recovers a configuration out of a `.vtk` frame's
  `configText`;
- writes UI-owned run metadata with solver identity and requested parameters;
- exposes a configurable decoded-VTK cache budget;
- shows `u`, `v`, `w`, speed, and pressure for result inspection;
- adds result-frame keyboard navigation, playback, series/current-frame range
  selection, and a Run details viewer;
- draws a volume frame in a 3D viewport and a plane of one in the 2D view,
  switched with `V`;
- retains VTK restart parsing infrastructure but does not expose unsupported
  restart/continuation launch arguments.

The two lines that used to close this list — that CLI compatibility was the
24-key contract and that gravity and continuation keys were not emitted — have
been untrue for several branches and are gone rather than reworded. What the UI
emits is decided per block by looking the key up in the selected executable's
own parameter table, which is the table under *Solver integration*, and `nz` is
the newest row in it.

## Validation status of this source revision

Performed in the available Linux validation environment:

- Full Linux Release link of `Fluid Solver UI`, with and without AVX2/OpenMP.
- All ten test suites green, `VolumeFrameTests` among them: the volume frame
  layout, an old flat frame still read as an old flat frame, the velocity's
  third component, and a slice matching the cells it was cut from.
- Reader equivalence: the same data written as BINARY and as ASCII decodes to
  bit-identical pressure, solid, velocity, speed, finite masks and ranges,
  including frames carrying NaN and infinity.
- Reader robustness: truncated payload, truncated header, empty file and a solid
  value outside {0,1} all raise `VtkParseError` rather than misbehaving.
- Ranges and speeds checked against independently computed reference values.
- Solver discovery: the extension-less solver is found beside the UI, a
  non-executable one is refused with a message that says so, and a configured
  path is used when nothing sits beside the UI.
- Generated 30-name release matrix compared exactly against the solver
  `release/0.1` archive set: 30/30, no missing or extra rows.
- Linux x64 release rows built and packaged end to end through
  `scripts/build-ui-release.py`.

Not performed here:

- Windows and macOS builds;
- GUI launch and interactive visual verification (the container has no display).

That second line is worth reading twice now that there is a 3D viewport in
here. Everything in this UI that is not pure drawing is covered by the suites
in `tests/`, which link `mask_ui_core` and need no window — the volume reader,
the slicing, the marching cubes, the streamline tracer, the picking arithmetic,
the body track and the configuration file all have tests. What nobody in this
environment has done is *look at it*. The orbit feeling right, the isosurface
being the shape you expected and the colours being legible are not things a
headless container can tell you.
