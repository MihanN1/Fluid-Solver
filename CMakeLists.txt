cmake_minimum_required(VERSION 3.28)

project(Fluid-Solver
    VERSION 1.0.0
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(FetchContent)
include(CheckLanguage)

option(CFD_ENABLE_OPENMP "Enable OpenMP parallel loops when available" ON)
option(CFD_ENABLE_AVX2 "Compile CPU C++ sources with AVX2 support" ON)
option(CFD_ENABLE_CUDA "Enable the CUDA multigrid backend" ON)
option(CFD_ENABLE_CUDA_EXPLICIT "Treat a missing CUDA toolkit as a hard error" OFF)
option(CFD_ENABLE_OPENMP_EXPLICIT "Treat a missing OpenMP runtime as a hard error" OFF)

# AppleClang carries no OpenMP runtime of its own, so libomp has to be pointed
# at: Homebrew's prefix for a native build, an unpacked bottle for the other
# architecture when one Mac builds both. The prefix must hold include/omp.h and
# lib/libomp.a or lib/libomp.dylib. Empty means "let find_package look", which
# on macOS finds nothing and produces an OpenMP-less binary under an OpenMP
# name, so the release script never leaves it empty for an OpenMP row.
set(CFD_OPENMP_ROOT "" CACHE PATH
    "Prefix of the libomp to build against (macOS)"
)

set(CFD_APP_NAME "Fluid Solver" CACHE STRING
    "File name of the produced executable, without the extension"
)

# Which repository the update check asks about. Split in two because the API
# host and the web host spell the same thing differently.
set(CFD_REPO_OWNER "MihanN1" CACHE STRING "GitHub owner of the release repo")
set(CFD_REPO_NAME "Fluid-Solver" CACHE STRING "GitHub name of the release repo")

# What the release is called - "0.2", not "0.2.0". Archive names, the release
# folder and the git tag all use the short form, and the update check has to
# compare against the same thing it would find on GitHub.
set(CFD_RELEASE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    CACHE STRING "Version string used for release artefacts and update checks")

# Release builds ship as one file. What that costs differs per platform, and
# getting it wrong is quiet rather than loud, so it lives here instead of in
# whatever command line happened to produce a given binary:
#   MSVC       static CRT (/MT). The OpenMP runtime has no static form, so an
#              OpenMP build still needs libomp140.<arch>.dll beside it.
#   GCC/Clang  -static, plus libgomp.a rather than the libgomp.so that CMake's
#              OpenMP::OpenMP_CXX target otherwise hands the linker.
#   with CUDA  no full -static: libcudart_static needs dlopen and pthread, and
#              the driver is dynamic by definition. The C++ runtime is still
#              linked in, which leaves libc, libm and libcuda.so.1.
#   macOS      there is no static libSystem, so this only means no third-party
#              dylibs. That is as static as an Apple binary is allowed to be.
option(CFD_STATIC "Link the runtime into the executable" ON)

if(CFD_STATIC AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(
    CFD_RUNTIME_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/bin"
    CACHE PATH "Root directory for the generated cfd_app executable"
)

# A released binary has to run on more than the machine that built it. CMake
# emits SASS for each of these plus PTX for the last, so newer cards JIT and
# work; without the older entries nothing before Turing runs at all. CUDA 13
# dropped Maxwell, Pascal and Volta, so building the release rows against
# CUDA 12.x is what keeps 50..70 reachable - a 13.x toolkit will refuse them
# and the list has to be trimmed to 75 and up.
set(CFD_CUDA_ARCHITECTURES "50;60;61;70;75;80;86;89;90" CACHE STRING
    "CUDA architectures to generate code for."
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CFD_ENABLE_CUDA)
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES "${CFD_CUDA_ARCHITECTURES}" CACHE STRING
            "CUDA architectures used by all CUDA targets."
        )
    endif()

    check_language(CUDA)
    if(NOT CMAKE_CUDA_COMPILER)
        # CFD_ENABLE_CUDA defaults to ON, so a machine without a toolkit used to
        # fail to configure at all. Fall back to the CPU backend instead and say
        # so; only an explicit request is treated as an error.
        if(CFD_ENABLE_CUDA_EXPLICIT)
            message(FATAL_ERROR
                "CUDA compiler detection failed while CFD_ENABLE_CUDA=ON.\n"
                "CUDA_PATH='$ENV{CUDA_PATH}'"
            )
        endif()
        message(WARNING
            "No CUDA compiler found; building the CPU backend only. "
            "Install the CUDA Toolkit (with the Visual Studio Integration "
            "component on Windows) and reconfigure in a fresh build directory "
            "to enable the GPU path. CUDA_PATH='$ENV{CUDA_PATH}'"
        )
        set(CFD_ENABLE_CUDA OFF)
    endif()
endif()

if(CFD_ENABLE_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)

    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_EXTENSIONS OFF)

    if(MSVC AND MSVC_VERSION GREATER_EQUAL 1950 AND CUDAToolkit_VERSION VERSION_LESS 13.2)
        message(FATAL_ERROR
            "Visual Studio 2026 requires CUDA Toolkit 13.2 or newer. "
            "Detected CUDA ${CUDAToolkit_VERSION}."
        )
    endif()
endif()

set(CFD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(CFD_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(CFD_MODELS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/models")
set(CFD_STL_READER_DIR "${CFD_LIB_DIR}/stl_reader")
set(CFD_TINYOBJ_HEADER "${CFD_INCLUDE_DIR}/tiny_obj_loader.h")

# Keep generated files outside the source tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CFD_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CFD_RUNTIME_OUTPUT_DIRECTORY}/${config}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib/${config}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib/${config}")
endforeach()

# tinyobjloader is a header-only dependency in this project.
if(NOT EXISTS "${CFD_TINYOBJ_HEADER}")
    message(FATAL_ERROR
        "Missing dependency: ${CFD_TINYOBJ_HEADER}\n"
        "Restore include/tiny_obj_loader.h before configuring the project."
    )
endif()

add_library(cfd_tinyobj INTERFACE)
target_include_directories(cfd_tinyobj INTERFACE "${CFD_INCLUDE_DIR}")

# stl_reader is header-only. Prefer the copy included in the project.
if(EXISTS "${CFD_STL_READER_DIR}/include/stl_reader/stl_reader.h")
    add_library(cfd_stl_reader INTERFACE)
    target_include_directories(cfd_stl_reader INTERFACE "${CFD_STL_READER_DIR}/include")
else()
    FetchContent_Declare(
        stl_reader
        URL https://github.com/sreiter/stl_reader/archive/refs/tags/v2.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(stl_reader)
    add_library(cfd_stl_reader ALIAS stl_reader)
endif()

# Everything except main() lives in a library so the tests can link the same
# objects the executable runs. Nothing else about the build changes: every
# setting below is PUBLIC on cfd_core and reaches cfd_app through the link.
set(CFD_CORE_SOURCES
    src/tiny_obj_loader_impl.cpp
    src/AppPaths.cpp
    src/Boundary.cpp
    src/Config.cpp
    src/Phase.cpp
    src/RigidBody.cpp
    src/Turbulence.cpp
    src/AmrHierarchy.cpp
    src/AmrDriver.cpp
    src/SolverCompressible.cpp
    src/Restart.cpp
    src/Mesh.cpp
    src/Solver.cpp
    src/Multigrid.cpp
    src/Runtime.cpp
    src/UpdateCheck.cpp
    src/Progress.cpp
)

if(CFD_ENABLE_CUDA)
    list(APPEND CFD_CORE_SOURCES src/MultigridCuda.cu)
    list(APPEND CFD_CORE_SOURCES src/SolverCompressibleCuda.cu)
endif()

set(CFD_SOURCES src/main.cpp)

# ---- what this binary knows about itself ------------------------------------
# The version, the repository the update check asks about, and which of the
# three accelerators were actually compiled in. Generated rather than written
# by hand, because a literal pasted into the resource script, the update check
# and the tray tooltip is a literal that drifts.
# CFD_ENABLE_CUDA has already been turned off above if no toolkit was found, so
# these two are final. OpenMP is settled further down, after find_package has
# had its say, and the feature string is composed there from all three.
set(CFD_BUILT_WITH_AVX2 ${CFD_ENABLE_AVX2})
set(CFD_BUILT_WITH_CUDA ${CFD_ENABLE_CUDA})
set(CFD_BUILT_WITH_OPENMP ${CFD_ENABLE_OPENMP})

set(CFD_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# The icon and the version block live in a resource script, which is a Windows
# concept only. Elsewhere the icon belongs to the .desktop file or the app
# bundle instead, so there is nothing to compile in.
option(CFD_ENABLE_ICON "Embed the icon and version block on Windows" ON)

if(WIN32 AND CFD_ENABLE_ICON)
    enable_language(RC)
    set(CFD_ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/logo/fluid-solver.ico")
    if(EXISTS "${CFD_ICON_SOURCE}")
        # rc.exe reads the .rc file in the system ANSI code page, not UTF-8,
        # so an absolute path with non-ASCII characters anywhere in it - and
        # this project lives under one - arrives as mojibake and the icon
        # "does not exist". Nothing in the .rc may be non-ASCII. So the icon is
        # copied next to the generated .rc and referenced by a bare name, which
        # rc.exe resolves relative to the .rc itself.
        file(COPY "${CFD_ICON_SOURCE}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
        set(CFD_ICON_PATH "fluid-solver.ico")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
            @ONLY
        )
        list(APPEND CFD_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    else()
        message(WARNING
            "logo/fluid-solver.ico is missing; the executable gets the default "
            "Windows icon and no version information."
        )
    endif()
endif()

add_library(cfd_core STATIC ${CFD_CORE_SOURCES})
add_executable(cfd_app ${CFD_SOURCES})
target_link_libraries(cfd_app PRIVATE cfd_core)

add_custom_command(
    TARGET cfd_app
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E echo
        "Built executable: $<TARGET_FILE:cfd_app>"
    VERBATIM
)

set_target_properties(cfd_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

set_target_properties(cfd_app PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    # The target keeps its old name so the GUI project, which looks for
    # cfd_app, still resolves; only the file on disk is renamed. There will be
    # other solvers next to this one, and "cfd_app" says nothing about which.
    OUTPUT_NAME "${CFD_APP_NAME}"
)

if(CFD_ENABLE_CUDA)
    set_target_properties(cfd_core PROPERTIES
        CUDA_STANDARD 17
        CUDA_STANDARD_REQUIRED ON
        CUDA_EXTENSIONS OFF
        CUDA_ARCHITECTURES "${CFD_CUDA_ARCHITECTURES}"
    )
endif()

target_include_directories(cfd_core PUBLIC
    "${CFD_INCLUDE_DIR}"
    "${CFD_GENERATED_DIR}"
)

target_link_libraries(cfd_core
    PUBLIC
        cfd_tinyobj
        cfd_stl_reader
)

if(CFD_ENABLE_CUDA)
    if(CFD_STATIC)
        target_link_libraries(cfd_core PUBLIC CUDA::cudart_static)
    else()
        target_link_libraries(cfd_core PUBLIC CUDA::cudart)
    endif()
endif()

if(WIN32)
    # winhttp  the update check
    # shell32  the tray icon, and opening the release page in a browser
    # ole32    ITaskbarList3, which draws the progress on the taskbar button
    # user32   the hidden window the tray icon posts its clicks to
    target_link_libraries(cfd_core PUBLIC winhttp shell32 ole32 user32)
endif()

if(NOT WIN32)
    # The tray thread and, on Linux, the OpenMP runtime.
    find_package(Threads REQUIRED)

    set(CFD_WHOLE_PTHREAD OFF)
    if(CFD_STATIC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
       AND NOT APPLE AND NOT CFD_ENABLE_CUDA)
        set(CFD_WHOLE_PTHREAD ON)
    endif()

    if(CFD_WHOLE_PTHREAD)
        target_compile_options(cfd_core PUBLIC -pthread)
    else()
        target_link_libraries(cfd_core PUBLIC Threads::Threads)
    endif()
endif()

target_compile_definitions(cfd_core
    PUBLIC
        CFD_MODELS_DIR="${CFD_MODELS_DIR}"
        NOMINMAX
)

if(CFD_ENABLE_CUDA)
    target_compile_definitions(cfd_core PUBLIC USE_CUDA=1)
endif()

# CUDA 13.x CCCL requires MSVC's standards-conforming preprocessor.
# For .cu files, nvcc must forward the option to its MSVC host compiler.
if(MSVC)
    target_compile_options(cfd_core
        PUBLIC
            $<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>
    )
    if(CFD_ENABLE_CUDA)
        target_compile_options(cfd_core
            PUBLIC
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:preprocessor>
        )
    endif()
endif()

if(CFD_ENABLE_OPENMP)
    # CMake's OpenMP::OpenMP_CXX records the path of the *shared* libgomp, and
    # -static then dies with "attempted static link of dynamic object". Point
    # the search at the archive before find_package runs, and the imported
    # target comes out static without anything downstream noticing.
    if(CFD_STATIC AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        separate_arguments(CFD_LIBGOMP_QUERY NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" ${CFD_LIBGOMP_QUERY} -print-file-name=libgomp.a
            OUTPUT_VARIABLE CFD_LIBGOMP_A
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(EXISTS "${CFD_LIBGOMP_A}")
            set(OpenMP_CXX_FLAGS "-fopenmp")
            set(OpenMP_CXX_LIB_NAMES "gomp")
            set(OpenMP_gomp_LIBRARY "${CFD_LIBGOMP_A}")
            message(STATUS "OpenMP: static ${CFD_LIBGOMP_A}")
        endif()
    endif()

    # Same idea on macOS, one level further down: there is no OpenMP in the
    # compiler at all, so the flags, the library name and the library itself are
    # all supplied here and find_package only verifies them. The static archive
    # wins when it is there, or the binary stops working the moment the machine
    # running it has no Homebrew.
    if(APPLE AND CFD_OPENMP_ROOT AND EXISTS "${CFD_OPENMP_ROOT}/include/omp.h")
        if(CFD_STATIC AND EXISTS "${CFD_OPENMP_ROOT}/lib/libomp.a")
            set(OpenMP_omp_LIBRARY "${CFD_OPENMP_ROOT}/lib/libomp.a")
        elseif(EXISTS "${CFD_OPENMP_ROOT}/lib/libomp.dylib")
            set(OpenMP_omp_LIBRARY "${CFD_OPENMP_ROOT}/lib/libomp.dylib")
        endif()
        if(OpenMP_omp_LIBRARY)
            set(OpenMP_CXX_FLAGS "-Xclang -fopenmp -I${CFD_OPENMP_ROOT}/include")
            set(OpenMP_CXX_LIB_NAMES "omp")
            message(STATUS "OpenMP: ${OpenMP_omp_LIBRARY}")
        endif()
    endif()

    # MSVC's classic /openmp is OpenMP 2.0 and refuses the collapse clause the
    # volumetric loops are written with, so the Windows build needs the LLVM
    # runtime instead. It is supplied the same way as the two above: the flag
    # and an empty library list, so find_package only verifies that the
    # compiler takes it. A toolchain too old for /openmp:llvm fails that check
    # and the build says OpenMP was not found rather than dying in a kernel.
    if(MSVC AND NOT OpenMP_CXX_FLAGS)
        set(OpenMP_CXX_FLAGS "/openmp:llvm")
        set(OpenMP_CXX_LIB_NAMES "")
        message(STATUS "OpenMP: /openmp:llvm")
    endif()

    find_package(OpenMP QUIET COMPONENTS CXX)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(cfd_core PUBLIC OpenMP::OpenMP_CXX)
        message(STATUS "OpenMP: enabled")
    else()
        # Same reason as the CUDA one: silently dropping OpenMP here would
        # publish a binary named for a feature it does not have.
        if(CFD_ENABLE_OPENMP_EXPLICIT)
            message(FATAL_ERROR
                "OpenMP was requested with CFD_ENABLE_OPENMP_EXPLICIT=ON but no "
                "runtime was found. On macOS install libomp (brew install libomp) "
                "and pass -DCFD_OPENMP_ROOT= the prefix brew --prefix libomp prints."
            )
        endif()
        message(STATUS "OpenMP: not found; building without OpenMP")
        # The switch in the program has to say "not in this build" rather than
        # "off", so what actually happened is recorded rather than what was
        # asked for.
        set(CFD_BUILT_WITH_OPENMP OFF)
    endif()
else()
    set(CFD_BUILT_WITH_OPENMP OFF)
endif()

# The feature tag of this build - the same "avx2-omp-cuda" down to "plain" the
# release archives and the installers are indexed by. Composed from what was
# actually compiled in rather than from what was asked for, so a machine
# missing a toolkit cannot produce a binary that calls itself something it is
# not.
set(CFD_BUILD_FEATURES "")
if(CFD_BUILT_WITH_AVX2)
    string(APPEND CFD_BUILD_FEATURES "avx2-")
endif()
if(CFD_BUILT_WITH_OPENMP)
    string(APPEND CFD_BUILD_FEATURES "omp-")
endif()
if(CFD_BUILT_WITH_CUDA)
    string(APPEND CFD_BUILD_FEATURES "cuda-")
endif()
if(CFD_BUILD_FEATURES STREQUAL "")
    set(CFD_BUILD_FEATURES "plain")
else()
    string(REGEX REPLACE "-$" "" CFD_BUILD_FEATURES "${CFD_BUILD_FEATURES}")
endif()

# Now that all three answers are final, the header the program reads them from.
# include/Version.hpp includes this one and fills in stand-ins when it has not
# been generated yet, which is what stops an editor from underlining every
# CFD_* macro before the first configure.
configure_file(
    "${CFD_INCLUDE_DIR}/VersionGenerated.hpp.in"
    "${CFD_GENERATED_DIR}/VersionGenerated.hpp"
    @ONLY
)

# Everything except MSVC, which was handled by CMAKE_MSVC_RUNTIME_LIBRARY above.
if(CFD_STATIC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(APPLE)
        # No static libSystem exists; only the C++ runtime can be pulled in.
        target_link_options(cfd_app PRIVATE -static-libstdc++)
    elseif(CFD_ENABLE_CUDA)
        # libcudart_static wants dlopen and pthread, so the executable stays
        # dynamically linked against libc and the driver.
        #
        # -static-libstdc++ does not name an archive, it turns the driver's
        # -lstdc++ into a static one and lets -L decide which. CMake puts
        # nvcc's own host compiler directory on the link line, and where that
        # is a different GCC from the one compiling C++ - which is the normal
        # case, since nvcc pins an older one - the archive that comes back is
        # from the wrong release. The binary links, and then dies inside the
        # first << it reaches, because std::cout was never constructed. Asking
        # the compiler where its own libstdc++.a lives and putting that
        # directory in front settles it.
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++.a
            OUTPUT_VARIABLE CFD_LIBSTDCXX_A
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(EXISTS "${CFD_LIBSTDCXX_A}")
            get_filename_component(CFD_LIBSTDCXX_DIR "${CFD_LIBSTDCXX_A}" DIRECTORY)
            target_link_options(cfd_app PRIVATE "-L${CFD_LIBSTDCXX_DIR}")
            message(STATUS "Static libstdc++: ${CFD_LIBSTDCXX_A}")
        endif()
        target_link_options(cfd_app PRIVATE -static-libgcc -static-libstdc++)
    else()
        target_link_options(cfd_app PRIVATE
            -static -static-libgcc -static-libstdc++)
        # A static link only resolves backwards, so pthread and dl have to come
        # AFTER libgomp.a - otherwise libgomp's dlopen/dlsym calls in
        # gomp_target_init are still undefined when the linker gets to them,
        # which is what killed every static OpenMP row. Neither link_options
        # nor link_libraries can be made to land there; CMAKE_CXX_STANDARD_
        # LIBRARIES is appended at the very end of the line, which is exactly
        # what this needs.
        #
        # pthread is pulled in whole because a weakly referenced static
        # libpthread links to stubs that fail at run time.
        string(APPEND CMAKE_CXX_STANDARD_LIBRARIES
            " -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -ldl")
    endif()
endif()

if(CFD_ENABLE_AVX2)
    if(MSVC)
        target_compile_options(cfd_core PUBLIC $<$<COMPILE_LANGUAGE:CXX>:/arch:AVX2>)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # /arch:AVX2 already implies FMA on MSVC; -mavx2 alone does not on
        # GCC/Clang, and the predictor contracts into FMA once it is allowed.
        target_compile_options(cfd_core PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-mavx2 -mfma>)
    else()
        message(WARNING "AVX2 flags are not configured for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

option(BUILD_TESTING "Build the solver test suite" OFF)
include(CTest)

if(BUILD_TESTING)
    set(CFD_TESTS
        PoissonTests
        ConservationTests
        RestartTests
        BackendAgreementTests
        ConvectionTests
        ChannelTests
        CavityTests
        InletTests
        MultiphaseTests
        SurfaceTensionTests
        MovingBodyTests
        TurbulenceTests
        CompressibleTests
        VolumeTests
    )
    foreach(test ${CFD_TESTS})
        add_executable(${test} "tests/${test}.cpp")
        target_link_libraries(${test} PRIVATE cfd_core)
        add_test(NAME ${test} COMMAND ${test})
        set_tests_properties(${test} PROPERTIES TIMEOUT 900)
    endforeach()
endif()


install(
    TARGETS cfd_app
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

install(
    DIRECTORY "${CFD_INCLUDE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

if(EXISTS "${CFD_STL_READER_DIR}/include/")
    install(
        DIRECTORY "${CFD_STL_READER_DIR}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
endif()

install(
    DIRECTORY "${CFD_MODELS_DIR}/"
    DESTINATION models
)

message(STATUS "CFD Solver configuration")
message(STATUS "  Release:      ${CFD_RELEASE_VERSION} (project ${PROJECT_VERSION})")
message(STATUS "  Executable:   ${CFD_APP_NAME}")
message(STATUS "  Feature tag:  ${CFD_BUILD_FEATURES}")
message(STATUS "  CUDA:         ${CFD_BUILT_WITH_CUDA}")
if(CFD_BUILT_WITH_CUDA)
    message(STATUS "  CUDA toolkit: ${CUDAToolkit_VERSION}")
    message(STATUS "  CUDA arch:    ${CFD_CUDA_ARCHITECTURES}")
endif()
message(STATUS "  OpenMP:       ${CFD_BUILT_WITH_OPENMP}")
message(STATUS "  AVX2:         ${CFD_BUILT_WITH_AVX2}")
message(STATUS "  Static:       ${CFD_STATIC}")
message(STATUS "  Updates from: ${CFD_REPO_OWNER}/${CFD_REPO_NAME}")
message(STATUS "  Build dir:    ${CMAKE_BINARY_DIR}")
