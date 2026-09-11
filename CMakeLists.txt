cmake_minimum_required(VERSION 3.28)

project(CFDMaskUIOptimized
    VERSION 0.2.0
    LANGUAGES C CXX
)

include(FetchContent)
option(BUILD_TESTING "Build CFD Mask UI tests" OFF)
option(CFD_UI_ENABLE_AVX2 "Compile UI-owned code with AVX2 instructions" OFF)
option(CFD_UI_ENABLE_OPENMP "Decode and colour VTK frames on every core" OFF)
option(CFD_UI_ENABLE_CUDA "Carry the CUDA release tag on this UI build" OFF)
option(CFD_UI_STATIC_RUNTIME "Prefer static C/C++ runtimes where the platform supports it" ON)
# A row must never be published under a name promising a feature the build does
# not have - the same rule the solver's release script enforces.
option(CFD_UI_ENABLE_OPENMP_EXPLICIT "Treat a missing OpenMP runtime as a hard error" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release binaries should carry as little redistributable runtime baggage as
# the platform permits. Windows can statically link the MSVC CRT. Linux keeps
# the system graphics/window stack dynamic but can carry libgcc/libstdc++.
# macOS does not support statically linking its system C++ runtime.
if(MSVC AND CFD_UI_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# The GUI is buildable without the CFD solver source tree.
# tinyobjloader is vendored into this GUI package because it is a direct
# source dependency of GeometryProcessor/tiny_obj_loader_impl.
set(CFD_TINYOBJ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyobjloader")
if(NOT EXISTS "${CFD_TINYOBJ_DIR}/tiny_obj_loader.h")
    message(FATAL_ERROR
        "GUI package is incomplete: ${CFD_TINYOBJ_DIR}/tiny_obj_loader.h is missing."
    )
endif()

# The solver executable is optional at build time. The running GUI can select
# Fluid Solver.exe later without rebuilding the GUI.
set(
    CFD_SOLVER_EXE
    ""
    CACHE FILEPATH "Optional path to Fluid Solver.exe to copy beside the GUI"
)
if(NOT CFD_SOLVER_EXE STREQUAL "")
    get_filename_component(CFD_SOLVER_EXE "${CFD_SOLVER_EXE}" ABSOLUTE)
endif()
if(NOT EXISTS "${CFD_SOLVER_EXE}")
    if(NOT CFD_SOLVER_EXE STREQUAL "")
        message(WARNING
            "CFD_SOLVER_EXE does not exist: ${CFD_SOLVER_EXE}. "
            "The GUI will still build; select Fluid Solver.exe at runtime."
        )
    else()
        message(STATUS
            "No solver EXE supplied. The GUI will build standalone and can "
            "select Fluid Solver.exe at runtime."
        )
    endif()
    set(CFD_SOLVER_EXE "")
endif()
file(TO_CMAKE_PATH "${CFD_SOLVER_EXE}" CFD_SOLVER_EXE_PATH)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_WINDOW ON CACHE BOOL "" FORCE)
set(SFML_BUILD_GRAPHICS ON CACHE BOOL "" FORCE)
set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(SFML_BUILD_TEST_SUITE OFF CACHE BOOL "" FORCE)
set(SFML_CONFIGURE_EXTRAS OFF CACHE BOOL "" FORCE)
set(SFML_INSTALL_PKGCONFIG_FILES OFF CACHE BOOL "" FORCE)
# Keep FreeType/HarfBuzz/SheenBidi inside the static SFML build instead of
# taking shared system copies. Native window/OpenGL libraries remain system
# dependencies where the OS requires them.
set(SFML_USE_SYSTEM_DEPS OFF CACHE BOOL "" FORCE)

# Prefer the GUI's own bundled SFML source so configuration never depends on a
# solver checkout. SFML may still FetchContent its pinned FreeType/HarfBuzz/
# SheenBidi sources when static bundled dependencies are selected.
set(CFD_BUNDLED_SFML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sfml")
if(EXISTS "${CFD_BUNDLED_SFML_DIR}/CMakeLists.txt")
    add_subdirectory(
        "${CFD_BUNDLED_SFML_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/sfml-build"
    )
else()
    message(STATUS "Bundled SFML is absent; fetching pinned SFML 3.1.0.")
    FetchContent_Declare(
        SFML
        URL https://github.com/SFML/SFML/archive/refs/tags/3.1.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SFML)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(mask_ui_core STATIC
    src/BodyRows.cpp
    src/BodyTrack.cpp
    src/ConfigurationFile.cpp
    src/ExplorerTarget.cpp
    src/FluidSolverRun.cpp
    src/GeometryProcessor.cpp
    src/NumericInput.cpp
    src/ParameterInfo.cpp
    src/ResultView.cpp
    src/SectionAdapter.cpp
    src/tiny_obj_loader_impl.cpp
    src/TrayIcon.cpp
    src/VtkFrame.cpp
    src/VelocityOverlay.cpp
    src/Viewport3D.cpp
)

target_link_libraries(mask_ui_core PUBLIC SFML::Graphics)

target_include_directories(mask_ui_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CFD_TINYOBJ_DIR}"
)

target_compile_features(mask_ui_core PUBLIC cxx_std_17)

if(CFD_UI_ENABLE_AVX2)
    if(MSVC)
        target_compile_options(mask_ui_core PRIVATE /arch:AVX2)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # -mfma alongside -mavx2 for the same reason the solver passes it: the
        # magnitude and colour maths contracts into FMA only once it is allowed.
        target_compile_options(mask_ui_core PRIVATE -mavx2 -mfma)
    else()
        message(FATAL_ERROR
            "CFD_UI_ENABLE_AVX2 is ON, but this compiler has no configured AVX2 flag.")
    endif()
endif()

# OpenMP is what turns frame decoding and the colour map loose on every core.
# The static runtime matters here too: an OpenMP row that needs libgomp.so on
# the machine that runs it is not the single-file build the release promises.
if(CFD_UI_ENABLE_OPENMP)
    if(CFD_UI_STATIC_RUNTIME AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" ${CMAKE_CXX_FLAGS} -print-file-name=libgomp.a
            OUTPUT_VARIABLE CFD_UI_LIBGOMP_A
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(EXISTS "${CFD_UI_LIBGOMP_A}")
            set(OpenMP_CXX_FLAGS "-fopenmp")
            set(OpenMP_CXX_LIB_NAMES "gomp")
            set(OpenMP_gomp_LIBRARY "${CFD_UI_LIBGOMP_A}")
            message(STATUS "UI OpenMP: static ${CFD_UI_LIBGOMP_A}")
        endif()
    endif()

    # AppleClang carries no OpenMP runtime of its own, so libomp has to be
    # pointed at, exactly as the solver does it.
    if(APPLE AND CFD_UI_OPENMP_ROOT AND
       EXISTS "${CFD_UI_OPENMP_ROOT}/include/omp.h")
        if(CFD_UI_STATIC_RUNTIME AND
           EXISTS "${CFD_UI_OPENMP_ROOT}/lib/libomp.a")
            set(OpenMP_omp_LIBRARY "${CFD_UI_OPENMP_ROOT}/lib/libomp.a")
        elseif(EXISTS "${CFD_UI_OPENMP_ROOT}/lib/libomp.dylib")
            set(OpenMP_omp_LIBRARY "${CFD_UI_OPENMP_ROOT}/lib/libomp.dylib")
        endif()
        if(OpenMP_omp_LIBRARY)
            set(OpenMP_CXX_FLAGS
                "-Xclang -fopenmp -I${CFD_UI_OPENMP_ROOT}/include")
            set(OpenMP_CXX_LIB_NAMES "omp")
            message(STATUS "UI OpenMP: ${OpenMP_omp_LIBRARY}")
        endif()
    endif()

    find_package(OpenMP QUIET COMPONENTS CXX)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(mask_ui_core PUBLIC OpenMP::OpenMP_CXX)
        message(STATUS "UI OpenMP: enabled")
    elseif(CFD_UI_ENABLE_OPENMP_EXPLICIT)
        message(FATAL_ERROR
            "CFD_UI_ENABLE_OPENMP was requested with "
            "CFD_UI_ENABLE_OPENMP_EXPLICIT=ON, but no OpenMP runtime was found. "
            "On macOS install libomp (brew install libomp) and pass "
            "-DCFD_UI_OPENMP_ROOT=$(brew --prefix libomp)."
        )
    else()
        message(STATUS "UI OpenMP: not found; building single-threaded")
    endif()
endif()

# CUDA deliberately changes nothing here. Decoding a frame is a read plus a byte
# swap, and the colour map is one pass whose result has to end up in host memory
# for the texture upload anyway - sending either to a GPU would cost more in
# transfers than the arithmetic is worth, which is the whole reason this UI is
# fast on AVX2 and OpenMP instead. The option exists so a "-cuda" row can be
# built and paired with the solver row of the same name; because it emits no
# code, one binary serves both the "-cuda" and the plain row of a variant.
if(CFD_UI_ENABLE_CUDA)
    message(STATUS
        "UI CUDA: release tag only - this UI runs no CUDA and the binary is "
        "identical to the same row without it")
endif()

# The icon and the version block live in a resource script, which is a Windows
# concept only - elsewhere the icon belongs to the .desktop file or the .app
# bundle, and the installers put it there. Without this the executable had no
# icon at all and Explorer drew it as a blank binary.
option(CFD_UI_ENABLE_ICON "Embed the icon and version block on Windows" ON)

set(CFD_UI_SOURCES src/main.cpp src/Application.cpp)

if(WIN32 AND CFD_UI_ENABLE_ICON)
    enable_language(RC)
    set(CFD_UI_ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/logo/fluid-solver.ico")
    if(EXISTS "${CFD_UI_ICON_SOURCE}")
        # rc.exe reads the .rc in the system ANSI code page, not UTF-8, so an
        # absolute path with non-ASCII characters anywhere in it arrives as
        # mojibake and the icon "does not exist". The icon is therefore copied
        # next to the generated .rc and referenced by a bare name, which rc.exe
        # resolves relative to the .rc itself.
        file(COPY "${CFD_UI_ICON_SOURCE}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
        set(CFD_UI_ICON_PATH "fluid-solver.ico")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ui.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/ui.rc"
            @ONLY
        )
        list(APPEND CFD_UI_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/ui.rc")
    else()
        message(WARNING
            "logo/fluid-solver.ico is missing; the UI gets the default Windows "
            "icon and no version information."
        )
    endif()
endif()

add_executable(cfd_mask_ui_optimized ${CFD_UI_SOURCES})

target_link_libraries(cfd_mask_ui_optimized
    PRIVATE
        mask_ui_core
        SFML::Graphics
)

if(WIN32)
    # shell32 the tray icon, ole32 ITaskbarList3 (the progress on the taskbar
    # button), user32 the hidden window the tray posts its clicks to.
    target_link_libraries(cfd_mask_ui_optimized PRIVATE shell32 ole32 user32)
    target_link_libraries(mask_ui_core PUBLIC shell32 ole32 user32)
else()
    # The tray thread, and fork/exec's waitpid.
    find_package(Threads REQUIRED)
    target_link_libraries(mask_ui_core PUBLIC Threads::Threads)
endif()

target_compile_definitions(cfd_mask_ui_optimized
    PRIVATE
        CFD_SOLVER_EXE="${CFD_SOLVER_EXE_PATH}"
        CFD_MASK_UI_VERSION="${PROJECT_VERSION}"
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_compile_features(cfd_mask_ui_optimized PRIVATE cxx_std_17)
set_target_properties(cfd_mask_ui_optimized PROPERTIES OUTPUT_NAME "Fluid Solver UI")

if(CFD_UI_ENABLE_AVX2)
    if(MSVC)
        target_compile_options(cfd_mask_ui_optimized PRIVATE /arch:AVX2)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cfd_mask_ui_optimized PRIVATE -mavx2 -mfma)
    endif()
endif()

if(CFD_UI_STATIC_RUNTIME AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(cfd_mask_ui_optimized PRIVATE -static-libgcc -static-libstdc++)
endif()

if(WIN32)
    set_target_properties(cfd_mask_ui_optimized PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(
        cfd_mask_ui_optimized
        PRIVATE
            SFML::Main
            Comdlg32
            Shell32
            Comctl32
    )
endif()

if(EXISTS "${CFD_SOLVER_EXE}")
    add_custom_command(
        TARGET cfd_mask_ui_optimized
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CFD_SOLVER_EXE}"
            "$<TARGET_FILE_DIR:cfd_mask_ui_optimized>/Fluid Solver.exe"
        COMMENT "Copying the selected Fluid Solver beside the GUI"
        VERBATIM
    )
endif()

include(CTest)

if(BUILD_TESTING)
    function(add_mask_ui_test TARGET_NAME)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${TARGET_NAME}.cpp")
            add_executable(
                ${TARGET_NAME}
                "tests/${TARGET_NAME}.cpp"
            )
            target_link_libraries(${TARGET_NAME} PRIVATE mask_ui_core)
            add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
        endif()
    endfunction()

    foreach(MASK_UI_TEST
        ExplorerTargetTests
        VtkFrameTests
        VolumeFrameTests
        FluidSolverRunTests
        NumericInputTests
        VelocityOverlayTests
        SectionAdapterTests
        GeometryProcessorTests
        ContinuationTests
        BodyTrackTests
        BodyRowsTests
        ConfigurationFileTests
        ResultViewTests
        ParameterInfoTests
    )
        add_mask_ui_test(${MASK_UI_TEST})
    endforeach()

    if(EXISTS "${CFD_SOLVER_EXE}")
        add_mask_ui_test(SolverCompatibilityTests)
        if(TARGET SolverCompatibilityTests)
            target_compile_definitions(SolverCompatibilityTests
                PRIVATE CFD_SOLVER_EXE="${CFD_SOLVER_EXE_PATH}"
            )
        endif()
    endif()
endif()
