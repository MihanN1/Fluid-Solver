#include "Config.hpp"
#include "Mesh.hpp"
#include "Progress.hpp"
#include "Restart.hpp"
#include "Runtime.hpp"
#include "Solver.hpp"
#include "SolverCompressible.hpp"
#include "UpdateCheck.hpp"
#include "Version.hpp"
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif
#include <chrono>

static void printUsage(const char* exe) {
    std::cout <<
        "Usage:\n"
        "  " << exe << "                       interactive configuration\n"
        "  " << exe << " key=value [key=value] non-interactive run\n"
        "  " << exe << " --settings             change AVX2/OpenMP/CUDA and exit\n"
        "  " << exe << " --hardware             what this machine can run, and\n"
        "                                       which download to take\n"
        "  " << exe << " --check-updates        ask GitHub for a newer release\n"
        "\n"
        "Keys: Lx Ly Lz nx ny nz U0 nu CFL totalTime dtUpdateInterval dtSafety\n"
        "      omega smootherOmega mgIterations mgTolerance mgMinCoarseSize\n"
        "      saveInterval outputDir geometryFile sliceAngleX sliceAngleY\n"
        "      sliceAngleZ sliceRotation invertSection ro useCuda restart\n"
        "      restartFile addTime gravityEnabled gravityAccel gravityAngle\n"
        "      gravityTilt phaseZ wallMotion bcFront bcBack bcFrontSpeed\n"
        "      bcBackSpeed inletFrom2 inletTo2\n"
        "\n"
        "nz is 1 by default, which is the plane case every earlier version\n"
        "solved. nz>1 turns the same run into a volume: every feature - phases,\n"
        "gravity, turbulence, moving bodies, the compressible solver, AMR -\n"
        "works there, and Lz says how deep the box is.\n"
        "\n"
        "Rules:\n"
        "  key=value, no spaces around '='  nx=256      not  nx = 256\n"
        "  keys are case insensitive        NX=256, --nx=256 also work\n"
        "  decimal separator is a dot       nu=0.002    not  nu=0,002\n"
        "  switches take 1 or 0             useCuda=1   (true/false, yes/no too)\n"
        "  no units inside the value        totalTime=2 not  totalTime=2s\n"
        "  quote paths with spaces          \"geometryFile=C:\\my models\\a.stl\"\n"
        "  wallMotion has a grammar of its own:\n"
        "        <object>:<setting>=<value>,<setting>=<value>;<next object>:...\n"
        "        settings are rotX/rotY/rotZ=<deg/s>, slideX/slideY/slideZ=\n"
        "        <m/s>, slip=1 (rot= is rotZ=)\n"
        "        \"wallMotion=1:rot=90,slideX=0.5;2:slip=1\"\n"
        "        an object either moves (rot/slide) or slips, never both\n"
        "\n"
        "Acceleration, for this run only (the remembered defaults come from\n"
        "settings.ini, which --settings writes):\n"
        "      avx2=0|1 openmp=0|1 useCuda=0|1 threads=N tray=0|1\n"
        "\n"
        "Example:\n"
        "  " << exe << " nx=256 ny=128 Lx=2 Ly=1 U0=1 nu=0.002 "
                       "totalTime=2 saveInterval=25\n"
        "\n"
        "Continuing a run (restartFile takes a frame or the folder holding\n"
        "them; anything given after it overrides the stored configuration):\n"
        "  " << exe << " restart=1 restartFile=output totalTime=30 addTime=10\n"
                       "saveInterval=5\n";
}

// argv arrives already split by the shell, so "nx 256" shows up as a bare
// "nx" and a bare "256". Say which of the two happened instead of one
// "Malformed argument" for everything.
static std::string describeMalformed(const std::string& arg) {
    if (arg.empty())
        return "not a right way to write an empty argument: every argument is "
               "key=value, e.g. nx=256.";
    if (arg.front() == '=')
        return "not a right way to write '" + arg +
               "': the key in front of '=' is missing, e.g. nx=256.";

    const std::string known = Config::canonicalKey(arg);
    if (!known.empty())
        return "not a right way to write '" + arg + "': " + known +
               " needs its value glued to it, with no spaces around '='. "
               "Write " + known + "=<value>.";

    const std::string guess = Config::suggestKey(arg);
    if (!guess.empty())
        return "not a right way to write '" + arg + "': arguments are key=value "
               "with no spaces. Did you mean " + guess + "=<value>?";

    return "not a right way to write '" + arg + "': arguments are key=value "
           "with no spaces, e.g. nx=256. Run with --help for the list of keys.";
}

// The three accelerators can be steered per run without touching the file the
// choice is remembered in. Handled before Config sees the argument, because
// only "useCuda" is a simulation parameter - the other three are not.
static bool applyRuntimeArg(const std::string& rawKey, const std::string& value) {
    std::string key = rawKey;
    while (!key.empty() && (key.front() == '-' || key.front() == '/'))
        key.erase(key.begin());
    for (char& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    const bool on = std::atoi(value.c_str()) != 0;
    if (key == "avx2")          { runtime::mutableSettings().useAvx2 = on; return true; }
    if (key == "openmp" || key == "omp")
                                { runtime::mutableSettings().useOpenMp = on; return true; }
    if (key == "tray")          { runtime::mutableSettings().tray = on; return true; }
    if (key == "threads")       {
        const int count = std::atoi(value.c_str());
        runtime::mutableSettings().threads = (count > 0) ? count : 0;
        return true;
    }
    return false;
}

// Referenced through a volatile pointer so no compiler decides the strings are
// unused and drops them: the UI finds this build's version and feature set by
// searching the executable's bytes for them, without having to run it.
namespace {
const char* const kBuildMarkers[] = {CFD_VERSION_MARKER, CFD_FEATURES_MARKER};
const char* const* const kBuildMarkersKeepAlive = kBuildMarkers;
}   // namespace

static void warnAboutScheme(const Config& cfg) {
    if (cfg.convection == ConvectionScheme::Upwind ||
        cfg.timeScheme != TimeScheme::Euler)
        return;
    std::cout << "\n!!! convection is second order while timeScheme is euler. "
                 "That pair is only\n    conditionally stable and usually is "
                 "not: use timeScheme=rk2 or rk3, or\n    drop back to "
                 "convection=upwind.\n\n";
}

#ifdef _WIN32
namespace {

// A narrow main() is handed its arguments already squeezed through the ANSI
// code page, and what that page cannot spell is lost before the program has
// run a line. A path under "C:\Users\...\Файлы" on a machine whose console is
// 866 came back as the bytes of one name read as another, so the run created
// that folder and wrote every frame into it, while the caller looked in the
// folder it had asked for and found nothing.
//
// The wide command line is what Windows actually holds, so it is read straight
// and handed on as UTF-8. usePathsAsUtf8 then tells the path conversion to
// stop guessing, because there is no longer anything to guess about.
class Utf8Arguments {
public:
    Utf8Arguments(int argc, char** argv) {
        int count = 0;
        LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (wide != nullptr && count > 0) {
            for (int index = 0; index < count; ++index) {
                const int bytes = WideCharToMultiByte(
                    CP_UTF8, 0, wide[index], -1, nullptr, 0, nullptr, nullptr);
                std::string text;
                if (bytes > 0) {
                    text.assign(static_cast<size_t>(bytes), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wide[index], -1, &text[0],
                                        bytes, nullptr, nullptr);
                    text.pop_back();
                }
                storage_.push_back(std::move(text));
            }
            usePathsAsUtf8();
        } else {
            for (int index = 0; index < argc; ++index)
                storage_.emplace_back(argv[index]);
        }
        if (wide != nullptr)
            LocalFree(wide);
        pointers_.reserve(storage_.size() + 1);
        for (std::string& text : storage_)
            pointers_.push_back(&text[0]);
        pointers_.push_back(nullptr);
    }

    int count() const { return static_cast<int>(storage_.size()); }
    char** values() { return pointers_.data(); }

private:
    std::vector<std::string> storage_;
    std::vector<char*> pointers_;
};

} // namespace
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    Utf8Arguments arguments(argc, argv);
    argc = arguments.count();
    argv = arguments.values();
#endif
    (void)kBuildMarkersKeepAlive;
    std::cout << "=== Fluid Solver " << CFD_RELEASE_VERSION << " ("
              << CFD_BUILD_FEATURES << ") ===\n\n";

    // Read before anything asks a question, so the answers the user gave last
    // time are already in place - and so --settings has something to edit.
    runtime::load();

    Config cfg;
    // The remembered choice is the default; "useCuda=" on the command line and
    // the interactive prompt both still override it, and a build with no CUDA
    // in it cannot be talked into having some.
    cfg.useCuda = runtime::cudaEnabled();
    std::vector<std::pair<std::string, std::string>> overrides;

    if (argc > 1) {
        // --help wins wherever it sits on the line, even behind a broken
        // argument, which is exactly when it tends to be needed.
        for (int a = 1; a < argc; ++a) {
            const std::string arg = argv[a];
            if (arg == "-h" || arg == "--help" || arg == "/?") {
                printUsage(argv[0]);
                return 0;
            }
            if (arg == "--version" || arg == "-v") {
                // Two machine-readable lines first, then the sentence. The UI
                // reads the same two strings straight out of the file, so what
                // is printed here and what is found there cannot disagree.
                std::cout << CFD_VERSION_MARKER << "\n"
                          << CFD_FEATURES_MARKER << "\n"
                          << CFD_APP_NAME << " " << CFD_RELEASE_VERSION
                          << " (build " << CFD_APP_VERSION << ", "
                          << CFD_BUILD_FEATURES << ")\n";
                return 0;
            }
            if (arg == "--settings" || arg == "--accel") {
                runtime::configureInteractively();
                return 0;
            }
            if (arg == "--hardware") {
                std::cout << runtime::hardwareReport();
                return 0;
            }
            if (arg == "--check-updates") {
                const update::Result result = update::check();
                if (!result.checked) {
                    std::cout << "Could not check: " << result.error << "\n";
                    return 1;
                }
                std::cout << "This build: " << CFD_RELEASE_VERSION
                          << "\nNewest published: " << result.latest << "\n"
                          << (result.newer ? result.url
                                           : std::string("Already up to date."))
                          << "\n";
                return 0;
            }
        }

        // Every bad argument is collected, so one run reports all of them
        // instead of one per attempt.
        std::vector<std::string> problems;
        bool wallMotionRefused = false;
        bool vorticesRefused = false;
        bool sourcesRefused = false;

        for (int a = 1; a < argc; ++a) {
            const std::string arg = argv[a];
            const size_t eq = arg.find('=');
            if (eq == std::string::npos || eq == 0) {
                problems.push_back(describeMalformed(arg));
                continue;
            }
            const std::string key = arg.substr(0, eq);
            const std::string value = arg.substr(eq + 1);

            if (applyRuntimeArg(key, value))
                continue;

            std::string error, warning;
            if (!cfg.setParam(key, value, error, &warning)) {
                problems.push_back(error);
                const std::string canon = Config::canonicalKey(key);
                if (canon == "wallMotion")
                    wallMotionRefused = true;
                else if (canon == "vortices")
                    vorticesRefused = true;
                else if (canon == "sources")
                    sourcesRefused = true;
                continue;
            }
            if (!warning.empty())
                std::cout << "Warning: " << warning << "\n";
            overrides.emplace_back(key, value);
        }

        if (!problems.empty()) {
            std::cerr << "\n" << problems.size()
                      << (problems.size() == 1 ? " argument is wrong:\n"
                                               : " arguments are wrong:\n");
            for (const std::string& problem : problems)
                std::cerr << "  " << problem << "\n";
            std::cerr << "\nNothing has been started. Fix the line and run it "
                         "again.\n\n";
            printUsage(argv[0]);
            // The keys with a grammar the usage block cannot hold in a single
            // line, so they get their own explanation when they are the one
            // that failed.
            if (wallMotionRefused)
                std::cout << wallMotionHelp();
            if (vorticesRefused)
                std::cout << vorticesHelp();
            if (sourcesRefused)
                std::cout << sourcesHelp();
            return 1;
        }
        runtime::apply();
        update::runStartupCheck(false);
        std::cout << "Acceleration:\n" << runtime::summary();
        warnAboutScheme(cfg);
        cfg.print();
    } else {
        runtime::apply();
        update::runStartupCheck(true);

        std::cout << "Acceleration (remembered from last time):\n"
                  << runtime::summary()
                  << "Change it? [y/N] ";
        std::cout.flush();
        std::string answer;
        if (std::getline(std::cin, answer) && !answer.empty() &&
            (answer[0] == 'y' || answer[0] == 'Y')) {
            runtime::configureInteractively();
            cfg.useCuda = runtime::cudaEnabled();
        }
        std::cout << "\n";

        cfg.readFromConsole();
        warnAboutScheme(cfg);

        if (!cfg.restart) {
            while (!cfg.confirm()) {
            }

            std::cout << "\n--- Final Configuration ---\n";
            cfg.print();

            std::cout << "\nNote: This version supports STL and OBJ models.\n";
            std::cout << "      The mask is generated from a central plane section of the model.\n";
            std::cout << "      Slice angles, in-plane rotation, and optional mirroring are applied.\n";
            std::cout << "      Enter 'none' to use the circle verification geometry.\n";

            std::cout << "      Total simulation time: " << cfg.totalTime << " s.\n";
        }

        // The configuration asks about CUDA as well, and that answer is the
        // later of the two, so it becomes the remembered default rather than
        // being forgotten the moment this run ends. Only here: a "useCuda=" on
        // the command line is an override for one run and has no business
        // rewriting a file.
        if (runtime::builtWithCuda() &&
            cfg.useCuda != runtime::settings().useCuda) {
            runtime::mutableSettings().useCuda = cfg.useCuda;
            runtime::apply();
            runtime::save();
        }
    }

    RestartData restart;
    std::filesystem::path restartPath;

    if (cfg.restart) {
        std::string error;
        restartPath = resolveRestartPath(cfg.restartFile, error);
        if (restartPath.empty() || !loadRestart(restartPath, restart, error)) {
            std::cerr << "Cannot continue: " << error << "\n";
            return 1;
        }
        std::cout << "\nContinuing from " << pathToConsole(restartPath) << "\n";

        const std::string requested = cfg.restartFile;
        cfg = restart.cfg;
        cfg.restart = true;
        cfg.restartFile = requested;
        for (const auto& override : overrides)
            cfg.setParam(override.first, override.second);
        const auto applyAddTime = [&]() {
            if (cfg.addTime > 0.0) {
                cfg.totalTime = restart.currentTime + cfg.addTime;
                std::cout << "addTime " << cfg.addTime
                          << " s -> totalTime " << cfg.totalTime << " s\n";
            } else if (cfg.addTime < 0.0) {
                std::cout << "\n!!! addTime " << cfg.addTime
                          << " s is negative and does nothing. It counts "
                             "forward from the\n    time this frame stopped at ("
                          << restart.currentTime
                          << " s). To stop earlier, set totalTime itself,\n"
                             "    and it still has to be past "
                          << restart.currentTime << " s.\n";
                cfg.addTime = 0.0;
            }
        };
        applyAddTime();
        const auto validate = [&](std::string& reason) {
            std::ostringstream why;
            if (cfg.nx != restart.nx || cfg.ny != restart.ny) {
                why << "nx and ny cannot change on a continuation ("
                    << restart.nx << "x" << restart.ny << " in the frame).";
            } else if (std::fabs(cfg.Lx - restart.cfg.Lx) >
                           1e-6f * restart.cfg.Lx ||
                       std::fabs(cfg.Ly - restart.cfg.Ly) >
                           1e-6f * restart.cfg.Ly) {
                why << "Lx and Ly cannot change on a continuation ("
                    << restart.cfg.Lx << " x " << restart.cfg.Ly
                    << " in the frame).";
            } else if (cfg.totalTime <= restart.currentTime) {
                why << "totalTime (" << cfg.totalTime
                    << " s) is not past the time this frame already reached ("
                    << restart.currentTime << " s), there would be nothing to "
                       "compute. Type 'totalTime' and give it more.";
            } else {
                return true;
            }
            reason = why.str();
            return false;
        };

        std::string reason;
        if (argc <= 1) {
            std::cout << "\nThe configuration below came out of that frame. "
                         "Change whatever you want\n"
                         "(totalTime and saveInterval are the usual ones), "
                         "then press Enter to start.\n"
                         "The grid and the geometry are fixed by the frame "
                         "and cannot be changed.\n";

            for (;;) {
                while (!cfg.confirm()) {
                }
                applyAddTime();
                if (validate(reason))
                    break;
                std::cout << "\n!!! " << reason << "\n";
            }
        } else if (!validate(reason)) {
            std::cerr << reason << "\n";
            return 1;
        }
        cfg.print();

        std::cout << "\nNote: the solid mask is taken from the frame, so the "
                     "geometry parameters\n"
                     "      above are only informational and the model file "
                     "is not needed.\n";
    }

    {
        std::string regimeError;
        if (!cfg.regimeConsistent(regimeError)) {
            std::cerr << "\n!!! " << regimeError << "\n\nNothing has been "
                         "started.\n";
            return 1;
        }
    }

    if (cfg.nx < 8 || cfg.ny < 8) {
        std::cerr << "nx and ny must be at least 8.\n";
        return 1;
    }

    const bool rebuildForBodies = cfg.restart && cfg.bodiesMove();
    Mesh mesh(cfg,
              (cfg.restart && !rebuildForBodies) ? &restart.solid : nullptr);
    if (!mesh.valid()) {
        std::cerr << "\n!!! " << mesh.error() << "\n\nNothing has been "
                     "started.\n";
        return 1;
    }
    mesh.printInfo();

    double sourceInflow = 0.0;
    for (const FlowSource& source : cfg.resolvedSources())
        sourceInflow += static_cast<double>(source.rate) * 2.0 *
                        3.14159265358979 * source.radius;

    std::string balanceError;
    if (!checkBoundaryMassBalance(cfg.boundaries, cfg.U0,
                                  DomainExtent{cfg.Lx, cfg.Ly, cfg.Lz,
                                               cfg.nx, cfg.ny, cfg.nz},
                                  mesh.solid, balanceError, sourceInflow)) {
        std::cerr << "\n!!! " << balanceError << "\n\nNothing has been "
                     "started.\n";
        return 1;
    }

    const auto startTime = std::chrono::steady_clock::now();

    if (cfg.compressible()) {
        CompressibleRun solver(cfg, mesh);
        if (cfg.restart &&
            !solver.setInitialState(std::move(restart),
                                    restartPath.stem().string())) {
            std::cerr << "That frame carries no compressible state, so this "
                         "run has nothing to continue from.\n";
            return 1;
        }
        solver.run();
    } else {
        Solver solver(cfg, mesh);
        if (cfg.restart &&
            !solver.setInitialState(std::move(restart),
                                    restartPath.stem().string())) {
            std::cerr << "The state in the frame does not match the grid.\n";
            return 1;
        }
        solver.run();
    }

    const auto endTime = std::chrono::steady_clock::now();

    const double seconds =
        std::chrono::duration<double>(endTime - startTime).count();
    std::cout << "Wall clock: " << seconds << " s\n";

    // Takes the tray icon down and clears the taskbar progress. Before the
    // "press Enter" below, or the icon sits there for as long as the window
    // stays open with nothing behind it.
    progress::shutdown();

    if (argc <= 1) {
        std::cout << "\nSimulation complete. Press Enter to exit...";
        std::cin.get();
    }
    return 0;
}