# Which download do I take?

**Short answer: take the installer.** It reads your machine and picks for you.

```
Windows    Fluid Solver 1.0 windows setup.exe
Linux      Fluid-Solver-1.0-linux.run
macOS      Fluid Solver 1.0 macos.pkg
```

One file per system. Each one contains every build there is and installs only
the one your computer can actually run. Nothing to choose, nothing to look up.

**If you would rather not install anything**, take a portable `.zip` instead —
it is just a folder you unzip and run, and deleting the folder is the uninstall.
But then you pick the build yourself, and picking the wrong one has a real cost.
The rest of this file is how to pick.

---

## Just tell me which zip

Download **any** portable zip with `plain` in the name — that one runs on
everything — unzip it, and run:

```
Fluid Solver --hardware
```

It prints your system, whether you have AVX2, whether you have an NVIDIA card,
and the exact filename to download:

```
This machine
  system     : windows-x64
  cores      : 8
  AVX2       : yes
  NVIDIA GPU : yes

Download this row:

    Fluid Solver 1.0 windows-x64 avx2-omp-cuda
```

Copy that line into the search box on the releases page, or Ctrl+F for it, and
take the `.zip` with that name.

That is the whole procedure. Everything below is for people who want to know
what those words mean.

---

## Reading the filename

```
Fluid Solver 1.0 windows-x64 avx2-omp-cuda.zip
             ^^^ ^^^^^^^^^^^ ^^^^^^^^^^^^^^
          version   system     what is turned on
```

There is one zip per combination, so pick the system first and the accelerators
second. A `-ui` on the end means the same thing plus the desktop application — a
window where you set the run up and watch the frames, instead of typing at a
prompt. Take `-ui` unless you specifically want the console program on its own.

---

## The three words in the filename

### `avx2` — a faster set of CPU instructions

Modern x86 processors can do arithmetic on eight numbers at once instead of one.
That is AVX2, and it is the single biggest speed difference between these
builds.

**You have it if** your Intel is Haswell (2013) or newer, or your AMD is
Excavator (2015) or newer. In practice: any desktop or laptop bought after about
2015. Not on ARM at all — no Apple Silicon Mac, no Snapdragon laptop and no
Raspberry Pi has AVX2, because it is an x86 instruction set.

**This is the one that matters.** An `avx2` build on a CPU without AVX2 does not
run slowly and does not warn you — it dies on the first step with "illegal
instruction", or a Windows crash box. That is what the `plain` builds are for.

**Check it:**

| | |
|---|---|
| any system | run `Fluid Solver --hardware` out of a `plain` build |
| Windows | Task Manager → Performance → CPU gives the model name; look it up, or install CPU-Z, which lists AVX2 under Instructions |
| Linux | `grep -o avx2 /proc/cpuinfo \| head -1` — prints `avx2` if you have it, nothing if you do not |
| macOS Intel | `sysctl -a \| grep AVX2` — a line means yes |
| macOS Apple Silicon | you do not have it, and there are no `avx2` builds for it |

### `omp` — use all your CPU cores

OpenMP spreads the work over every core instead of running on one. On an
eight-core machine that is most of an eightfold speed-up.

**You want this.** Every computer made this century has more than one core.
There is a build without it only because single-core virtual machines and very
restricted containers exist.

**It is safe.** Unlike AVX2, an `omp` build on a one-core machine does not
crash — it just has nothing to spread across.

**Count your cores:** Windows — Task Manager → Performance → CPU → "Logical
processors". Linux — `nproc`. macOS — `sysctl -n hw.ncpu`.

> **Windows only:** the `omp` builds need `vcomp140.dll`, which is in the zip
> next to the executable. Keep the folder together and it works. Copy the .exe
> out on its own and it will not start.

### `cuda` — use an NVIDIA graphics card

The slowest part of this solver is the pressure solve, and a CUDA build does
that part on an NVIDIA GPU. Whether that is faster than your CPU depends on
both: a strong GPU against a weak CPU is a large win, the other way round is
not.

**You need** an NVIDIA card and its driver. AMD and Intel graphics do not work
here, and no Mac does — macOS has had no CUDA since 10.14.

**It is safe.** A CUDA build on a machine with no NVIDIA card does not fail: it
says so at startup and does the pressure solve on the CPU. So `cuda` costs you
nothing if you are not sure.

**Check it:** run `nvidia-smi` in a terminal or Command Prompt. A table
describing your card means yes; "command not found" means no driver.

### `plain` — none of the above

Runs on anything. Slower, and by a lot — but this is the one to take when a
faster build crashed, when you have no idea what is in your machine, or when you
want something with no dependencies at all.

---

## Which system am I?

| you have | take |
|---|---|
| a normal Windows PC or laptop | `windows-x64` |
| Windows on a Snapdragon or other ARM laptop | `windows-arm64` |
| very old 32-bit Windows | `windows-x86` |
| a normal Linux PC | `linux-x64` |
| Linux on a Raspberry Pi 4/5 or an ARM server | `linux-arm64` |
| 32-bit Linux | `linux-x86` |
| a Mac with an M1, M2, M3 or M4 | `macos-arm64` |
| a Mac from 2020 or earlier with an Intel chip | `macos-x64` |

Windows: Settings → System → About → "System type". Mac: Apple menu → About This
Mac; "Apple M…" is `arm64`, "Intel" is `x64`.

---

## Not every combination exists

If you cannot find the name you expected, this is why:

- **No `avx2` on any ARM row** — AVX2 is an x86 instruction set.
- **No `cuda` on 32-bit anything** — there has been no 32-bit CUDA since CUDA 9.
- **No `cuda` on Windows ARM** — NVIDIA ships no toolkit for it.
- **No `cuda` on macOS** — none since 10.14.

So `macos-arm64` has exactly two builds, `omp` and `plain`, and that is not an
oversight.

---

## After you download

- **Windows** shows "Windows protected your PC" on an unsigned download. More
  info → Run anyway. Check the file against `SHA256SUMS.txt` first if you want
  to be careful.
- **macOS** refuses to open an unsigned package on a double-click. Right-click
  it → Open, then confirm.
- **Linux** `.run` installer: `chmod +x Fluid-Solver-1.0-linux.run`, then run
  it. `--help` lists its options.

Portable zips write their frames into the `output/` folder next to the
executable. `Fluid Solver --help` lists everything it accepts; the README
explains what each setting does.

---

## Can I change my mind afterwards?

Yes, within what you downloaded. `Fluid Solver --settings` turns AVX2, OpenMP
and CUDA on and off and remembers the choice for every later run. It cannot add
something that was never compiled in, which is why the filename still matters.

Turning one off changes how long a run takes, not what it solves.
