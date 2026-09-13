#include "Application.hpp"

#include "ExplorerTarget.hpp"
#include "FluidSolverRun.hpp"
#include "NumericInput.hpp"
#include "BodyRows.hpp"
#include "BodyTrack.hpp"
#include "ConfigurationFile.hpp"
#include "GeometryProcessor.hpp"
#include "ParameterInfo.hpp"
#include "ResultView.hpp"
#include "SectionAdapter.hpp"
#include "TrayIcon.hpp"
#include "Viewport3D.hpp"
#include "VtkFrame.hpp"
#include "VelocityOverlay.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Window/Clipboard.hpp>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef CFD_MASK_UI_VERSION
#define CFD_MASK_UI_VERSION "dev"
#endif

// Normally supplied by target_compile_definitions in CMakeLists.txt. Spelled
// out here as well because Visual Studio's IntelliSense does not read a
// target's compile definitions, so without this it reports the constructor call
// in Implementation() as an error even though the build itself is fine.
#ifndef CFD_SOLVER_EXE
#define CFD_SOLVER_EXE ""
#endif

namespace maskui {
namespace {

constexpr float LEFT_PANEL_WIDTH = 330.0f;
constexpr float HEADER_HEIGHT = 44.0f;
constexpr float OUTLINER_TOP = 52.0f;
constexpr float OUTLINER_HEIGHT = 162.0f;
constexpr float PARAMETER_TOP = 274.0f;
constexpr float PARAMETER_STRIP_TOP = 222.0f;
constexpr float PARAMETER_STRIP_HEIGHT = 28.0f;
constexpr float PARAMETER_BOTTOM_MARGIN = 126.0f;
constexpr float PARAMETER_ROW_HEIGHT = 44.0f;
constexpr float PARAMETER_GROUP_HEIGHT = 25.0f;
constexpr float PARAMETER_SCROLL_STEP = 88.0f;
constexpr double PI = 3.14159265358979323846;
constexpr std::size_t DECODED_FRAME_CACHE_BYTES = 1024ull * 1024ull * 1024ull;
// The byte budget above is the real limit; this only stops a series of tiny
// frames from filling the cache with thousands of entries. It used to be 16,
// which is why flipping past the sixth step always hit the disk again.
constexpr std::size_t MAX_ADAPTIVE_RESIDENT_FRAMES = 256;

const sf::Color BACKGROUND{24, 24, 24};
const sf::Color PANEL{40, 40, 40};
const sf::Color HEADER{31, 31, 31};
const sf::Color VIEW_BACKGROUND{30, 30, 30};
const sf::Color TEXT{224, 224, 224};
const sf::Color MUTED{150, 150, 150};
const sf::Color ACCENT{68, 214, 44};
const sf::Color ACCENT_DARK{40, 96, 32};
const sf::Color SOLID_COLOR{58, 58, 58};
const sf::Color CONTROL_BACKGROUND{30, 30, 30};
const sf::Color CONTROL_RAIL{72, 72, 72};
const sf::Color BUTTON_DISABLED{45, 45, 45};
const sf::Color BUTTON_BACKGROUND{56, 56, 56};
const sf::Color BUTTON_HOVER{72, 72, 72};
const sf::Color BORDER{64, 64, 64};
const sf::Color OVERLAY_BACKGROUND{35, 35, 35, 245};
const sf::Color SECTION_PLANE{68, 214, 44, 14};
const sf::Color SECTION_PLANE_OUTLINE{86, 220, 62, 190};
const sf::Color INVALID_COLOR{255, 0, 180};
const sf::Color CUT_COLOR{255, 157, 46};
const sf::Color CUT_GLOW{68, 32, 8, 220};
const sf::Color WARNING_BACKGROUND{92, 35, 24, 235};
const sf::Color WARNING_OUTLINE{255, 137, 74};
const sf::Color WARNING_TEXT{255, 225, 205};

float clampFloat(float value, float minimum, float maximum) {
    return std::clamp(value, minimum, maximum);
}

double wrapDegrees(double value) {
    while (value > 180.0) {
        value -= 360.0;
    }
    while (value < -180.0) {
        value += 360.0;
    }
    return value;
}

double snapCardinalDegrees(double value) {
    value = wrapDegrees(value);
    const double cardinal = 90.0 * std::round(value / 90.0);
    return std::abs(value - cardinal) <= 0.25
               ? wrapDegrees(cardinal)
               : value;
}

std::string formatValue(double value, bool integer, const std::string& unit) {
    std::ostringstream output;
    if (integer) {
        output << static_cast<long long>(std::llround(value));
    } else if (std::abs(value) > 0.0 && std::abs(value) < 0.001) {
        output << std::scientific << std::setprecision(2) << value;
    } else {
        output << std::fixed << std::setprecision(2) << value;
    }
    if (!unit.empty()) {
        output << ' ' << unit;
    }
    return output.str();
}

sf::Text makeText(
    const sf::Font& font,
    const std::string& value,
    unsigned int size,
    sf::Vector2f position,
    sf::Color color = TEXT) {
    sf::Text text(font, value, size);
    text.setPosition(position);
    text.setFillColor(color);
    return text;
}

void drawThickLine(
    sf::RenderTarget& target,
    sf::Vector2f first,
    sf::Vector2f second,
    float thickness,
    sf::Color color) {
    const sf::Vector2f direction = second - first;
    const float segmentLength = std::hypot(direction.x, direction.y);
    if (segmentLength <= 0.0f) {
        return;
    }
    const sf::Vector2f perpendicular{
        -direction.y / segmentLength * thickness * 0.5f,
        direction.x / segmentLength * thickness * 0.5f
    };
    sf::ConvexShape segment(4);
    segment.setPoint(0, first + perpendicular);
    segment.setPoint(1, second + perpendicular);
    segment.setPoint(2, second - perpendicular);
    segment.setPoint(3, first - perpendicular);
    segment.setFillColor(color);
    target.draw(segment);
}

enum class ControlKind {
    Number,
    Choice,
    Text
};

struct Slider {
    std::string label;
    std::string unit;
    double minimum = 0.0;
    double maximum = 1.0;
    double value = 0.0;
    bool integer = false;
    bool logarithmic = false;
    bool boolean = false;
    double defaultValue = 0.0;
    ControlKind kind = ControlKind::Number;
    std::vector<std::string> options;
    std::string text;
    std::string defaultText;
    sf::FloatRect track{{0.0f, 0.0f}, {1.0f, 1.0f}};
    bool dragging = false;

    double normalized() const {
        if (logarithmic) {
            const double low = std::log10(minimum);
            const double high = std::log10(maximum);
            return (std::log10(value) - low) / (high - low);
        }
        return (value - minimum) / (maximum - minimum);
    }

    void setNormalized(double normalizedValue) {
        normalizedValue = std::clamp(normalizedValue, 0.0, 1.0);
        if (kind == ControlKind::Text)
            return;
        if (logarithmic) {
            const double low = std::log10(minimum);
            const double high = std::log10(maximum);
            value = std::pow(10.0, low + normalizedValue * (high - low));
        } else {
            value = minimum + normalizedValue * (maximum - minimum);
        }
        if (integer) {
            value = std::round(value);
        }
    }

    void setFromX(float mouseX) {
        if (kind == ControlKind::Text)
            return;
        if (kind == ControlKind::Choice) {
            const double span = static_cast<double>(std::max<std::size_t>(
                options.size(), 1u));
            const double t = std::clamp(
                static_cast<double>((mouseX - track.position.x) / track.size.x),
                0.0, 0.999999);
            value = std::floor(t * span);
            return;
        }
        if (boolean) {
            value = mouseX >= track.position.x + track.size.x * 0.5f
                        ? 1.0
                        : 0.0;
            return;
        }
        setNormalized(
            static_cast<double>((mouseX - track.position.x) / track.size.x));
    }

    bool hit(sf::Vector2f point) const {
        const sf::FloatRect hitBounds{
            {track.position.x - 8.0f, track.position.y - 10.0f},
            {track.size.x + 16.0f, track.size.y + 20.0f}
        };
        return hitBounds.contains(point);
    }

    sf::FloatRect editBounds() const {
        return {
            {track.position.x + track.size.x - 132.0f,
             track.position.y - 28.0f},
            {132.0f, 23.0f}
        };
    }

    sf::FloatRect stepMinusBounds() const {
        return {
            {track.position.x + track.size.x - 184.0f,
             track.position.y - 28.0f},
            {20.0f, 20.0f}
        };
    }

    sf::FloatRect stepPlusBounds() const {
        return {
            {track.position.x + track.size.x - 158.0f,
             track.position.y - 28.0f},
            {20.0f, 20.0f}
        };
    }

    bool valueHit(sf::Vector2f point) const {
        return editBounds().contains(point);
    }

    const std::string& choice() const {
        static const std::string empty;
        if (options.empty())
            return empty;
        const std::size_t index = static_cast<std::size_t>(
            std::clamp(std::llround(value), 0LL,
                       static_cast<long long>(options.size()) - 1));
        return options[index];
    }

    bool setChoice(const std::string& wanted) {
        for (std::size_t index = 0; index < options.size(); ++index) {
            if (options[index] == wanted) {
                value = static_cast<double>(index);
                return true;
            }
        }
        return false;
    }

    std::string displayValue() const {
        if (kind == ControlKind::Choice)
            return choice();
        if (kind == ControlKind::Text)
            return text.empty() ? std::string("none") : text;
        if (boolean)
            return value >= 0.5 ? "Requested" : "Off";
        return formatValue(value, integer, unit);
    }

    bool setFromText(const std::string& input, std::string& error) {
        if (kind == ControlKind::Text) {
            if (input.find_first_of("\r\n") != std::string::npos) {
                error = "this has to stay on one line";
                return false;
            }
            text = input;
            return true;
        }
        if (kind == ControlKind::Choice) {
            for (std::size_t index = 0; index < options.size(); ++index) {
                std::string a = options[index];
                std::string b = input;
                for (char& c : a)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                for (char& c : b)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                while (!b.empty() && std::isspace(static_cast<unsigned char>(b.back())))
                    b.pop_back();
                while (!b.empty() && std::isspace(static_cast<unsigned char>(b.front())))
                    b.erase(b.begin());
                if (a == b) {
                    value = static_cast<double>(index);
                    return true;
                }
            }
            error = "this one takes one of: ";
            for (std::size_t index = 0; index < options.size(); ++index)
                error += (index ? ", " : "") + options[index];
            return false;
        }
        const std::string& text = input;
        double parsed = value;
        if (!parseNumericInput(
                text,
                NumericInputRules{integer, logarithmic},
                parsed,
                error)) {
            return false;
        }
        if (boolean && parsed != 0.0 && parsed != 1.0) {
            error = "boolean value must be 0 or 1";
            return false;
        }
        value = parsed;
        return true;
    }

    void draw(sf::RenderTarget& target,
              const sf::Font& font,
              bool editing,
              const std::string& inputText,
              bool invalid = false) const {
        target.draw(makeText(
            font,
            label,
            13,
            {track.position.x, track.position.y - 24.0f},
            invalid ? WARNING_OUTLINE : TEXT));
        const std::string display =
            editing ? inputText + "|" : displayValue();
        if (editing) {
            const sf::FloatRect bounds = editBounds();
            sf::RectangleShape editor(bounds.size);
            editor.setPosition(bounds.position);
            editor.setFillColor(CONTROL_BACKGROUND);
            editor.setOutlineColor(ACCENT);
            editor.setOutlineThickness(1.0f);
            target.draw(editor);
        }
        sf::Text valueText = makeText(
            font,
            display,
            12,
            {track.position.x + track.size.x, track.position.y - 23.0f},
            editing ? TEXT : MUTED);
        if (invalid) {
            valueText.setFillColor(WARNING_TEXT);
        }
        valueText.setOrigin({valueText.getLocalBounds().size.x, 0.0f});
        target.draw(valueText);

        if (integer && !boolean && !editing && kind == ControlKind::Number) {
            for (const auto& control :
                 std::array<std::pair<sf::FloatRect, const char*>, 2>{{
                     {stepMinusBounds(), "-"},
                     {stepPlusBounds(), "+"}
                 }}) {
                sf::RectangleShape box(control.first.size);
                box.setPosition(control.first.position);
                box.setFillColor(CONTROL_BACKGROUND);
                box.setOutlineColor(BORDER);
                box.setOutlineThickness(1.0f);
                target.draw(box);
                target.draw(makeText(
                    font,
                    control.second,
                    12,
                    control.first.position + sf::Vector2f{6.0f, 1.0f},
                    MUTED));
            }
        }

        sf::RectangleShape rail(track.size);
        rail.setPosition(track.position);
        rail.setFillColor(CONTROL_RAIL);
        target.draw(rail);

        const float fraction =
            static_cast<float>(std::clamp(normalized(), 0.0, 1.0));
        sf::RectangleShape fill({track.size.x * fraction, track.size.y});
        fill.setPosition(track.position);
        fill.setFillColor(ACCENT_DARK);
        target.draw(fill);

        sf::CircleShape handle(boolean ? 8.0f : 7.0f);
        const float handleRadius = handle.getRadius();
        handle.setOrigin({handleRadius, handleRadius});
        handle.setPosition({
            track.position.x + track.size.x * fraction,
            track.position.y + track.size.y / 2.0f
        });
        handle.setFillColor(dragging ? sf::Color::White : ACCENT);
        target.draw(handle);
    }
};

struct Button {
    std::string label;
    sf::FloatRect bounds{{0.0f, 0.0f}, {1.0f, 1.0f}};
    bool selected = false;
    bool enabled = true;

    bool hit(sf::Vector2f point) const {
        return enabled && bounds.contains(point);
    }

    void draw(sf::RenderTarget& target,
              const sf::Font& font,
              sf::Vector2f cursor = {-1.0f, -1.0f}) const {
        sf::RectangleShape rectangle(bounds.size);
        rectangle.setPosition(bounds.position);
        rectangle.setFillColor(
            !enabled
                ? BUTTON_DISABLED
                : selected
                      ? ACCENT_DARK
                      : bounds.contains(cursor)
                            ? BUTTON_HOVER
                            : BUTTON_BACKGROUND);
        target.draw(rectangle);
        if (selected) {
            sf::RectangleShape mark({bounds.size.x, 2.0f});
            mark.setPosition(
                {bounds.position.x,
                 bounds.position.y + bounds.size.y - 2.0f});
            mark.setFillColor(ACCENT);
            target.draw(mark);
        }

        sf::Text text = makeText(font, label, 13, {0.0f, 0.0f});
        const sf::FloatRect textBounds = text.getLocalBounds();
        text.setPosition({
            bounds.position.x +
                (bounds.size.x - textBounds.size.x) / 2.0f,
            bounds.position.y +
                (bounds.size.y - textBounds.size.y) / 2.0f - 2.0f
        });
        text.setFillColor(enabled ? TEXT : MUTED);
        target.draw(text);
    }
};

#ifdef _WIN32
std::wstring quoteWindowsArgument(const std::wstring& argument) {
    if (!argument.empty() &&
        argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }

    std::wstring quoted = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}
#endif

struct ChildProcess {
#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    DWORD processId = 0;
#else
    // fork/exec rather than system(): the arguments carry spaces and the
    // occasional non-ASCII path, and handing those to a shell is how a run
    // directory called "Модели 2" turns into four arguments.
    pid_t process = -1;
#endif
    bool active = false;

    ~ChildProcess() {
        if (!terminate()) {
            closeHandles();
        }
    }

    bool terminate(std::string* error = nullptr) {
#ifdef _WIN32
        DWORD exitCode = 0;
        if (active && process != nullptr) {
            if (!GetExitCodeProcess(process, &exitCode)) {
                if (error != nullptr) {
                    *error = "Cannot inspect Fluid Solver. Windows error " +
                        std::to_string(GetLastError()) + ".";
                }
                return false;
            }
            if (exitCode == STILL_ACTIVE) {
                // Ask before killing. The child is in its own process group
                // (CREATE_NEW_PROCESS_GROUP below) so a Ctrl+Break reaches it
                // and nothing else; 0.2 and newer take that as "finish the
                // step, write the frame, exit", which leaves a frame the run
                // can be continued from. Older builds ignore it and are killed
                // a moment later, exactly as they were before.
                GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, processId);
                if (WaitForSingleObject(process, 4000) != WAIT_OBJECT_0 &&
                    !TerminateProcess(process, ERROR_CANCELLED)) {
                    if (error != nullptr) {
                        *error = "Cannot stop Fluid Solver. Windows error " +
                            std::to_string(GetLastError()) + ".";
                    }
                    return false;
                }
                if (WaitForSingleObject(process, 5000) != WAIT_OBJECT_0) {
                    if (error != nullptr) {
                        *error = "Fluid Solver did not stop within 5 seconds.";
                    }
                    return false;
                }
            }
        }
#else
        if (active && process > 0) {
            // SIGINT first, not SIGKILL: 0.2 and newer treat it as "finish the
            // step, write the frame, exit", which leaves a frame the run can be
            // continued from. Older builds die on it, which is what SIGKILL
            // would have done anyway. Five seconds, then the hammer.
            ::kill(process, SIGINT);
            bool stopped = false;
            for (int attempt = 0; attempt < 500; ++attempt) {
                int state = 0;
                const pid_t result = ::waitpid(process, &state, WNOHANG);
                if (result == process || result < 0) {
                    stopped = true;
                    break;
                }
                ::usleep(10000);
            }
            if (!stopped) {
                ::kill(process, SIGKILL);
                int state = 0;
                ::waitpid(process, &state, 0);
            }
        }
        (void)error;
#endif
        closeHandles();
        return true;
    }

    void closeHandles() {
#ifdef _WIN32
        if (thread != nullptr) {
            CloseHandle(thread);
            thread = nullptr;
        }
        if (process != nullptr) {
            CloseHandle(process);
            process = nullptr;
        }
        processId = 0;
#else
        process = -1;
#endif
        active = false;
    }

    bool start(
        const std::filesystem::path& solverExecutable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& runDirectory,
        std::string& error) {
        if (active) {
            error = "A simulation is already running.";
            return false;
        }
#ifdef _WIN32
        std::wstring command;
        try {
            command = quoteWindowsArgument(solverExecutable.wstring());
            for (const std::string& argument : arguments) {
                command.push_back(L' ');
                command += quoteWindowsArgument(
                    std::filesystem::u8path(argument).wstring());
            }
        } catch (const std::exception& exception) {
            error = "Cannot encode Fluid Solver arguments: " +
                    std::string(exception.what());
            return false;
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        const std::filesystem::path outputFile =
            runDirectory / "solver-output.txt";
        const std::filesystem::path errorFile =
            runDirectory / "solver-error.txt";
        HANDLE childInput = CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        HANDLE childOutput = CreateFileW(
            outputFile.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            &security,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        HANDLE childError = CreateFileW(
            errorFile.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            &security,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (childInput == INVALID_HANDLE_VALUE ||
            childOutput == INVALID_HANDLE_VALUE ||
            childError == INVALID_HANDLE_VALUE) {
            const DWORD windowsError = GetLastError();
            if (childInput != INVALID_HANDLE_VALUE) {
                CloseHandle(childInput);
            }
            if (childOutput != INVALID_HANDLE_VALUE) {
                CloseHandle(childOutput);
            }
            if (childError != INVALID_HANDLE_VALUE) {
                CloseHandle(childError);
            }
            error = "Cannot open solver log files. Windows error " +
                    std::to_string(windowsError) + ".";
            return false;
        }

        std::vector<wchar_t> commandBuffer(command.begin(), command.end());
        commandBuffer.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = childInput;
        startup.hStdOutput = childOutput;
        startup.hStdError = childError;
        PROCESS_INFORMATION information{};
        const BOOL created = CreateProcessW(
            solverExecutable.c_str(),
            commandBuffer.data(),
            nullptr,
            nullptr,
            TRUE,
            // The new process group is what makes a Ctrl+Break in terminate()
            // reach the solver and nothing else - including this UI.
            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
            nullptr,
            runDirectory.c_str(),
            &startup,
            &information);
        const DWORD windowsError = created ? ERROR_SUCCESS : GetLastError();
        CloseHandle(childInput);
        CloseHandle(childOutput);
        CloseHandle(childError);
        if (!created) {
            error = "Cannot start Fluid Solver. Windows error " +
                    std::to_string(windowsError) + ".";
            return false;
        }
        process = information.hProcess;
        thread = information.hThread;
        processId = information.dwProcessId;
        active = true;
        return true;
#else
        // This used to say "implemented for Windows only", which meant the
        // Linux and macOS UI builds the release publishes could load frames
        // and draw them but never actually start a solver. fork/exec is the
        // same three things CreateProcess does above: redirect the two output
        // streams into the run directory, change into it, and run.
        const std::string executablePath = solverExecutable.string();
        std::vector<std::string> owned;
        owned.reserve(arguments.size() + 1u);
        owned.push_back(executablePath);
        for (const std::string& argument : arguments) {
            owned.push_back(argument);
        }
        std::vector<char*> argv;
        argv.reserve(owned.size() + 1u);
        for (std::string& text : owned) {
            argv.push_back(text.data());
        }
        argv.push_back(nullptr);

        const std::string outputFile =
            (runDirectory / "solver-output.txt").string();
        const std::string errorFile =
            (runDirectory / "solver-error.txt").string();

        const pid_t child = ::fork();
        if (child < 0) {
            error = "Cannot start Fluid Solver: fork failed (" +
                std::string(std::strerror(errno)) + ").";
            return false;
        }
        if (child == 0) {
            // Child. Nothing here may throw or allocate in a way that matters:
            // every failure ends in _exit, which the parent sees as a non-zero
            // exit code and reports through solver-error.txt being empty.
            if (::chdir(runDirectory.c_str()) != 0) {
                ::_exit(127);
            }
            const int nullInput = ::open("/dev/null", O_RDONLY);
            const int output = ::open(
                outputFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            const int errors = ::open(
                errorFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (nullInput < 0 || output < 0 || errors < 0) {
                ::_exit(127);
            }
            ::dup2(nullInput, STDIN_FILENO);
            ::dup2(output, STDOUT_FILENO);
            ::dup2(errors, STDERR_FILENO);
            ::close(nullInput);
            ::close(output);
            ::close(errors);
            ::execv(executablePath.c_str(), argv.data());
            ::_exit(127);
        }

        process = child;
        active = true;
        return true;
#endif
    }

    std::optional<unsigned long> poll() {
        if (!active) {
            return std::nullopt;
        }
#ifdef _WIN32
        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeProcess(process, &exitCode)) {
            closeHandles();
            return static_cast<unsigned long>(-1);
        }
        if (exitCode == STILL_ACTIVE) {
            return std::nullopt;
        }
        const unsigned long completedCode = exitCode;
        closeHandles();
        return completedCode;
#else
        int state = 0;
        const pid_t result = ::waitpid(process, &state, WNOHANG);
        if (result == 0) {
            return std::nullopt;   // still running
        }
        if (result < 0) {
            closeHandles();
            return static_cast<unsigned long>(-1);
        }
        unsigned long completedCode = 0;
        if (WIFEXITED(state)) {
            completedCode = static_cast<unsigned long>(WEXITSTATUS(state));
        } else if (WIFSIGNALED(state)) {
            // A killed solver is a failed one as far as the caller is
            // concerned, and 128+signal is what a shell would have reported.
            completedCode =
                static_cast<unsigned long>(128 + WTERMSIG(state));
        }
        closeHandles();
        return completedCode;
#endif
    }
};

enum class DisplayMode {
    Setup,
    Results
};

enum class ResultQuantity {
    Pressure,
    Velocity,
    Scalar
};

enum ViewControl : std::size_t {
    ControlFrameAll,
    ControlOrtho,
    ControlRotateTool,
    ControlMoveTool,
    ControlBox,
    ControlGrid,
    ControlSolid,
    ControlWire,
    ControlSliceX,
    ControlSliceY,
    ControlSliceZ,
    ControlIso,
    ControlVortices,
    ControlStreamlines,
    ControlTracers,
    ControlColour,
    ControlFront,
    ControlBack,
    ControlLeft,
    ControlRight,
    ControlTop,
    ControlBottom,
    ControlAxisX,
    ControlAxisY,
    ControlAxisZ,
    ViewControlCount
};

enum ViewTrackIndex : std::size_t {
    TrackSliceX,
    TrackSliceY,
    TrackSliceZ,
    TrackIso,
    TrackVortex,
    TrackSlice2D,
    ViewTrackCount
};

enum class ResultOrigin {
    FluidSolverRun,
    ContinuedFluidSolverRun,
    StoppedFluidSolverRun,
    ImportedFiles
};


struct ProjectedPoint {
    sf::Vector2f position;
    double depth = 0.0;
};

struct PreviewTriangle {
    std::array<sf::Vector2f, 3> points;
    double depth = 0.0;
    sf::Color color;
};

Vec3 add(const Vec3& first, const Vec3& second) {
    return {
        first.x + second.x,
        first.y + second.y,
        first.z + second.z
    };
}

Vec3 multiply(const Vec3& value, double scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3 subtract(const Vec3& first, const Vec3& second) {
    return {
        first.x - second.x,
        first.y - second.y,
        first.z - second.z
    };
}

Vec3 cross(const Vec3& first, const Vec3& second) {
    return {
        first.y * second.z - first.z * second.y,
        first.z * second.x - first.x * second.z,
        first.x * second.y - first.y * second.x
    };
}

double length(const Vec3& value) {
    return std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z);
}

#ifdef _WIN32
class ScopedFileDropTarget {
public:
    explicit ScopedFileDropTarget(sf::WindowHandle owner)
        : window_(reinterpret_cast<HWND>(owner)) {
        if (window_ != nullptr &&
            SetWindowSubclass(
                window_,
                &ScopedFileDropTarget::windowProcedure,
                SUBCLASS_ID,
                reinterpret_cast<DWORD_PTR>(this)) != FALSE) {
            attached_ = true;
            DragAcceptFiles(window_, TRUE);
        } else {
            window_ = nullptr;
        }
    }

    ~ScopedFileDropTarget() {
        detach();
    }

    ScopedFileDropTarget(const ScopedFileDropTarget&) = delete;
    ScopedFileDropTarget& operator=(const ScopedFileDropTarget&) = delete;

    bool available() const {
        return attached_;
    }

    std::vector<std::vector<std::filesystem::path>> takeBatches() {
        std::vector<std::vector<std::filesystem::path>> result =
            std::move(batches_);
        batches_.clear();
        return result;
    }

    bool takeReadFailure() {
        const bool result = readFailure_;
        readFailure_ = false;
        return result;
    }

private:
    struct DropHandleGuard {
        HDROP handle = nullptr;

        ~DropHandleGuard() {
            if (handle != nullptr) {
                DragFinish(handle);
            }
        }
    };

    static constexpr UINT_PTR SUBCLASS_ID = 0x4346444Du;

    void capture(HDROP drop) noexcept {
        DropHandleGuard guard{drop};
        try {
            const UINT count =
                DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
            std::vector<std::filesystem::path> paths;
            paths.reserve(static_cast<std::size_t>(count));
            for (UINT index = 0; index < count; ++index) {
                const UINT length =
                    DragQueryFileW(drop, index, nullptr, 0);
                if (length == 0) {
                    readFailure_ = true;
                    return;
                }
                std::vector<wchar_t> buffer(
                    static_cast<std::size_t>(length) + 1u,
                    L'\0');
                if (DragQueryFileW(
                        drop,
                        index,
                        buffer.data(),
                        length + 1u) != length) {
                    readFailure_ = true;
                    return;
                }
                paths.emplace_back(buffer.data());
            }
            if (!paths.empty()) {
                batches_.push_back(std::move(paths));
            }
        } catch (...) {
            readFailure_ = true;
        }
    }

    void detach() noexcept {
        if (!attached_ || window_ == nullptr) {
            return;
        }
        DragAcceptFiles(window_, FALSE);
        RemoveWindowSubclass(
            window_,
            &ScopedFileDropTarget::windowProcedure,
            SUBCLASS_ID);
        attached_ = false;
        window_ = nullptr;
    }

    static LRESULT CALLBACK windowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR referenceData) noexcept {
        auto* self =
            reinterpret_cast<ScopedFileDropTarget*>(referenceData);
        if (message == WM_DROPFILES && self != nullptr) {
            self->capture(reinterpret_cast<HDROP>(wParam));
            return 0;
        }
        if (message == WM_NCDESTROY && self != nullptr) {
            self->attached_ = false;
            self->window_ = nullptr;
            RemoveWindowSubclass(
                window,
                &ScopedFileDropTarget::windowProcedure,
                subclassId);
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }

    HWND window_ = nullptr;
    bool attached_ = false;
    bool readFailure_ = false;
    std::vector<std::vector<std::filesystem::path>> batches_;
};
#endif

std::filesystem::path chooseGeometryFile(sf::WindowHandle owner) {
#ifdef _WIN32
    std::array<wchar_t, 32768> filename{};
    const wchar_t filter[] =
        L"3D models (*.stl;*.obj)\0*.stl;*.obj\0"
        L"STL files (*.stl)\0*.stl\0"
        L"OBJ files (*.obj)\0*.obj\0"
        L"All files (*.*)\0*.*\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = reinterpret_cast<HWND>(owner);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.Flags =
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog) != FALSE) {
        return std::filesystem::path(filename.data());
    }
#else
    (void)owner;
#endif
    return {};
}

std::filesystem::path chooseSolverExecutable(
    sf::WindowHandle owner,
    std::string& error) {
#ifdef _WIN32
    std::array<wchar_t, 32768> filename{};
    const wchar_t filter[] =
        L"Windows executables (*.exe)\0*.exe\0"
        L"All files (*.*)\0*.*\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = reinterpret_cast<HWND>(owner);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrTitle = L"Select Fluid Solver executable";
    dialog.lpstrDefExt = L"exe";
    dialog.Flags =
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog) != FALSE) {
        return std::filesystem::path(filename.data());
    }
    const DWORD dialogError = CommDlgExtendedError();
    if (dialogError != 0) {
        error = "Windows solver file dialog failed with error " +
            std::to_string(dialogError) + ".";
    }
#else
    (void)owner;
    (void)error;
#endif
    return {};
}

std::filesystem::path chooseOutputFolder(
    sf::WindowHandle owner,
    std::string& error) {
#ifdef _WIN32
    BROWSEINFOW dialog{};
    dialog.hwndOwner = reinterpret_cast<HWND>(owner);
    dialog.lpszTitle = L"Select the parent folder for simulation runs";
    dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE selection = SHBrowseForFolderW(&dialog);
    if (selection == nullptr) {
        return {};
    }
    std::array<wchar_t, MAX_PATH> path{};
    const BOOL decoded = SHGetPathFromIDListW(selection, path.data());
    CoTaskMemFree(selection);
    if (decoded == FALSE) {
        error = "Windows could not decode the selected output folder.";
        return {};
    }
    return std::filesystem::path(path.data());
#else
    (void)owner;
    error = "Output folder selection is implemented for Windows only.";
    return {};
#endif
}

std::filesystem::path chooseUiConfigFile(
    sf::WindowHandle owner,
    bool save,
    const std::filesystem::path& fallback,
    std::string& error) {
#ifdef _WIN32
    std::array<wchar_t, 32768> filename{};
    const wchar_t filter[] =
        L"CFD Mask UI configuration (*.cfdui)\0*.cfdui\0"
        L"Text files (*.txt)\0*.txt\0"
        L"All files (*.*)\0*.*\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = reinterpret_cast<HWND>(owner);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrTitle = save
        ? L"Save CFD Mask UI configuration"
        : L"Load CFD Mask UI configuration";
    dialog.lpstrDefExt = L"cfdui";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!save) {
        dialog.Flags |= OFN_FILEMUSTEXIST;
    } else {
        dialog.Flags |= OFN_OVERWRITEPROMPT;
    }

    const BOOL accepted = save
        ? GetSaveFileNameW(&dialog)
        : GetOpenFileNameW(&dialog);
    if (accepted != FALSE) {
        return std::filesystem::path(filename.data());
    }
    const DWORD dialogError = CommDlgExtendedError();
    if (dialogError != 0) {
        error = "Windows configuration file dialog failed with error " +
            std::to_string(dialogError) + ".";
    }
    (void)fallback;
    return {};
#else
    (void)owner;
    if (fallback.empty()) {
        error = "There is no file dialog here and no default path to use.";
        return {};
    }
    std::error_code fileError;
    if (!save && (!std::filesystem::is_regular_file(fallback, fileError) ||
                  fileError)) {
        error = "There is no file dialog here, so the configuration is read "
                "from " + fallback.string() + ", and that file is not there.";
        return {};
    }
    return fallback;
#endif
}

std::vector<std::filesystem::path> chooseVtkFiles(
    sf::WindowHandle owner,
    std::string& error) {
#ifdef _WIN32
    std::vector<wchar_t> selectionBuffer(65536u, L'\0');
    const wchar_t filter[] =
        L"VTK solution frames (*.vtk)\0*.vtk\0"
        L"All files (*.*)\0*.*\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = reinterpret_cast<HWND>(owner);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = selectionBuffer.data();
    dialog.nMaxFile = static_cast<DWORD>(selectionBuffer.size());
    dialog.lpstrTitle = L"Open VTK solution frame(s)";
    dialog.lpstrDefExt = L"vtk";
    dialog.Flags =
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
        OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (GetOpenFileNameW(&dialog) == FALSE) {
        const DWORD dialogError = CommDlgExtendedError();
        if (dialogError != 0) {
            error =
                "Windows VTK file dialog failed with error " +
                std::to_string(dialogError) + ".";
        }
        return {};
    }

    const wchar_t* cursor = selectionBuffer.data();
    const std::filesystem::path first(cursor);
    cursor += std::char_traits<wchar_t>::length(cursor) + 1u;
    if (*cursor == L'\0') {
        return {first};
    }

    std::vector<std::filesystem::path> paths;
    while (*cursor != L'\0') {
        paths.push_back(first / cursor);
        cursor += std::char_traits<wchar_t>::length(cursor) + 1u;
    }
    return paths;
#else
    (void)owner;
    error = "VTK file selection is implemented for Windows only.";
    return {};
#endif
}

void includeDataRange(DataRange& combined, const DataRange& range) {
    if (!range.available) {
        return;
    }
    if (!combined.available) {
        combined = range;
        return;
    }
    combined.minimum = std::min(combined.minimum, range.minimum);
    combined.maximum = std::max(combined.maximum, range.maximum);
}

bool openExplorerTarget(
    sf::WindowHandle owner,
    const ExplorerTarget& target,
    std::string& error) {
#ifdef _WIN32
    if (target.location.empty()) {
        error = "Explorer target is empty.";
        return false;
    }

    std::error_code filesystemError;
    const bool targetExists = target.selectFile
        ? std::filesystem::is_regular_file(
              target.location,
              filesystemError)
        : std::filesystem::is_directory(
              target.location,
              filesystemError);
    if (filesystemError || !targetExists) {
        error =
            (target.selectFile ? "VTK file is unavailable: "
                               : "VTK run directory is unavailable: ") +
            target.location.string();
        return false;
    }

    const std::filesystem::path absolute =
        std::filesystem::absolute(target.location, filesystemError);
    if (filesystemError) {
        error =
            "Cannot resolve Explorer target: " +
            filesystemError.message();
        return false;
    }

    HINSTANCE result = nullptr;
    if (target.selectFile) {
        const std::wstring parameters =
            L"/select,\"" + absolute.wstring() + L"\"";
        result = ShellExecuteW(
            reinterpret_cast<HWND>(owner),
            L"open",
            L"explorer.exe",
            parameters.c_str(),
            nullptr,
            SW_SHOWNORMAL);
    } else {
        result = ShellExecuteW(
            reinterpret_cast<HWND>(owner),
            L"open",
            absolute.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
    }

    const std::intptr_t resultCode =
        reinterpret_cast<std::intptr_t>(result);
    if (resultCode <= 32) {
        error =
            "Windows Explorer failed with code " +
            std::to_string(resultCode) + ".";
        return false;
    }
    return true;
#else
    // The UI ships for Linux and macOS now, so "show the folder" has to work
    // there rather than apologise. Both systems have one command for it, and
    // neither can select a file inside the window, so the containing folder is
    // what gets opened.
    (void)owner;
    if (target.location.empty()) {
        error = "No folder to show.";
        return false;
    }
    std::error_code filesystemError;
    const std::filesystem::path resolved =
        std::filesystem::absolute(target.location, filesystemError);
    if (filesystemError) {
        error = "Cannot resolve the folder to show: " +
            filesystemError.message();
        return false;
    }
    const std::filesystem::path folder =
        target.selectFile ? resolved.parent_path() : resolved;
    if (!std::filesystem::is_directory(folder, filesystemError)) {
        error = "Folder is unavailable: " + folder.string();
        return false;
    }
#if defined(__APPLE__)
    const std::string opener = "open";
#else
    const std::string opener = "xdg-open";
#endif
    std::string quoted;
    quoted.reserve(folder.string().size() + 8);
    for (const char character : folder.string()) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    const std::string command =
        opener + " '" + quoted + "' >/dev/null 2>&1 &";
    if (std::system(command.c_str()) != 0) {
        error = "Could not run " + opener + " for " + folder.string() + ".";
        return false;
    }
    return true;
#endif
}

std::filesystem::path defaultOutputRoot(
    const std::filesystem::path& executablePath) {
    // "output" beside the executable, which is where the solver writes its own
    // frames and where the installers create the folder. The working directory
    // is not that place: a Start Menu shortcut, a desktop icon or a drag-and-
    // drop all hand the process some other directory, and the frames used to
    // land wherever that happened to be.
    const std::filesystem::path directory = executablePath.parent_path();
    if (!directory.empty()) {
        return (directory / "output").lexically_normal();
    }

    // Only reachable when the executable path is unknown, which the platform
    // lookups make unlikely. Keep an absolute fallback so the solver's
    // absolute-output contract is still satisfied.
    std::error_code filesystemError;
    const std::filesystem::path current =
        std::filesystem::current_path(filesystemError);
    if (!filesystemError) {
        return (current / "output").lexically_normal();
    }
    return std::filesystem::path("output");
}

std::filesystem::path createRunDirectory(
    const std::filesystem::path& root,
    std::string& error) {
    if (root.empty()) {
        error = "Output folder is empty.";
        return {};
    }
    const auto timestamp =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        root / ("run-" + std::to_string(timestamp));
    std::error_code filesystemError;
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) {
        error = "Cannot create run directory: " + filesystemError.message();
        return {};
    }
    return directory;
}

std::string readDiagnosticFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    std::string text{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    for (char& character : text) {
        if (character == '\r' || character == '\n') {
            character = ' ';
        }
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

double contourArea(const std::vector<Vec2>& contour) {
    if (contour.size() < 3) {
        return 0.0;
    }
    double twiceArea = 0.0;
    for (std::size_t index = 0; index < contour.size(); ++index) {
        const Vec2& current = contour[index];
        const Vec2& next = contour[(index + 1) % contour.size()];
        twiceArea += current.x * next.y - next.x * current.y;
    }
    return 0.5 * std::abs(twiceArea);
}

struct SolverExecutableInfo {
    bool valid = false;
    bool recognized = false;
    bool cudaCapable = false;
    bool avx2Capable = false;
    bool openMpCapable = false;
    // What this solver understands, worked out from its version. A 0.1 build
    // exits on the first argument it does not know, so every one of these has
    // to be checked before the argument is put on the command line.
    bool supportsRuntimeSwitches = false;   // avx2= openmp= threads= tray=
    bool supportsContinuation = false;      // restart= restartFile= addTime=
    bool supportsMultipleContours = false;  // more than one closed loop
    bool supportsGravity = false;           // gravityEnabled= Accel= Angle=
    bool supportsWallMotion = false;        // wallMotion=
    bool supportsBodyMotion = false;
    bool supportsTurbulence = false;
    bool supportsCompressible = false;
    bool supportsProfiles = false;
    bool supportsExtraFields = false;
    bool supportsSchemes = false;
    bool supportsBoundaries = false;
    bool supportsCase = false;
    bool supportsPhases = false;
    bool supportsTension = false;
    bool supportsVolume = false;
    std::string version = "unknown";
    std::string features;
    std::string build = "Unknown build";
    std::string detail;
};

bool binaryContains(const std::filesystem::path& path,
                    const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    const std::string bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    return bytes.find(needle) != std::string::npos;
}

bool binaryContainsUtf16Ascii(const std::filesystem::path& path,
                              const std::string& text) {
    std::string encoded;
    encoded.reserve(text.size() * 2u);
    for (const unsigned char character : text) {
        encoded.push_back(static_cast<char>(character));
        encoded.push_back('\0');
    }
    return binaryContains(path, encoded);
}

// The printable run of characters that follows a marker inside the executable.
// Fluid Solver 0.2 and newer carry "FluidSolverVersion=0.2" and
// "FluidSolverFeatures=avx2-omp-cuda" as single literals precisely so this
// works, and prints the same two lines for "--version" so the two can never
// disagree. Older builds have neither, and the empty string is how that is
// reported.
std::string binaryValueAfter(const std::filesystem::path& path,
                             const std::string& marker) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    const std::string bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    const std::size_t at = bytes.find(marker);
    if (at == std::string::npos) {
        return {};
    }
    std::string value;
    for (std::size_t index = at + marker.size();
         index < bytes.size() && value.size() < 32u;
         ++index) {
        const unsigned char character =
            static_cast<unsigned char>(bytes[index]);
        // Versions and feature tags are digits, dots and dashes. Anything else
        // ends the value - including the terminating zero.
        if (std::isalnum(character) || character == '.' || character == '-') {
            value.push_back(static_cast<char>(character));
        } else {
            break;
        }
    }
    return value;
}

// -1 when a < b, 0 when equal, 1 when a > b, compared number by number so
// "0.10" is newer than "0.9" rather than alphabetically older.
int compareSolverVersions(const std::string& first, const std::string& second) {
    const auto numbers = [](const std::string& text) {
        std::vector<long> parts;
        long current = 0;
        bool inNumber = false;
        for (const char character : text) {
            if (character >= '0' && character <= '9') {
                current = current * 10 + (character - '0');
                inNumber = true;
            } else if (character == '.') {
                parts.push_back(inNumber ? current : 0);
                current = 0;
                inNumber = false;
            } else {
                break;
            }
        }
        if (inNumber) {
            parts.push_back(current);
        }
        return parts;
    };
    const std::vector<long> left = numbers(first);
    const std::vector<long> right = numbers(second);
    const std::size_t count = std::max(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        const long a = index < left.size() ? left[index] : 0;
        const long b = index < right.size() ? right[index] : 0;
        if (a < b) {
            return -1;
        }
        if (a > b) {
            return 1;
        }
    }
    return 0;
}

SolverExecutableInfo inspectSolverExecutable(
    const std::filesystem::path& executable) {
    SolverExecutableInfo info;
    std::string error;
    info.valid = validateFluidSolverExecutable(executable, error);
    if (!info.valid) {
        info.detail = error;
        return info;
    }

    std::string filename = executable.filename().string();
    std::transform(
        filename.begin(), filename.end(), filename.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    // "Fluid Solver.exe" on Windows, "Fluid Solver" everywhere else. Insisting
    // on the .exe meant the UI refused to run a perfectly good Linux or macOS
    // build - it is the same program, and the release ships it under both
    // names on purpose.
    const bool correctName =
        filename == "fluid solver.exe" || filename == "fluid solver";
    const bool productMarker =
        binaryContainsUtf16Ascii(executable, "Fluid Solver") ||
        binaryContains(executable, "Fluid Solver");
    info.recognized = correctName && productMarker;

    // 0.2 and newer say what they are. Everything below is the guesswork that
    // had to stand in before they did.
    info.version = binaryValueAfter(executable, "FluidSolverVersion=");
    info.features = binaryValueAfter(executable, "FluidSolverFeatures=");
    if (info.version.empty()) {
        info.version =
            binaryContainsUtf16Ascii(executable, "0.1.0") ? "0.1.0" : "unknown";
    }

    if (!info.features.empty()) {
        const auto hasTag = [&info](const char* tag) {
            return info.features.find(tag) != std::string::npos;
        };
        info.cudaCapable = hasTag("cuda");
        info.avx2Capable = hasTag("avx2");
        info.openMpCapable = hasTag("omp");
        info.build = info.features;
    } else {
        info.cudaCapable =
            binaryContains(executable, "MultigridCuda.cu") ||
            binaryContains(executable, "CUDA error at");
        // Nothing in a 0.1 binary says whether AVX2 or OpenMP went in, and
        // every published 0.1 row that the UI could plausibly be pointed at
        // had both, so this is what it assumed then and what it keeps
        // assuming for those.
        info.avx2Capable = true;
        info.openMpCapable = true;
        info.build = info.cudaCapable
            ? "AVX2 + OpenMP + CUDA"
            : "AVX2 + OpenMP";
    }

    // What the solver can be asked to do, rather than what it is made of.
    // A 0.1 solver stops on the first argument it does not know, so anything
    // added since has to be held back from it.
    info.supportsRuntimeSwitches =
        info.version != "unknown" &&
        compareSolverVersions(info.version, "0.2") >= 0;
    info.supportsMultipleContours = info.supportsRuntimeSwitches;
    // These two came after 0.2 was published, so a version number cannot tell
    // them apart. The key names are in the binary's own parameter table, which
    // is the thing that decides whether the argument is accepted.
    info.supportsGravity = binaryContains(executable, "gravityEnabled");
    info.supportsWallMotion = binaryContains(executable, "wallMotion");
    info.supportsBodyMotion = binaryContains(executable, "bodyMotion");
    info.supportsTurbulence = binaryContains(executable, "turbLengthScale");
    info.supportsCompressible = binaryContains(executable, "machInlet");
    info.supportsProfiles = binaryContains(executable, "profiles");
    info.supportsExtraFields = binaryContains(executable, "extraFields");
    info.supportsSchemes = binaryContains(executable, "timeScheme");
    info.supportsBoundaries = binaryContains(executable, "bcBottom");
    info.supportsCase = binaryContains(executable, "caseType");
    info.supportsPhases = binaryContains(executable, "vofScheme");
    info.supportsTension = binaryContains(executable, "surfaceTension");
    info.supportsVolume = binaryContains(executable, "gravityTilt");
    info.supportsContinuation =
        info.version != "unknown" &&
        compareSolverVersions(info.version, "0.1.1") >= 0;

    if (!info.recognized) {
        info.detail =
            "Executable is not recognized as the expected Fluid Solver build.";
    }
    return info;
}

// The solver numbers the bodies by flood-filling its mask 8-connected in grid
// scan order, and wallMotion is written in those numbers. Counting them the
// same way here is what lets the UI address them at all.
std::size_t countSolidBodies(const std::vector<int>& cells, int nx, int ny) {
    if (nx <= 0 || ny <= 0 ||
        cells.size() < static_cast<std::size_t>(nx) * ny) {
        return 0;
    }
    std::vector<char> seen(cells.size(), 0);
    std::vector<int> pending;
    std::size_t bodies = 0;
    for (int seed = 0; seed < nx * ny; ++seed) {
        if (cells[seed] == 0 || seen[seed]) {
            continue;
        }
        ++bodies;
        seen[seed] = 1;
        pending.push_back(seed);
        while (!pending.empty()) {
            const int id = pending.back();
            pending.pop_back();
            const int i = id % nx;
            const int j = id / nx;
            for (int nj = std::max(j - 1, 0); nj <= std::min(j + 1, ny - 1); ++nj) {
                for (int ni = std::max(i - 1, 0); ni <= std::min(i + 1, nx - 1); ++ni) {
                    const int neighbour = nj * nx + ni;
                    if (cells[neighbour] == 0 || seen[neighbour]) {
                        continue;
                    }
                    seen[neighbour] = 1;
                    pending.push_back(neighbour);
                }
            }
        }
    }
    return bodies;
}

bool confirmUseLargestContour(sf::WindowHandle owner,
                              std::size_t contourCount) {
#ifdef _WIN32
    const std::wstring message =
        L"The current Fluid Solver uses one closed contour, but the UI "
        L"detected " + std::to_wstring(contourCount) +
        L" disconnected contours.\n\n"
        L"Continue using only the largest contour?\n\n"
        L"The preview and written adapter will be reduced to that contour "
        L"so the UI and solver solve the same geometry.";
    return MessageBoxW(
        reinterpret_cast<HWND>(owner),
        message.c_str(),
        L"Multiple contours detected",
        MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
#else
    (void)owner;
    (void)contourCount;
    return false;
#endif
}

bool confirmStopAndExit(sf::WindowHandle owner) {
#ifdef _WIN32
    return MessageBoxW(
        reinterpret_cast<HWND>(owner),
        L"A simulation is still running. Stop the Fluid Solver and exit?",
        L"Simulation is running",
        MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
#else
    (void)owner;
    return false;
#endif
}

} // namespace

class Application::Implementation {
public:
    Implementation(
        std::filesystem::path executablePath,
        std::filesystem::path initialModelPath)
        : executablePath_(std::move(executablePath)),
          initialModelPath_(std::move(initialModelPath)),
          solverSelectionFile_(
              executablePath_.parent_path() / "solver-selection.txt"),
          preferencesFile_(
              executablePath_.parent_path() / "ui-preferences.txt"),
          fluidSolverExecutable_(resolveFluidSolverExecutable(
              executablePath_,
              std::filesystem::path(CFD_SOLVER_EXE))),
          outputRoot_(defaultOutputRoot(executablePath_)),
          sliders_{
              Slider{"Wind speed U0", "m/s", 0.0, 200.0, 1.0, false, false},
              Slider{"Viscosity nu", "m2/s", 1e-8, 10.0, 0.01, false, true},
              Slider{"Density rho (ro)", "kg/m3", 0.01, 5000.0, 1.225, false, true},
              Slider{"Phases", "", 1.0, 2.0, 1.0, true, false},
              Slider{"Fluid 1 density", "kg/m3", 0.01, 20000.0, 1000.0, false, true},
              Slider{"Fluid 1 viscosity", "m2/s", 1e-8, 10.0, 1e-6, false, true},
              Slider{"Fluid 2 density", "kg/m3", 0.01, 20000.0, 1.225, false, true},
              Slider{"Fluid 2 viscosity", "m2/s", 1e-8, 10.0, 1.5e-5, false, true},
              Slider{"Start shape", "", 0.0, 3.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"layer", "drop", "column", "file"}},
              Slider{"Start level", "", 0.0, 1.0, 0.5, false, false},
              Slider{"Start x", "", 0.0, 1.0, 0.5, false, false},
              Slider{"Start y", "", 0.0, 1.0, 0.5, false, false},
              Slider{"Start z", "", 0.0, 1.0, 0.5, false, false},
              Slider{"Interface scheme", "", 0.0, 2.0, 1.0, true, false, false, 0.0,
                     ControlKind::Choice, {"upwind", "hric", "cicsam"}},
              Slider{"Mixing", "", 0.0, 1.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"immiscible", "miscible"}},
              Slider{"Diffusivity", "m2/s", 0.0, 1.0, 1e-6, false, true},
              Slider{"Surface tension", "mN/m", 0.0, 1000.0, 0.0, false, false},
              Slider{"Contact angle", "deg", 0.0, 180.0, 90.0, false, false},
              Slider{"Flow sources", "", 0.0, 1.0, 0.0, false, false, false, 0.0,
                     ControlKind::Text},
              Slider{"Gravity", "", 0.0, 1.0, 0.0, true, false, true},
              Slider{"Gravity g", "m/s2", 0.0, 100.0, 9.81, false, false},
              Slider{"Gravity angle", "deg", -180.0, 180.0, 0.0, false, false},
              Slider{"Gravity tilt", "deg", -180.0, 180.0, 0.0, false, false},
              Slider{"Gravity mode", "", 0.0, 1.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"reduced", "body"}},
              Slider{"Regime", "", 0.0, 1.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"incompressible", "compressible"}},
              Slider{"gamma", "", 1.01, 3.0, 1.4, false, false},
              Slider{"Gas constant R", "J/(kg K)", 1.0, 100000.0, 287.05,
                     false, true},
              Slider{"gamma, gas 2", "", 1.01, 3.0, 1.667, false, false},
              Slider{"R, gas 2", "J/(kg K)", 1.0, 100000.0, 2077.0, false,
                     true},
              Slider{"Temperature T0", "K", 1.0, 100000.0, 288.15, false, true},
              Slider{"Ambient pressure", "Pa", 1.0, 1e12, 101325.0, false,
                     true},
              Slider{"Inlet Mach", "", 0.0, 20.0, 0.5, false, false},
              Slider{"Species mode", "", 0.0, 1.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"active", "passive"}},
              Slider{"Grid stretch", "", 0.0, 3.0, 0.0, true, false, false,
                     0.0, ControlKind::Choice,
                     {"off", "body", "wake", "edges"}},
              Slider{"Stretch ratio", "", 1.0, 1.5, 1.05, false, false},
              Slider{"Fine band", "", 0.01, 1.0, 0.25, false, false},
              Slider{"Refinement levels", "", 0.0, 4.0, 0.0, true, false},
              Slider{"Refine on", "", 0.0, 4.0, 4.0, true, false, false, 0.0,
                     ControlKind::Choice,
                     {"density", "vorticity", "species", "body",
                      "everything"}},
              Slider{"Refine above", "", 0.001, 1.0, 0.2, false, true},
              Slider{"Regrid every", "steps", 1.0, 100000.0, 8.0, true, true},
              Slider{"Acoustic fields", "", 0.0, 1.0, 0.0, true, false, true},
              Slider{"Acoustic window", "s", 1e-6, 1000.0, 0.02, false, true},
              Slider{"0 dB reference", "Pa", 1e-12, 1e6, 2e-5, false, true},
              Slider{"Microphones", "", 0.0, 1.0, 0.0, false, false, false, 0.0,
                     ControlKind::Text},
              Slider{"Mic interval", "steps", 1.0, 1000000.0, 1.0, true, true},
              Slider{"Write .wav", "", 0.0, 1.0, 0.0, true, false, true},
              Slider{"Audio rate", "Hz", 1000.0, 384000.0, 44100.0, true,
                     false},
              Slider{"Audio speed", "x real", 1e-4, 1000.0, 1.0, false, true},
              Slider{"Turbulence model", "", 0.0, 2.0, 0.0, true, false, false,
                     0.0, ControlKind::Choice,
                     {"none", "smagorinsky", "kOmegaSST"}},
              Slider{"Smagorinsky Cs", "", 0.01, 1.0, 0.17, false, false},
              Slider{"Turbulence intensity", "", 0.0, 1.0, 0.05, false, false},
              Slider{"Turbulence length", "m", 0.0, 100.0, 0.0, false, false},
              Slider{"Slice X", "deg", -180.0, 180.0, 90.0, true, false},
              Slider{"Slice Y", "deg", -180.0, 180.0, 0.0, true, false},
              Slider{"Slice Z", "deg", -180.0, 180.0, 90.0, true, false},
              Slider{"Slice rotation", "deg", -180.0, 180.0, 0.0, false, false},
              Slider{"Extra profiles", "", 0.0, 1.0, 0.0, false, false, false, 0.0,
                     ControlKind::Text},
              Slider{"Case", "", 0.0, 1.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"channel", "cavity"}},
              Slider{"Lid speed", "m/s", -200.0, 200.0, 1.0, false, false},
              Slider{"Left boundary", "", 0.0, 4.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"inlet", "outlet", "wall", "movingWall", "slip"}},
              Slider{"Right boundary", "", 0.0, 4.0, 1.0, true, false, false, 0.0,
                     ControlKind::Choice, {"inlet", "outlet", "wall", "movingWall", "slip"}},
              Slider{"Bottom boundary", "", 0.0, 4.0, 4.0, true, false, false, 0.0,
                     ControlKind::Choice, {"inlet", "outlet", "wall", "movingWall", "slip"}},
              Slider{"Top boundary", "", 0.0, 4.0, 4.0, true, false, false, 0.0,
                     ControlKind::Choice, {"inlet", "outlet", "wall", "movingWall", "slip"}},
              Slider{"Front boundary", "", 0.0, 4.0, 4.0, true, false, false, 0.0,
                     ControlKind::Choice, {"inlet", "outlet", "wall", "movingWall", "slip"}},
              Slider{"Back boundary", "", 0.0, 4.0, 4.0, true, false, false, 0.0,
                     ControlKind::Choice, {"inlet", "outlet", "wall", "movingWall", "slip"}},
              Slider{"Left side speed", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Right side speed", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Bottom side speed", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Top side speed", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Front side speed", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Back side speed", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Inlet band from", "", 0.0, 1.0, 0.0, false, false},
              Slider{"Inlet band to", "", 0.0, 1.0, 1.0, false, false},
              Slider{"Inlet span from", "", 0.0, 1.0, 0.0, false, false},
              Slider{"Inlet span to", "", 0.0, 1.0, 1.0, false, false},
              Slider{"Inlet profile", "", 0.0, 2.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice,
                     {"uniform", "parabolic", "parabolicSpan"}},
              Slider{"Wall motion", "", 0.0, 1.0, 0.0, false, false, false, 0.0,
                     ControlKind::Text},
              Slider{"Body", "", 1.0, 32.0, 1.0, true, false},
              Slider{"Position X", "m", -1000.0, 1000.0, 0.0, false, false},
              Slider{"Position Y", "m", -1000.0, 1000.0, 0.0, false, false},
              Slider{"Position Z", "m", -1000.0, 1000.0, 0.0, false, false},
              Slider{"Size", "m", 0.0, 1000.0, 0.0, false, false},
              Slider{"Tilt X", "deg", -180.0, 180.0, 0.0, false, false},
              Slider{"Tilt Y", "deg", -180.0, 180.0, 0.0, false, false},
              Slider{"Tilt Z", "deg", -180.0, 180.0, 0.0, false, false},
              Slider{"Turn in plane", "deg", -180.0, 180.0, 0.0, false, false},
              Slider{"Move along", "", 0.0, 2.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"x", "y", "z"}},
              Slider{"Move by", "m", -1000.0, 1000.0, 0.0, false, false},
              Slider{"Turn about", "", 0.0, 3.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"x", "y", "z", "plane"}},
              Slider{"Turn by", "deg", -360.0, 360.0, 0.0, false, false},
              Slider{"Behaviour", "", 0.0, 4.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice,
                     {"static", "drag", "slip", "travel", "free"}},
              Slider{"Surface spin", "deg/s", -3600.0, 3600.0, 0.0, false, false},
              Slider{"Surface roll X", "deg/s", -3600.0, 3600.0, 0.0, false, false},
              Slider{"Surface roll Y", "deg/s", -3600.0, 3600.0, 0.0, false, false},
              Slider{"Surface slide X", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Surface slide Y", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Surface slide Z", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Body velocity X", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Body velocity Y", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Body velocity Z", "m/s", -200.0, 200.0, 0.0, false, false},
              Slider{"Body spin", "deg/s", -3600.0, 3600.0, 0.0, false, false},
              Slider{"Body roll X", "deg/s", -3600.0, 3600.0, 0.0, false, false},
              Slider{"Body roll Y", "deg/s", -3600.0, 3600.0, 0.0, false, false},
              Slider{"Body mass", "kg/m", 0.0, 100000.0, 0.0, false, false},
              Slider{"Body density", "kg/m3", 0.0, 25000.0, 0.0, false, false},
              Slider{"Body inertia X", "kg m2", 0.0, 100000.0, 0.0, false, false},
              Slider{"Body inertia Y", "kg m2", 0.0, 100000.0, 0.0, false, false},
              Slider{"Pinned", "", 0.0, 7.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice,
                     {"nothing", "x", "y", "x+y", "spin", "x+spin",
                      "y+spin", "everything"}},
              Slider{"Pin z", "", 0.0, 1.0, 0.0, true, false, true},
              Slider{"Pin roll X", "", 0.0, 1.0, 0.0, true, false, true},
              Slider{"Pin roll Y", "", 0.0, 1.0, 0.0, true, false, true},
              Slider{"Body motion", "", 0.0, 1.0, 0.0, false, false, false, 0.0,
                     ControlKind::Text},
              Slider{"Body track", "", 0.0, 1.0, 0.0, false, false, false, 0.0,
                     ControlKind::Text},
              Slider{"Body path", "", 0.0, 1.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"curve", "keys"}},
              Slider{"Coupling", "", 0.0, 2.0, 1.0, true, false, false, 0.0,
                     ControlKind::Choice, {"weak", "added", "strong"}},
              Slider{"Collisions", "", 0.0, 1.0, 0.0, true, false, true},
              Slider{"Bounciness", "", 0.0, 1.0, 0.2, false, false},
              Slider{"Report forces", "", 0.0, 1.0, 0.0, true, false, true},
              Slider{"Domain Lx", "m", 0.01, 100.0, 1.0, false, true},
              Slider{"Domain Ly", "m", 0.01, 100.0, 1.0, false, true},
              Slider{"Domain Lz", "m", 0.01, 100.0, 1.0, false, true},
              Slider{"Cells nx", "", 8.0, 5000.0, 50.0, true, true},
              Slider{"Cells ny", "", 8.0, 5000.0, 50.0, true, true},
              Slider{"Cells nz", "", 1.0, 1024.0, 1.0, true, true},
              Slider{"CFL", "", 0.01, 1.0, 0.5, false, false},
              Slider{"Total time", "s", 0.001, 10000.0, 10.0, false, true},
              Slider{"Stop when steady", "", 0.0, 0.001, 0.0, false, false},
              Slider{"Continue: add time", "s", 0.0, 10000.0, 0.0, false, true},
              Slider{"dt update interval", "steps", 1.0, 1000.0, 5.0, true, false},
              Slider{"dt safety", "", 0.01, 1.0, 0.9, false, false},
              Slider{"Convection", "", 0.0, 2.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"upwind", "muscl", "central"}},
              Slider{"Limiter", "", 0.0, 2.0, 1.0, true, false, false, 0.0,
                     ControlKind::Choice, {"minmod", "vanLeer", "superbee"}},
              Slider{"Time scheme", "", 0.0, 2.0, 0.0, true, false, false, 0.0,
                     ControlKind::Choice, {"euler", "rk2", "rk3"}},
              Slider{"Coarse SOR omega", "", 0.1, 1.99, 1.85, false, false},
              Slider{"MG smoother omega", "", 0.1, 1.99, 1.15, false, false},
              Slider{"MG V-cycles", "", 1.0, 100.0, 2.0, true, false},
              Slider{"MG tolerance", "", 1e-10, 1e-2, 1e-4, false, true},
              Slider{"MG minimum coarse size", "cells", 1.0, 512.0, 8.0, true, false},
              Slider{"VTK save interval", "steps", 1.0, 100000.0, 20.0, true, true},
              Slider{"Extra frame fields", "", 0.0, 1.0, 0.0, false, false, false, 0.0,
                     ControlKind::Text},
              Slider{"Request CUDA", "", 0.0, 1.0, 1.0, true, false, true},
              Slider{"Use AVX2", "", 0.0, 1.0, 1.0, true, false, true},
              Slider{"Use OpenMP", "", 0.0, 1.0, 1.0, true, false, true},
              Slider{"Solver threads", "", 0.0, 256.0, 0.0, true, false},
              Slider{"VTK cache", "MB", 128.0, 4096.0, 1024.0, true, true}
          } {
        for (Slider& slider : sliders_) {
            slider.defaultValue = slider.value;
            slider.defaultText = slider.text;
        }
        loadPreferences();
        std::string selectionError;
        const std::optional<std::filesystem::path> selected =
            readFluidSolverSelection(
                solverSelectionFile_,
                selectionError);
        if (selected) {
            fluidSolverExecutable_ = *selected;
        } else if (!selectionError.empty()) {
            status_ =
                "Saved solver selection is unavailable; using fallback. " +
                selectionError;
        }
        std::string solverError;
        if (!validateFluidSolverExecutable(
                fluidSolverExecutable_, solverError)) {
            status_ =
                "Default solver not found; use Select solver EXE before "
                "running. " + solverError;
        }
        refreshSolverInfo();
        if (solverInfo_.valid && !solverInfo_.recognized) {
            status_ =
                "Default EXE is not recognized as Fluid Solver.exe; select "
                "one of the finished Fluid Solver builds before running.";
        }
        applyCacheBudget();
    }

    int run() {
        if (!loadFont()) {
            return 2;
        }

        // The 3D viewport needs a depth buffer, and SFML asks for none unless
        // it is told to. Without one Viewport3D has to draw with the depth
        // test off, which paints every triangle in the order it was submitted:
        // a slice plane then sits in front of or behind the body depending on
        // nothing but that order, and swings through it as the camera turns.
        sf::ContextSettings contextSettings;
        contextSettings.depthBits = 24;

        sf::RenderWindow window(
            sf::VideoMode({1600u, 900u}),
            std::string("CFD Mask UI ") + CFD_MASK_UI_VERSION,
            sf::Style::Default,
            sf::State::Windowed,
            contextSettings);
        window.setMinimumSize(sf::Vector2u{1000u, 800u});
        window.setFramerateLimit(60);
        window_ = &window;
        windowTitle_ = std::string("CFD Mask UI ") + CFD_MASK_UI_VERSION;
        // The icon belongs to the window as well as to the tray: without this
        // the taskbar button and the Alt+Tab list show SFML's default while
        // Explorer shows the one compiled into the executable.
        applyWindowIcon();
        tray_.attach(
            reinterpret_cast<void*>(window.getNativeHandle()),
            windowTitle_);
#ifdef _WIN32
        ScopedFileDropTarget fileDropTarget(window.getNativeHandle());
        if (!fileDropTarget.available()) {
            status_ =
                "VTK drag-and-drop is unavailable; use Open VTK frame(s).";
        }
#endif
        if (!initialModelPath_.empty()) {
            std::error_code pathError;
            if (std::filesystem::is_directory(
                    initialModelPath_,
                    pathError) &&
                !pathError) {
                loadExternalResultInputs({initialModelPath_});
            } else if (initialModelPath_.extension() == ".vtk") {
                loadExternalResultInputs({initialModelPath_});
            } else {
                loadGeometry(initialModelPath_);
            }
        }

        const auto hasContinuousInput = [&]() {
            if (!window.hasFocus()) {
                return false;
            }
            const bool planarMovement =
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::W) ||
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::A) ||
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::S) ||
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::D);
            if (mode_ == DisplayMode::Results) {
                return planarMovement;
            }
            return planarMovement ||
                   sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Left) ||
                   sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Right) ||
                   sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Up) ||
                   sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Down) ||
                   sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Q) ||
                   sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::E);
        };
        const auto hasBackgroundWork = [&]() {
            return solverProcess_.active ||
                   resultCatalogFuture_.valid() ||
                   !inFlightFrames_.empty() ||
                   playingFrames_ ||
                   (mode_ == DisplayMode::Results && view3D_ &&
                    view3DSettings_.animateTracers);
        };

        sf::Clock frameClock;
        bool redrawRequested = true;
        while (window.isOpen()) {
            const bool continuousInputBefore = hasContinuousInput();
            const bool backgroundWorkBefore = hasBackgroundWork();
            const sf::Time waitDuration =
                redrawRequested
                    ? sf::milliseconds(1)
                    : (continuousInputBefore
                           ? sf::milliseconds(16)
                           : (backgroundWorkBefore
                                  ? sf::milliseconds(100)
                                  : sf::Time::Zero));
            bool receivedEvent = false;
            if (const std::optional<sf::Event> event =
                    window.waitEvent(waitDuration)) {
                handleEvent(*event);
                receivedEvent = true;
            }
            while (const std::optional<sf::Event> event = window.pollEvent()) {
                handleEvent(*event);
                receivedEvent = true;
            }
            if (!window.isOpen()) {
                break;
            }
            syncWindowLayout(window.getSize());
            syncRowVisibility();
#ifdef _WIN32
            bool receivedFileDrop = false;
            if (fileDropTarget.takeReadFailure()) {
                status_ = "VTK drop failed while reading Windows file paths.";
                receivedFileDrop = true;
            }
            for (const auto& batch : fileDropTarget.takeBatches()) {
                loadExternalResultInputs(batch);
                receivedFileDrop = true;
            }
#endif

            float elapsed =
                std::min(frameClock.restart().asSeconds(), 0.1f);
            const bool continuousInputAfterEvents = hasContinuousInput();
            if (!continuousInputBefore && continuousInputAfterEvents) {
                elapsed = 0.0f;
            }
            update(elapsed);

            redrawRequested =
                redrawRequested || receivedEvent || continuousInputBefore ||
                continuousInputAfterEvents || backgroundWorkBefore ||
                hasBackgroundWork();
#ifdef _WIN32
            redrawRequested = redrawRequested || receivedFileDrop;
#endif
            if (!redrawRequested) {
                continue;
            }

            window.clear(BACKGROUND);
            drawAreas();
            if (mode_ == DisplayMode::Setup) {
                drawSetup();
            } else {
                drawResults();
            }
            drawProperties();
            drawTopTabs();
            drawLoadingIndicator();
            drawStatus();
            window.display();
            redrawRequested = false;
        }
        tray_.detach();
        window_ = nullptr;
        return 0;
    }

private:
    bool loadFont() {
        const std::array<std::filesystem::path, 3> candidates{{
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/arial.ttf",
            executablePath_.parent_path() / "assets/Tuffy.ttf"
        }};
        for (const auto& candidate : candidates) {
            if (font_.openFromFile(candidate)) {
                return true;
            }
        }
        return false;
    }

    void syncRowVisibility() {
        std::string signature = sliders_[RegimeKind].choice();
        signature += sliders_[Phases].value >= 1.5 ? "|2" : "|1";
        signature += sliders_[AcousticFields].value >= 0.5 ? "|a" : "|-";
        signature += sliders_[MicrophoneLine].text.empty() ? "|-" : "|m";
        signature += sliders_[MicAudio].value >= 0.5 ? "|w" : "|-";
        signature += "|" + sliders_[CaseKind].choice();
        signature += volumeRun() ? "|v" : "|-";
        if (signature == visibilitySignature_)
            return;
        visibilitySignature_ = signature;
        updateLayout(layoutSize_);
    }

    void syncWindowLayout(sf::Vector2u size) {
        if (size.x == 0u || size.y == 0u ||
            (size.x == layoutSize_.x && size.y == layoutSize_.y)) {
            return;
        }
        const sf::FloatRect clientBounds{
            {0.0f, 0.0f},
            {
                static_cast<float>(size.x),
                static_cast<float>(size.y)
            }
        };
        window_->setView(sf::View(clientBounds));
        updateLayout(size);
    }

    void updateLayout(sf::Vector2u size) {
        layoutSize_ = size;
        const float width = static_cast<float>(size.x);
        const float height = static_cast<float>(size.y);
        panelX_ = std::max(360.0f, width - LEFT_PANEL_WIDTH);
        {
            float x = 12.0f;
            const auto place = [&](Button& button, float w) {
                button.bounds = {{x, 6.0f}, {w, 32.0f}};
                x += w + 4.0f;
            };
            place(setupTab_, 80.0f);
            place(resultsTab_, 80.0f);
            place(openVtkButton_, 130.0f);
            place(stopSimulationButton_, 128.0f);
            place(revealVtkButton_, 150.0f);
            place(solverExeButton_, 124.0f);
            place(importButton_, 132.0f);
            place(outputFolderButton_, 124.0f);
        }
        resetDefaultsButton_.bounds = {
            {panelX_ + 18.0f, height - 106.0f}, {92.0f, 34.0f}};
        saveConfigButton_.bounds = {
            {panelX_ + 118.0f, height - 106.0f}, {92.0f, 34.0f}};
        loadConfigButton_.bounds = {
            {panelX_ + 218.0f, height - 106.0f}, {94.0f, 34.0f}};
        generateButton_.bounds = {
            {panelX_ + 18.0f, height - 62.0f},
            {294.0f, 38.0f}
        };

        {
            const float gap = 4.0f;
            const float span =
                (294.0f - gap * (PARAMETER_TABS.size() - 1)) /
                PARAMETER_TABS.size();
            float x = panelX_ + 18.0f;
            for (std::size_t index = 0; index < tabButtons_.size(); ++index) {
                tabButtons_[index].label = PARAMETER_TABS[index].label;
                tabButtons_[index].bounds = {
                    {x, PARAMETER_STRIP_TOP}, {span, PARAMETER_STRIP_HEIGHT}};
                tabButtons_[index].selected = index == activeTab_;
                x += span + gap;
            }
        }

        const float parameterBottom =
            std::max(PARAMETER_TOP + 40.0f, height - PARAMETER_BOTTOM_MARGIN);
        const float viewportHeight = parameterBottom - PARAMETER_TOP;

        refreshRowVisibility();
        applyTabAndSearchFilter();

        const auto groupOf = [](std::size_t index) {
            std::size_t found = 0;
            for (std::size_t g = 0; g < PARAMETER_GROUPS.size(); ++g)
                if (PARAMETER_GROUPS[g].firstIndex <= index)
                    found = g;
            return found;
        };

        groupShown_.fill(false);
        groupFirstShown_.fill(0);
        std::size_t shownRows = 0;
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            if (rowHidden_[index])
                continue;
            ++shownRows;
            const std::size_t g = groupOf(index);
            if (!groupShown_[g]) {
                groupShown_[g] = true;
                groupFirstShown_[g] = index;
            }
        }
        const std::size_t shownGroups = static_cast<std::size_t>(
            std::count(groupShown_.begin(), groupShown_.end(), true));

        const float contentHeight =
            static_cast<float>(shownRows) * PARAMETER_ROW_HEIGHT +
            static_cast<float>(shownGroups) * PARAMETER_GROUP_HEIGHT;
        maxParameterScroll_ = std::max(0.0f, contentHeight - viewportHeight);
        parameterScrollOffset_ =
            clampFloat(parameterScrollOffset_, 0.0f, maxParameterScroll_);

        float cursor = PARAMETER_TOP + 24.0f - parameterScrollOffset_;
        groupHeaderY_.fill(-100000.0f);
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            if (rowHidden_[index]) {
                sliders_[index].track =
                    {{panelX_ + 20.0f, -100000.0f}, {278.0f, 5.0f}};
                continue;
            }
            const std::size_t g = groupOf(index);
            if (groupShown_[g] && groupFirstShown_[g] == index) {
                cursor += PARAMETER_GROUP_HEIGHT;
                groupHeaderY_[g] = cursor - 47.0f;
            }
            sliders_[index].track = {{panelX_ + 20.0f, cursor}, {278.0f, 5.0f}};
            cursor += PARAMETER_ROW_HEIGHT;
        }

        const float timelineTop = height - 108.0f;
        setupViewport_ = {
            {12.0f, HEADER_HEIGHT + 8.0f},
            {
                std::max(320.0f, panelX_ - 24.0f),
                std::max(300.0f, timelineTop - HEADER_HEIGHT - 16.0f)
            }
        };

        {
            paintButton_.bounds = {
                {setupViewport_.position.x + 12.0f,
                 setupViewport_.position.y + 12.0f},
                {96.0f, 30.0f}};
            const float y = setupViewport_.position.y +
                            setupViewport_.size.y - 42.0f;
            float x = setupViewport_.position.x + 12.0f;
            const auto place = [&](Button& button, float w) {
                button.bounds = {{x, y}, {w, 30.0f}};
                x += w + 8.0f;
            };
            place(paintFluid1Button_, 78.0f);
            place(paintFluid2Button_, 78.0f);
            place(paintSourceButton_, 78.0f);
            place(paintFillButton_, 62.0f);
            place(paintClearButton_, 62.0f);
            place(paintUndoButton_, 62.0f);

            layoutButton_.bounds = {
                {setupViewport_.position.x + 120.0f,
                 setupViewport_.position.y + 12.0f},
                {96.0f, 30.0f}};
            float lx = setupViewport_.position.x + 228.0f;
            const auto placeLayout = [&](Button& button, float w) {
                button.bounds = {
                    {lx, setupViewport_.position.y + 12.0f}, {w, 30.0f}};
                lx += w + 8.0f;
            };
            placeLayout(layoutKeyButton_, 86.0f);
            placeLayout(layoutDropButton_, 78.0f);
            placeLayout(layoutInterpButton_, 86.0f);
            placeLayout(layoutClearButton_, 66.0f);
        }

        layoutResultBar(timelineTop);
        playbackButton_.bounds = {{32.0f, height - 96.0f}, {84.0f, 26.0f}};
        rebuildOutliner();
        legendBounds_ = {
            {panelX_ - 94.0f, resultViewport_.position.y + 28.0f},
            {34.0f, std::max(120.0f, resultViewport_.size.y - 96.0f)}
        };
        const float trackSpan = std::max(320.0f, panelX_ - 176.0f);
        zoomTrack_ = {
            {144.0f, view3D_ ? -100000.0f : height - 44.0f},
            {trackSpan * 0.3f, 5.0f}};
        frameTrack_ = {
            {144.0f + trackSpan * 0.42f, height - 44.0f},
            {trackSpan * 0.58f, 5.0f}
        };

        syncControlState();
    }

    bool viewportShowsVolume() const {
        return activeFrame_ && activeFrame_->volumetric();
    }

    void layoutResultBar(float timelineTop) {
        const float right = panelX_ - 12.0f;
        float x = 20.0f;
        float y = HEADER_HEIGHT + 8.0f;
        const auto place = [&](Button& button, float w) {
            if (x > 20.0f && x + w > right) {
                x = 20.0f;
                y += 34.0f;
            }
            button.bounds = {{x, y}, {w, 30.0f}};
            x += w + 4.0f;
        };
        const auto newRow = [&]() {
            if (x > 20.0f) {
                x = 20.0f;
                y += 34.0f;
            }
        };
        place(pressureButton_, 104.0f);
        place(velocityButton_, 104.0f);
        place(fieldButton_, 146.0f);
        place(vectorButton_, 114.0f);
        place(rangeButton_, 132.0f);
        place(runDetailsButton_, 110.0f);
        place(continueRunButton_, 128.0f);
        place(recoverSetupButton_, 130.0f);
        place(viewModeButton_, 92.0f);
        for (Button& control : viewControls_) {
            control.bounds = {{0.0f, -100000.0f}, {1.0f, 1.0f}};
            control.enabled = false;
        }
        for (sf::FloatRect& track : viewTracks_) {
            track = {{0.0f, -100000.0f}, {1.0f, 1.0f}};
        }
        const auto placeTrack = [&](std::size_t index) {
            if (x > 20.0f && x + 152.0f > right) {
                x = 20.0f;
                y += 40.0f;
            }
            viewTracks_[index] = {{x, y + 24.0f}, {140.0f, 5.0f}};
            x += 152.0f;
        };
        const auto enable = [&](std::size_t index, float w) {
            viewControls_[index].enabled = true;
            place(viewControls_[index], w);
        };
        if (view3D_ && activeFrame_) {
            newRow();
            enable(ControlFrameAll, 88.0f);
            enable(ControlOrtho, 82.0f);
            enable(ControlRotateTool, 80.0f);
            enable(ControlMoveTool, 72.0f);
            enable(ControlBox, 62.0f);
            enable(ControlGrid, 62.0f);
            enable(ControlSolid, 70.0f);
            enable(ControlWire, 70.0f);
            enable(ControlSliceX, 74.0f);
            enable(ControlSliceY, 74.0f);
            enable(ControlSliceZ, 74.0f);
            enable(ControlIso, 66.0f);
            enable(ControlVortices, 96.0f);
            enable(ControlStreamlines, 96.0f);
            enable(ControlTracers, 90.0f);
            enable(ControlColour, 148.0f);
            newRow();
            enable(ControlFront, 68.0f);
            enable(ControlBack, 68.0f);
            enable(ControlLeft, 68.0f);
            enable(ControlRight, 68.0f);
            enable(ControlTop, 68.0f);
            enable(ControlBottom, 74.0f);
            newRow();
            if (view3DSettings_.sliceX)
                placeTrack(TrackSliceX);
            if (view3DSettings_.sliceY)
                placeTrack(TrackSliceY);
            if (view3DSettings_.sliceZ)
                placeTrack(TrackSliceZ);
            if (view3DSettings_.showIsosurface)
                placeTrack(TrackIso);
            if (view3DSettings_.showVortices)
                placeTrack(TrackVortex);
        } else if (viewportShowsVolume()) {
            newRow();
            enable(ControlAxisX, 74.0f);
            enable(ControlAxisY, 74.0f);
            enable(ControlAxisZ, 74.0f);
            placeTrack(TrackSlice2D);
        }
        resultBarBottom_ = y + 36.0f;
        resultViewport_ = {
            {20.0f, resultBarBottom_},
            {
                std::max(320.0f, panelX_ - 40.0f),
                std::max(200.0f, timelineTop - resultBarBottom_ - 8.0f)
            }
        };
    }

    void syncControlState() {
        setupTab_.selected = mode_ == DisplayMode::Setup;
        resultsTab_.selected = mode_ == DisplayMode::Results;
        resultsTab_.enabled = !frames_.empty();
        pressureButton_.selected =
            resultQuantity_ == ResultQuantity::Pressure;
        velocityButton_.selected =
            resultQuantity_ == ResultQuantity::Velocity;
        const std::vector<std::string>& available =
            activeFrame_ ? activeFrame_->scalarNames : emptyScalarNames();
        fieldButton_.enabled = !available.empty();
        fieldButton_.selected = resultQuantity_ == ResultQuantity::Scalar;
        fieldButton_.label =
            available.empty()
                ? "Field: none"
                : "Field: " + (activeScalarName_.empty() ? available.front()
                                                         : activeScalarName_);
        vectorButton_.label =
            showVelocityVectors_ ? "Vectors: On" : "Vectors: Off";
        vectorButton_.selected = showVelocityVectors_;
        vectorButton_.enabled = !frames_.empty();
        const bool loadingResults =
            resultCatalogFuture_.valid() || !inFlightFrames_.empty();
        const bool solverAvailable =
            solverInfo_.valid && solverInfo_.recognized;

        const bool haveSomethingToRun =
            !geometry_.empty() || solverInfo_.supportsCase;
        generateButton_.enabled =
            solverAvailable && haveSomethingToRun &&
            !solverProcess_.active && !loadingResults;
        outputFolderButton_.enabled =
            !solverProcess_.active && !loadingResults;
        openVtkButton_.enabled = !solverProcess_.active && !loadingResults;
        stopSimulationButton_.enabled = solverProcess_.active;
        solverExeButton_.enabled = !solverProcess_.active;
        resetDefaultsButton_.enabled = !solverProcess_.active && !loadingResults;
        saveConfigButton_.enabled = !solverProcess_.active && !loadingResults;
        loadConfigButton_.enabled = !solverProcess_.active && !loadingResults;
        revealVtkButton_.enabled = currentExplorerTarget().has_value();
        rangeButton_.enabled = !frames_.empty();
        rangeButton_.label =
            std::string("Range: ") + (useSeriesRange_ ? "series" : "frame") +
            (trimmedRange_ ? " 99%" : " full");
        playbackButton_.enabled = frames_.size() > 1;
        playbackButton_.label = playingFrames_ ? "Pause" : "Play";
        runDetailsButton_.enabled =
            !currentRunDirectory_.empty() || !frames_.empty();
        runDetailsButton_.selected = showRunDetails_;
        // A frame can be continued from when it carries the state to do it -
        // 0.1.1 and newer write that into every frame - and when the solver on
        // hand knows the arguments. Everything else about it is checked when
        // the button is pressed, where there is somewhere to put the reason.
        continueRunButton_.enabled =
            activeFrame_ != nullptr &&
            activeFrame_->restart.restartCapable &&
            solverInfo_.valid && solverInfo_.recognized &&
            solverInfo_.supportsContinuation &&
            !solverProcess_.active && !loadingResults;
        recoverSetupButton_.enabled =
            activeFrame_ != nullptr && activeFrame_->restart.hasConfigText &&
            !solverProcess_.active && !loadingResults;
        viewModeButton_.enabled = activeFrame_ != nullptr;
        viewModeButton_.label = view3D_ ? "View: 3D" : "View: 2D";
        viewModeButton_.selected = view3D_;
        const auto toggle = [this](std::size_t control, const char* label,
                                   bool on) {
            viewControls_[control].label = label;
            viewControls_[control].selected = on;
        };
        toggle(ControlFrameAll, "Frame all", false);
        toggle(ControlOrtho, "Ortho",
               viewport3D_.camera().orthographic);
        toggle(ControlRotateTool, "Rotate", !moveTool_);
        toggle(ControlMoveTool, "Move", moveTool_);
        toggle(ControlBox, "Box", view3DSettings_.showBox);
        toggle(ControlGrid, "Grid", view3DSettings_.showGrid);
        toggle(ControlSolid, "Solid", view3DSettings_.showSolid);
        toggle(ControlWire, "Wire", view3DSettings_.wireframeSolid);
        toggle(ControlSliceX, "Slice X", view3DSettings_.sliceX);
        toggle(ControlSliceY, "Slice Y", view3DSettings_.sliceY);
        toggle(ControlSliceZ, "Slice Z", view3DSettings_.sliceZ);
        toggle(ControlIso, "Iso", view3DSettings_.showIsosurface);
        toggle(ControlVortices, "Vortices", view3DSettings_.showVortices);
        toggle(ControlStreamlines, "Streams",
               view3DSettings_.showStreamlines);
        toggle(ControlTracers, "Tracers", view3DSettings_.animateTracers);
        viewControls_[ControlColour].label =
            std::string("Colour: ") + volumeFieldName(view3DSettings_.colourBy);
        viewControls_[ControlColour].selected = false;
        toggle(ControlFront, "Front", false);
        toggle(ControlBack, "Back", false);
        toggle(ControlLeft, "Left", false);
        toggle(ControlRight, "Right", false);
        toggle(ControlTop, "Top", false);
        toggle(ControlBottom, "Bottom", false);
        toggle(ControlAxisX, "Axis X", sliceAxis_ == SliceAxis::X);
        toggle(ControlAxisY, "Axis Y", sliceAxis_ == SliceAxis::Y);
        toggle(ControlAxisZ, "Axis Z", sliceAxis_ == SliceAxis::Z);
    }

    void handleEvent(const sf::Event& event) {
        if (event.is<sf::Event::Closed>()) {
            if (solverProcess_.active &&
                !confirmStopAndExit(window_->getNativeHandle())) {
                return;
            }
            if (solverProcess_.active) {
                std::string ignored;
                solverProcess_.terminate(&ignored);
            }
            window_->close();
            return;
        }
        if (const auto* resized = event.getIf<sf::Event::Resized>()) {
            endDragging();
            syncWindowLayout(resized->size);
            return;
        }
        if (event.is<sf::Event::FocusLost>()) {
            endDragging();
            cancelSliderEdit(false);
            return;
        }
        if (!window_->hasFocus()) {
            return;
        }

        if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
            if (editingSlider_.has_value() && (key->control || key->system)) {
                if (handleEditClipboard(*key))
                    return;
            }
            if (!editingSlider_.has_value() && handleShortcut(*key))
                return;
        }

        if (editingSlider_.has_value()) {
            if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
                handleSliderEditKey(*key);
                return;
            }
            if (const auto* text = event.getIf<sf::Event::TextEntered>()) {
                handleSliderEditText(text->unicode);
                return;
            }
        }

        if (searchActive_) {
            if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
                if (handleSearchKey(*key))
                    return;
            }
            if (const auto* text = event.getIf<sf::Event::TextEntered>()) {
                if (handleSearchText(text->unicode))
                    return;
            }
        }

        if (const auto* key = event.getIf<sf::Event::KeyPressed>();
            key != nullptr && !searchActive_) {
            if (mode_ == DisplayMode::Setup && layoutMode_) {
                if (handleLayoutKeyPressed(*key))
                    return;
            }
            if (mode_ == DisplayMode::Results) {
                if (handleResultsKeyPressed(*key)) {
                    return;
                }
            }
        }

        if (const auto* pressed =
                event.getIf<sf::Event::MouseButtonPressed>()) {
            handleMousePressed(
                pressed->button,
                window_->mapPixelToCoords(pressed->position));
        } else if (const auto* released =
                       event.getIf<sf::Event::MouseButtonReleased>()) {
            handleMouseReleased(released->button);
        } else if (const auto* moved =
                       event.getIf<sf::Event::MouseMoved>()) {
            handleMouseMoved(window_->mapPixelToCoords(moved->position));
        } else if (const auto* wheel =
                       event.getIf<sf::Event::MouseWheelScrolled>()) {
            handleWheel(
                window_->mapPixelToCoords(wheel->position),
                wheel->delta);
        }
    }

    void handleMousePressed(
        sf::Mouse::Button button,
        sf::Vector2f position) {
        lastMouse_ = position;

        if (button == sf::Mouse::Button::Left &&
            editingSlider_.has_value()) {
            const bool insideEditor =
                sliders_[*editingSlider_].valueHit(position);
            if (!insideEditor && !commitSliderEdit()) {
                return;
            }
        }

        if (button == sf::Mouse::Button::Left &&
            setupTab_.hit(position)) {
            mode_ = DisplayMode::Setup;
            return;
        }
        if (button == sf::Mouse::Button::Left &&
            resultsTab_.hit(position)) {
            mode_ = DisplayMode::Results;
            return;
        }
        if (button == sf::Mouse::Button::Left &&
            openVtkButton_.hit(position)) {
            openVtkFiles();
            return;
        }
        if (button == sf::Mouse::Button::Left &&
            revealVtkButton_.hit(position)) {
            revealVtkLocation();
            return;
        }
        if (button == sf::Mouse::Button::Left &&
            solverExeButton_.hit(position)) {
            selectFluidSolverExecutable();
            return;
        }
        if (button == sf::Mouse::Button::Left &&
            stopSimulationButton_.hit(position)) {
            stopSimulationAndLoadFrames();
            return;
        }

        if (button == sf::Mouse::Button::Left &&
            importButton_.hit(position)) {
            importGeometry();
            return;
        }
        if (button == sf::Mouse::Button::Left &&
            outputFolderButton_.hit(position)) {
            selectOutputFolder();
            return;
        }
        if (button == sf::Mouse::Button::Left &&
            handleOutlinerClick(position)) {
            return;
        }
        if (handlePropertiesMousePressed(button, position)) {
            return;
        }
        if (mode_ == DisplayMode::Setup) {
            handleSetupMousePressed(button, position);
        } else {
            handleResultsMousePressed(button, position);
        }
    }

    bool handlePropertiesMousePressed(
        sf::Mouse::Button button,
        sf::Vector2f position) {
        if (button == sf::Mouse::Button::Left &&
            resetDefaultsButton_.hit(position)) {
            resetDefaults();
            return true;
        }
        if (button == sf::Mouse::Button::Left &&
            saveConfigButton_.hit(position)) {
            saveConfiguration();
            return true;
        }
        if (button == sf::Mouse::Button::Left &&
            loadConfigButton_.hit(position)) {
            loadConfiguration();
            return true;
        }
        if (button == sf::Mouse::Button::Left && !searchActive_ &&
            searchQuery_.empty()) {
            for (std::size_t index = 0; index < tabButtons_.size(); ++index) {
                if (!tabButtons_[index].hit(position))
                    continue;
                activeTab_ = index;
                parameterScrollOffset_ = 0.0f;
                updateLayout(layoutSize_);
                return true;
            }
        }
        if (button == sf::Mouse::Button::Left &&
            (searchActive_ || !searchQuery_.empty())) {
            const sf::FloatRect field{{panelX_ + 18.0f, PARAMETER_STRIP_TOP},
                                      {294.0f, PARAMETER_STRIP_HEIGHT}};
            if (field.contains(position)) {
                if (searchActive_)
                    closeSearch(true);
                else
                    openSearch();
                return true;
            }
        }
        if (button == sf::Mouse::Button::Left &&
            maxParameterScroll_ > 0.0f) {
            const sf::FloatRect thumb = parameterScrollbarThumb();
            sf::FloatRect hit = parameterScrollbarRail();
            hit.position.x -= 5.0f;
            hit.size.x += 10.0f;
            if (hit.contains(position)) {
                draggingParameterScrollbar_ = true;
                if (thumb.contains(position)) {
                    parameterScrollbarGrabOffset_ =
                        position.y - thumb.position.y;
                } else {
                    parameterScrollbarGrabOffset_ =
                        thumb.size.y * 0.5f;
                    setParameterScrollFromThumb(
                        position.y - parameterScrollbarGrabOffset_);
                }
                return true;
            }
        }
        if (button == sf::Mouse::Button::Left &&
            generateButton_.hit(position)) {
            generateAndRun();
            return true;
        }
        if (button == sf::Mouse::Button::Left) {
            for (std::size_t index = 0; index < sliders_.size(); ++index) {
                if (!parameterRowOnScreen(index)) {
                    continue;
                }
                if (sliders_[index].integer && !sliders_[index].boolean &&
                    sliders_[index].stepMinusBounds().contains(position)) {
                    focusedSlider_ = index;
                    pushUndo();
                    sliders_[index].value = std::max(sliders_[index].minimum, sliders_[index].value - 1.0);
                    invalidSlider_.reset();
                    if (index == CacheMegabytes) {
                        applyCacheBudget();
                        savePreferences();
                    }
                    return true;
                }
                if (sliders_[index].integer && !sliders_[index].boolean &&
                    sliders_[index].stepPlusBounds().contains(position)) {
                    focusedSlider_ = index;
                    pushUndo();
                    sliders_[index].value = std::min(sliders_[index].maximum, sliders_[index].value + 1.0);
                    invalidSlider_.reset();
                    if (index == CacheMegabytes) {
                        applyCacheBudget();
                        savePreferences();
                    }
                    return true;
                }
                if (sliders_[index].valueHit(position)) {
                    handleSliderValueClick(index);
                    return true;
                }
            }
        }
        if (button == sf::Mouse::Button::Left) {
            for (std::size_t index = 0; index < sliders_.size(); ++index) {
                if (!parameterRowOnScreen(index)) {
                    continue;
                }
                if (sliders_[index].hit(position)) {
                    focusedSlider_ = index;
                    pushUndo();
                    if (index == UseCuda && !solverInfo_.cudaCapable) {
                        status_ =
                            "CUDA is unavailable in the selected CPU-only "
                            "Fluid Solver build.";
                        sliders_[UseCuda].value = 0.0;
                        return true;
                    }
                    activeSlider_ = index;
                    sliders_[index].dragging = true;
                    sliders_[index].setFromX(position.x);
                    return true;
                }
            }
        }

        return false;
    }

    void handleSetupMousePressed(
        sf::Mouse::Button button,
        sf::Vector2f position) {
        if (button == sf::Mouse::Button::Left &&
            layoutButton_.hit(position)) {
            layoutMode_ = !layoutMode_;
            if (layoutMode_) {
                painting_ = false;
                layoutSignature_.clear();
                refreshLayoutMask();
                status_ = "Layout: click a body to pick it, drag to place it, "
                          "K drops a keyframe, Tab cycles bodies.";
            }
            return;
        }
        if (handleLayoutMousePressed(button, position))
            return;
        if (!layoutMode_ && handlePaintMousePressed(button, position))
            return;
        const sf::FloatRect invertBox = invertBounds();
        if (button == sf::Mouse::Button::Left &&
            invertBox.contains(position)) {
            invertSection_ = !invertSection_;
            return;
        }
        if (!setupViewport_.contains(position)) {
            return;
        }
        if (horizontalSliceTrack().contains(position) &&
            button == sf::Mouse::Button::Left) {
            draggingHorizontalSlice_ = true;
            setHorizontalSlice(position.x);
            return;
        }
        if (verticalSliceTrack().contains(position) &&
            button == sf::Mouse::Button::Left) {
            draggingVerticalSlice_ = true;
            setVerticalSlice(position.y);
            return;
        }

        if (layoutMode_)
            return;
        if (button == sf::Mouse::Button::Right) {
            rotatingRoll_ = true;
        } else if (button == sf::Mouse::Button::Left) {
            rotatingObject_ = true;
        }
    }

    void handleResultsMousePressed(
        sf::Mouse::Button button,
        sf::Vector2f position) {
        if (button == sf::Mouse::Button::Middle) {
            if (view3D_ && resultViewport_.contains(position)) {
                panning3D_ = true;
            }
            return;
        }
        if (button != sf::Mouse::Button::Left) {
            return;
        }
        if (recoverSetupButton_.hit(position)) {
            loadConfigurationFromFrame();
            return;
        }
        if (viewModeButton_.hit(position)) {
            setViewMode(!view3D_);
            return;
        }
        for (std::size_t control = 0; control < viewControls_.size();
             ++control) {
            if (viewControls_[control].hit(position)) {
                handleViewControl(control);
                return;
            }
        }
        for (std::size_t track = 0; track < viewTracks_.size(); ++track) {
            const sf::FloatRect& rail = viewTracks_[track];
            if (rail.position.y < 0.0f) {
                continue;
            }
            const sf::FloatRect hit{
                {rail.position.x - 8.0f, rail.position.y - 12.0f},
                {rail.size.x + 16.0f, 29.0f}};
            if (hit.contains(position)) {
                draggingViewTrack_ = track;
                setViewTrackFromX(track, position.x);
                return;
            }
        }
        if (pressureButton_.hit(position)) {
            resultQuantity_ = ResultQuantity::Pressure;
            resultTextureCacheValid_ = false;
            return;
        }
        if (velocityButton_.hit(position)) {
            resultQuantity_ = ResultQuantity::Velocity;
            resultTextureCacheValid_ = false;
            return;
        }
        if (fieldButton_.hit(position) && fieldButton_.enabled) {
            const std::vector<std::string>& names = activeFrame_->scalarNames;
            std::size_t next = 0;
            if (resultQuantity_ == ResultQuantity::Scalar) {
                for (std::size_t index = 0; index < names.size(); ++index)
                    if (names[index] == activeScalarName_)
                        next = index + 1;
            }
            if (next >= names.size()) {
                resultQuantity_ = ResultQuantity::Pressure;
                activeScalarName_.clear();
            } else {
                resultQuantity_ = ResultQuantity::Scalar;
                activeScalarName_ = names[next];
            }
            resultTextureCacheValid_ = false;
            return;
        }
        if (vectorButton_.hit(position)) {
            showVelocityVectors_ = !showVelocityVectors_;
            status_ = showVelocityVectors_
                ? "Velocity vectors enabled: arrows point with local flow."
                : "Velocity vectors disabled.";
            return;
        }
        if (rangeButton_.hit(position)) {
            // Four states, cycled by one button: series trimmed, series full,
            // frame trimmed, frame full.
            if (trimmedRange_) {
                trimmedRange_ = false;
            } else {
                trimmedRange_ = true;
                useSeriesRange_ = !useSeriesRange_;
            }
            resultTextureCacheValid_ = false;
            status_ = std::string("Colours span ") +
                (useSeriesRange_ ? "the whole series" : "this frame") +
                (trimmedRange_
                     ? ", with the outermost 0.5% at each end left out so a "
                       "few extreme cells cannot flatten the rest."
                     : ", every value included.");
            return;
        }
        if (continueRunButton_.hit(position)) {
            continueFromSelectedFrame();
            return;
        }
        if (playbackButton_.hit(position)) {
            playingFrames_ = !playingFrames_;
            playbackAccumulator_ = 0.0f;
            status_ = playingFrames_
                ? "VTK playback started. Space pauses."
                : "VTK playback paused.";
            return;
        }
        if (runDetailsButton_.hit(position)) {
            showRunDetails_ = !showRunDetails_;
            if (showRunDetails_) {
                refreshRunDetailsText();
            }
            return;
        }
        if (zoomHitBounds().contains(position)) {
            draggingZoom_ = true;
            setZoomFromSlider(position.x);
            return;
        }
        if (frameHitBounds().contains(position) && !frames_.empty()) {
            draggingFrame_ = true;
            setFrameFromSlider(position.x);
            return;
        }
        if (resultViewport_.contains(position)) {
            if (view3D_) {
                orbiting3D_ = true;
                orbitMoved_ = false;
                // Decided here rather than on every mouse move, so letting go
                // of Shift halfway through a drag does not change what the
                // drag is doing.
                dragSlides_ = moveTool_ ||
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);
                return;
            }
            panningResults_ = true;
        }
    }

    bool handleResultsKeyPressed(const sf::Event::KeyPressed& key) {
        if (frames_.empty()) {
            return false;
        }
        if (key.code == sf::Keyboard::Key::V) {
            setViewMode(!view3D_);
            return true;
        }
        if (view3D_) {
            // A running transform owns the keyboard until Enter or Escape, so
            // that typing 5 into it means five and not "numpad 5 flips the
            // projection".
            if (transformMode_ != 0)
                return handleTransformKey(key);
            // orbit() counts pixels of mouse travel, so a keyboard step has to
            // be spelled in those: 0.01 radians a pixel puts 15 degrees at
            // 26.18 of them and half a turn at 314.16.
            constexpr float kOrbitStep = 26.18f;
            constexpr float kHalfTurn = 314.16f;
            switch (key.code) {
            case sf::Keyboard::Key::F:
                viewport3D_.frameAll();
                status_ = "Framed the whole volume.";
                return true;
            case sf::Keyboard::Key::Numpad1:
                viewport3D_.setView(2, key.control);
                status_ = key.control ? "Back view." : "Front view.";
                return true;
            case sf::Keyboard::Key::Numpad3:
                viewport3D_.setView(0, key.control);
                status_ = key.control ? "Left view." : "Right view.";
                return true;
            case sf::Keyboard::Key::Numpad7:
                viewport3D_.setView(1, key.control);
                status_ = key.control ? "Bottom view." : "Top view.";
                return true;
            case sf::Keyboard::Key::Numpad5:
                viewport3D_.camera().orthographic =
                    !viewport3D_.camera().orthographic;
                status_ = viewport3D_.camera().orthographic
                    ? "Isometric: parallel projection, no perspective."
                    : "Perspective projection.";
                return true;
            case sf::Keyboard::Key::Numpad4:
                viewport3D_.orbit(-kOrbitStep, 0.0f);
                return true;
            case sf::Keyboard::Key::Numpad6:
                viewport3D_.orbit(kOrbitStep, 0.0f);
                return true;
            case sf::Keyboard::Key::Numpad8:
                viewport3D_.orbit(0.0f, -kOrbitStep);
                return true;
            case sf::Keyboard::Key::Numpad2:
                viewport3D_.orbit(0.0f, kOrbitStep);
                return true;
            case sf::Keyboard::Key::Numpad9:
                viewport3D_.orbit(-kHalfTurn, 0.0f);
                status_ = "Turned to the opposite side.";
                return true;
            case sf::Keyboard::Key::G:
                beginTransform(1);
                return true;
            case sf::Keyboard::Key::R:
                beginTransform(2);
                return true;
            default:
                break;
            }
        } else if (sliceCache_.slicing()) {
            if (key.code == sf::Keyboard::Key::Up) {
                stepSlice(1);
                return true;
            }
            if (key.code == sf::Keyboard::Key::Down) {
                stepSlice(-1);
                return true;
            }
            if (key.code == sf::Keyboard::Key::X) {
                setSlicePlane(SliceAxis::X, sliceIndex_);
                return true;
            }
            if (key.code == sf::Keyboard::Key::Y) {
                setSlicePlane(SliceAxis::Y, sliceIndex_);
                return true;
            }
            if (key.code == sf::Keyboard::Key::Z) {
                setSlicePlane(SliceAxis::Z, sliceIndex_);
                return true;
            }
        }
        const std::size_t current = desiredFrame_.value_or(selectedFrame_);
        if (key.code == sf::Keyboard::Key::Left) {
            playingFrames_ = false;
            requestSelectedFrame(current == 0 ? 0 : current - 1);
            return true;
        }
        if (key.code == sf::Keyboard::Key::Right) {
            playingFrames_ = false;
            requestSelectedFrame(
                std::min(current + 1, frames_.size() - 1));
            return true;
        }
        if (key.code == sf::Keyboard::Key::Home) {
            playingFrames_ = false;
            requestSelectedFrame(0);
            return true;
        }
        if (key.code == sf::Keyboard::Key::End) {
            playingFrames_ = false;
            requestSelectedFrame(frames_.size() - 1);
            return true;
        }
        if (key.code == sf::Keyboard::Key::Space) {
            playingFrames_ = !playingFrames_;
            playbackAccumulator_ = 0.0f;
            return true;
        }
        return false;
    }

    void handleMouseReleased(sf::Mouse::Button button) {
        if (button == sf::Mouse::Button::Left && orbiting3D_ &&
            !orbitMoved_ && activeFrame_) {
            applyPickSelection(
                viewport3D_.pickAt(
                    resultViewport_, lastMouse_.x, lastMouse_.y));
        }
        if (button == sf::Mouse::Button::Middle) {
            panning3D_ = false;
            return;
        }
        if (button == sf::Mouse::Button::Left ||
            button == sf::Mouse::Button::Right) {
            endDragging();
        }
    }

    void handleMouseMoved(sf::Vector2f position) {
        const sf::Vector2f delta = position - lastMouse_;
        lastMouse_ = position;
        if (orbiting3D_) {
            if (delta.x != 0.0f || delta.y != 0.0f) {
                orbitMoved_ = true;
            }
            if (dragSlides_) {
                viewport3D_.pan(delta.x, delta.y);
            } else {
                viewport3D_.orbit(delta.x, delta.y);
            }
            return;
        }
        if (panning3D_) {
            viewport3D_.pan(delta.x, delta.y);
            return;
        }
        if (draggingViewTrack_.has_value()) {
            setViewTrackFromX(*draggingViewTrack_, position.x);
            return;
        }
        if (mode_ == DisplayMode::Results && view3D_ && activeFrame_) {
            pickText_ = resultViewport_.contains(position)
                ? pickDescription(viewport3D_.pickAt(
                      resultViewport_, position.x, position.y))
                : std::string();
        }
        if (layoutDragging_) {
            dragLayoutSelection(delta);
        } else if (layoutRotating_) {
            rotateLayoutSelection(delta.x);
        } else if (draggingLayoutTime_) {
            setLayoutTimeFromX(position.x);
        } else if (paintStroke_) {
            paintAt(position,
                    sf::Mouse::isButtonPressed(sf::Mouse::Button::Right));
        } else if (draggingParameterScrollbar_) {
            setParameterScrollFromThumb(
                position.y - parameterScrollbarGrabOffset_);
        } else if (activeSlider_.has_value()) {
            sliders_[*activeSlider_].setFromX(position.x);
        } else if (draggingHorizontalSlice_) {
            setHorizontalSlice(position.x);
        } else if (draggingVerticalSlice_) {
            setVerticalSlice(position.y);
        } else if (rotatingObject_) {
            const bool shift =
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::LShift) ||
                sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::RShift);
            if (shift) {
                sliders_[SliceRotation].value =
                    wrapDegrees(
                        sliders_[SliceRotation].value +
                        static_cast<double>(delta.x) * 0.4);
            } else {
                sliders_[SliceZ].value =
                    snapCardinalDegrees(
                        sliders_[SliceZ].value +
                        static_cast<double>(delta.x) * 0.35);
                sliders_[SliceX].value =
                    snapCardinalDegrees(
                        sliders_[SliceX].value -
                        static_cast<double>(delta.y) * 0.35);
            }
        } else if (rotatingRoll_) {
            sliders_[SliceRotation].value =
                wrapDegrees(
                    sliders_[SliceRotation].value +
                    static_cast<double>(delta.x) * 0.4);
        } else if (draggingZoom_) {
            setZoomFromSlider(position.x);
        } else if (draggingFrame_) {
            setFrameFromSlider(position.x);
        } else if (panningResults_) {
            resultPan_ += delta;
        }
    }

    void handleWheel(sf::Vector2f position, float delta) {
        if (outlinerBounds().contains(position)) {
            const float rows =
                static_cast<float>(outlinerRows_.size()) * 17.0f;
            outlinerScroll_ = clampFloat(
                outlinerScroll_ - delta * 17.0f,
                0.0f,
                std::max(0.0f, rows - (OUTLINER_HEIGHT - 28.0f)));
            return;
        }
        if (mode_ == DisplayMode::Setup &&
            parameterViewport().contains(position)) {
            parameterScrollOffset_ = clampFloat(
                parameterScrollOffset_ - delta * PARAMETER_SCROLL_STEP,
                0.0f,
                maxParameterScroll_);
            activeSlider_.reset();
            updateLayout(layoutSize_);
            return;
        }
        if (mode_ == DisplayMode::Setup && painting_ && phasesOn() &&
            setupViewport_.contains(position)) {
            paintBrush_ = std::min(64, std::max(0,
                paintBrush_ + (delta > 0.0f ? 1 : -1)));
            return;
        }
        if (mode_ == DisplayMode::Setup &&
            setupViewport_.contains(position)) {
            setupZoom_ = clampFloat(
                setupZoom_ * std::pow(1.12f, delta),
                0.35f,
                5.0f);
            return;
        }
        if (mode_ == DisplayMode::Results &&
            resultViewport_.contains(position) &&
            !frames_.empty()) {
            if (view3D_) {
                viewport3D_.zoom(delta);
                return;
            }
            if (sliceCache_.view()) {
                zoomResultsAt(position, std::pow(1.15f, delta));
            }
        }
    }

    void endDragging() {
        orbiting3D_ = false;
        panning3D_ = false;
        draggingViewTrack_.reset();
        paintStroke_ = false;
        layoutDragging_ = false;
        layoutRotating_ = false;
        draggingLayoutTime_ = false;
        const std::optional<std::size_t> releasedSlider = activeSlider_;
        if (activeSlider_.has_value()) {
            sliders_[*activeSlider_].dragging = false;
        }
        activeSlider_.reset();
        if (releasedSlider == CacheMegabytes) {
            applyCacheBudget();
            savePreferences();
        }
        if (releasedSlider.has_value()) {
            invalidSlider_.reset();
            syncBodyRows(*releasedSlider);
        }
        draggingParameterScrollbar_ = false;
        draggingHorizontalSlice_ = false;
        draggingVerticalSlice_ = false;
        rotatingObject_ = false;
        rotatingRoll_ = false;
        draggingZoom_ = false;
        draggingFrame_ = false;
        panningResults_ = false;
    }

    void handleSliderValueClick(std::size_t index) {
        focusedSlider_ = index;
        if (editingSlider_ == index) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const bool doubleClick =
            lastValueClickSlider_ == index &&
            now - lastValueClickTime_ <= std::chrono::milliseconds(500);
        lastValueClickSlider_ = index;
        lastValueClickTime_ = now;
        if (!doubleClick) {
            status_ =
                "Double-click " + sliders_[index].label +
                " value to type an exact number.";
            return;
        }

        editingSlider_ = index;
        const Slider& editingRow = sliders_[index];
        sliderEditText_ =
            editingRow.kind == ControlKind::Text
                ? editingRow.text
                : editingRow.kind == ControlKind::Choice
                      ? editingRow.choice()
                      : editableNumber(editingRow.value, editingRow.integer);
        lastValueClickSlider_.reset();
        status_ =
            "Editing " + sliders_[index].label +
            ": Enter applies; Esc cancels.";
    }

    bool handleEditClipboard(const sf::Event::KeyPressed& key) {
        if (!editingSlider_.has_value())
            return false;
        if (key.code == sf::Keyboard::Key::C ||
            key.code == sf::Keyboard::Key::X) {
            sf::Clipboard::setString(sf::String(sliderEditText_));
            if (key.code == sf::Keyboard::Key::X)
                sliderEditText_.clear();
            return true;
        }
        if (key.code == sf::Keyboard::Key::V) {
            const std::string incoming =
                sf::Clipboard::getString().toAnsiString();
            for (char character : incoming)
                if (character >= 32 && character < 127 &&
                    sliderEditText_.size() < 96)
                    sliderEditText_.push_back(character);
            return true;
        }
        if (key.code == sf::Keyboard::Key::A) {
            sliderEditText_.clear();
            return true;
        }
        return false;
    }

    void handleSliderEditKey(const sf::Event::KeyPressed& key) {
        if (!editingSlider_.has_value()) {
            return;
        }
        if (key.code == sf::Keyboard::Key::Enter) {
            commitSliderEdit();
        } else if (key.code == sf::Keyboard::Key::Escape) {
            cancelSliderEdit(true);
        } else if (key.code == sf::Keyboard::Key::Backspace &&
                   !sliderEditText_.empty()) {
            sliderEditText_.pop_back();
        }
    }

    void handleSliderEditText(char32_t unicode) {
        if (!editingSlider_.has_value() ||
            sliderEditText_.size() >= 64 ||
            unicode > 127) {
            return;
        }
        const char character = static_cast<char>(unicode);
        static const std::string allowed = "0123456789+-.eE";
        if (allowed.find(character) != std::string::npos) {
            sliderEditText_.push_back(character);
        }
    }

    bool commitSliderEdit() {
        pushUndo();
        if (!editingSlider_.has_value()) {
            return true;
        }
        const std::size_t index = *editingSlider_;
        std::string error;
        if (!sliders_[index].setFromText(sliderEditText_, error)) {
            status_ =
                "Invalid " + sliders_[index].label + ": " + error + ".";
            return false;
        }
        if (index == UseCuda && !solverInfo_.cudaCapable) {
            sliders_[index].value = 0.0;
            status_ =
                "CUDA is unavailable in the selected CPU-only Fluid Solver "
                "build; Request CUDA remains Off.";
        } else {
            status_ =
                sliders_[index].label + " = " +
                formatValue(
                    sliders_[index].value,
                    sliders_[index].integer,
                    sliders_[index].unit) +
                ".";
        }
        if (index == CacheMegabytes) {
            applyCacheBudget();
            savePreferences();
        }
        invalidSlider_.reset();
        editingSlider_.reset();
        sliderEditText_.clear();
        syncBodyRows(index);
        return true;
    }

    void cancelSliderEdit(bool announce) {
        if (!editingSlider_.has_value()) {
            return;
        }
        const std::string label = sliders_[*editingSlider_].label;
        editingSlider_.reset();
        sliderEditText_.clear();
        if (announce) {
            status_ = label + " edit cancelled.";
        }
    }

    void update(float elapsed) {
        pollTrayCommands();
        pollResultCatalog();
        pollSelectedFrame();
        // Playback no longer waits for the loaders to fall idle. They are
        // always busy pulling the window in now, and the next step is usually
        // already decoded; a cache miss simply lands a frame or two later.
        if (mode_ == DisplayMode::Results && playingFrames_ &&
            frames_.size() > 1 && !desiredFrame_ &&
            !resultCatalogFuture_.valid()) {
            playbackAccumulator_ += elapsed;
            if (playbackAccumulator_ >= 0.12f) {
                playbackAccumulator_ = 0.0f;
                const std::size_t next = (selectedFrame_ + 1) % frames_.size();
                requestSelectedFrame(next);
            }
        }
        if (mode_ == DisplayMode::Results && view3D_ &&
            view3DSettings_.animateTracers) {
            viewport3D_.advance(elapsed);
        }
        if (window_->hasFocus()) {
            const double rotationSpeed = 70.0 * elapsed;
            if (mode_ == DisplayMode::Setup) {
                const bool rotateLeft =
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::A) ||
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Left);
                const bool rotateRight =
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::D) ||
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Right);
                const bool rotateUp =
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::W) ||
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Up);
                const bool rotateDown =
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::S) ||
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Down);
                if (rotateLeft) {
                    sliders_[SliceZ].value =
                        snapCardinalDegrees(
                            sliders_[SliceZ].value - rotationSpeed);
                }
                if (rotateRight) {
                    sliders_[SliceZ].value =
                        snapCardinalDegrees(
                            sliders_[SliceZ].value + rotationSpeed);
                }
                if (rotateUp) {
                    sliders_[SliceX].value =
                        snapCardinalDegrees(
                            sliders_[SliceX].value + rotationSpeed);
                }
                if (rotateDown) {
                    sliders_[SliceX].value =
                        snapCardinalDegrees(
                            sliders_[SliceX].value - rotationSpeed);
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::Q)) {
                    sliders_[SliceRotation].value =
                        wrapDegrees(
                            sliders_[SliceRotation].value - rotationSpeed);
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::E)) {
                    sliders_[SliceRotation].value =
                        wrapDegrees(
                            sliders_[SliceRotation].value + rotationSpeed);
                }
            } else {
                const float panSpeed = 190.0f * elapsed;
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::W)) {
                    resultPan_.y -= panSpeed;
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::S)) {
                    resultPan_.y += panSpeed;
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::A)) {
                    resultPan_.x -= panSpeed;
                }
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Scan::D)) {
                    resultPan_.x += panSpeed;
                }
            }
        }

        if (solverProcess_.active) {
            const std::optional<unsigned long> result =
                solverProcess_.poll();
            if (result.has_value()) {
                endRunProgress(*result == 0);
                if (*result == 0) {
                    loadResultFrames();
                } else {
                    currentRunRequiresComputedFrame_ = false;
                    std::string solverError = readDiagnosticFile(
                        currentRunDirectory_ / "solver-error.txt");
                    if (solverError.empty()) {
                        solverError = readDiagnosticFile(
                            currentRunDirectory_ / "solver-output.txt");
                    }
                    status_ = solverError.empty()
                                  ? "Fluid Solver failed with exit code " +
                                        std::to_string(*result) + "."
                                  : "Fluid Solver failed: " + solverError;
                }
            } else if (std::chrono::steady_clock::now() >=
                       nextSolverProgressUpdate_) {
                refreshSolverProgress();
                nextSolverProgressUpdate_ =
                    std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(500);
            }
        }
        syncControlState();
    }

    // The tail of the solver's own stdout, which the UI already redirects into
    // the run directory. Reading the end of it is cheaper than opening a VTK
    // frame, works with a 0.1 solver, and gives the one number a progress bar
    // needs - the simulated time the run has reached.
    std::optional<double> readSolverSimulatedTime() const {
        std::ifstream input(
            currentRunDirectory_ / "solver-output.txt",
            std::ios::binary);
        if (!input.is_open()) {
            return std::nullopt;
        }
        input.seekg(0, std::ios::end);
        const std::streamoff size = input.tellg();
        const std::streamoff window = 8192;
        input.seekg(size > window ? size - window : 0, std::ios::beg);
        const std::string tail{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };

        // "Step 1230, t = 3.71591 s, dt = ..." - the last one wins.
        std::optional<double> latest;
        const std::string needle = ", t = ";
        std::size_t at = tail.find(needle);
        while (at != std::string::npos) {
            try {
                std::size_t consumed = 0;
                const double value =
                    std::stod(tail.substr(at + needle.size(), 32), &consumed);
                if (consumed > 0 && std::isfinite(value)) {
                    latest = value;
                }
            } catch (const std::exception&) {
            }
            at = tail.find(needle, at + needle.size());
        }
        return latest;
    }

    void refreshSolverProgress() {
        std::size_t frameCount = 0;
        std::string scanProblem;
        try {
            frameCount =
                VtkFrameParser::discoverFrames(currentRunDirectory_).size();
        } catch (const std::exception& exception) {
            scanProblem = exception.what();
        }

        const std::optional<double> reached = readSolverSimulatedTime();
        runElapsedSeconds_ = reached.value_or(runStartSeconds_);
        runProgress_ = -1.0;
        if (reached && runTargetSeconds_ > runStartSeconds_) {
            runProgress_ = std::clamp(
                (*reached - runStartSeconds_) /
                    (runTargetSeconds_ - runStartSeconds_),
                0.0,
                1.0);
        }

        std::string progressText = "Fluid Solver is running";
        if (runProgress_ >= 0.0) {
            progressText += " - " +
                formatValue(runElapsedSeconds_, false, "") + " / " +
                formatValue(runTargetSeconds_, false, "s") + " (" +
                std::to_string(static_cast<int>(runProgress_ * 100.0 + 0.5)) +
                "%)";
        } else if (reached) {
            progressText += " - t = " +
                formatValue(runElapsedSeconds_, false, "s");
        }
        progressText += ". " + std::to_string(frameCount) + " frame(s) saved.";
        if (!scanProblem.empty()) {
            progressText += " Progress scan failed: " + scanProblem;
        }
        runProgressText_ = progressText;
        status_ = progressText + " Closing the GUI cancels this run.";
        publishRunProgress();
    }

    // The same numbers, wherever this platform can show them: the tray tooltip
    // and the taskbar button on Windows, the window title everywhere else -
    // which is what the taskbar entry or the Dock label reads there.
    void publishRunProgress() {
        const bool running = solverProcess_.active;
        std::string tooltip = windowTitle_;
        if (running && !runProgressText_.empty()) {
            tooltip = runProgressText_;
        }
        tray_.setProgress(running, running ? runProgress_ : 0.0, tooltip);
        if (!tray_.available() && window_ != nullptr) {
            std::string title = windowTitle_;
            if (running && runProgress_ >= 0.0) {
                title += " - " +
                    std::to_string(
                        static_cast<int>(runProgress_ * 100.0 + 0.5)) +
                    "% (" + formatValue(runElapsedSeconds_, false, "") + " / " +
                    formatValue(runTargetSeconds_, false, "s") + ")";
            } else if (running) {
                title += " - running";
            }
            if (title != publishedTitle_) {
                window_->setTitle(title);
                publishedTitle_ = title;
            }
        }
    }

    void beginRunProgress(double startSeconds, double targetSeconds) {
        runStartSeconds_ = startSeconds;
        runTargetSeconds_ = targetSeconds;
        runElapsedSeconds_ = startSeconds;
        runProgress_ = targetSeconds > startSeconds ? 0.0 : -1.0;
        runProgressText_ = "Fluid Solver is starting.";
        tray_.setSimulationRunning(true);
        publishRunProgress();
    }

    void endRunProgress(bool finished) {
        runProgress_ = -1.0;
        runProgressText_.clear();
        tray_.setSimulationRunning(false);
        publishRunProgress();
        if (windowHidden_) {
            // A run that ends while the window is in the tray is the one time
            // it is worth bringing back unasked: the frames are what the run
            // was for.
            setWindowHidden(false);
        }
        tray_.notify(
            "Fluid Solver",
            finished ? "The simulation finished. The frames are loaded."
                     : "The simulation stopped early.",
            !finished);
    }

    void pollTrayCommands() {
        for (;;) {
            const TrayIcon::Command command = tray_.takeCommand();
            if (command == TrayIcon::Command::None) {
                return;
            }
            switch (command) {
                case TrayIcon::Command::ShowWindow:
                    setWindowHidden(false);
                    break;
                case TrayIcon::Command::HideWindow:
                    setWindowHidden(true);
                    break;
                case TrayIcon::Command::OpenOutput:
                    revealVtkLocation();
                    break;
                case TrayIcon::Command::StopSimulation:
                    stopSimulationAndLoadFrames();
                    break;
                case TrayIcon::Command::Quit:
                    if (window_ != nullptr) {
                        window_->close();
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void setWindowHidden(bool hidden) {
        if (window_ == nullptr) {
            return;
        }
        window_->setVisible(!hidden);
        if (!hidden) {
            window_->requestFocus();
        }
        windowHidden_ = hidden;
        tray_.setWindowVisible(!hidden);
    }

    // SFML draws its own default icon on the taskbar button unless it is told
    // otherwise, so the one compiled into the executable is loaded and handed
    // over. Windows only: elsewhere the icon belongs to the .desktop entry or
    // the .app bundle, which the installers write.
    void applyWindowIcon() {
#ifdef _WIN32
        if (window_ == nullptr) {
            return;
        }
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        // Not "small" and "large": rpcndr.h, which windows.h drags in, does
        // #define small char. MSVC then reads "HICON char = ..." and MinGW,
        // which does not define it, compiles the same line happily.
        const HICON largeIcon = static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
            LR_DEFAULTCOLOR));
        const HICON smallIcon = static_cast<HICON>(LoadImageW(
            instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR));
        const HWND handle = window_->getNativeHandle();
        if (handle == nullptr) {
            return;
        }
        if (largeIcon != nullptr) {
            SendMessageW(handle, WM_SETICON, ICON_BIG,
                         reinterpret_cast<LPARAM>(largeIcon));
        }
        if (smallIcon != nullptr) {
            SendMessageW(handle, WM_SETICON, ICON_SMALL,
                         reinterpret_cast<LPARAM>(smallIcon));
        }
#endif
    }

    void stopSimulationAndLoadFrames() {
        if (!solverProcess_.active) {
            status_ = "No Fluid Solver simulation is active.";
            return;
        }
        std::string error;
        if (!solverProcess_.terminate(&error)) {
            status_ = error;
            return;
        }
        currentRunRequiresComputedFrame_ = false;
        try {
            const std::vector<std::filesystem::path> paths =
                VtkFrameParser::discoverFrames(currentRunDirectory_);
            if (paths.empty()) {
                status_ = "Stopped Fluid Solver; no VTK frames were saved.";
                return;
            }
            loadResultPaths(paths, ResultOrigin::StoppedFluidSolverRun);
        } catch (const std::exception& exception) {
            status_ = std::string("Stopped Fluid Solver. VTK load failed: ") +
                exception.what();
        }
    }

    sf::FloatRect parameterViewport() const {
        const float height = static_cast<float>(layoutSize_.y);
        const float bottom =
            std::max(PARAMETER_TOP + 40.0f, height - PARAMETER_BOTTOM_MARGIN);
        return {
            {panelX_, PARAMETER_TOP},
            {LEFT_PANEL_WIDTH, bottom - PARAMETER_TOP}
        };
    }

    bool compressibleOn() const {
        return sliders_[RegimeKind].choice() == "compressible";
    }

    bool volumeRun() const {
        return std::lround(sliders_[CellsZ].value) > 1 ||
               (activeFrame_ && activeFrame_->volumetric());
    }

    void refreshRowVisibility() {
        rowHidden_.fill(false);
        const bool gas = compressibleOn();
        const bool two = sliders_[Phases].value >= 1.5;
        const bool volume = volumeRun();

        const std::size_t volumeOnly[] = {
            PhaseSpotZ, GravityTilt, SliceY, BcFront, BcBack, BcFrontSpeed,
            BcBackSpeed, InletFrom2, InletTo2, BodyRotationX, BodyRotationY,
            BodySlideZ, BodyVelocityZ, BodySpinX, BodySpinY, BodyInertiaX,
            BodyInertiaY, BodyPinZ, BodyPinRotX, BodyPinRotY};
        for (std::size_t index : volumeOnly)
            rowHidden_[index] = !volume;

        const std::size_t incompressibleOnly[] = {
            Viscosity, Density, Density1, Viscosity1, Density2, Viscosity2,
            VofSchemeKind, MixingKindRow, SurfaceTension, ContactAngle,
            SourceLine, GravityEnabled, GravityAccel, GravityAngle,
            GravityMode, CoarseSorOmega, SmootherOmega, MgIterations,
            MgTolerance, MgMinCoarseSize, TurbulenceKindRow, SmagorinskyCs,
            TurbIntensity, TurbLengthScale, LidSpeed};
        const std::size_t compressibleOnly[] = {
            Gamma1, GasConstant1, Gamma2, GasConstant2, Temperature0,
            AmbientPressure, MachInlet, SpeciesModeRow, GridStretchKind,
            StretchRatio, RefineNear, AmrLevels, AmrCriterionKind,
            AmrThreshold, AmrEvery, AcousticFields,
            AcousticWindow, AcousticRef, MicrophoneLine, MicInterval,
            MicAudio, MicAudioRate, MicAudioSpeed};

        for (std::size_t index : incompressibleOnly)
            rowHidden_[index] = gas;
        for (std::size_t index : compressibleOnly)
            rowHidden_[index] = !gas;

        if (gas) {
            rowHidden_[Gamma2] = !two;
            rowHidden_[GasConstant2] = !two;
            rowHidden_[SpeciesModeRow] = !two;
            const bool stretching =
                sliders_[GridStretchKind].choice() != "off";
            rowHidden_[StretchRatio] = !stretching;
            rowHidden_[RefineNear] =
                !stretching || sliders_[GridStretchKind].choice() == "edges";
            const bool refining =
                std::lround(sliders_[AmrLevels].value) > 0;
            rowHidden_[AmrCriterionKind] = !refining;
            rowHidden_[AmrThreshold] = !refining;
            rowHidden_[AmrEvery] = !refining;
            const bool listening = sliders_[AcousticFields].value >= 0.5;
            rowHidden_[AcousticWindow] =
                !listening && sliders_[MicrophoneLine].text.empty();
            rowHidden_[AcousticRef] = rowHidden_[AcousticWindow];
            rowHidden_[MicInterval] = sliders_[MicrophoneLine].text.empty();
            rowHidden_[MicAudio] = rowHidden_[MicInterval];
            const bool recording =
                !rowHidden_[MicAudio] && sliders_[MicAudio].value >= 0.5;
            rowHidden_[MicAudioRate] = !recording;
            rowHidden_[MicAudioSpeed] = !recording;
            rowHidden_[WindSpeed] = true;
        } else {
            rowHidden_[LidSpeed] =
                sliders_[CaseKind].choice() != "cavity";
        }
        if (!solverInfo_.supportsVolume) {
            rowHidden_[DomainZ] = true;
            rowHidden_[CellsZ] = true;
        }
    }

    void applyTabAndSearchFilter() {
        const auto groupOf = [](std::size_t index) {
            std::size_t found = 0;
            for (std::size_t g = 0; g < PARAMETER_GROUPS.size(); ++g)
                if (PARAMETER_GROUPS[g].firstIndex <= index)
                    found = g;
            return found;
        };
        const bool filtering = !searchQuery_.empty();
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            if (rowHidden_[index])
                continue;
            if (filtering) {
                if (!rowMatchesSearch(index))
                    rowHidden_[index] = true;
                continue;
            }
            if (activeTab_ == 0)
                continue;
            const int group = static_cast<int>(groupOf(index));
            bool wanted = false;
            for (int allowed : PARAMETER_TABS[activeTab_].groups)
                if (allowed == group)
                    wanted = true;
            if (!wanted)
                rowHidden_[index] = true;
        }
        rowHidden_[BodyTrackLine] = true;
        if (focusedSlider_.has_value() && rowHidden_[*focusedSlider_])
            focusedSlider_.reset();
    }

    bool parameterRowOnScreen(std::size_t index) const {
        if (index >= sliders_.size() || rowHidden_[index]) {
            return false;
        }
        const sf::FloatRect viewport = parameterViewport();
        const float visualTop = sliders_[index].track.position.y - 26.0f;
        const float visualBottom = sliders_[index].track.position.y + 12.0f;
        return visualTop >= viewport.position.y &&
               visualBottom <= viewport.position.y + viewport.size.y;
    }

    MaskParameters sectionParameters() const {
        MaskParameters parameters;
        parameters.Lx = sliders_[DomainX].value;
        parameters.Ly = sliders_[DomainY].value;
        parameters.nx =
            static_cast<int>(std::lround(sliders_[CellsX].value));
        parameters.ny =
            static_cast<int>(std::lround(sliders_[CellsY].value));
        parameters.sliceAngleX = sliders_[SliceX].value;
        parameters.sliceAngleY = sliders_[SliceY].value;
        parameters.sliceAngleZ = sliders_[SliceZ].value;
        parameters.sliceRotation = sliders_[SliceRotation].value;
        parameters.invertSection = invertSection_;
        return parameters;
    }

    FluidSolverRunConfig fluidSolverRunConfig() const {
        FluidSolverRunConfig config;
        const MaskParameters parameters = sectionParameters();
        config.Lx = parameters.Lx;
        config.Ly = parameters.Ly;
        config.Lz = sliders_[DomainZ].value;
        config.nx = parameters.nx;
        config.ny = parameters.ny;
        config.nz = static_cast<int>(std::lround(sliders_[CellsZ].value));
        config.supportsVolume = solverInfo_.supportsVolume;
        config.U0 = sliders_[WindSpeed].value;
        config.nu = sliders_[Viscosity].value;
        config.ro = sliders_[Density].value;
        config.CFL = sliders_[Cfl].value;
        config.totalTime = sliders_[TotalTime].value;
        config.dtUpdateInterval =
            static_cast<int>(std::lround(sliders_[DtUpdateInterval].value));
        config.dtSafety = sliders_[DtSafety].value;
        config.omega = sliders_[CoarseSorOmega].value;
        config.smootherOmega = sliders_[SmootherOmega].value;
        config.mgIterations =
            static_cast<int>(std::lround(sliders_[MgIterations].value));
        config.mgTolerance = sliders_[MgTolerance].value;
        config.mgMinCoarseSize =
            static_cast<int>(std::lround(sliders_[MgMinCoarseSize].value));
        config.saveInterval =
            static_cast<int>(std::lround(sliders_[SaveInterval].value));
        config.useCuda = sliders_[UseCuda].value >= 0.5;

        config.geometryFile =
            (geometry_.triangles().empty() && solverInfo_.supportsCase)
                ? std::filesystem::path("empty")
                : geometry_.sourcePath();
        config.sliceAngleX = parameters.sliceAngleX;
        config.sliceAngleY = parameters.sliceAngleY;
        config.sliceAngleZ = parameters.sliceAngleZ;
        config.sliceRotation = parameters.sliceRotation;
        config.invertSection = parameters.invertSection;
        config.addTime = sliders_[AddTime].value;

        // Only put the 0.2 switches on the command line when the solver has
        // them: a 0.1 build stops on the first argument it does not know.
        config.supportsRuntimeSwitches = solverInfo_.supportsRuntimeSwitches;
        config.useAvx2 = sliders_[UseAvx2].value >= 0.5;
        config.useOpenMp = sliders_[UseOpenMp].value >= 0.5;
        config.threads =
            static_cast<int>(std::lround(sliders_[SolverThreads].value));
        // The solver's own tray icon would sit next to this one saying the
        // same thing, so it is turned off for a run the UI started. The
        // progress is here instead.
        config.solverTray = false;

        config.supportsGravity = solverInfo_.supportsGravity;
        config.gravityEnabled = sliders_[GravityEnabled].value >= 0.5;
        config.gravityAccel = sliders_[GravityAccel].value;
        config.gravityAngle = sliders_[GravityAngle].value;
        config.gravityTilt = sliders_[GravityTilt].value;

        config.supportsWallMotion = solverInfo_.supportsWallMotion;
        config.wallMotion = sliders_[WallMotionLine].text;

        config.supportsBodyMotion = solverInfo_.supportsBodyMotion;
        config.bodyMotion = sliders_[BodyMotionLine].text;
        config.bodyCoupling = sliders_[BodyCouplingKind].choice();
        config.bodyCollisions = sliders_[BodyCollisions].value >= 0.5;
        config.bodyRestitution = sliders_[BodyRestitution].value;
        config.bodyForceReport = sliders_[BodyForceReport].value >= 0.5;

        config.supportsCompressible = solverInfo_.supportsCompressible;
        config.regime = sliders_[RegimeKind].choice();
        config.gamma = sliders_[Gamma1].value;
        config.gasConstant = sliders_[GasConstant1].value;
        config.gamma2 = sliders_[Gamma2].value;
        config.gasConstant2 = sliders_[GasConstant2].value;
        config.T0 = sliders_[Temperature0].value;
        config.pInf = sliders_[AmbientPressure].value;
        config.machInlet = sliders_[MachInlet].value;
        config.speciesMode = sliders_[SpeciesModeRow].choice();
        config.amrLevels =
            static_cast<int>(std::lround(sliders_[AmrLevels].value));
        config.amrCriterion = sliders_[AmrCriterionKind].choice();
        config.amrThreshold = sliders_[AmrThreshold].value;
        config.amrEvery =
            static_cast<int>(std::lround(sliders_[AmrEvery].value));
        config.gridStretch = sliders_[GridStretchKind].choice();
        config.stretchRatio = sliders_[StretchRatio].value;
        config.refineNear = sliders_[RefineNear].value;
        config.acousticFields = sliders_[AcousticFields].value >= 0.5;
        config.acousticWindow = sliders_[AcousticWindow].value;
        config.acousticRef = sliders_[AcousticRef].value;
        config.microphones = sliders_[MicrophoneLine].text;
        config.micInterval =
            static_cast<int>(std::lround(sliders_[MicInterval].value));
        config.micAudio = sliders_[MicAudio].value >= 0.5;
        config.micAudioRate =
            static_cast<int>(std::lround(sliders_[MicAudioRate].value));
        config.micAudioSpeed = sliders_[MicAudioSpeed].value;

        config.supportsTurbulence = solverInfo_.supportsTurbulence;
        config.turbulence = sliders_[TurbulenceKindRow].choice();
        config.turbulenceCs = sliders_[SmagorinskyCs].value;
        config.turbIntensity = sliders_[TurbIntensity].value;
        config.turbLengthScale = sliders_[TurbLengthScale].value;

        config.supportsProfiles = solverInfo_.supportsProfiles;
        config.profiles = sliders_[Profiles].text;
        config.supportsExtraFields = solverInfo_.supportsExtraFields;
        config.extraFields = sliders_[ExtraFields].text;

        config.supportsSchemes = solverInfo_.supportsSchemes;
        config.convection = sliders_[Convection].choice();
        config.limiter = sliders_[Limiter].choice();
        config.timeScheme = sliders_[TimeSchemeKind].choice();
        config.gravityMode = sliders_[GravityMode].choice();

        config.supportsPhases = solverInfo_.supportsPhases;
        config.phases =
            static_cast<int>(std::lround(sliders_[Phases].value));
        config.rho1 = sliders_[Density1].value;
        config.rho2 = sliders_[Density2].value;
        config.nu1 = sliders_[Viscosity1].value;
        config.nu2 = sliders_[Viscosity2].value;
        config.phaseInit = sliders_[PhaseInitKind].choice();
        config.phaseLevel = sliders_[PhaseLevel].value;
        config.phaseX = sliders_[PhaseSpotX].value;
        config.phaseY = sliders_[PhaseSpotY].value;
        config.phaseZ = sliders_[PhaseSpotZ].value;
        config.vofScheme = sliders_[VofSchemeKind].choice();
        config.supportsTension = solverInfo_.supportsTension;
        config.mixing = sliders_[MixingKindRow].choice();
        config.diffusivity = sliders_[Diffusivity].value;
        config.surfaceTension = sliders_[SurfaceTension].value * 1e-3;
        config.contactAngle = sliders_[ContactAngle].value;
        config.sources = sliders_[SourceLine].text;
        config.initialPhaseFile.clear();

        config.supportsCase = solverInfo_.supportsCase;
        config.caseType = sliders_[CaseKind].choice();
        config.lidSpeed = sliders_[LidSpeed].value;
        config.steadyTolerance = sliders_[SteadyTolerance].value;

        config.supportsBoundaries = solverInfo_.supportsBoundaries;
        for (int side = 0; side < 6; ++side) {
            config.boundaryKind[side] =
                sliders_[boundaryKindRow(side)].choice();
            config.boundarySpeed[side] =
                sliders_[boundarySpeedRow(side)].value;
        }
        config.inletFrom = sliders_[InletFrom].value;
        config.inletTo = sliders_[InletTo].value;
        config.inletFrom2 = sliders_[InletFrom2].value;
        config.inletTo2 = sliders_[InletTo2].value;
        config.inletProfile = sliders_[InletProfileKind].choice();
        return config;
    }

    std::optional<std::size_t> sliderForValidationError(
        const std::string& error) const {
        const std::array<std::pair<const char*, ParameterIndex>, 51> mappings{{
            {"Lx", DomainX}, {"Ly", DomainY}, {"Lz", DomainZ},
            {"nx * ny * nz", CellsZ},
            {"nx", CellsX}, {"ny", CellsY}, {"nz", CellsZ},
            {"U0", WindSpeed}, {"nu", Viscosity}, {"ro", Density},
            {"CFL", Cfl}, {"totalTime", TotalTime},
            {"dtUpdateInterval", DtUpdateInterval}, {"dtSafety", DtSafety},
            {"omega", CoarseSorOmega}, {"smootherOmega", SmootherOmega},
            {"mgIterations", MgIterations}, {"mgTolerance", MgTolerance},
            {"mgMinCoarseSize", MgMinCoarseSize},
            {"saveInterval", SaveInterval}, {"useCuda", UseCuda},
            {"mixing", MixingKindRow}, {"diffusivity", Diffusivity},
            {"surfaceTension", SurfaceTension}, {"contactAngle", ContactAngle},
            {"regime", RegimeKind}, {"gamma", Gamma1}, {"machInlet", MachInlet},
            {"T0", Temperature0}, {"pInf", AmbientPressure},
            {"speciesMode", SpeciesModeRow},
            {"amrLevels", AmrLevels}, {"amrCriterion", AmrCriterionKind},
            {"amrThreshold", AmrThreshold}, {"amrEvery", AmrEvery},
            {"gridStretch", GridStretchKind}, {"stretchRatio", StretchRatio},
            {"refineNear", RefineNear},
            {"acousticWindow", AcousticWindow},
            {"microphones", MicrophoneLine}, {"micInterval", MicInterval},
            {"micAudio", MicAudio}, {"micAudioRate", MicAudioRate},
            {"micAudioSpeed", MicAudioSpeed},
            {"bodyMotion", BodyMotionLine}, {"bodyCoupling", BodyCouplingKind},
            {"bodyRestitution", BodyRestitution},
            {"gravityTilt", GravityTilt}, {"sliceAngleY", SliceY},
            {"inletFrom2", InletFrom2}, {"inletTo2", InletTo2}
        }};
        for (const auto& mapping : mappings) {
            if (error.rfind(mapping.first, 0) == 0) {
                return static_cast<std::size_t>(mapping.second);
            }
        }
        if (error.find("nx * ny") != std::string::npos) {
            return CellsX;
        }
        return std::nullopt;
    }

    void applyRestartControlDefaults(const VtkFrame& frame) {
        const auto assignDouble =
            [&frame, this](const char* key, ParameterIndex index) {
                const auto found = frame.restart.config.find(key);
                if (found == frame.restart.config.end()) {
                    return;
                }
                try {
                    std::size_t consumed = 0;
                    const double value = std::stod(found->second, &consumed);
                    if (consumed == found->second.size() &&
                        std::isfinite(value)) {
                        sliders_[index].value = value;
                    }
                } catch (const std::exception&) {
                }
            };
        // The grid and the domain come first and are not optional: a
        // continuation has to run on the frame's grid, and the solver refuses
        // outright when nx, ny, Lx or Ly differ from what the frame holds.
        // Leaving them at whatever the setup panel happened to show was why
        // "continue this run" could only ever work by accident.
        assignDouble("Lx", DomainX);
        assignDouble("Ly", DomainY);
        assignDouble("Lz", DomainZ);
        assignDouble("nx", CellsX);
        assignDouble("ny", CellsY);
        assignDouble("nz", CellsZ);
        assignDouble("U0", WindSpeed);
        assignDouble("nu", Viscosity);
        assignDouble("ro", Density);
        assignDouble("CFL", Cfl);
        assignDouble("totalTime", TotalTime);
        assignDouble("dtUpdateInterval", DtUpdateInterval);
        assignDouble("dtSafety", DtSafety);
        assignDouble("omega", CoarseSorOmega);
        assignDouble("smootherOmega", SmootherOmega);
        assignDouble("mgIterations", MgIterations);
        assignDouble("mgTolerance", MgTolerance);
        assignDouble("mgMinCoarseSize", MgMinCoarseSize);
        assignDouble("saveInterval", SaveInterval);
        assignDouble("gravityAccel", GravityAccel);
        assignDouble("gravityAngle", GravityAngle);
        const auto gravity = frame.restart.config.find("gravityEnabled");
        if (gravity != frame.restart.config.end()) {
            sliders_[GravityEnabled].value =
                gravity->second == "1" || gravity->second == "true" ? 1.0 : 0.0;
        }
        const auto cuda = frame.restart.config.find("useCuda");
        if (cuda != frame.restart.config.end()) {
            sliders_[UseCuda].value =
                cuda->second == "1" || cuda->second == "true" ? 1.0 : 0.0;
        }
    }

    std::string frameProgressLabel(const VtkFrame& frame) const {
        const int step = frame.restart.restartStep.value_or(frame.frameNumber);
        std::string label = "Solver step " + std::to_string(step);
        if (!frame.restart.currentTime) {
            return label;
        }
        label += " | t " +
            formatValue(*frame.restart.currentTime, false, "s");
        if (frame.restart.totalTime && *frame.restart.totalTime > 0.0) {
            const double percent = std::clamp(
                100.0 * *frame.restart.currentTime /
                    *frame.restart.totalTime,
                0.0,
                100.0);
            label += " / " +
                formatValue(*frame.restart.totalTime, false, "s") +
                " (" + formatValue(percent, false, "%") + ")";
        }
        return label;
    }

    void refreshSolverInfo() {
        solverInfo_ = inspectSolverExecutable(fluidSolverExecutable_);
        if (!solverInfo_.cudaCapable) {
            sliders_[UseCuda].value = 0.0;
            sliders_[UseCuda].label = "Request CUDA (unavailable)";
        } else {
            sliders_[UseCuda].label = "Request CUDA";
        }
    }

    std::filesystem::path defaultConfigurationFile() const {
        return executablePath_.parent_path() / "configuration.cfdui";
    }

    void applyCacheBudget() {
        const double megabytes = std::clamp(
            sliders_[CacheMegabytes].value, 32.0, 16384.0);
        const std::size_t bytes = static_cast<std::size_t>(
            std::llround(megabytes * 1024.0 * 1024.0));
        decodedFrameCache_.setByteBudget(bytes);
    }

    void loadPreferences() {
        std::ifstream input(preferencesFile_, std::ios::binary);
        if (!input.is_open()) {
            return;
        }
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const std::size_t separator = line.find('=');
            if (separator == std::string::npos) {
                continue;
            }
            const std::string key = line.substr(0, separator);
            const std::string value = line.substr(separator + 1);
            if (key == "outputRoot" && !value.empty()) {
                outputRoot_ = std::filesystem::u8path(value);
            } else if (key == "resultView3D") {
                view3D_ = value == "1";
            } else if (key == "uiCacheMB") {
                try {
                    const double parsed = std::stod(value);
                    if (std::isfinite(parsed) && parsed > 0.0) {
                        sliders_[CacheMegabytes].value = parsed;
                    }
                } catch (const std::exception&) {
                }
            }
        }
    }

    void savePreferences() const {
        std::ofstream output(
            preferencesFile_, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return;
        }
        output << "outputRoot=" << outputRoot_.u8string() << '\n';
        output << "resultView3D=" << (view3D_ ? 1 : 0) << '\n';
        output << "uiCacheMB=" <<
            editableNumber(sliders_[CacheMegabytes].value, true) << '\n';
    }

    std::string configurationValue(std::size_t index) const {
        const Slider& slider = sliders_[index];
        if (slider.kind == ControlKind::Text)
            return slider.text;
        if (slider.kind == ControlKind::Choice)
            return slider.choice();
        std::ostringstream out;
        out << std::setprecision(
            std::numeric_limits<double>::max_digits10);
        out << (index == SurfaceTension ? slider.value * 1e-3 : slider.value);
        return out.str();
    }

    std::array<std::string, ParameterCount> configurationValues() const {
        std::array<std::string, ParameterCount> values;
        for (std::size_t index = 0; index < sliders_.size(); ++index)
            values[index] = configurationValue(index);
        return values;
    }

    std::vector<std::pair<std::string, std::string>>
    configurationExtras() const {
        return {
            {"model", geometry_.sourcePath().u8string()},
            {"outputRoot", outputRoot_.u8string()},
            {"solver", fluidSolverExecutable_.u8string()},
            {"invertSection", invertSection_ ? "1" : "0"}
        };
    }

    std::string configurationText() const {
        return formatConfiguration(
            configurationValues(), configurationExtras());
    }

    bool applyConfigurationRow(std::size_t index,
                               const std::string& value,
                               std::string& error) {
        Slider& slider = sliders_[index];
        if (slider.kind == ControlKind::Text) {
            if (value.find_first_of("\r\n") != std::string::npos) {
                error = "has to stay on one line";
                return false;
            }
            slider.text = value;
            return true;
        }
        if (slider.kind == ControlKind::Choice)
            return slider.setFromText(value, error);
        try {
            std::size_t consumed = 0;
            const double parsed = std::stod(value, &consumed);
            if (consumed != value.size() || !std::isfinite(parsed)) {
                error = "does not carry a number";
                return false;
            }
            slider.value =
                index == SurfaceTension ? parsed * 1e3 : parsed;
            return true;
        } catch (const std::exception&) {
            error = "does not carry a number";
            return false;
        }
    }

    std::string applyConfigurationDocument(
        const ConfigurationDocument& document,
        bool withPaths) {
        const bool millinewtons = document.format == "CFDMaskUI-1";
        std::size_t applied = 0;
        std::vector<std::string> refused;
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            if (!document.present[index])
                continue;
            std::string error;
            if (!applyConfigurationRow(index, document.values[index], error)) {
                refused.push_back(
                    std::string(parameterKey(index)) + " " + error);
                continue;
            }
            if (millinewtons && index == SurfaceTension)
                sliders_[index].value *= 1e-3;
            ++applied;
        }
        for (const auto& extra : document.extras)
            if (extra.first == "invertSection")
                invertSection_ = extra.second == "1";
        if (withPaths) {
            const std::string root = document.extra("outputRoot");
            if (!root.empty())
                outputRoot_ = std::filesystem::u8path(root);
            const std::string chosen = document.extra("solver");
            if (!chosen.empty()) {
                const std::filesystem::path solver =
                    std::filesystem::u8path(chosen);
                const SolverExecutableInfo info =
                    inspectSolverExecutable(solver);
                if (info.valid && info.recognized) {
                    fluidSolverExecutable_ = solver;
                    solverInfo_ = info;
                }
            }
            const std::string model = document.extra("model");
            if (!model.empty()) {
                const std::filesystem::path file =
                    std::filesystem::u8path(model);
                std::error_code fileError;
                if (std::filesystem::is_regular_file(file, fileError) &&
                    !fileError)
                    loadGeometry(file);
            }
            // A configuration written by hand names its models in profiles=,
            // which is a solver argument and nothing this panel draws. Without
            // this the setup opened on an empty scene and said nothing about
            // why, while the solver went on to fly a body the screen had never
            // shown. The first model in the line is the one the preview takes;
            // model= still wins when the file carries it.
            if (model.empty() && document.present[Profiles]) {
                for (const std::string& entry :
                     splitProfileEntries(sliders_[Profiles].text)) {
                    const std::string named = profileFileOf(entry);
                    if (named.empty())
                        continue;
                    const std::filesystem::path file =
                        std::filesystem::u8path(named);
                    std::error_code fileError;
                    if (std::filesystem::is_regular_file(file, fileError) &&
                        !fileError) {
                        loadGeometry(file);
                        break;
                    }
                }
            }
        }
        invalidSlider_.reset();
        loadBodyRows();
        updateLayout(layoutSize_);

        std::string report = std::to_string(applied) + " setting(s) applied";
        if (!document.ignored.empty())
            report += ", " + std::to_string(document.ignored.size()) +
                " the panel does not carry (" + document.ignored.front() +
                (document.ignored.size() > 1 ? ", ..." : "") + ")";
        if (!refused.empty())
            report += ", refused " + refused.front() +
                (refused.size() > 1
                     ? " and " + std::to_string(refused.size() - 1) + " more"
                     : "");
        if (!document.unknown.empty())
            report += ", " + std::to_string(document.unknown.size()) +
                " unrecognised key(s) ignored (" + document.unknown.front() +
                (document.unknown.size() > 1 ? ", ..." : "") + ")";
        return report + ".";
    }

    bool writeConfigurationFile(
        const std::filesystem::path& path,
        std::string& error) const {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            error = "Cannot open configuration file for writing: " +
                path.string();
            return false;
        }
        output << configurationText();
        output.flush();
        if (!output) {
            error = "Failed while writing configuration file: " +
                path.string();
            return false;
        }
        return true;
    }

    std::string configurationTextOfFrame(
        const std::filesystem::path& path,
        std::string& error) const {
        try {
            const VtkFrame frame = VtkFrameParser::parse(path);
            if (!frame.restart.hasConfigText) {
                error = "That frame carries no configText, so there is "
                        "nothing in it to recover the settings from.";
                return std::string();
            }
            std::string text;
            for (const auto& entry : frame.restart.config)
                text += entry.first + "=" + entry.second + "\n";
            return text;
        } catch (const std::exception& exception) {
            error = std::string("Cannot read that frame: ") + exception.what();
            return std::string();
        }
    }

    bool readConfigurationFile(
        const std::filesystem::path& path,
        std::string& error) {
        std::string text;
        if (path.extension() == ".vtk") {
            text = configurationTextOfFrame(path, error);
            if (text.empty()) {
                return false;
            }
        } else {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                error = "Cannot open configuration file: " + path.string();
                return false;
            }
            text.assign(
                (std::istreambuf_iterator<char>(input)),
                std::istreambuf_iterator<char>());
        }
        const ConfigurationDocument document = parseConfiguration(text);
        if (!document.recognised) {
            error = "Nothing in that file is a solver or UI setting.";
            return false;
        }
        const std::string report = applyConfigurationDocument(document, true);
        refreshSolverInfo();
        applyCacheBudget();
        savePreferences();
        error = report;
        return true;
    }

    void saveConfiguration() {
        std::string error;
        const std::filesystem::path path =
            chooseUiConfigFile(
                window_->getNativeHandle(),
                true,
                defaultConfigurationFile(),
                error);
        if (path.empty()) {
            status_ = error.empty()
                ? std::string("Configuration save cancelled.")
                : error;
            return;
        }
        if (!writeConfigurationFile(path, error)) {
            status_ = error;
            return;
        }
        status_ = "Saved configuration: " + path.string();
    }

    void loadConfiguration() {
        pushUndo();
        std::string error;
        const std::filesystem::path path =
            chooseUiConfigFile(
                window_->getNativeHandle(),
                false,
                defaultConfigurationFile(),
                error);
        if (path.empty()) {
            undoStack_.pop_back();
            status_ = error.empty()
                ? std::string("Configuration load cancelled.")
                : error;
            return;
        }
        if (!readConfigurationFile(path, error)) {
            undoStack_.pop_back();
            status_ = error;
            return;
        }
        status_ = "Loaded " + path.string() + ": " + error;
    }

    void loadConfigurationFromFrame() {
        if (!activeFrame_ || !activeFrame_->restart.hasConfigText) {
            status_ =
                "That frame carries no configText, so there is nothing to "
                "recover its settings from.";
            return;
        }
        std::string text;
        for (const auto& entry : activeFrame_->restart.config)
            text += entry.first + "=" + entry.second + "\n";
        pushUndo();
        const ConfigurationDocument document = parseConfiguration(text);
        if (!document.recognised) {
            undoStack_.pop_back();
            status_ = "That frame's configText holds nothing this panel knows.";
            return;
        }
        const std::string report = applyConfigurationDocument(document, false);
        applyCacheBudget();
        mode_ = DisplayMode::Setup;
        status_ = "Recovered the settings of solver step " +
            std::to_string(activeFrame_->frameNumber) + ": " + report;
    }

    void resetDefaults() {
        pushUndo();
        for (Slider& slider : sliders_) {
            slider.value = slider.defaultValue;
            slider.text = slider.defaultText;
        }
        if (!solverInfo_.cudaCapable) {
            sliders_[UseCuda].value = 0.0;
        }
        invertSection_ = false;
        invalidSlider_.reset();
        applyCacheBudget();
        status_ = "Solver and UI parameters reset to defaults.";
    }

    void importGeometry() {
        const std::filesystem::path selected =
            chooseGeometryFile(window_->getNativeHandle());
        if (selected.empty()) {
            return;
        }
        loadGeometry(selected);
    }

    void selectOutputFolder() {
        std::string error;
        const std::filesystem::path selected =
            chooseOutputFolder(window_->getNativeHandle(), error);
        if (!error.empty()) {
            status_ = error;
            return;
        }
        if (selected.empty()) {
            return;
        }
        std::error_code absoluteError;
        const std::filesystem::path absolute =
            std::filesystem::absolute(selected, absoluteError);
        outputRoot_ = absoluteError ? selected : absolute;
        savePreferences();
        status_ = "Simulation output root: " + outputRoot_.string() +
            ". Each run uses a new child directory.";
    }

    void openVtkFiles() {
        if (solverProcess_.active) {
            status_ =
                "Cannot import VTK frames while the Fluid Solver is running.";
            return;
        }
        std::string error;
        const std::vector<std::filesystem::path> selected =
            chooseVtkFiles(window_->getNativeHandle(), error);
        if (!error.empty()) {
            status_ = error;
            return;
        }
        if (!selected.empty()) {
            loadExternalResultInputs(selected);
        }
    }

    void selectFluidSolverExecutable() {
        if (solverProcess_.active) {
            status_ =
                "Cannot change the Fluid Solver while it is running.";
            return;
        }
        std::string error;
        const std::filesystem::path selected =
            chooseSolverExecutable(window_->getNativeHandle(), error);
        if (!error.empty()) {
            status_ = error;
            return;
        }
        if (selected.empty()) {
            return;
        }

        std::error_code absoluteError;
        const std::filesystem::path absolute =
            std::filesystem::absolute(selected, absoluteError);
        const std::filesystem::path executable =
            absoluteError ? selected : absolute;
        const SolverExecutableInfo info = inspectSolverExecutable(executable);
        if (!info.valid) {
            status_ = "Cannot select Fluid Solver: " + info.detail;
            return;
        }
        if (!info.recognized) {
            status_ =
                "Selected file is not recognized as the Fluid Solver; "
                "selection was rejected.";
            return;
        }

        fluidSolverExecutable_ = executable;
        solverInfo_ = info;
        if (!solverInfo_.cudaCapable) {
            sliders_[UseCuda].value = 0.0;
        }
        if (!writeFluidSolverSelection(
                solverSelectionFile_,
                fluidSolverExecutable_,
                error)) {
            status_ =
                "Selected Fluid Solver for this session, but could not "
                "persist it: " + error;
            return;
        }
        status_ =
            "Selected Fluid Solver " + solverInfo_.version + " (" +
            solverInfo_.build + "): " + fluidSolverExecutable_.string();
    }

    std::optional<ExplorerTarget> currentExplorerTarget() const {
        const std::filesystem::path selectedFrame =
            !frames_.empty() && selectedFrame_ < frames_.size()
                ? frames_[selectedFrame_].sourcePath
                : std::filesystem::path{};
        return chooseExplorerTarget(
            selectedFrame,
            currentRunDirectory_);
    }

    void revealVtkLocation() {
        const std::optional<ExplorerTarget> target =
            currentExplorerTarget();
        if (!target) {
            status_ = "No VTK file or run directory is available.";
            return;
        }

        std::string error;
        if (!openExplorerTarget(
                window_->getNativeHandle(),
                *target,
                error)) {
            status_ = "Cannot open VTK location: " + error;
            return;
        }

        status_ = target->selectFile
            ? "Selected " + target->location.filename().string() +
                  " in File Explorer."
            : "Opened the run folder in the file manager.";
    }

    void loadGeometry(const std::filesystem::path& selected) {
        std::string error;
        if (!geometry_.load(selected, error)) {
            status_ = "Import failed: " + error;
            return;
        }
        status_ =
            "Loaded model (" +
            std::to_string(geometry_.triangles().size()) +
            " triangles).";
        sectionSegments_.clear();
        sectionSegmentsSliceX_ = std::numeric_limits<double>::quiet_NaN();
        sectionSegmentsSliceZ_ = std::numeric_limits<double>::quiet_NaN();
        setupZoom_ = 1.0f;
    }

    void generateAndRun() {
        std::string error;
        refreshSolverInfo();
        if (!solverInfo_.valid || !solverInfo_.recognized) {
            status_ =
                "Cannot run: selected executable is not a recognized Fluid "
                "Solver build.";
            return;
        }
        FluidSolverRunConfig requestedConfig = fluidSolverRunConfig();
        if (!validateFluidSolverRunConfig(requestedConfig, error)) {
            invalidSlider_ = sliderForValidationError(error);
            status_ = "Cannot run Fluid Solver: " + error;
            return;
        }
        invalidSlider_.reset();
        applyCacheBudget();

        const bool emptyDomain = requestedConfig.geometryFile == "empty";

        MaskResult mask;
        if (emptyDomain) {
            mask.success = true;
            mask.cells.assign(
                static_cast<std::size_t>(requestedConfig.nx) *
                    static_cast<std::size_t>(requestedConfig.ny),
                0);
        } else {
            mask = geometry_.generateMask(sectionParameters());
        }
        if (!mask.success) {
            if (mask.error.find("10000000-cell") != std::string::npos) {
                invalidSlider_ = CellsX;
            }
            status_ = "Mask generation failed: " + mask.error;
            return;
        }

        bool reducedToLargestContour = false;
        const std::size_t originalContourCount = mask.contours.size();
        // Fluid Solver 0.2 keeps every closed loop the section cut - two
        // aerofoils side by side stay two aerofoils, and a loop inside another
        // loop comes out as a hole. Before that it kept the largest and threw
        // the rest away, so the UI had to ask first and throw them away itself
        // to keep the preview honest about what would be solved.
        if (!emptyDomain && mask.contours.size() > 1 &&
            !solverInfo_.supportsMultipleContours) {
            if (!confirmUseLargestContour(
                    window_->getNativeHandle(), mask.contours.size())) {
                status_ =
                    "Run cancelled: Fluid Solver " + solverInfo_.version +
                    " accepts one closed contour, while the UI detected " +
                    std::to_string(mask.contours.size()) +
                    ". Fluid Solver 0.2 takes all of them.";
                return;
            }
            const auto largest = std::max_element(
                mask.contours.begin(),
                mask.contours.end(),
                [](const std::vector<Vec2>& first,
                   const std::vector<Vec2>& second) {
                    return contourArea(first) < contourArea(second);
                });
            std::vector<std::vector<Vec2>> selectedContours{
                *largest
            };
            mask = geometry_.rasterizeContours(
                sectionParameters(), std::move(selectedContours));
            if (!mask.success) {
                status_ =
                    "Largest-contour preview failed: " + mask.error;
                return;
            }
            reducedToLargestContour = true;
        }

        solidBodyCount_ =
            emptyDomain
                ? 0u
                : std::max<std::size_t>(
                      countSolidBodies(
                          mask.cells, requestedConfig.nx, requestedConfig.ny),
                      1u);

        const std::filesystem::path runDirectory =
            createRunDirectory(outputRoot_, error);
        if (runDirectory.empty()) {
            status_ = error;
            return;
        }
        std::vector<std::string> requestedArguments;
        if (!buildFluidSolverArguments(
                requestedConfig,
                runDirectory,
                requestedArguments,
                error)) {
            status_ = "Cannot build requested solver arguments: " + error;
            return;
        }
        const std::filesystem::path requestedArgumentFile =
            runDirectory / "requested-arguments.txt";
        if (!writeFluidSolverArguments(
                requestedArgumentFile,
                requestedArguments,
                error)) {
            status_ = "Cannot write requested run record: " + error;
            return;
        }

        const std::filesystem::path uiRequestFile =
            runDirectory / "ui-request.txt";
        std::ofstream uiRequest(
            uiRequestFile,
            std::ios::out | std::ios::trunc);
        if (!uiRequest.is_open()) {
            status_ =
                "Cannot write UI run record: " + uiRequestFile.string();
            return;
        }
        uiRequest << std::setprecision(
            std::numeric_limits<double>::max_digits10);
        uiRequest << "model=" << geometry_.sourcePath().string() << '\n';
        uiRequest << "uiVersion=" << CFD_MASK_UI_VERSION << '\n';
        uiRequest << "solverPath=" << fluidSolverExecutable_.u8string() << '\n';
        uiRequest << "solverVersion=" << solverInfo_.version << '\n';
        uiRequest << "solverBuild=" << solverInfo_.build << '\n';
        uiRequest << "outputRoot=" << outputRoot_.u8string() << '\n';
        uiRequest << "sourceContourCount=" << originalContourCount << '\n';
        uiRequest << "solverContourCount=" << mask.contours.size() << '\n';
        uiRequest << "reducedToLargestContour="
                  << (reducedToLargestContour ? 1 : 0) << '\n';
        uiRequest << "sliceRotationDegrees="
                  << requestedConfig.sliceRotation << '\n';
        uiRequest << "solverObjectCount=" << solidBodyCount_ << '\n';
        uiRequest << "wallMotion=" << requestedConfig.wallMotion << '\n';
        uiRequest.flush();
        if (!uiRequest) {
            status_ =
                "Failed while writing UI run record: " +
                uiRequestFile.string();
            return;
        }

        const bool volumeDomain =
            requestedConfig.supportsVolume && requestedConfig.nz > 1;
        const std::filesystem::path adapterFile =
            runDirectory / "section-adapter.obj";
        if (!emptyDomain && !volumeDomain &&
            !writeSectionAdapterOBJ(adapterFile, mask.contours, error)) {
            status_ = "Cannot write section adapter: " + error;
            return;
        }

        FluidSolverRunConfig solverConfig = requestedConfig;
        solverConfig.geometryFile =
            emptyDomain
                ? std::filesystem::path("empty")
                : (volumeDomain ? geometry_.sourcePath() : adapterFile);

        ensurePaintField();
        if (requestedConfig.phases > 1 && paintFieldUsed()) {
            const std::filesystem::path phaseFile =
                runDirectory / "initial-phase.txt";
            if (!writePaintField(phaseFile, error)) {
                status_ = error;
                return;
            }
            solverConfig.initialPhaseFile = phaseFile;
        }
        if (!volumeDomain) {
            solverConfig.sliceAngleX = 0.0;
            solverConfig.sliceAngleY = 0.0;
            solverConfig.sliceAngleZ = 0.0;
            solverConfig.sliceRotation = 0.0;
            solverConfig.invertSection = false;
        }
        std::vector<std::string> solverArguments;
        if (!buildFluidSolverArguments(
                solverConfig,
                runDirectory,
                solverArguments,
                error)) {
            status_ = "Cannot build Fluid Solver arguments: " + error;
            return;
        }
        const std::filesystem::path solverArgumentFile =
            runDirectory / "solver-arguments.txt";
        if (!writeFluidSolverArguments(
                solverArgumentFile,
                solverArguments,
                error)) {
            status_ = "Cannot write Fluid Solver arguments: " + error;
            return;
        }
        if (!solverProcess_.start(
                fluidSolverExecutable_,
                solverArguments,
                runDirectory,
                error)) {
            status_ = error;
            return;
        }

        currentRunDirectory_ = runDirectory;
        currentRunIsContinuation_ = false;
        continuationSourceStep_ = -1;
        currentRunRequiresComputedFrame_ =
            requestedConfig.totalTime > 0.0;
        beginRunProgress(0.0, requestedConfig.totalTime);
        nextSolverProgressUpdate_ =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(500);
        previewSolid_.assign(mask.cells.begin(), mask.cells.end());
        previewNx_ = static_cast<std::size_t>(requestedConfig.nx);
        previewNy_ = static_cast<std::size_t>(requestedConfig.ny);
        frames_.clear();
        playingFrames_ = false;
        showRunDetails_ = false;
        runDetailsText_.clear();
        activeFrame_.reset();
        refreshDisplayFrame();
        decodedFrameCache_.clear();
        desiredFrame_.reset();
        inFlightFrames_.clear();
        resultTextureCacheValid_ = false;
        resultsWarning_.clear();
        selectedFrame_ = 0;
        status_ =
            (reducedToLargestContour
                 ? "Reduced " + std::to_string(originalContourCount) +
                       " disconnected contours to the largest one. "
                 : (originalContourCount > 1
                        ? "All " + std::to_string(originalContourCount) +
                              " contours go to the solver. "
                        : std::string{})) +
            "Preview contains " + std::to_string(mask.contours.size()) +
            " solver contour(s) and " +
            std::to_string(mask.solidCellCount) +
            " solid cells. Fluid Solver started." +
            (requestedConfig.totalTime > 0.0
                 ? " Projection validation stops the run before saving any "
                   "timestep that exceeds the divergence limit."
                 : "");
    }

    // Pick up an earlier run where a frame left off.
    //
    // Everything that decides the physics comes out of the frame: the grid,
    // the domain, the solid mask, the velocity and pressure fields. The model
    // file is not needed and is not asked for - it may not even still exist.
    // What the person supplies is how much further to go, and that is either
    // "Continue: add time" seconds past where the frame stopped, or a "Total
    // time" that is further along than the frame already is.
    void continueFromSelectedFrame() {
        if (!activeFrame_) {
            status_ = "Select a frame to continue from first.";
            return;
        }
        const VtkFrame& frame = *activeFrame_;
        if (!frame.restart.restartCapable) {
            status_ =
                "This frame carries no restart state, so there is nothing to "
                "continue from. Frames written by Fluid Solver 0.1.1 and newer "
                "do.";
            return;
        }
        refreshSolverInfo();
        if (!solverInfo_.valid || !solverInfo_.recognized) {
            status_ =
                "Cannot continue: the selected executable is not a recognized "
                "Fluid Solver build.";
            return;
        }
        if (!solverInfo_.supportsContinuation) {
            status_ =
                "Cannot continue: Fluid Solver " + solverInfo_.version +
                " has no restart arguments. 0.1.1 or newer is needed.";
            return;
        }

        const double frameTime = frame.restart.currentTime.value_or(0.0);
        const double addTime = sliders_[AddTime].value;
        double target = sliders_[TotalTime].value;
        if (addTime > 0.0) {
            target = frameTime + addTime;
        } else if (target <= frameTime) {
            status_ =
                "Nothing to compute: this frame is already at " +
                formatValue(frameTime, false, "s") +
                " and Total time is " +
                formatValue(target, false, "s") +
                ". Raise Total time, or set \"Continue: add time\".";
            invalidSlider_ = TotalTime;
            return;
        }
        invalidSlider_.reset();
        applyCacheBudget();

        FluidSolverRunConfig config = fluidSolverRunConfig();
        config.restart = true;
        config.restartFile = frame.sourcePath;
        config.totalTime = target;
        config.addTime = addTime;

        if (config.wallMotion.empty())
            config.supportsWallMotion = false;
        if (config.bodyMotion.empty())
            config.supportsBodyMotion = false;
        if (config.profiles.empty())
            config.supportsProfiles = false;

        std::string error;
        if (!validateFluidSolverRunConfig(config, error)) {
            invalidSlider_ = sliderForValidationError(error);
            status_ = "Cannot continue: " + error;
            return;
        }

        const std::filesystem::path runDirectory =
            createRunDirectory(outputRoot_, error);
        if (runDirectory.empty()) {
            status_ = error;
            return;
        }
        std::vector<std::string> arguments;
        if (!buildFluidSolverArguments(
                config, runDirectory, arguments, error)) {
            status_ = "Cannot build continuation arguments: " + error;
            return;
        }
        if (!writeFluidSolverArguments(
                runDirectory / "solver-arguments.txt", arguments, error)) {
            status_ = "Cannot write continuation arguments: " + error;
            return;
        }

        // The same record a fresh run writes, so a continuation directory can
        // be read back the same way - and says where it came from.
        std::ofstream uiRequest(
            runDirectory / "ui-request.txt",
            std::ios::out | std::ios::trunc);
        if (uiRequest.is_open()) {
            uiRequest << std::setprecision(
                std::numeric_limits<double>::max_digits10);
            uiRequest << "continuedFrom=" << frame.sourcePath.u8string() << '\n';
            uiRequest << "continuedFromStep="
                      << frame.restart.restartStep.value_or(frame.frameNumber)
                      << '\n';
            uiRequest << "continuedFromTime=" << frameTime << '\n';
            uiRequest << "targetTime=" << target << '\n';
            uiRequest << "uiVersion=" << CFD_MASK_UI_VERSION << '\n';
            uiRequest << "solverPath=" << fluidSolverExecutable_.u8string()
                      << '\n';
            uiRequest << "solverVersion=" << solverInfo_.version << '\n';
            uiRequest << "solverBuild=" << solverInfo_.build << '\n';
        }

        if (!solverProcess_.start(
                fluidSolverExecutable_, arguments, runDirectory, error)) {
            status_ = error;
            return;
        }

        currentRunDirectory_ = runDirectory;
        currentRunIsContinuation_ = true;
        continuationSourceStep_ =
            frame.restart.restartStep.value_or(frame.frameNumber);
        currentRunRequiresComputedFrame_ = true;
        beginRunProgress(frameTime, target);
        nextSolverProgressUpdate_ =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(500);
        frames_.clear();
        playingFrames_ = false;
        showRunDetails_ = false;
        runDetailsText_.clear();
        activeFrame_.reset();
        refreshDisplayFrame();
        decodedFrameCache_.clear();
        desiredFrame_.reset();
        inFlightFrames_.clear();
        resultTextureCacheValid_ = false;
        resultsWarning_.clear();
        selectedFrame_ = 0;
        status_ =
            "Continuing from solver step " +
            std::to_string(continuationSourceStep_) + " at " +
            formatValue(frameTime, false, "s") + " up to " +
            formatValue(target, false, "s") +
            ". The grid and the geometry come from the frame.";
    }

    void loadExternalResultInputs(
        const std::vector<std::filesystem::path>& inputs) {
        if (solverProcess_.active || resultCatalogFuture_.valid() ||
            !inFlightFrames_.empty()) {
            status_ =
                "Cannot import VTK frames while another load or solver runs.";
            return;
        }
        try {
            std::vector<std::filesystem::path> paths;
            for (const std::filesystem::path& input : inputs) {
                std::error_code pathError;
                const bool isDirectory =
                    std::filesystem::is_directory(input, pathError);
                if (pathError) {
                    throw VtkParseError(
                        "Cannot inspect input path " + input.string() +
                        ": " + pathError.message());
                }
                if (!isDirectory) {
                    paths.push_back(input);
                    continue;
                }
                const std::vector<std::filesystem::path> discovered =
                    VtkFrameParser::discoverFrames(input);
                if (discovered.empty()) {
                    throw VtkParseError(
                        "Directory contains no solver solution VTK frames: " +
                        input.string());
                }
                paths.insert(
                    paths.end(),
                    discovered.begin(),
                    discovered.end());
            }
            loadResultPaths(paths, ResultOrigin::ImportedFiles);
        } catch (const std::exception& exception) {
            status_ = std::string("VTK load failed: ") + exception.what();
        }
    }

    void loadResultFrames() {
        try {
            const std::vector<std::filesystem::path> paths =
                VtkFrameParser::discoverFrames(currentRunDirectory_);
            if (paths.empty()) {
                std::string diagnostic = readDiagnosticFile(
                    currentRunDirectory_ / "solver-error.txt");
                if (diagnostic.empty()) {
                    diagnostic = readDiagnosticFile(
                        currentRunDirectory_ / "solver-output.txt");
                }
                status_ =
                    "Solver completed but produced no VTK frames." +
                    (diagnostic.empty() ? "" : " " + diagnostic);
                return;
            }
            loadResultPaths(
                paths,
                currentRunIsContinuation_
                    ? ResultOrigin::ContinuedFluidSolverRun
                    : ResultOrigin::FluidSolverRun);
        } catch (const std::exception& exception) {
            const std::string diagnostic = readDiagnosticFile(
                currentRunDirectory_ / "solver-error.txt");
            status_ =
                std::string("VTK load failed: ") + exception.what() +
                (diagnostic.empty() ? "" : " " + diagnostic);
        }
    }

    void loadResultPaths(
        const std::vector<std::filesystem::path>& paths,
        ResultOrigin origin) {
        const bool stopped =
            origin == ResultOrigin::StoppedFluidSolverRun;
        if (resultCatalogFuture_.valid() || !inFlightFrames_.empty()) {
            status_ = "A VTK load is already running.";
            return;
        }
        prefetchQueue_.clear();
        adaptiveWindowIndices_.clear();
        adaptiveWindowDivisor_ = 1;
        pendingResultOrigin_ = origin;
        resultCatalogStarted_ = std::chrono::steady_clock::now();
        resultCatalogFuture_ = std::async(
            std::launch::async,
            [paths, stopped, origin]() {
                VtkSeriesCatalog catalog =
                    VtkFrameParser::indexSeries(paths, stopped);
                if ((origin == ResultOrigin::FluidSolverRun ||
                     origin == ResultOrigin::ContinuedFluidSolverRun) &&
                    catalog.frames.size() > 1) {
                    const VtkFrame initial = VtkFrameParser::parse(
                        catalog.frames.front().sourcePath);
                    VtkFrameParser::validateCompatibility(
                        initial,
                        catalog.activeFrame);
                }
                return catalog;
            });
        status_ = "Indexing " + std::to_string(paths.size()) +
            " VTK frame(s) and decoding only the active frame...";
    }

    void pollResultCatalog() {
        if (!resultCatalogFuture_.valid() ||
            resultCatalogFuture_.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready) {
            return;
        }
        const ResultOrigin origin = *pendingResultOrigin_;
        pendingResultOrigin_.reset();
        try {
            commitResultCatalog(resultCatalogFuture_.get(), origin);
        } catch (const std::exception& exception) {
            if (origin != ResultOrigin::ImportedFiles) {
                currentRunRequiresComputedFrame_ = false;
            }
            status_ = std::string("VTK load failed: ") + exception.what();
        }
    }

    void commitResultCatalog(
        VtkSeriesCatalog catalog,
        ResultOrigin origin) {
        const bool stopped =
            origin == ResultOrigin::StoppedFluidSolverRun;
        const bool continued =
            origin == ResultOrigin::ContinuedFluidSolverRun;
        if (catalog.frames.empty()) {
            throw VtkParseError("No complete compatible VTK frames exist");
        }
        const std::size_t rejectedCount = catalog.rejected.size();
        const std::size_t warningCount = catalog.warningCount;
        const bool fromFluidSolver =
            origin != ResultOrigin::ImportedFiles;
        if (origin == ResultOrigin::FluidSolverRun &&
            currentRunRequiresComputedFrame_ &&
                (catalog.frames.front().frameNumber != 0 ||
                 catalog.frames.size() < 2 ||
                 catalog.frames.back().frameNumber <= 0)) {
            throw VtkParseError(
                "Fluid Solver completed without the required initial "
                "and positive-step VTK frames");
        }
        if (continued && currentRunRequiresComputedFrame_ &&
            catalog.frames.back().frameNumber <= continuationSourceStep_) {
            throw VtkParseError(
                "Continuation completed without a solver step newer than " +
                std::to_string(continuationSourceStep_));
        }
        const bool hasPreview =
            fromFluidSolver && !previewSolid_.empty();
        const bool previewMatches =
            hasPreview &&
            catalog.activeFrame.nx == previewNx_ &&
            catalog.activeFrame.ny == previewNy_ &&
            catalog.activeFrame.solid == previewSolid_;
        const std::string solverDiagnostic =
            fromFluidSolver
                ? readDiagnosticFile(
                      currentRunDirectory_ / "solver-error.txt")
                : std::string{};
        frames_ = std::move(catalog.frames);
        playingFrames_ = false;
        showRunDetails_ = false;
        runDetailsText_.clear();
        activeFrame_ = std::make_shared<VtkFrame>(
            std::move(catalog.activeFrame));
        applyRestartControlDefaults(*activeFrame_);
        setViewMode(activeFrame_->volumetric());
        refreshDisplayFrame();
        currentRunRequiresComputedFrame_ = false;
        currentRunIsContinuation_ = false;
        selectedFrame_ = frames_.size() - 1;
        desiredFrame_.reset();
        inFlightFrames_.clear();
        decodedFrameCache_.clear();
        decodedFrameCache_.insert(selectedFrame_, activeFrame_);
        planAdaptivePrefetch(selectedFrame_);
        startNextPrefetch();
        pressureRange_ = catalog.pressureRange;
        velocityRange_ = catalog.velocityMagnitudeRange;
        pressureTrimmedRange_ = catalog.pressureTrimmedRange;
        velocityTrimmedRange_ = catalog.velocityMagnitudeTrimmedRange;
        resultZoom_ = 1.0f;
        resultPan_ = {};
        resultTextureCacheValid_ = false;
        resultsWarning_.clear();
        if (stopped) {
            if (!resultsWarning_.empty()) {
                resultsWarning_ += "  ";
            }
            resultsWarning_ += "STOPPED EARLY: complete frames only.";
        }
        if (hasPreview && !previewMatches) {
            if (!resultsWarning_.empty()) {
                resultsWarning_ += "  ";
            }
            resultsWarning_ +=
                "MASK MISMATCH: Fluid Solver VTK differs from GUI preview.";
        }
        if (!solverDiagnostic.empty()) {
            if (!resultsWarning_.empty()) {
                resultsWarning_ += "  ";
            }
            resultsWarning_ += "SOLVER REPORTED STDERR.";
        }
        mode_ = DisplayMode::Results;
        const auto indexMilliseconds = std::chrono::duration_cast<
            std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - resultCatalogStarted_).count();
        status_ = hasPreview && !previewMatches
            ? "Adapter verification failed: Fluid Solver VTK mask differs "
              "from the GUI preview."
            : "Indexed " + std::to_string(frames_.size()) +
                  " VTK frame(s) in " +
                  std::to_string(indexMilliseconds) + " ms; loaded solver step " +
                  std::to_string(activeFrame_->frameNumber) +
                  ". Series steps " +
                  std::to_string(frames_.front().frameNumber) + " to " +
                  std::to_string(frames_.back().frameNumber) + "." +
                  (hasPreview
                       ? " Fluid Solver mask matches the GUI preview."
                       : "");
        if (stopped) {
            status_ = "Stopped Fluid Solver. " + status_;
        } else if (continued) {
            status_ = "Continuation completed. " + status_;
        }
        if (rejectedCount != 0) {
            status_ += " Skipped " + std::to_string(rejectedCount) +
                " incomplete or incompatible frame file(s).";
        }
        status_ +=
            (warningCount == 0
                 ? ""
                 : " Parser warnings: " +
                       std::to_string(warningCount) + ".");
        if (!solverDiagnostic.empty()) {
            status_ += " Solver stderr: " + solverDiagnostic;
        }
    }

    std::size_t adaptiveResidentFrameLimit() const {
        if (!activeFrame_) {
            return 1;
        }
        const std::size_t frameBytes = activeFrame_->decodedByteSize();
        if (frameBytes == 0) {
            return MAX_ADAPTIVE_RESIDENT_FRAMES;
        }
        return std::max<std::size_t>(
            1,
            std::min(
                MAX_ADAPTIVE_RESIDENT_FRAMES,
                decodedFrameCache_.byteBudget() / frameBytes));
    }

    void planAdaptivePrefetch(std::size_t centerIndex) {
        prefetchQueue_.clear();
        if (frames_.empty() || !activeFrame_) {
            adaptiveWindowIndices_.clear();
            adaptiveWindowDivisor_ = 1;
            return;
        }

        AdaptiveFrameWindow window = planAdaptiveFrameWindow(
            frames_.size(),
            centerIndex,
            adaptiveResidentFrameLimit());
        adaptiveWindowDivisor_ = window.divisor;
        adaptiveWindowIndices_ = std::move(window.nearestIndices);
        decodedFrameCache_.retainOnly(adaptiveWindowIndices_);
        for (const std::size_t index : adaptiveWindowIndices_) {
            if (index == centerIndex || frameIsInFlight(index) ||
                decodedFrameCache_.contains(index)) {
                continue;
            }
            prefetchQueue_.push_back(index);
        }
    }

    std::string decodedCacheStatus() const {
        return std::to_string(
            decodedFrameCache_.usedBytes() / (1024u * 1024u)) +
            " MiB in " + std::to_string(decodedFrameCache_.entryCount()) +
            "/" + std::to_string(adaptiveWindowIndices_.size()) +
            " nearby frame(s), target 1/" +
            std::to_string(adaptiveWindowDivisor_) + " of series";
    }

    void commitSelectedFrame(
        std::size_t index,
        std::shared_ptr<const VtkFrame> frame,
        const std::string& source) {
        VtkFrameParser::validateCompatibility(*activeFrame_, *frame);
        activeFrame_ = std::move(frame);
        applyRestartControlDefaults(*activeFrame_);
        refreshDisplayFrame();
        selectedFrame_ = index;
        if (desiredFrame_ == index) {
            desiredFrame_.reset();
        }
        frames_[index].warningCount = activeFrame_->warnings.size();
        includeDataRange(pressureRange_, activeFrame_->pressureRange);
        includeDataRange(
            velocityRange_,
            activeFrame_->velocityMagnitudeRange);
        includeDataRange(
            pressureTrimmedRange_, activeFrame_->pressureTrimmedRange);
        includeDataRange(
            velocityTrimmedRange_,
            activeFrame_->velocityMagnitudeTrimmedRange);
        resultTextureCacheValid_ = false;
        planAdaptivePrefetch(index);
        status_ = source + " solver step " +
            std::to_string(activeFrame_->frameNumber) + "; " +
            decodedCacheStatus() + ".";
    }

    // How many frames may decode at once. One per spare core, because a decode
    // is now a read plus a byte swap and scales with them, with a ceiling so a
    // 32-core machine does not put a gigabyte of half-built frames in flight.
    static std::size_t frameLoaderCount() {
        const unsigned int cores = std::thread::hardware_concurrency();
        const std::size_t spare = cores > 1u
            ? static_cast<std::size_t>(cores - 1u)
            : std::size_t(1);
        return std::min<std::size_t>(std::max<std::size_t>(spare, 2u), 8u);
    }

    bool frameIsInFlight(std::size_t index) const {
        for (const InFlightFrame& load : inFlightFrames_) {
            if (load.index == index) {
                return true;
            }
        }
        return false;
    }

    void startFrameLoad(std::size_t index, bool prefetch) {
        if (frameIsInFlight(index)) {
            return;
        }
        const std::filesystem::path path = frames_[index].sourcePath;
        InFlightFrame load;
        load.index = index;
        load.started = std::chrono::steady_clock::now();
        load.future = std::async(
            std::launch::async,
            [path]() {
                return std::make_shared<VtkFrame>(
                    VtkFrameParser::parse(path));
            });
        inFlightFrames_.push_back(std::move(load));
        if (!prefetch) {
            status_ = "Loading solver step " +
                std::to_string(frames_[index].frameNumber) + "...";
        }
    }

    // Fills the loader slots rather than starting one frame. The queue is
    // ordered by distance from the frame on screen, so the nearest steps are
    // always the ones being worked on.
    void startNextPrefetch() {
        const std::size_t loaders = frameLoaderCount();
        while (inFlightFrames_.size() < loaders && !prefetchQueue_.empty()) {
            const std::size_t index = prefetchQueue_.front();
            prefetchQueue_.pop_front();
            if (index == selectedFrame_ || decodedFrameCache_.contains(index) ||
                frameIsInFlight(index)) {
                continue;
            }
            startFrameLoad(index, true);
        }
    }

    void requestSelectedFrame(std::size_t index) {
        if (index >= frames_.size() || resultCatalogFuture_.valid()) {
            return;
        }
        desiredFrame_ = index;
        if (index == selectedFrame_) {
            desiredFrame_.reset();
            status_ = "Solver step " +
                std::to_string(frames_[index].frameNumber) +
                " is already displayed.";
            return;
        }
        if (const auto cached = decodedFrameCache_.find(index)) {
            commitSelectedFrame(index, cached, "Cache hit for");
            startNextPrefetch();
            return;
        }
        // The frame the user is waiting for goes in even when every loader is
        // busy: a prefetch that finishes later is worth less than the step on
        // screen, and holding this back was what made fast flipping stall.
        startFrameLoad(index, false);
    }

    void pollSelectedFrame() {
        bool harvested = false;
        for (std::size_t slot = 0; slot < inFlightFrames_.size();) {
            InFlightFrame& load = inFlightFrames_[slot];
            if (!load.future.valid() ||
                load.future.wait_for(std::chrono::seconds(0)) !=
                    std::future_status::ready) {
                ++slot;
                continue;
            }
            const std::size_t index = load.index;
            const auto started = load.started;
            std::future<std::shared_ptr<VtkFrame>> future =
                std::move(load.future);
            inFlightFrames_.erase(inFlightFrames_.begin() +
                static_cast<std::ptrdiff_t>(slot));
            harvested = true;
            try {
                std::shared_ptr<const VtkFrame> frame = future.get();
                VtkFrameParser::validateCompatibility(*activeFrame_, *frame);
                frames_[index].warningCount = frame->warnings.size();
                decodedFrameCache_.insert(index, frame);
                if (desiredFrame_ == index) {
                    const auto milliseconds = std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count();
                    commitSelectedFrame(
                        index,
                        std::move(frame),
                        "Loaded in " + std::to_string(milliseconds) + " ms:");
                }
            } catch (const std::exception& exception) {
                if (desiredFrame_ == index) {
                    desiredFrame_.reset();
                    status_ = std::string("VTK frame load failed: ") +
                        exception.what();
                }
            }
        }

        if (!harvested) {
            return;
        }
        // The eviction pass runs once per poll rather than once per frame, so a
        // batch that lands together is not trimmed against a window that the
        // first of them has already moved.
        decodedFrameCache_.retainOnly(adaptiveWindowIndices_);
        if (desiredFrame_ && *desiredFrame_ != selectedFrame_) {
            const std::size_t latest = *desiredFrame_;
            if (const auto cached = decodedFrameCache_.find(latest)) {
                commitSelectedFrame(latest, cached, "Cache hit for");
            } else {
                startFrameLoad(latest, false);
            }
        } else if (desiredFrame_ == selectedFrame_) {
            desiredFrame_.reset();
        }
        startNextPrefetch();
    }

    sf::FloatRect horizontalSliceTrack() const {
        return {
            {
                setupViewport_.position.x + 56.0f,
                setupViewport_.position.y +
                    setupViewport_.size.y - 25.0f
            },
            {setupViewport_.size.x - 116.0f, 8.0f}
        };
    }

    sf::FloatRect verticalSliceTrack() const {
        return {
            {
                setupViewport_.position.x +
                    setupViewport_.size.x - 25.0f,
                setupViewport_.position.y + 56.0f
            },
            {8.0f, setupViewport_.size.y - 116.0f}
        };
    }

    sf::FloatRect invertBounds() const {
        return {
            {
                setupViewport_.position.x +
                    setupViewport_.size.x - 47.0f,
                setupViewport_.position.y +
                    setupViewport_.size.y - 47.0f
            },
            {24.0f, 24.0f}
        };
    }

    void setHorizontalSlice(float mouseX) {
        const sf::FloatRect track = horizontalSliceTrack();
        const double normalized = std::clamp(
            static_cast<double>(
                (mouseX - track.position.x) / track.size.x),
            0.0,
            1.0);
        sliders_[SliceZ].value =
            std::round(-180.0 + 360.0 * normalized);
    }

    void setVerticalSlice(float mouseY) {
        const sf::FloatRect track = verticalSliceTrack();
        const double normalized = std::clamp(
            static_cast<double>(
                (mouseY - track.position.y) / track.size.y),
            0.0,
            1.0);
        sliders_[SliceX].value =
            std::round(180.0 - 360.0 * normalized);
    }

    sf::FloatRect zoomHitBounds() const {
        return {
            {zoomTrack_.position.x - 8.0f, zoomTrack_.position.y - 12.0f},
            {zoomTrack_.size.x + 16.0f, 29.0f}
        };
    }

    sf::FloatRect frameHitBounds() const {
        return {
            {frameTrack_.position.x - 8.0f, frameTrack_.position.y - 12.0f},
            {frameTrack_.size.x + 16.0f, 29.0f}
        };
    }

    ResultImageTransform resultImageTransform(const VtkFrame& frame) const {
        const double physicalWidth = frame.spanX();
        const double physicalHeight = frame.spanY();
        if (physicalWidth <= 0.0 || physicalHeight <= 0.0) {
            return {};
        }
        const double fit = std::min(
            static_cast<double>(resultViewport_.size.x) / physicalWidth,
            static_cast<double>(resultViewport_.size.y) / physicalHeight);
        const double pixelWidth =
            physicalWidth / std::max<std::size_t>(1, frame.nx) * fit *
            resultZoom_;
        const double pixelHeight =
            physicalHeight / std::max<std::size_t>(1, frame.ny) * fit *
            resultZoom_;
        const double imageWidth = static_cast<double>(frame.nx) * pixelWidth;
        const double imageHeight = static_cast<double>(frame.ny) * pixelHeight;
        return {
            static_cast<double>(resultViewport_.position.x) +
                (static_cast<double>(resultViewport_.size.x) - imageWidth) /
                    2.0 +
                static_cast<double>(resultPan_.x),
            static_cast<double>(resultViewport_.position.y) +
                (static_cast<double>(resultViewport_.size.y) - imageHeight) /
                    2.0 +
                static_cast<double>(resultPan_.y),
            pixelWidth,
            pixelHeight
        };
    }

    void setZoomFromSlider(float mouseX) {
        const double normalized = std::clamp(
            static_cast<double>(
                (mouseX - zoomTrack_.position.x) / zoomTrack_.size.x),
            0.0,
            1.0);
        resultZoom_ =
            static_cast<float>(0.5 * std::pow(16.0, normalized));
        resultPan_ = {};
    }

    void setFrameFromSlider(float mouseX) {
        if (frames_.empty() || resultCatalogFuture_.valid()) {
            return;
        }
        const double normalized = std::clamp(
            static_cast<double>(
                (mouseX - frameTrack_.position.x) / frameTrack_.size.x),
            0.0,
            1.0);
        const std::size_t requestedFrame = static_cast<std::size_t>(
            std::llround(
                normalized *
                static_cast<double>(frames_.size() - 1)));
        if (desiredFrame_ != requestedFrame ||
            (!desiredFrame_ && selectedFrame_ != requestedFrame)) {
            requestSelectedFrame(requestedFrame);
        }
    }

    void zoomResultsAt(sf::Vector2f cursor, float factor) {
        const VtkFrame& frame = displayFrame();
        const float physicalWidth =
            static_cast<float>(
                static_cast<double>(frame.nx) * frame.spacingX);
        const float physicalHeight =
            static_cast<float>(
                static_cast<double>(frame.ny) * frame.spacingY);
        const float fit = std::min(
            resultViewport_.size.x / physicalWidth,
            resultViewport_.size.y / physicalHeight);
        const sf::Vector2f centre{
            resultViewport_.position.x + resultViewport_.size.x / 2.0f,
            resultViewport_.position.y + resultViewport_.size.y / 2.0f
        };
        const sf::Vector2f oldSize{
            physicalWidth * fit * resultZoom_,
            physicalHeight * fit * resultZoom_
        };
        const sf::Vector2f oldOrigin =
            centre - oldSize / 2.0f + resultPan_;
        const sf::Vector2f dataCoordinate{
            (cursor.x - oldOrigin.x) / (fit * resultZoom_),
            (cursor.y - oldOrigin.y) / (fit * resultZoom_)
        };

        const float newZoom =
            clampFloat(resultZoom_ * factor, 0.5f, 8.0f);
        const sf::Vector2f newSize{
            physicalWidth * fit * newZoom,
            physicalHeight * fit * newZoom
        };
        resultPan_ =
            cursor - centre + newSize / 2.0f -
            sf::Vector2f{
                dataCoordinate.x * fit * newZoom,
                dataCoordinate.y * fit * newZoom
            };
        resultZoom_ = newZoom;
    }

    ProjectedPoint projectPoint(
        const Vec3& point,
        const sf::FloatRect& viewport) const {
        const GeometryBounds& bounds = geometry_.bounds();
        Vec3 relative = subtract(point, bounds.centre);
        const double inverseScale =
            bounds.characteristicLength > 0.0
                ? 1.0 / bounds.characteristicLength
                : 1.0;
        relative = multiply(relative, inverseScale);

        const double yaw = -35.0 * PI / 180.0;
        const double pitch = 25.0 * PI / 180.0;
        const double yawX =
            std::cos(yaw) * relative.x -
            std::sin(yaw) * relative.z;
        const double yawZ =
            std::sin(yaw) * relative.x +
            std::cos(yaw) * relative.z;
        const double pitchY =
            std::cos(pitch) * relative.y -
            std::sin(pitch) * yawZ;
        const double depth =
            std::sin(pitch) * relative.y +
            std::cos(pitch) * yawZ;

        const float scale =
            0.66f * std::min(viewport.size.x, viewport.size.y) *
            setupZoom_;
        return {
            {
                viewport.position.x + viewport.size.x / 2.0f +
                    static_cast<float>(yawX) * scale,
                viewport.position.y + viewport.size.y / 2.0f -
                    static_cast<float>(pitchY) * scale
            },
            depth
        };
    }

    sf::FloatRect parameterScrollbarRail() const {
        const sf::FloatRect viewport = parameterViewport();
        return {
            {panelX_ + LEFT_PANEL_WIDTH - 12.0f, viewport.position.y},
            {4.0f, viewport.size.y}
        };
    }

    sf::FloatRect parameterScrollbarThumb() const {
        const sf::FloatRect rail = parameterScrollbarRail();
        if (maxParameterScroll_ <= 0.0f) {
            return {rail.position, {rail.size.x, rail.size.y}};
        }

        const float contentHeight =
            static_cast<float>(sliders_.size()) * PARAMETER_ROW_HEIGHT +
            static_cast<float>(PARAMETER_GROUPS.size()) *
                PARAMETER_GROUP_HEIGHT;
        const float thumbHeight = std::max(
            32.0f,
            rail.size.y *
                std::min(1.0f, rail.size.y / contentHeight));
        const float travel = std::max(0.0f, rail.size.y - thumbHeight);
        const float fraction =
            parameterScrollOffset_ / maxParameterScroll_;
        return {
            {rail.position.x, rail.position.y + travel * fraction},
            {rail.size.x, thumbHeight}
        };
    }

    void setParameterScrollFromThumb(float thumbTop) {
        const sf::FloatRect rail = parameterScrollbarRail();
        const sf::FloatRect thumb = parameterScrollbarThumb();
        const float travel = std::max(0.0f, rail.size.y - thumb.size.y);
        if (travel <= 0.0f || maxParameterScroll_ <= 0.0f) {
            parameterScrollOffset_ = 0.0f;
            updateLayout(layoutSize_);
            return;
        }

        const float clampedTop = clampFloat(
            thumbTop,
            rail.position.y,
            rail.position.y + travel);
        const float fraction =
            (clampedTop - rail.position.y) / travel;
        parameterScrollOffset_ = fraction * maxParameterScroll_;
        updateLayout(layoutSize_);
    }

    void drawParameterScrollbar() {
        if (maxParameterScroll_ <= 0.0f) {
            return;
        }

        const sf::FloatRect railBounds = parameterScrollbarRail();
        sf::RectangleShape rail(railBounds.size);
        rail.setPosition(railBounds.position);
        rail.setFillColor(CONTROL_RAIL);
        window_->draw(rail);

        const sf::FloatRect thumbBounds = parameterScrollbarThumb();
        sf::RectangleShape thumb(thumbBounds.size);
        thumb.setPosition(thumbBounds.position);
        thumb.setFillColor(ACCENT);
        window_->draw(thumb);
    }

    static std::vector<std::string> wrapText(const std::string& text,
                                             std::size_t columns) {
        std::vector<std::string> lines;
        std::string line;
        std::size_t at = 0;
        while (at < text.size()) {
            std::size_t space = text.find(' ', at);
            if (space == std::string::npos)
                space = text.size();
            const std::string word = text.substr(at, space - at);
            if (!line.empty() && line.size() + 1 + word.size() > columns) {
                lines.push_back(line);
                line.clear();
            }
            if (!line.empty())
                line += ' ';
            line += word;
            at = space + 1;
        }
        if (!line.empty())
            lines.push_back(line);
        return lines;
    }

    void drawParameterTooltip() {
        if (editingSlider_.has_value() || activeSlider_.has_value())
            return;
        std::optional<std::size_t> hovered;
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            if (!parameterRowOnScreen(index))
                continue;
            const sf::FloatRect row{
                {panelX_ + 12.0f, sliders_[index].track.position.y - 29.0f},
                {292.0f, 42.0f}};
            if (row.contains(lastMouse_)) {
                hovered = index;
                break;
            }
        }
        if (!hovered.has_value())
            return;
        const std::string hint = parameterHint(*hovered);
        if (hint.empty())
            return;

        const std::vector<std::string> lines = wrapText(hint, 52);
        float widest = 0.0f;
        for (const std::string& line : lines)
            widest = std::max(widest,
                              makeText(font_, line, 11, {0.0f, 0.0f})
                                  .getLocalBounds()
                                  .size.x);

        const float padding = 8.0f;
        const float lineHeight = 14.0f;
        const sf::Vector2f box{
            widest + padding * 2.0f,
            lines.size() * lineHeight + padding * 2.0f - 2.0f};

        sf::Vector2f at{lastMouse_.x + 18.0f, lastMouse_.y + 16.0f};
        const float width = static_cast<float>(layoutSize_.x);
        const float height = static_cast<float>(layoutSize_.y);
        if (at.x + box.x > width - 8.0f)
            at.x = std::max(8.0f, width - 8.0f - box.x);
        if (at.y + box.y > height - 8.0f)
            at.y = std::max(8.0f, lastMouse_.y - 12.0f - box.y);

        sf::RectangleShape shadow(box);
        shadow.setPosition({at.x + 2.0f, at.y + 2.0f});
        shadow.setFillColor(sf::Color{0, 0, 0, 110});
        window_->draw(shadow);

        sf::RectangleShape frame(box);
        frame.setPosition(at);
        frame.setFillColor(OVERLAY_BACKGROUND);
        frame.setOutlineColor(ACCENT_DARK);
        frame.setOutlineThickness(1.0f);
        window_->draw(frame);

        for (std::size_t line = 0; line < lines.size(); ++line)
            window_->draw(makeText(
                font_, lines[line], 11,
                {at.x + padding, at.y + padding + line * lineHeight - 2.0f},
                TEXT));
    }

    void drawParameterStrip() {
        if (searchActive_ || !searchQuery_.empty()) {
            sf::RectangleShape field({294.0f, PARAMETER_STRIP_HEIGHT});
            field.setPosition({panelX_ + 18.0f, PARAMETER_STRIP_TOP});
            field.setFillColor(CONTROL_BACKGROUND);
            field.setOutlineColor(searchActive_ ? ACCENT : BORDER);
            field.setOutlineThickness(1.0f);
            window_->draw(field);
            std::string shown = "Find: " + searchQuery_;
            if (searchActive_)
                shown += "_";
            if (searchQuery_.empty() && !searchActive_)
                shown = "Find: (Ctrl+F)";
            window_->draw(makeText(font_, shown, 13,
                                   {panelX_ + 26.0f,
                                    PARAMETER_STRIP_TOP + 6.0f},
                                   searchQuery_.empty() ? MUTED : TEXT));
            return;
        }
        for (const Button& tab : tabButtons_)
            tab.draw(*window_, font_, lastMouse_);
    }

    void drawParameterRowBackdrops() {
        const sf::FloatRect viewport = parameterViewport();
        std::size_t stripe = 0;
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            if (!parameterRowOnScreen(index))
                continue;
            const float top = sliders_[index].track.position.y - 33.0f;
            if (top + PARAMETER_ROW_HEIGHT < viewport.position.y ||
                top > viewport.position.y + viewport.size.y)
                continue;
            const bool focused = focusedSlider_ == index;
            if ((stripe % 2 == 1) || focused) {
                sf::RectangleShape band({296.0f, PARAMETER_ROW_HEIGHT - 4.0f});
                band.setPosition({panelX_ + 16.0f, top});
                band.setFillColor(focused ? sf::Color{58, 58, 58}
                                          : sf::Color{255, 255, 255, 8});
                window_->draw(band);
            }
            if (focused) {
                sf::RectangleShape edge({2.0f, PARAMETER_ROW_HEIGHT - 4.0f});
                edge.setPosition({panelX_ + 16.0f, top});
                edge.setFillColor(ACCENT);
                window_->draw(edge);
            }
            ++stripe;
        }
    }

    void drawParameterGroupHeaders() {
        const sf::FloatRect viewport = parameterViewport();
        for (std::size_t index = 0; index < PARAMETER_GROUPS.size(); ++index) {
            const ParameterGroupInfo& group = PARAMETER_GROUPS[index];
            if (group.firstIndex >= sliders_.size() || !groupShown_[index]) {
                continue;
            }
            const float y = groupHeaderY_[index];
            if (y < viewport.position.y - 18.0f ||
                y > viewport.position.y + viewport.size.y) {
                continue;
            }
            window_->draw(makeText(
                font_, group.label, 11, {panelX_ + 20.0f, y}, ACCENT));
            sf::RectangleShape divider({278.0f, 1.0f});
            divider.setPosition({panelX_ + 20.0f, y + 17.0f});
            divider.setFillColor(BORDER);
            window_->draw(divider);
        }
    }

    std::string compactPath(
        const std::filesystem::path& path,
        std::size_t maximum = 62) const {
        std::string text = path.string();
        if (text.size() <= maximum) {
            return text;
        }
        return "..." + text.substr(text.size() - (maximum - 3));
    }

    std::string setupSummaryText() const {
        const double lx = sliders_[DomainX].value;
        const double ly = sliders_[DomainY].value;
        const long long nx = std::max<long long>(
            1, std::llround(sliders_[CellsX].value));
        const long long ny = std::max<long long>(
            1, std::llround(sliders_[CellsY].value));
        const double dx = lx / static_cast<double>(nx);
        const double dy = ly / static_cast<double>(ny);
        const long double cells =
            static_cast<long double>(nx) * static_cast<long double>(ny);
        const double nu = sliders_[Viscosity].value;
        const double u0 = sliders_[WindSpeed].value;
        const double reynolds = nu > 0.0 ? u0 * lx / nu : 0.0;

        double dtEstimate = 0.0;
        if (dx > 0.0 && dy > 0.0 && nu > 0.0) {
            const double advectiveRate = u0 > 0.0 ? u0 / dx : 0.0;
            const double advective = advectiveRate > 0.0
                ? sliders_[Cfl].value / advectiveRate
                : std::numeric_limits<double>::infinity();
            const double diffusive = 1.0 /
                (2.0 * nu * (1.0 / (dx * dx) + 1.0 / (dy * dy)));
            dtEstimate = sliders_[DtSafety].value *
                std::min(advective, diffusive);
        }
        long long estimatedSteps = 0;
        long long estimatedVtks = 0;
        if (std::isfinite(dtEstimate) && dtEstimate > 0.0) {
            estimatedSteps = static_cast<long long>(std::ceil(
                sliders_[TotalTime].value / dtEstimate));
            const long long interval = std::max<long long>(
                1, std::llround(sliders_[SaveInterval].value));
            estimatedVtks = 1 + estimatedSteps / interval;
        }

        int coarseX = static_cast<int>(std::min<long long>(nx, 1'000'000));
        int coarseY = static_cast<int>(std::min<long long>(ny, 1'000'000));
        const int minimumCoarse = std::max(
            1, static_cast<int>(std::llround(sliders_[MgMinCoarseSize].value)));
        int levels = 1;
        for (; levels < 32; ++levels) {
            bool changed = false;
            if (coarseX % 2 == 0 && coarseX / 2 >= minimumCoarse) {
                coarseX /= 2;
                changed = true;
            }
            if (coarseY % 2 == 0 && coarseY / 2 >= minimumCoarse) {
                coarseY /= 2;
                changed = true;
            }
            if (!changed) {
                break;
            }
        }

        std::ostringstream text;
        text << "Fluid Solver " << solverInfo_.version << " | "
             << solverInfo_.build << '\n'
             << "Grid " << nx << " x " << ny << " = "
             << std::fixed << std::setprecision(0) << cells << " cells"
             << " | dx " << formatValue(dx, false, "m")
             << " | dy " << formatValue(dy, false, "m") << '\n'
             << "Re(Lx) " << formatValue(reynolds, false, "")
             << " | approx MG levels " << levels;
        if (estimatedSteps > 0) {
            text << " | rough steps " << estimatedSteps
                 << " | VTK ~" << estimatedVtks;
        }
        text << '\n'
             << "Output: " << compactPath(outputRoot_) << '\n'
             << "Solver: " << compactPath(fluidSolverExecutable_);
        if (cells > 10'000'000.0L) {
            text << "\nWARNING: UI preview mask limit is 10,000,000 cells.";
        } else if (cells > 2'000'000.0L) {
            text << "\nWARNING: large grid; preview, RAM and output cost increase sharply.";
        }
        return text.str();
    }

    void drawSetupInfoOverlay() {
        const float width = std::min(560.0f, setupViewport_.size.x - 36.0f);
        if (width < 300.0f) {
            return;
        }
        const sf::Vector2f position{
            setupViewport_.position.x + setupViewport_.size.x - width - 18.0f,
            setupViewport_.position.y + 18.0f
        };
        sf::RectangleShape background({width, 128.0f});
        background.setPosition(position);
        background.setFillColor(OVERLAY_BACKGROUND);
        background.setOutlineColor(BORDER);
        background.setOutlineThickness(1.0f);
        window_->draw(background);
        window_->draw(makeText(
            font_, setupSummaryText(), 11,
            position + sf::Vector2f{10.0f, 8.0f}, TEXT));

        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            if (!parameterRowOnScreen(index)) {
                continue;
            }
            const sf::FloatRect row{
                {12.0f, sliders_[index].track.position.y - 29.0f},
                {292.0f, 42.0f}
            };
            if (!row.contains(lastMouse_)) {
                continue;
            }
            const std::string help = parameterHelp(index);
            if (help.empty()) {
                break;
            }
            const std::size_t columns = static_cast<std::size_t>(
                std::max(24.0f, (width - 20.0f) / 5.4f));
            const std::vector<std::string> lines = wrapText(help, columns);
            const float lineHeight = 14.0f;
            sf::RectangleShape helpBox(
                {width, lines.size() * lineHeight + 18.0f});
            helpBox.setPosition({position.x, position.y + 136.0f});
            helpBox.setFillColor(OVERLAY_BACKGROUND);
            helpBox.setOutlineColor(ACCENT_DARK);
            helpBox.setOutlineThickness(1.0f);
            window_->draw(helpBox);
            for (std::size_t line = 0; line < lines.size(); ++line)
                window_->draw(makeText(
                    font_, lines[line], 11,
                    {position.x + 10.0f,
                     position.y + 144.0f + line * lineHeight},
                    MUTED));
            break;
        }
    }

    int selectedBody() const {
        return static_cast<int>(std::lround(sliders_[BodySelect].value));
    }

    void syncBodyRows(std::size_t index) {
        if (index == BodySelect) {
            loadBodyRows();
            return;
        }
        if (index >= BodyBehaviour && index <= BodyPinRotY)
            storeBodyRows();
        else if (index == BodyMotionLine || index == WallMotionLine)
            loadBodyRows();
        else if (index == BodyMoveBy || index == BodyTurnBy)
            applyBodyNudge(index);
        else if (index >= BodyPlaceX && index <= BodyTurnPlane)
            storePlacementRows();
        else if (index == Profiles)
            loadPlacementRows();
    }

    // Where the model sits comes out of the profiles line rather than out of a
    // parameter of its own, so the rows are read from it and written back to
    // it, the same way the motion rows sit on top of bodyMotion.
    void loadPlacementRows() {
        const BodyPlacement place =
            readPlacement(sliders_[Profiles].text, selectedBody());
        sliders_[BodyPlaceX].value = place.x;
        sliders_[BodyPlaceY].value = place.y;
        sliders_[BodyPlaceZ].value = place.z;
        sliders_[BodyPlaceSize].value = place.size;
        sliders_[BodyTiltX].value = place.angleX;
        sliders_[BodyTiltY].value = place.angleY;
        sliders_[BodyTiltZ].value = place.angleZ;
        sliders_[BodyTurnPlane].value = place.rot;
    }

    void storePlacementRows() {
        const int object = selectedBody();
        BodyPlacement place = readPlacement(sliders_[Profiles].text, object);
        if (!place.present) {
            // Nothing in profiles for this body yet. There is one model it can
            // be - the imported one - and only when this is the first body;
            // any other number has nothing to name.
            if (object != 1 || geometry_.sourcePath().empty())
                return;
            place.file = geometry_.sourcePath().string();
        }
        place.x = sliders_[BodyPlaceX].value;
        place.y = sliders_[BodyPlaceY].value;
        place.z = sliders_[BodyPlaceZ].value;
        place.placed = true;
        place.size = sliders_[BodyPlaceSize].value;
        place.sized = place.size > 0.0;
        place.angleX = sliders_[BodyTiltX].value;
        place.angleY = sliders_[BodyTiltY].value;
        place.angleZ = sliders_[BodyTiltZ].value;
        place.rot = sliders_[BodyTurnPlane].value;
        writePlacement(sliders_[Profiles].text, object, place);
    }

    static const char* axisName(int axis) {
        return axis == 0 ? "x"
             : (axis == 1 ? "y" : (axis == 2 ? "z" : "the cut plane"));
    }

    // Typed rather than dragged: the amount is added to where the model
    // already is, so the same number typed twice moves it twice as far. Both
    // the panel rows and the viewport's own G and R end up here.
    void nudgeBody(bool turn, int axis, double amount) {
        if (amount == 0.0)
            return;
        const std::size_t row = turn
            ? (axis == 0 ? BodyTiltX
              : (axis == 1 ? BodyTiltY
              : (axis == 2 ? BodyTiltZ : BodyTurnPlane)))
            : (axis == 0 ? BodyPlaceX : (axis == 1 ? BodyPlaceY : BodyPlaceZ));

        double placed = sliders_[row].value + amount;
        if (turn) {
            while (placed > 180.0)
                placed -= 360.0;
            while (placed < -180.0)
                placed += 360.0;
        }
        sliders_[row].value = placed;
        sliders_[row].text.clear();
        storePlacementRows();

        status_ = std::string(turn ? "Turned body " : "Moved body ") +
            std::to_string(selectedBody()) + " by " +
            editableNumber(amount, false) + (turn ? " deg about " : " m along ") +
            axisName(axis) + ", now at " +
            editableNumber(placed, false) + (turn ? " deg." : " m.");
    }

    void applyBodyNudge(std::size_t index) {
        const double amount = sliders_[index].value;
        if (amount == 0.0)
            return;
        sliders_[index].value = 0.0;
        sliders_[index].text.clear();
        const bool turn = index == BodyTurnBy;
        const int axis = static_cast<int>(std::lround(
            sliders_[turn ? BodyTurnAxis : BodyMoveAxis].value));
        nudgeBody(turn, axis, amount);
    }

    // The viewport's own transform, the way a 3D package does it: G or R, then
    // an axis letter, then the number, then Enter. It is modal on purpose -
    // while it is running every key belongs to it, which is why the prompt
    // sits in the status line until Enter or Escape ends it.
    bool bodyIsPlaceable() const {
        const int object = selectedBody();
        if (readPlacement(sliders_[Profiles].text, object).present)
            return true;
        return object == 1 && !geometry_.sourcePath().empty();
    }

    void showTransformPrompt() {
        status_ = std::string(transformMode_ == 2 ? "Turn " : "Move ") +
            "body " + std::to_string(selectedBody()) + " along " +
            axisName(transformAxis_) + ": " + transformText_ + "_   " +
            (transformMode_ == 2 ? "deg" : "m") +
            "   (x/y/z axis, Enter applies, Esc cancels)";
    }

    void beginTransform(int mode) {
        if (!bodyIsPlaceable()) {
            status_ = "Body " + std::to_string(selectedBody()) +
                " has no model in the profiles line to move. Click a body "
                "first, or import a model.";
            return;
        }
        transformMode_ = mode;
        // Turning a single plane can only mean the rotation inside it; there
        // is no out-of-plane axis to tip into.
        transformAxis_ = (mode == 2 && activeFrame_ && activeFrame_->nz <= 1u)
            ? 3 : 0;
        transformText_.clear();
        showTransformPrompt();
    }

    bool handleTransformKey(const sf::Event::KeyPressed& key) {
        using Key = sf::Keyboard::Key;
        switch (key.code) {
        case Key::Escape:
            transformMode_ = 0;
            transformText_.clear();
            status_ = "Cancelled.";
            return true;
        case Key::Enter: {
            double amount = 0.0;
            std::string error;
            const bool ok = !transformText_.empty() &&
                parseNumericInput(transformText_, NumericInputRules{},
                                  amount, error);
            const bool turn = transformMode_ == 2;
            const int axis = transformAxis_;
            transformMode_ = 0;
            transformText_.clear();
            if (!ok) {
                status_ = "Nothing typed, so nothing moved.";
                return true;
            }
            nudgeBody(turn, axis, amount);
            return true;
        }
        case Key::Backspace:
            if (!transformText_.empty())
                transformText_.pop_back();
            showTransformPrompt();
            return true;
        case Key::X:
            transformAxis_ = 0;
            showTransformPrompt();
            return true;
        case Key::Y:
            transformAxis_ = 1;
            showTransformPrompt();
            return true;
        case Key::Z:
            transformAxis_ = 2;
            showTransformPrompt();
            return true;
        case Key::C:
            if (transformMode_ == 2)
                transformAxis_ = 3;
            showTransformPrompt();
            return true;
        case Key::Hyphen:
        case Key::Subtract:
            transformText_ += '-';
            showTransformPrompt();
            return true;
        case Key::Period:
            transformText_ += '.';
            showTransformPrompt();
            return true;
        default:
            break;
        }
        const int code = static_cast<int>(key.code);
        const int zero = static_cast<int>(Key::Num0);
        const int padZero = static_cast<int>(Key::Numpad0);
        if (code >= zero && code <= zero + 9)
            transformText_ += static_cast<char>('0' + (code - zero));
        else if (code >= padZero && code <= padZero + 9)
            transformText_ += static_cast<char>('0' + (code - padZero));
        showTransformPrompt();
        return true;
    }

    void loadBodyRows() {
        loadPlacementRows();
        const int object = selectedBody();
        const BodyRowValues values = readBodyRows(
            sliders_[WallMotionLine].text,
            sliders_[BodyMotionLine].text,
            object);

        sliders_[BodyBehaviour].value = values.behaviour;
        sliders_[BodyRotation].value = values.rotation;
        sliders_[BodyRotationX].value = values.rotationX;
        sliders_[BodyRotationY].value = values.rotationY;
        sliders_[BodySlideX].value = values.slideX;
        sliders_[BodySlideY].value = values.slideY;
        sliders_[BodySlideZ].value = values.slideZ;
        sliders_[BodyVelocityX].value = values.velocityX;
        sliders_[BodyVelocityY].value = values.velocityY;
        sliders_[BodyVelocityZ].value = values.velocityZ;
        sliders_[BodySpin].value = values.spin;
        sliders_[BodySpinX].value = values.spinX;
        sliders_[BodySpinY].value = values.spinY;
        sliders_[BodyMass].value = values.mass;
        sliders_[BodyDensity].value = values.density;
        sliders_[BodyInertiaX].value = values.inertiaX;
        sliders_[BodyInertiaY].value = values.inertiaY;
        sliders_[BodyPins].value =
            values.pins & (PinSlideX | PinSlideY | PinSpinZ);
        sliders_[BodyPinZ].value = (values.pins & PinSlideZ) ? 1.0 : 0.0;
        sliders_[BodyPinRotX].value = (values.pins & PinSpinX) ? 1.0 : 0.0;
        sliders_[BodyPinRotY].value = (values.pins & PinSpinY) ? 1.0 : 0.0;
    }

    void storeBodyRows() {
        BodyRowValues values;
        values.behaviour =
            static_cast<int>(std::lround(sliders_[BodyBehaviour].value));
        values.rotation = sliders_[BodyRotation].value;
        values.rotationX = sliders_[BodyRotationX].value;
        values.rotationY = sliders_[BodyRotationY].value;
        values.slideX = sliders_[BodySlideX].value;
        values.slideY = sliders_[BodySlideY].value;
        values.slideZ = sliders_[BodySlideZ].value;
        values.velocityX = sliders_[BodyVelocityX].value;
        values.velocityY = sliders_[BodyVelocityY].value;
        values.velocityZ = sliders_[BodyVelocityZ].value;
        values.spin = sliders_[BodySpin].value;
        values.spinX = sliders_[BodySpinX].value;
        values.spinY = sliders_[BodySpinY].value;
        values.mass = sliders_[BodyMass].value;
        values.density = sliders_[BodyDensity].value;
        values.inertiaX = sliders_[BodyInertiaX].value;
        values.inertiaY = sliders_[BodyInertiaY].value;
        values.pins =
            static_cast<int>(std::lround(sliders_[BodyPins].value)) |
            (sliders_[BodyPinZ].value >= 0.5 ? PinSlideZ : 0) |
            (sliders_[BodyPinRotX].value >= 0.5 ? PinSpinX : 0) |
            (sliders_[BodyPinRotY].value >= 0.5 ? PinSpinY : 0);

        writeBodyRows(sliders_[WallMotionLine].text,
                      sliders_[BodyMotionLine].text,
                      selectedBody(),
                      values);
    }

    bool phasesOn() const {
        return solverInfo_.supportsPhases &&
               std::lround(sliders_[Phases].value) > 1;
    }

    void ensurePaintField() {
        const int nx = static_cast<int>(std::lround(sliders_[CellsX].value));
        const int ny = static_cast<int>(std::lround(sliders_[CellsY].value));
        if (nx == paintNx_ && ny == paintNy_ &&
            paintField_.size() == static_cast<std::size_t>(nx) * ny)
            return;

        paintNx_ = nx;
        paintNy_ = ny;
        paintField_.assign(static_cast<std::size_t>(std::max(nx, 1)) *
                               std::max(ny, 1),
                           0.0f);
        paintUndo_.clear();
    }

    bool paintFieldUsed() const {
        for (float value : paintField_)
            if (value > 0.0f)
                return true;
        return false;
    }

    sf::FloatRect paintCanvasRect() const {
        sf::FloatRect area = setupViewport_;
        area.position.x += 12.0f;
        area.position.y += 52.0f;
        area.size.x -= 24.0f;
        area.size.y -= 146.0f;
        const float want =
            static_cast<float>(sliders_[DomainX].value /
                               std::max(1e-9, sliders_[DomainY].value));
        const float have = area.size.x / std::max(1.0f, area.size.y);
        if (want > have) {
            const float h = area.size.x / want;
            area.position.y += (area.size.y - h) * 0.5f;
            area.size.y = h;
        } else {
            const float w = area.size.y * want;
            area.position.x += (area.size.x - w) * 0.5f;
            area.size.x = w;
        }
        return area;
    }

    bool paintCellAt(sf::Vector2f point, int& outI, int& outJ) const {
        const sf::FloatRect canvas = paintCanvasRect();
        if (paintNx_ < 1 || paintNy_ < 1 || !canvas.contains(point))
            return false;
        const float fx = (point.x - canvas.position.x) / canvas.size.x;

        const float fy = 1.0f - (point.y - canvas.position.y) / canvas.size.y;
        outI = std::min(paintNx_ - 1,
                        std::max(0, static_cast<int>(fx * paintNx_)));
        outJ = std::min(paintNy_ - 1,
                        std::max(0, static_cast<int>(fy * paintNy_)));
        return true;
    }

    void paintAt(sf::Vector2f point, bool secondary) {
        int ci = 0, cj = 0;
        if (!paintCellAt(point, ci, cj))
            return;
        if (paintTarget_ == 2) {
            addSourceAt(ci, cj);
            return;
        }

        const bool wantSecond = (paintTarget_ == 1) != secondary;
        const float value = wantSecond ? 0.0f : 1.0f;
        const int r = std::max(0, paintBrush_);
        for (int j = cj - r; j <= cj + r; ++j) {
            if (j < 0 || j >= paintNy_)
                continue;
            for (int i = ci - r; i <= ci + r; ++i) {
                if (i < 0 || i >= paintNx_)
                    continue;
                const int dx = i - ci, dy = j - cj;
                if (dx * dx + dy * dy > r * r)
                    continue;
                paintField_[static_cast<std::size_t>(j) * paintNx_ + i] = value;
            }
        }
    }

    void addSourceAt(int ci, int cj) {
        const double dx = sliders_[DomainX].value / std::max(1, paintNx_);
        const double dy = sliders_[DomainY].value / std::max(1, paintNy_);
        const double x = (ci + 0.5) * dx;
        const double y = (cj + 0.5) * dy;
        const double radius = std::max(1, paintBrush_) * std::max(dx, dy);
        std::ostringstream line;
        line << std::setprecision(6) << "x=" << x << ",y=" << y
             << ",r=" << radius << ",rate=1,angle=90,phase=1";
        std::string& text = sliders_[SourceLine].text;
        if (!text.empty())
            text += ";";
        text += line.str();
        status_ = "Source added. Edit the Flow sources row for rate and angle.";
    }

    struct Snapshot {
        std::array<double, ParameterCount> values{};
        std::array<std::string, ParameterCount> texts{};
        std::vector<float> paint;
        int paintNx = 0;
        int paintNy = 0;
        bool invert = false;
    };

    using Pose = BodyPose;

    static const std::array<const char*, 8>& interpNames() {
        static const std::array<const char*, 8> names{
            {"linear", "smooth", "bezier", "sine", "quad", "cubic", "back",
             "elastic"}};
        return names;
    }

    static const std::array<const char*, 3>& easeNames() {
        static const std::array<const char*, 3> names{{"in", "out", "inout"}};
        return names;
    }

    std::vector<Pose> poseTrack(int object) const {
        return parseBodyTrack(
            motionEntryOf(sliders_[BodyTrackLine].text, object));
    }

    bool curvedPath() const {
        return sliders_[BodyPathKind].choice() == "curve";
    }

    std::string trackMotion(const std::vector<Pose>& track) const {
        return curvedPath() ? bodyCurveToMotion(track)
                            : bodyTrackToMotion(track);
    }

    void setPoseTrack(int object, const std::vector<Pose>& track) {
        setMotionEntry(sliders_[BodyTrackLine].text, object,
                       formatBodyTrack(track));
        setMotionEntry(sliders_[BodyMotionLine].text, object,
                       trackMotion(track));
        loadBodyRows();
    }

    Pose poseAt(int object, double when) const {
        const std::vector<Pose> track = poseTrack(object);
        return curvedPath() ? bodyPoseOnCurve(track, when)
                            : bodyPoseAt(track, when);
    }

    void dropKeyframe(int object, const Pose& pose) {
        std::vector<Pose> track = poseTrack(object);
        dropBodyPose(track, pose);
        setPoseTrack(object, track);
    }

    void removeKeyframe(int object, double when) {
        std::vector<Pose> track = poseTrack(object);
        if (!removeBodyPose(track, when)) {
            status_ = "No keyframe at that time.";
            return;
        }
        setPoseTrack(object, track);
        status_ = "Keyframe removed.";
    }

    Snapshot captureState() const {
        Snapshot shot;
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            shot.values[index] = sliders_[index].value;
            shot.texts[index] = sliders_[index].text;
        }
        shot.paint = paintField_;
        shot.paintNx = paintNx_;
        shot.paintNy = paintNy_;
        shot.invert = invertSection_;
        return shot;
    }

    void applyState(const Snapshot& shot) {
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            sliders_[index].value = shot.values[index];
            sliders_[index].text = shot.texts[index];
        }
        paintField_ = shot.paint;
        paintNx_ = shot.paintNx;
        paintNy_ = shot.paintNy;
        invertSection_ = shot.invert;
        invalidSlider_.reset();
        cancelSliderEdit(false);
        updateLayout(layoutSize_);
    }

    bool sameState(const Snapshot& a, const Snapshot& b) const {
        return a.values == b.values && a.texts == b.texts &&
               a.paint == b.paint && a.invert == b.invert;
    }

    void pushUndo() {
        Snapshot shot = captureState();
        if (!undoStack_.empty() && sameState(undoStack_.back(), shot))
            return;
        undoStack_.push_back(std::move(shot));
        if (undoStack_.size() > 64)
            undoStack_.erase(undoStack_.begin());
        redoStack_.clear();
    }

    void performUndo() {
        if (undoStack_.empty()) {
            status_ = "Nothing left to undo.";
            return;
        }
        redoStack_.push_back(captureState());
        Snapshot shot = undoStack_.back();
        undoStack_.pop_back();
        applyState(shot);
        status_ = "Undone. Ctrl+Y puts it back.";
    }

    void performRedo() {
        if (redoStack_.empty()) {
            status_ = "Nothing left to redo.";
            return;
        }
        undoStack_.push_back(captureState());
        Snapshot shot = redoStack_.back();
        redoStack_.pop_back();
        applyState(shot);
        status_ = "Redone.";
    }

    void pushPaintUndo() {
        pushUndo();
    }

    void undoPaint() {
        performUndo();
    }

    std::string rowClipboardText(std::size_t index) const {
        const Slider& row = sliders_[index];
        if (row.kind == ControlKind::Text)
            return row.text;
        if (row.kind == ControlKind::Choice)
            return row.choice();
        return formatValue(row.value, row.integer, std::string());
    }

    void copyFocusedRow(bool cut) {
        if (!focusedSlider_.has_value()) {
            sf::Clipboard::setString(sf::String(configurationText()));
            status_ = "Whole configuration copied. Ctrl+V pastes one back.";
            return;
        }
        const std::size_t index = *focusedSlider_;
        sf::Clipboard::setString(sf::String(rowClipboardText(index)));
        if (!cut) {
            status_ = sliders_[index].label + " copied.";
            return;
        }
        pushUndo();
        sliders_[index].value = sliders_[index].defaultValue;
        sliders_[index].text = sliders_[index].defaultText;
        syncBodyRows(index);
        status_ = sliders_[index].label + " cut back to its default.";
    }

    void pasteIntoFocusedRow() {
        const std::string incoming = sf::Clipboard::getString().toAnsiString();
        if (incoming.empty()) {
            status_ = "The clipboard is empty.";
            return;
        }
        if (incoming.find('\n') != std::string::npos &&
            incoming.find('=') != std::string::npos) {
            pushUndo();
            const ConfigurationDocument document =
                parseConfiguration(incoming);
            if (document.recognised) {
                status_ = "Configuration pasted from the clipboard: " +
                    applyConfigurationDocument(document, false);
            } else {
                undoStack_.pop_back();
                status_ = "That is not a configuration: nothing in it is a "
                          "solver or UI setting.";
            }
            return;
        }
        if (!focusedSlider_.has_value()) {
            status_ = "Click a parameter first, then Ctrl+V into it.";
            return;
        }
        const std::size_t index = *focusedSlider_;
        std::string error;
        pushUndo();
        if (!sliders_[index].setFromText(incoming, error)) {
            undoStack_.pop_back();
            invalidSlider_ = index;
            status_ = sliders_[index].label + ": " + error;
            return;
        }
        invalidSlider_.reset();
        syncBodyRows(index);
        status_ = sliders_[index].label + " pasted.";
    }

    static std::string lowerCopy(std::string text) {
        for (char& character : text)
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        return text;
    }

    bool rowMatchesSearch(std::size_t index) const {
        if (searchQuery_.empty())
            return true;
        const std::string needle = lowerCopy(searchQuery_);
        if (lowerCopy(sliders_[index].label).find(needle) !=
            std::string::npos)
            return true;
        return lowerCopy(std::string(parameterKey(index))).find(needle) !=
               std::string::npos;
    }

    void openSearch() {
        searchActive_ = true;
        cancelSliderEdit(false);
        status_ = "Type to filter the parameters. Enter keeps it, Escape "
                  "clears it.";
        updateLayout(layoutSize_);
    }

    void closeSearch(bool clear) {
        searchActive_ = false;
        if (clear)
            searchQuery_.clear();
        updateLayout(layoutSize_);
    }

    void jumpToFirstMatch() {
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            if (rowHidden_[index])
                continue;
            focusedSlider_ = index;
            scrollRowIntoView(index);
            return;
        }
    }

    void scrollRowIntoView(std::size_t index) {
        const sf::FloatRect viewport = parameterViewport();
        const float y = sliders_[index].track.position.y;
        if (y < viewport.position.y + 40.0f) {
            parameterScrollOffset_ = clampFloat(
                parameterScrollOffset_ - (viewport.position.y + 40.0f - y),
                0.0f, maxParameterScroll_);
        } else if (y > viewport.position.y + viewport.size.y - 20.0f) {
            parameterScrollOffset_ = clampFloat(
                parameterScrollOffset_ +
                    (y - (viewport.position.y + viewport.size.y - 20.0f)),
                0.0f, maxParameterScroll_);
        }
        updateLayout(layoutSize_);
    }

    bool handleShortcut(const sf::Event::KeyPressed& key) {
        const bool control = key.control;
        if (!control)
            return false;
        switch (key.code) {
        case sf::Keyboard::Key::Z:
            if (key.shift)
                performRedo();
            else
                performUndo();
            return true;
        case sf::Keyboard::Key::Y:
            performRedo();
            return true;
        case sf::Keyboard::Key::C:
            copyFocusedRow(false);
            return true;
        case sf::Keyboard::Key::X:
            copyFocusedRow(true);
            return true;
        case sf::Keyboard::Key::V:
            pasteIntoFocusedRow();
            return true;
        case sf::Keyboard::Key::F:
            openSearch();
            return true;
        case sf::Keyboard::Key::S:
            saveConfiguration();
            return true;
        case sf::Keyboard::Key::O:
            loadConfiguration();
            return true;
        default:
            return false;
        }
    }

    bool handleSearchKey(const sf::Event::KeyPressed& key) {
        if (!searchActive_)
            return false;
        if (key.code == sf::Keyboard::Key::Escape) {
            closeSearch(true);
            return true;
        }
        if (key.code == sf::Keyboard::Key::Enter) {
            jumpToFirstMatch();
            searchActive_ = false;
            return true;
        }
        if (key.code == sf::Keyboard::Key::Backspace) {
            if (!searchQuery_.empty())
                searchQuery_.pop_back();
            updateLayout(layoutSize_);
            return true;
        }
        return false;
    }

    bool handleSearchText(char32_t unicode) {
        if (!searchActive_)
            return false;
        if (unicode >= 32 && unicode < 127 && searchQuery_.size() < 40) {
            searchQuery_.push_back(static_cast<char>(unicode));
            updateLayout(layoutSize_);
        }
        return true;
    }

    bool writePaintField(const std::filesystem::path& path,
                         std::string& error) const {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            error = "Cannot write the painted phase field: " + path.string();
            return false;
        }
        for (int j = 0; j < paintNy_; ++j) {
            for (int i = 0; i < paintNx_; ++i) {
                out << paintField_[static_cast<std::size_t>(j) * paintNx_ + i];
                out << (i + 1 == paintNx_ ? '\n' : ' ');
            }
        }
        out.flush();
        if (!out) {
            error = "The painted phase field did not write out fully.";
            return false;
        }
        return true;
    }

    void drawPaintCanvas() {
        ensurePaintField();
        const sf::FloatRect canvas = paintCanvasRect();

        if (paintNx_ > 0 && paintNy_ > 0) {
            sf::Image image({static_cast<unsigned>(paintNx_),
                             static_cast<unsigned>(paintNy_)},
                            sf::Color::Transparent);
            for (int j = 0; j < paintNy_; ++j)
                for (int i = 0; i < paintNx_; ++i) {
                    const float value =
                        paintField_[static_cast<std::size_t>(j) * paintNx_ + i];
                    const sf::Color colour = phaseColour(value);
                    image.setPixel({static_cast<unsigned>(i),
                                    static_cast<unsigned>(paintNy_ - 1 - j)},
                                   colour);
                }
            sf::Texture texture;
            if (texture.loadFromImage(image)) {
                texture.setSmooth(false);
                sf::Sprite sprite(texture);
                sprite.setPosition(canvas.position);
                sprite.setScale(
                    {canvas.size.x / static_cast<float>(paintNx_),
                     canvas.size.y / static_cast<float>(paintNy_)});
                window_->draw(sprite);
            }
        }

        sf::RectangleShape frame(canvas.size);
        frame.setPosition(canvas.position);
        frame.setFillColor(sf::Color::Transparent);
        frame.setOutlineColor(BORDER);
        frame.setOutlineThickness(1.0f);
        window_->draw(frame);

        int ci = 0, cj = 0;
        if (paintCellAt(lastMouse_, ci, cj)) {
            const float cellW = canvas.size.x / std::max(1, paintNx_);
            const float cellH = canvas.size.y / std::max(1, paintNy_);
            const float radius =
                std::max(1, paintBrush_) * std::max(cellW, cellH);
            sf::CircleShape cursor(radius);
            cursor.setOrigin({radius, radius});
            cursor.setPosition({
                canvas.position.x + (ci + 0.5f) * cellW,
                canvas.position.y + canvas.size.y - (cj + 0.5f) * cellH});
            cursor.setFillColor(sf::Color::Transparent);
            cursor.setOutlineColor(ACCENT);
            cursor.setOutlineThickness(1.5f);
            window_->draw(cursor);
        }

        drawPhaseLegend(canvas);
    }

    static sf::Color phaseColour(float fraction) {
        const float t = std::min(1.0f, std::max(0.0f, fraction));
        const sf::Color light(38, 44, 58);
        const sf::Color heavy(64, 156, 255);
        return sf::Color(
            static_cast<std::uint8_t>(light.r + (heavy.r - light.r) * t),
            static_cast<std::uint8_t>(light.g + (heavy.g - light.g) * t),
            static_cast<std::uint8_t>(light.b + (heavy.b - light.b) * t));
    }

    void drawPhaseLegend(const sf::FloatRect& canvas) {
        const float x = canvas.position.x;
        const float y = canvas.position.y + canvas.size.y + 10.0f;
        for (int k = 0; k < 64; ++k) {
            sf::RectangleShape swatch({3.0f, 12.0f});
            swatch.setPosition({x + k * 3.0f, y});
            swatch.setFillColor(phaseColour(k / 63.0f));
            window_->draw(swatch);
        }
        window_->draw(makeText(font_, "fluid 2", 12, {x + 196.0f, y - 2.0f},
                               MUTED));
        window_->draw(makeText(font_, "fluid 1", 12, {x + 250.0f, y - 2.0f},
                               MUTED));
        std::ostringstream hint;
        hint << "brush " << paintBrush_ << " cells - wheel resizes, drag paints "
             << (paintTarget_ == 1 ? "fluid 2" : "fluid 1")
             << ", right button paints the other, " << paintNx_ << "x"
             << paintNy_ << " grid";
        window_->draw(makeText(font_, hint.str(), 12,
                               {x + 320.0f, y - 2.0f}, MUTED));
    }

    void drawPaintControls() {
        paintButton_.label = painting_ ? "Painting" : "Paint";
        paintButton_.selected = painting_;
        paintFluid1Button_.label = "Fluid 1";
        paintFluid2Button_.label = "Fluid 2";
        paintSourceButton_.label = "Source";
        paintFillButton_.label = "Fill";
        paintClearButton_.label = "Clear";
        paintUndoButton_.label = "Undo";
        paintFluid1Button_.selected = paintTarget_ == 0;
        paintFluid2Button_.selected = paintTarget_ == 1;
        paintSourceButton_.selected = paintTarget_ == 2;
        paintFluid1Button_.enabled = painting_;
        paintFluid2Button_.enabled = painting_;
        paintSourceButton_.enabled = painting_;
        paintFillButton_.enabled = painting_;
        paintClearButton_.enabled = painting_;
        paintUndoButton_.enabled = painting_ && !paintUndo_.empty();

        paintButton_.draw(*window_, font_, lastMouse_);
        if (!painting_)
            return;
        paintFluid1Button_.draw(*window_, font_, lastMouse_);
        paintFluid2Button_.draw(*window_, font_, lastMouse_);
        paintSourceButton_.draw(*window_, font_, lastMouse_);
        paintFillButton_.draw(*window_, font_, lastMouse_);
        paintClearButton_.draw(*window_, font_, lastMouse_);
        paintUndoButton_.draw(*window_, font_, lastMouse_);
    }

    bool handlePaintMousePressed(sf::Mouse::Button button,
                                 sf::Vector2f position) {
        if (!phasesOn())
            return false;
        if (button == sf::Mouse::Button::Left && paintButton_.hit(position)) {
            painting_ = !painting_;
            if (painting_)
                ensurePaintField();
            return true;
        }
        if (!painting_)
            return false;
        if (button == sf::Mouse::Button::Left) {
            if (paintFluid1Button_.hit(position)) { paintTarget_ = 0; return true; }
            if (paintFluid2Button_.hit(position)) { paintTarget_ = 1; return true; }
            if (paintSourceButton_.hit(position)) { paintTarget_ = 2; return true; }
            if (paintFillButton_.hit(position)) {
                pushPaintUndo();
                std::fill(paintField_.begin(), paintField_.end(), 1.0f);
                return true;
            }
            if (paintClearButton_.hit(position)) {
                pushPaintUndo();
                std::fill(paintField_.begin(), paintField_.end(), 0.0f);
                return true;
            }
            if (paintUndoButton_.hit(position)) {
                undoPaint();
                return true;
            }
        }
        int ci = 0, cj = 0;
        if (!paintCellAt(position, ci, cj))
            return false;
        if (button != sf::Mouse::Button::Left &&
            button != sf::Mouse::Button::Right)
            return false;
        pushPaintUndo();
        paintStroke_ = true;
        paintAt(position, button == sf::Mouse::Button::Right);
        return true;
    }

    sf::FloatRect layoutCanvasRect() const {
        sf::FloatRect area = setupViewport_;
        area.position.x += 12.0f;
        area.position.y += 52.0f;
        area.size.x -= 24.0f;
        area.size.y -= 76.0f;
        const float want =
            static_cast<float>(sliders_[DomainX].value /
                               std::max(1e-9, sliders_[DomainY].value));
        const float have = area.size.x / std::max(1.0f, area.size.y);
        if (want > have) {
            const float h = area.size.x / want;
            area.position.y += (area.size.y - h) * 0.5f;
            area.size.y = h;
        } else {
            const float w = area.size.y * want;
            area.position.x += (area.size.x - w) * 0.5f;
            area.size.x = w;
        }
        return area;
    }

    sf::FloatRect layoutTimeTrack() const {
        return {{144.0f, static_cast<float>(layoutSize_.y) - 66.0f},
                {std::max(320.0f, panelX_ - 176.0f), 6.0f}};
    }

    std::string layoutMaskSignature() const {
        std::ostringstream out;
        out << std::setprecision(9) << sliders_[DomainX].value << '|'
            << sliders_[DomainY].value << '|' << sliders_[CellsX].value << '|'
            << sliders_[CellsY].value << '|' << sliders_[SliceX].value << '|'
            << sliders_[SliceZ].value << '|' << sliders_[SliceRotation].value
            << '|' << (invertSection_ ? 1 : 0) << '|'
            << geometry_.sourcePath().u8string() << '|'
            << sliders_[Profiles].text;
        return out.str();
    }

    void refreshLayoutMask() {
        const std::string signature = layoutMaskSignature();
        if (signature == layoutSignature_ && !layoutOwner_.empty())
            return;
        layoutSignature_ = signature;
        layoutOwner_.clear();
        layoutObjects_ = 0;
        layoutCentreX_.clear();
        layoutCentreY_.clear();
        if (geometry_.empty())
            return;

        MaskParameters parameters = sectionParameters();
        parameters.nx = std::min(parameters.nx, 400);
        parameters.ny = std::min(parameters.ny, 400);
        const MaskResult mask = geometry_.generateMask(parameters);
        if (!mask.success)
            return;

        layoutNx_ = parameters.nx;
        layoutNy_ = parameters.ny;
        layoutOwner_.assign(mask.cells.size(), 0);

        std::vector<int> stack;
        for (int seed = 0; seed < layoutNx_ * layoutNy_; ++seed) {
            if (mask.cells[seed] == 0 || layoutOwner_[seed] != 0)
                continue;
            ++layoutObjects_;
            stack.clear();
            stack.push_back(seed);
            layoutOwner_[seed] = layoutObjects_;
            double sumX = 0.0, sumY = 0.0, count = 0.0;
            while (!stack.empty()) {
                const int cell = stack.back();
                stack.pop_back();
                const int i = cell % layoutNx_;
                const int j = cell / layoutNx_;
                sumX += i;
                sumY += j;
                count += 1.0;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        if (di == 0 && dj == 0)
                            continue;
                        const int ni = i + di;
                        const int nj = j + dj;
                        if (ni < 0 || ni >= layoutNx_ || nj < 0 ||
                            nj >= layoutNy_)
                            continue;
                        const int neighbour = nj * layoutNx_ + ni;
                        if (mask.cells[neighbour] == 0 ||
                            layoutOwner_[neighbour] != 0)
                            continue;
                        layoutOwner_[neighbour] = layoutObjects_;
                        stack.push_back(neighbour);
                    }
            }
            const double dx = sliders_[DomainX].value / layoutNx_;
            const double dy = sliders_[DomainY].value / layoutNy_;
            layoutCentreX_.push_back((sumX / count + 0.5) * dx);
            layoutCentreY_.push_back((sumY / count + 0.5) * dy);
        }
        if (layoutSelected_ > layoutObjects_)
            layoutSelected_ = 0;
    }

    bool layoutPointToDomain(sf::Vector2f point, double& x, double& y) const {
        const sf::FloatRect canvas = layoutCanvasRect();
        if (!canvas.contains(point))
            return false;
        x = sliders_[DomainX].value *
            static_cast<double>((point.x - canvas.position.x) / canvas.size.x);
        y = sliders_[DomainY].value *
            static_cast<double>(1.0f - (point.y - canvas.position.y) /
                                           canvas.size.y);
        return true;
    }

    sf::Vector2f layoutDomainToPoint(double x, double y) const {
        const sf::FloatRect canvas = layoutCanvasRect();
        return {canvas.position.x +
                    static_cast<float>(x / std::max(1e-9,
                                                    sliders_[DomainX].value)) *
                        canvas.size.x,
                canvas.position.y + canvas.size.y -
                    static_cast<float>(y / std::max(1e-9,
                                                    sliders_[DomainY].value)) *
                        canvas.size.y};
    }

    int layoutObjectAt(sf::Vector2f point) const {
        double x = 0.0, y = 0.0;
        if (!layoutPointToDomain(point, x, y) || layoutOwner_.empty())
            return 0;
        for (int object = 1; object <= layoutObjects_; ++object) {
            const Pose pose = poseAt(object, layoutTime_);
            const double px = x - pose.x;
            const double py = y - pose.y;
            const int i = static_cast<int>(
                px / std::max(1e-9, sliders_[DomainX].value) * layoutNx_);
            const int j = static_cast<int>(
                py / std::max(1e-9, sliders_[DomainY].value) * layoutNy_);
            if (i < 0 || i >= layoutNx_ || j < 0 || j >= layoutNy_)
                continue;
            if (layoutOwner_[static_cast<std::size_t>(j) * layoutNx_ + i] ==
                object)
                return object;
        }
        return 0;
    }

    double layoutDuration() const {
        return std::max(1e-6, sliders_[TotalTime].value);
    }

    void setLayoutTimeFromX(float x) {
        const sf::FloatRect track = layoutTimeTrack();
        const double t = std::clamp(
            static_cast<double>((x - track.position.x) / track.size.x), 0.0,
            1.0);
        layoutTime_ = t * layoutDuration();
    }

    bool handleLayoutMousePressed(sf::Mouse::Button button,
                                  sf::Vector2f position) {
        if (!layoutMode_)
            return false;
        if (button == sf::Mouse::Button::Left) {
            if (layoutKeyButton_.hit(position)) {
                dropCurrentPose();
                return true;
            }
            if (layoutDropButton_.hit(position)) {
                pushUndo();
                removeKeyframe(layoutSelected_, layoutTime_);
                return true;
            }
            if (layoutInterpButton_.hit(position)) {
                layoutInterp_ =
                    (layoutInterp_ + 1) % interpNames().size();
                if (layoutInterp_ == 0)
                    layoutEase_ = (layoutEase_ + 1) % easeNames().size();
                status_ = std::string("Keyframes drop with interp=") +
                          interpNames()[layoutInterp_] + ", ease=" +
                          easeNames()[layoutEase_];
                return true;
            }
            if (layoutClearButton_.hit(position)) {
                pushUndo();
                setMotionEntry(sliders_[BodyTrackLine].text, layoutSelected_,
                               std::string());
                setMotionEntry(sliders_[BodyMotionLine].text, layoutSelected_,
                               std::string());
                loadBodyRows();
                status_ = "Track cleared for that body.";
                return true;
            }
            if (layoutTimeTrack().contains(
                    {position.x, position.y - 8.0f}) ||
                layoutTimeTrack().contains(position)) {
                draggingLayoutTime_ = true;
                setLayoutTimeFromX(position.x);
                return true;
            }
        }
        double x = 0.0, y = 0.0;
        if (!layoutPointToDomain(position, x, y))
            return false;
        if (button == sf::Mouse::Button::Left) {
            const int picked = layoutObjectAt(position);
            if (picked > 0) {
                layoutSelected_ = picked;
                sliders_[BodySelect].value = picked;
                loadBodyRows();
                pushUndo();
                layoutDragging_ = true;
                status_ = "Body " + std::to_string(picked) +
                          " selected. Drag to place it, K drops a keyframe "
                          "at the time cursor.";
            } else {
                layoutSelected_ = 0;
                status_ = "Nothing there. Click a body to pick it up.";
            }
            return true;
        }
        if (button == sf::Mouse::Button::Right && layoutSelected_ > 0) {
            pushUndo();
            layoutRotating_ = true;
            return true;
        }
        return false;
    }

    void dragLayoutSelection(sf::Vector2f delta) {
        if (layoutSelected_ <= 0)
            return;
        const sf::FloatRect canvas = layoutCanvasRect();
        Pose pose = poseAt(layoutSelected_, layoutTime_);
        pose.x += static_cast<double>(delta.x / canvas.size.x) *
                  sliders_[DomainX].value;
        pose.y -= static_cast<double>(delta.y / canvas.size.y) *
                  sliders_[DomainY].value;
        pose.time = layoutTime_;
        pose.interp = interpNames()[layoutInterp_];
        pose.ease = easeNames()[layoutEase_];
        dropKeyframe(layoutSelected_, pose);
    }

    void rotateLayoutSelection(float deltaX) {
        if (layoutSelected_ <= 0)
            return;
        Pose pose = poseAt(layoutSelected_, layoutTime_);
        pose.rot += static_cast<double>(deltaX) * 0.5;
        pose.time = layoutTime_;
        pose.interp = interpNames()[layoutInterp_];
        pose.ease = easeNames()[layoutEase_];
        dropKeyframe(layoutSelected_, pose);
    }

    void dropCurrentPose() {
        if (layoutSelected_ <= 0) {
            status_ = "Pick a body first.";
            return;
        }
        pushUndo();
        Pose pose = poseAt(layoutSelected_, layoutTime_);
        pose.time = layoutTime_;
        pose.interp = interpNames()[layoutInterp_];
        pose.ease = easeNames()[layoutEase_];
        dropKeyframe(layoutSelected_, pose);
        status_ = "Keyframe at " + formatValue(layoutTime_, false, "s") +
                  " for body " + std::to_string(layoutSelected_) + ".";
    }

    bool handleLayoutKeyPressed(const sf::Event::KeyPressed& key) {
        const double stepSeconds = layoutDuration() / 40.0;
        switch (key.code) {
        case sf::Keyboard::Key::K:
            dropCurrentPose();
            return true;
        case sf::Keyboard::Key::Delete:
            if (layoutSelected_ > 0) {
                pushUndo();
                removeKeyframe(layoutSelected_, layoutTime_);
            }
            return true;
        case sf::Keyboard::Key::I:
            layoutInterp_ = (layoutInterp_ + 1) % interpNames().size();
            status_ = std::string("Keyframes drop with interp=") +
                      interpNames()[layoutInterp_];
            return true;
        case sf::Keyboard::Key::Comma:
            layoutTime_ = std::max(0.0, layoutTime_ - stepSeconds);
            return true;
        case sf::Keyboard::Key::Period:
            layoutTime_ =
                std::min(layoutDuration(), layoutTime_ + stepSeconds);
            return true;
        case sf::Keyboard::Key::Q:
            if (layoutSelected_ > 0) {
                pushUndo();
                rotateLayoutSelection(-6.0f);
            }
            return true;
        case sf::Keyboard::Key::E:
            if (layoutSelected_ > 0) {
                pushUndo();
                rotateLayoutSelection(6.0f);
            }
            return true;
        case sf::Keyboard::Key::Tab:
            if (layoutObjects_ > 0) {
                layoutSelected_ = layoutSelected_ % layoutObjects_ + 1;
                sliders_[BodySelect].value = layoutSelected_;
                loadBodyRows();
            }
            return true;
        default:
            return false;
        }
    }

    void drawLayoutCanvas() {
        refreshLayoutMask();
        const sf::FloatRect canvas = layoutCanvasRect();

        sf::RectangleShape frame(canvas.size);
        frame.setPosition(canvas.position);
        frame.setFillColor(sf::Color{16, 20, 26});
        frame.setOutlineColor(BORDER);
        frame.setOutlineThickness(1.0f);
        window_->draw(frame);

        if (layoutOwner_.empty() || layoutNx_ < 1 || layoutNy_ < 1) {
            window_->draw(makeText(
                font_,
                geometry_.empty()
                    ? "Import a model and the bodies show up here."
                    : "No solid cells in this section yet.",
                16,
                {canvas.position.x + 16.0f, canvas.position.y + 16.0f},
                MUTED));
            return;
        }

        const float cellW = canvas.size.x / layoutNx_;
        const float cellH = canvas.size.y / layoutNy_;
        for (int object = 1; object <= layoutObjects_; ++object) {
            const Pose pose = poseAt(object, layoutTime_);
            const sf::Vector2f shift =
                layoutDomainToPoint(pose.x, pose.y) -
                layoutDomainToPoint(0.0, 0.0);
            const bool chosen = object == layoutSelected_;
            sf::Color colour =
                chosen ? ACCENT : sf::Color{96, 112, 140};
            sf::VertexArray cells(sf::PrimitiveType::Triangles);
            for (int j = 0; j < layoutNy_; ++j)
                for (int i = 0; i < layoutNx_; ++i) {
                    if (layoutOwner_[static_cast<std::size_t>(j) * layoutNx_ +
                                     i] != object)
                        continue;
                    const float x0 = canvas.position.x + i * cellW + shift.x;
                    const float y0 = canvas.position.y + canvas.size.y -
                                     (j + 1) * cellH + shift.y;
                    const sf::Vector2f a{x0, y0};
                    const sf::Vector2f b{x0 + cellW, y0};
                    const sf::Vector2f c{x0 + cellW, y0 + cellH};
                    const sf::Vector2f d{x0, y0 + cellH};
                    cells.append({a, colour});
                    cells.append({b, colour});
                    cells.append({c, colour});
                    cells.append({a, colour});
                    cells.append({c, colour});
                    cells.append({d, colour});
                }
            window_->draw(cells);

            const sf::Vector2f label = layoutDomainToPoint(
                layoutCentreX_[object - 1] + pose.x,
                layoutCentreY_[object - 1] + pose.y);
            std::string caption = std::to_string(object);
            if (std::fabs(pose.rot) > 0.05)
                caption += "  " + formatValue(pose.rot, false, "deg");
            window_->draw(makeText(font_, caption, chosen ? 15 : 12,
                                   {label.x + 6.0f, label.y - 8.0f},
                                   chosen ? TEXT : MUTED));

            const std::vector<Pose> track = poseTrack(object);
            if (track.size() > 1) {
                sf::VertexArray path(sf::PrimitiveType::LineStrip);
                const sf::Color pathColour =
                    chosen ? ACCENT : sf::Color{96, 96, 96};
                if (curvedPath() && track.size() > 2) {
                    const double from = track.front().time;
                    const double span = track.back().time - from;
                    const int steps =
                        static_cast<int>(track.size() - 1) * 12;
                    for (int step = 0; step <= steps; ++step) {
                        const Pose sample = bodyPoseOnCurve(
                            track,
                            from + span * static_cast<double>(step) / steps);
                        path.append({layoutDomainToPoint(
                                         layoutCentreX_[object - 1] + sample.x,
                                         layoutCentreY_[object - 1] + sample.y),
                                     pathColour});
                    }
                } else {
                    for (const Pose& step : track) {
                        const sf::Vector2f point = layoutDomainToPoint(
                            layoutCentreX_[object - 1] + step.x,
                            layoutCentreY_[object - 1] + step.y);
                        path.append({point, pathColour});
                    }
                }
                window_->draw(path);
                for (const Pose& step : track) {
                    const sf::Vector2f point = layoutDomainToPoint(
                        layoutCentreX_[object - 1] + step.x,
                        layoutCentreY_[object - 1] + step.y);
                    sf::CircleShape dot(chosen ? 4.0f : 2.5f);
                    dot.setOrigin({chosen ? 4.0f : 2.5f,
                                   chosen ? 4.0f : 2.5f});
                    dot.setPosition(point);
                    dot.setFillColor(chosen ? sf::Color::White
                                            : sf::Color{96, 96, 96});
                    window_->draw(dot);
                }
            }
        }

        drawLayoutTimeline();
    }

    void drawLayoutTimeline() {
        const sf::FloatRect track = layoutTimeTrack();
        sf::RectangleShape rail(track.size);
        rail.setPosition(track.position);
        rail.setFillColor(CONTROL_RAIL);
        window_->draw(rail);

        const double duration = layoutDuration();
        if (layoutSelected_ > 0)
            for (const Pose& pose : poseTrack(layoutSelected_)) {
                const float x =
                    track.position.x +
                    static_cast<float>(pose.time / duration) * track.size.x;
                sf::RectangleShape tick({2.0f, 14.0f});
                tick.setPosition({x - 1.0f, track.position.y - 4.0f});
                tick.setFillColor(sf::Color::White);
                window_->draw(tick);
            }

        const float cursor =
            track.position.x +
            static_cast<float>(layoutTime_ / duration) * track.size.x;
        sf::RectangleShape head({3.0f, 20.0f});
        head.setPosition({cursor - 1.5f, track.position.y - 7.0f});
        head.setFillColor(ACCENT);
        window_->draw(head);

        std::ostringstream caption;
        caption << std::fixed << std::setprecision(4) << layoutTime_ << " s of "
                << duration << " s";
        if (layoutSelected_ > 0)
            caption << "   |   body " << layoutSelected_ << "   |   "
                    << interpNames()[layoutInterp_] << " "
                    << easeNames()[layoutEase_];
        caption << "   |   click a body, drag to place, K keyframes, "
                   "right-drag or Q/E rotates, , and . step time";
        window_->draw(makeText(font_, caption.str(), 12,
                               {track.position.x, track.position.y + 12.0f},
                               MUTED));
    }

    void drawLayoutControls() {
        layoutButton_.label = layoutMode_ ? "Layout on" : "Layout";
        layoutButton_.selected = layoutMode_;
        layoutButton_.enabled = true;
        layoutButton_.draw(*window_, font_, lastMouse_);
        if (!layoutMode_)
            return;
        layoutKeyButton_.label = "Keyframe";
        layoutDropButton_.label = "Remove";
        layoutInterpButton_.label = interpNames()[layoutInterp_];
        layoutClearButton_.label = "Clear";
        layoutKeyButton_.enabled = layoutSelected_ > 0;
        layoutDropButton_.enabled = layoutSelected_ > 0;
        layoutInterpButton_.enabled = true;
        layoutClearButton_.enabled = layoutSelected_ > 0;
        layoutKeyButton_.draw(*window_, font_, lastMouse_);
        layoutDropButton_.draw(*window_, font_, lastMouse_);
        layoutInterpButton_.draw(*window_, font_, lastMouse_);
        layoutClearButton_.draw(*window_, font_, lastMouse_);
    }

    void drawProperties() {
        drawParameterStrip();
        drawParameterRowBackdrops();
        drawParameterGroupHeaders();
        for (std::size_t index = 0; index < sliders_.size(); ++index) {
            if (!parameterRowOnScreen(index)) {
                continue;
            }
            const bool editing = editingSlider_ == index;
            sliders_[index].draw(
                *window_,
                font_,
                editing,
                editing ? sliderEditText_ : std::string{},
                invalidSlider_ == index);
        }
        drawParameterScrollbar();
        resetDefaultsButton_.draw(*window_, font_, lastMouse_);
        saveConfigButton_.draw(*window_, font_, lastMouse_);
        loadConfigButton_.draw(*window_, font_, lastMouse_);
        generateButton_.draw(*window_, font_, lastMouse_);
        drawParameterTooltip();
    }

    void drawSetup() {
        sf::RectangleShape viewBackground(setupViewport_.size);
        viewBackground.setPosition(setupViewport_.position);
        viewBackground.setFillColor(VIEW_BACKGROUND);
        viewBackground.setOutlineColor(BORDER);
        viewBackground.setOutlineThickness(1.0f);
        window_->draw(viewBackground);

        if (layoutMode_) {
            drawLayoutCanvas();
        } else if (painting_ && phasesOn()) {
            drawPaintCanvas();
        } else if (geometry_.empty()) {
            window_->draw(makeText(
                font_,
                phasesOn()
                    ? "Import a model, or paint the fluid straight onto the "
                      "grid - a profile is optional now"
                    : "Import an STL or OBJ model",
                phasesOn() ? 18 : 24,
                {
                    setupViewport_.position.x + 40.0f,
                    setupViewport_.position.y + 50.0f
                },
                MUTED));
        } else {
            drawGeometryPreview();
        }
        if (phasesOn() && !layoutMode_)
            drawPaintControls();
        drawLayoutControls();
        if (!painting_ && !layoutMode_) {
            drawSetupInfoOverlay();
            drawSliceControls();
        }
    }

    void drawGeometryPreview() {
        std::vector<PreviewTriangle> projected;
        const auto& source = geometry_.triangles();
        const MaskParameters parameters = sectionParameters();
        const SectionFrame frame = geometry_.sectionFrame(parameters);
        if (!frame.valid) {
            return;
        }
        if (parameters.sliceAngleX != sectionSegmentsSliceX_ ||
            parameters.sliceAngleZ != sectionSegmentsSliceZ_) {
            sectionSegments_ = geometry_.sectionSegments(parameters);
            sectionSegmentsSliceX_ = parameters.sliceAngleX;
            sectionSegmentsSliceZ_ = parameters.sliceAngleZ;
        }
        const double rotation = parameters.sliceRotation * PI / 180.0;
        const double cosineRotation = std::cos(rotation);
        const double sineRotation = std::sin(rotation);
        const auto rotateForPreview =
            [&frame, cosineRotation, sineRotation](const Vec3& point) {
                const Vec3 relative = subtract(point, frame.centre);
                const double axial =
                    relative.x * frame.normal.x +
                    relative.y * frame.normal.y +
                    relative.z * frame.normal.z;
                return add(
                    frame.centre,
                    add(
                        add(
                            multiply(relative, cosineRotation),
                            multiply(
                                cross(frame.normal, relative),
                                sineRotation)),
                        multiply(
                            frame.normal,
                            axial * (1.0 - cosineRotation))));
            };
        const std::size_t stride =
            std::max<std::size_t>(1, source.size() / 12000);
        projected.reserve((source.size() + stride - 1) / stride);

        for (std::size_t index = 0; index < source.size(); index += stride) {
            const Triangle3& triangle = source[index];
            const Vec3 firstVertex = rotateForPreview(triangle.v0);
            const Vec3 secondVertex = rotateForPreview(triangle.v1);
            const Vec3 thirdVertex = rotateForPreview(triangle.v2);
            const ProjectedPoint first =
                projectPoint(firstVertex, setupViewport_);
            const ProjectedPoint second =
                projectPoint(secondVertex, setupViewport_);
            const ProjectedPoint third =
                projectPoint(thirdVertex, setupViewport_);

            const Vec3 normal = cross(
                subtract(secondVertex, firstVertex),
                subtract(thirdVertex, firstVertex));
            const double normalLength = length(normal);
            const double shade =
                normalLength > 0.0
                    ? std::clamp(
                          std::abs(
                              (normal.x * 0.3 +
                               normal.y * 0.6 +
                               normal.z * 0.74) /
                              normalLength),
                          0.0,
                          1.0)
                    : 0.0;
            const std::uint8_t brightness =
                static_cast<std::uint8_t>(75 + 105 * shade);
            projected.push_back({
                {first.position, second.position, third.position},
                (first.depth + second.depth + third.depth) / 3.0,
                 {brightness,
                  static_cast<std::uint8_t>(
                      std::min<int>(255, brightness + 2)),
                  static_cast<std::uint8_t>(
                      std::min<int>(255, brightness + 1))}
            });
        }

        std::sort(
            projected.begin(),
            projected.end(),
            [](const PreviewTriangle& first, const PreviewTriangle& second) {
                return first.depth < second.depth;
            });

        // Everything below is drawn through a view whose viewport is the
        // preview panel, so it is clipped to it. The section plane is a quad
        // projected from three dimensions and is routinely larger than the
        // panel when the model is turned edge-on - it used to be drawn over
        // the parameter list and the buttons beside it. The coordinates are
        // unchanged: the view covers the same rectangle in window space that
        // projectPoint already maps into, so this only removes what falls
        // outside.
        // A scissor rectangle rather than a second view: it leaves every
        // coordinate exactly where projectPoint put it and only discards what
        // falls outside the panel. The results view already clips its frame
        // the same way.
        const sf::View savedView = window_->getView();
        sf::View clippedView = savedView;
        const sf::Vector2f windowSize{
            static_cast<float>(std::max(1u, window_->getSize().x)),
            static_cast<float>(std::max(1u, window_->getSize().y))
        };
        clippedView.setScissor({
            {
                setupViewport_.position.x / windowSize.x,
                setupViewport_.position.y / windowSize.y
            },
            {
                setupViewport_.size.x / windowSize.x,
                setupViewport_.size.y / windowSize.y
            }
        });
        window_->setView(clippedView);

        for (const PreviewTriangle& triangle : projected) {
            sf::ConvexShape shape(3);
            for (std::size_t corner = 0; corner < 3; ++corner) {
                shape.setPoint(corner, triangle.points[corner]);
            }
            shape.setFillColor(triangle.color);
            shape.setOutlineColor(sf::Color{38, 42, 40, 150});
            shape.setOutlineThickness(0.5f);
            window_->draw(shape);
        }

        const std::array<Vec3, 4> planeCorners{{
            add(
                add(frame.centre, multiply(frame.axisX, frame.extent)),
                multiply(frame.axisY, frame.extent)),
            add(
                add(frame.centre, multiply(frame.axisX, -frame.extent)),
                multiply(frame.axisY, frame.extent)),
            add(
                add(frame.centre, multiply(frame.axisX, -frame.extent)),
                multiply(frame.axisY, -frame.extent)),
            add(
                add(frame.centre, multiply(frame.axisX, frame.extent)),
                multiply(frame.axisY, -frame.extent))
        }};

        sf::ConvexShape plane(4);
        for (std::size_t corner = 0; corner < planeCorners.size(); ++corner) {
            plane.setPoint(
                corner,
                projectPoint(planeCorners[corner], setupViewport_).position);
        }
        plane.setFillColor(SECTION_PLANE);
        plane.setOutlineColor(SECTION_PLANE_OUTLINE);
        plane.setOutlineThickness(2.0f);
        window_->draw(plane);

        for (const SectionSegment& segment : sectionSegments_) {
            const sf::Vector2f first = projectPoint(
                rotateForPreview(segment.first),
                setupViewport_).position;
            const sf::Vector2f second = projectPoint(
                rotateForPreview(segment.second),
                setupViewport_).position;
            drawThickLine(
                *window_,
                first,
                second,
                8.0f,
                CUT_GLOW);
            drawThickLine(
                *window_,
                first,
                second,
                3.5f,
                CUT_COLOR);
        }

        // Back to the whole window: the legend is positioned inside the panel
        // already and reads better without the clip's rounding on its edges.
        window_->setView(savedView);

        const sf::Vector2f legendPosition =
            setupViewport_.position + sf::Vector2f{18.0f, 18.0f};
        sf::RectangleShape legendBackground({330.0f, 59.0f});
        legendBackground.setPosition(legendPosition);
        legendBackground.setFillColor(sf::Color{8, 10, 9, 225});
        legendBackground.setOutlineColor(BORDER);
        legendBackground.setOutlineThickness(1.0f);
        window_->draw(legendBackground);
        window_->draw(makeText(
            font_,
            "GREEN = section plane",
            14,
            legendPosition + sf::Vector2f{12.0f, 8.0f},
            SECTION_PLANE_OUTLINE));
        window_->draw(makeText(
            font_,
            sectionSegments_.empty()
                ? "ORANGE = no mesh intersection"
                : "ORANGE = mesh-plane intersection",
            14,
            legendPosition + sf::Vector2f{12.0f, 31.0f},
            CUT_COLOR));
    }

    void drawSliceControls() {
        const sf::FloatRect horizontal = horizontalSliceTrack();
        sf::RectangleShape horizontalRail(horizontal.size);
        horizontalRail.setPosition(horizontal.position);
        horizontalRail.setFillColor(CONTROL_RAIL);
        window_->draw(horizontalRail);
        const float horizontalFraction = static_cast<float>(
            (sliders_[SliceZ].value + 180.0) / 360.0);
        sf::CircleShape horizontalHandle(7.0f);
        horizontalHandle.setOrigin({7.0f, 7.0f});
        horizontalHandle.setPosition({
            horizontal.position.x +
                horizontal.size.x * horizontalFraction,
            horizontal.position.y + horizontal.size.y / 2.0f
        });
        horizontalHandle.setFillColor(ACCENT);
        window_->draw(horizontalHandle);

        const sf::FloatRect vertical = verticalSliceTrack();
        sf::RectangleShape verticalRail(vertical.size);
        verticalRail.setPosition(vertical.position);
        verticalRail.setFillColor(CONTROL_RAIL);
        window_->draw(verticalRail);
        const float verticalFraction = static_cast<float>(
            (180.0 - sliders_[SliceX].value) / 360.0);
        sf::CircleShape verticalHandle(7.0f);
        verticalHandle.setOrigin({7.0f, 7.0f});
        verticalHandle.setPosition({
            vertical.position.x + vertical.size.x / 2.0f,
            vertical.position.y +
                vertical.size.y * verticalFraction
        });
        verticalHandle.setFillColor(ACCENT);
        window_->draw(verticalHandle);

        const sf::FloatRect invert = invertBounds();
        sf::RectangleShape box(invert.size);
        box.setPosition(invert.position);
        box.setFillColor(
            invertSection_ ? ACCENT_DARK : BUTTON_DISABLED);
        box.setOutlineColor(ACCENT);
        box.setOutlineThickness(1.0f);
        window_->draw(box);
        if (invertSection_) {
            window_->draw(makeText(
                font_,
                "x",
                16,
                {invert.position.x + 7.0f, invert.position.y + 1.0f}));
        }
        window_->draw(makeText(
            font_,
            "Slice Z",
            12,
            {horizontal.position.x, horizontal.position.y - 19.0f},
            MUTED));
        window_->draw(makeText(
            font_,
            "Slice X",
            12,
            {vertical.position.x - 28.0f, vertical.position.y - 20.0f},
            MUTED));
        window_->draw(makeText(
            font_,
            "Invert",
            11,
            {invert.position.x - 13.0f, invert.position.y - 18.0f},
            MUTED));
    }

    std::string readRunDetailFile(
        const std::filesystem::path& path,
        const std::string& heading,
        std::size_t maximumLines) const {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            return heading + ": unavailable\n";
        }
        std::ostringstream output;
        output << heading << ":\n";
        std::string line;
        std::size_t lines = 0;
        while (lines < maximumLines && std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.size() > 96) {
                line.resize(93);
                line += "...";
            }
            output << "  " << line << '\n';
            ++lines;
        }
        return output.str();
    }

    void refreshRunDetailsText() {
        std::filesystem::path directory = currentRunDirectory_;
        if (directory.empty() && !frames_.empty()) {
            directory = frames_[selectedFrame_].sourcePath.parent_path();
        }
        if (directory.empty()) {
            runDetailsText_ = "No run directory is available.";
            return;
        }
        std::ostringstream output;
        output << "RUN DIRECTORY\n  " << compactPath(directory, 90) << "\n\n";
        output << readRunDetailFile(
            directory / "ui-request.txt", "UI metadata", 8) << '\n';
        output << readRunDetailFile(
            directory / "solver-arguments.txt", "Fluid Solver arguments", 10);
        const std::string solverError = readDiagnosticFile(
            directory / "solver-error.txt");
        const std::string solverOutput = readDiagnosticFile(
            directory / "solver-output.txt");
        if (!solverError.empty()) {
            output << "\nLast stderr:\n  " << solverError.substr(0, 420) << '\n';
        } else if (!solverOutput.empty()) {
            output << "\nLast stdout:\n  " << solverOutput.substr(0, 420) << '\n';
        }
        runDetailsText_ = output.str();
    }

    void drawRunDetailsOverlay() {
        const float width = std::min(720.0f, resultViewport_.size.x - 40.0f);
        const float height = std::min(500.0f, resultViewport_.size.y - 40.0f);
        if (width < 300.0f || height < 180.0f) {
            return;
        }
        const sf::Vector2f position{
            resultViewport_.position.x + 20.0f,
            resultViewport_.position.y + 20.0f
        };
        sf::RectangleShape background({width, height});
        background.setPosition(position);
        background.setFillColor(OVERLAY_BACKGROUND);
        background.setOutlineColor(ACCENT_DARK);
        background.setOutlineThickness(1.0f);
        window_->draw(background);
        window_->draw(makeText(
            font_, runDetailsText_, 11,
            position + sf::Vector2f{12.0f, 10.0f}, TEXT));
    }

    void drawResults() {
        pressureButton_.draw(*window_, font_, lastMouse_);
        velocityButton_.draw(*window_, font_, lastMouse_);
        fieldButton_.draw(*window_, font_, lastMouse_);
        vectorButton_.draw(*window_, font_, lastMouse_);
        rangeButton_.draw(*window_, font_, lastMouse_);
        runDetailsButton_.draw(*window_, font_, lastMouse_);
        continueRunButton_.draw(*window_, font_, lastMouse_);

        sf::RectangleShape background(resultViewport_.size);
        background.setPosition(resultViewport_.position);
        background.setFillColor(VIEW_BACKGROUND);
        background.setOutlineColor(BORDER);
        background.setOutlineThickness(1.0f);
        window_->draw(background);

        if (!frames_.empty() && sliceCache_.view()) {
            if (view3D_) {
                viewport3D_.draw(*window_, resultViewport_);
                drawViewportOverlay();
            } else {
                drawResultCells();
                drawLegend();
                drawResultTooltip();
            }
            drawResultSliders();
            drawResultWarning();
        }
        drawResultControls();
        if (showRunDetails_) {
            drawRunDetailsOverlay();
        }
    }

    const VtkFrame& displayFrame() const {
        return *sliceCache_.view();
    }

    const char* sliceAxisName() const {
        return sliceAxis_ == SliceAxis::X
            ? "X"
            : (sliceAxis_ == SliceAxis::Y ? "Y" : "Z");
    }

    std::size_t slicePlaneCount() const {
        return sliceCache_.planeCount();
    }

    // A slice plane is the whole of a nz=1 result and only a cut through a
    // volume, so the two cases do not want the same view. A volume opens on
    // its isosurface with every plane off; a single plane opens on the plane,
    // because with that off there is nothing left to look at. Applied once per
    // kind of result, so switching a plane back on afterwards sticks until a
    // result of the other kind is loaded.
    void applyViewDefaults() {
        if (!activeFrame_) {
            return;
        }
        const int kind = activeFrame_->nz > 1u ? 1 : 0;
        if (viewDefaultsFor_ == kind) {
            return;
        }
        viewDefaultsFor_ = kind;
        view3DSettings_.sliceX = false;
        view3DSettings_.sliceY = false;
        view3DSettings_.sliceZ = kind == 0;
        view3DSettings_.showIsosurface = kind == 1;
        if (kind == 1) {
            view3DSettings_.isoField = view3DSettings_.colourBy;
        }
    }

    // "x=4.2,y=0.35,z=0.9;x=..." - the same row the solver is handed. A point
    // with no z sits at 0, which is where every microphone was before there
    // was a third dimension to put one in.
    std::vector<std::array<float, 3>> microphonePoints() const {
        std::vector<std::array<float, 3>> out;
        const std::string& line = sliders_[MicrophoneLine].text;
        std::size_t start = 0;
        while (start <= line.size()) {
            const std::size_t mark = line.find(';', start);
            const std::string token = line.substr(
                start, mark == std::string::npos ? std::string::npos
                                                 : mark - start);
            std::array<float, 3> point{0.0f, 0.0f, 0.0f};
            bool named = false;
            std::size_t at = 0;
            while (at <= token.size()) {
                const std::size_t comma = token.find(',', at);
                const std::string pair = token.substr(
                    at, comma == std::string::npos ? std::string::npos
                                                   : comma - at);
                const std::size_t equals = pair.find('=');
                if (equals != std::string::npos) {
                    std::string name = pair.substr(0, equals);
                    while (!name.empty() &&
                           std::isspace(
                               static_cast<unsigned char>(name.front())))
                        name.erase(name.begin());
                    while (!name.empty() &&
                           std::isspace(
                               static_cast<unsigned char>(name.back())))
                        name.pop_back();
                    const double value = std::atof(
                        pair.substr(equals + 1).c_str());
                    if (name == "x" || name == "X") {
                        point[0] = static_cast<float>(value);
                        named = true;
                    } else if (name == "y" || name == "Y") {
                        point[1] = static_cast<float>(value);
                        named = true;
                    } else if (name == "z" || name == "Z") {
                        point[2] = static_cast<float>(value);
                        named = true;
                    }
                }
                if (comma == std::string::npos)
                    break;
                at = comma + 1;
            }
            if (named)
                out.push_back(point);
            if (mark == std::string::npos)
                break;
            start = mark + 1;
        }
        return out;
    }

    void syncViewportSettings() {
        applyViewDefaults();
        view3DSettings_.microphones = microphonePoints();
        view3DSettings_.sliceIndexX =
            std::min(view3DSettings_.sliceIndexX,
                     activeFrame_ && activeFrame_->nx
                         ? activeFrame_->nx - 1u : 0u);
        view3DSettings_.sliceIndexY =
            std::min(view3DSettings_.sliceIndexY,
                     activeFrame_ && activeFrame_->ny
                         ? activeFrame_->ny - 1u : 0u);
        view3DSettings_.sliceIndexZ =
            std::min(view3DSettings_.sliceIndexZ,
                     activeFrame_ && activeFrame_->nz
                         ? activeFrame_->nz - 1u : 0u);
        viewport3D_.setSettings(view3DSettings_);
    }

    void refreshDisplayFrame() {
        sliceCache_.setSource(activeFrame_);
        sliceCache_.setPlane(sliceAxis_, sliceIndex_);
        sliceIndex_ = sliceCache_.index();
        if (view3D_ && viewport3D_.frame() != activeFrame_) {
            viewport3D_.setFrame(activeFrame_);
            syncViewportSettings();
        }
        resultTextureCacheValid_ = false;
        updateLayout(layoutSize_);
    }

    void setSlicePlane(SliceAxis axis, std::size_t index) {
        sliceCache_.setPlane(axis, index);
        if (sliceCache_.axis() == sliceAxis_ &&
            sliceCache_.index() == sliceIndex_) {
            return;
        }
        sliceAxis_ = sliceCache_.axis();
        sliceIndex_ = sliceCache_.index();
        resultTextureCacheValid_ = false;
        resultPan_ = {};
        updateLayout(layoutSize_);
        status_ = std::string("Slice ") + sliceAxisName() + " " +
            std::to_string(sliceIndex_ + 1) + "/" +
            std::to_string(slicePlaneCount()) + ".";
    }

    void stepSlice(int delta) {
        if (!sliceCache_.slicing() || view3D_) {
            return;
        }
        const std::size_t planes = slicePlaneCount();
        if (planes == 0) {
            return;
        }
        const long long wanted =
            static_cast<long long>(sliceIndex_) + delta;
        setSlicePlane(
            sliceAxis_,
            static_cast<std::size_t>(std::clamp(
                wanted, 0LL, static_cast<long long>(planes) - 1)));
    }

    void setViewMode(bool volumetricView) {
        if (view3D_ == volumetricView) {
            return;
        }
        view3D_ = volumetricView;
        if (view3D_) {
            if (viewport3D_.frame() != activeFrame_) {
                viewport3D_.setFrame(activeFrame_);
                syncViewportSettings();
                viewport3D_.frameAll();
            }
            status_ =
                "3D view: drag to orbit, middle-drag to pan, wheel to zoom, "
                "click to select. F frames it all.";
        } else {
            resultTextureCacheValid_ = false;
            status_ = sliceCache_.slicing()
                ? std::string("2D view: slice ") + sliceAxisName() + " " +
                      std::to_string(sliceIndex_ + 1) + "/" +
                      std::to_string(slicePlaneCount()) +
                      ". Up and down move the plane, X, Y and Z pick the axis."
                : std::string("2D view.");
        }
        savePreferences();
        updateLayout(layoutSize_);
    }

    const char* volumeFieldName(VolumeField field) const {
        switch (field) {
        case VolumeField::Pressure: return "pressure";
        case VolumeField::Speed: return "speed";
        case VolumeField::VelocityX: return "u";
        case VolumeField::VelocityY: return "v";
        case VolumeField::VelocityZ: return "w";
        case VolumeField::Vorticity: return "vorticity";
        case VolumeField::QCriterion: return "Q";
        default: return "scalar";
        }
    }

    void cycleVolumeField() {
        static const std::array<VolumeField, 7> order{{
            VolumeField::Speed, VolumeField::Pressure, VolumeField::VelocityX,
            VolumeField::VelocityY, VolumeField::VelocityZ,
            VolumeField::Vorticity, VolumeField::QCriterion}};
        std::size_t next = 0;
        for (std::size_t index = 0; index < order.size(); ++index)
            if (order[index] == view3DSettings_.colourBy)
                next = index + 1;
        view3DSettings_.colourBy = order[next % order.size()];
        syncViewportSettings();
        status_ = std::string("3D view coloured by ") +
            volumeFieldName(view3DSettings_.colourBy) + ".";
    }

    std::size_t viewTrackPlanes(std::size_t track) const {
        if (!activeFrame_) {
            return 1;
        }
        if (track == TrackSliceX) return activeFrame_->nx;
        if (track == TrackSliceY) return activeFrame_->ny;
        if (track == TrackSliceZ) return activeFrame_->nz;
        return slicePlaneCount();
    }

    float viewTrackFraction(std::size_t track) const {
        const auto planeFraction = [](std::size_t index, std::size_t count) {
            return count <= 1
                ? 0.0f
                : static_cast<float>(index) / static_cast<float>(count - 1u);
        };
        switch (track) {
        case TrackSliceX:
            return planeFraction(view3DSettings_.sliceIndexX,
                                 viewTrackPlanes(track));
        case TrackSliceY:
            return planeFraction(view3DSettings_.sliceIndexY,
                                 viewTrackPlanes(track));
        case TrackSliceZ:
            return planeFraction(view3DSettings_.sliceIndexZ,
                                 viewTrackPlanes(track));
        case TrackIso:
            return view3DSettings_.isoLevel;
        case TrackVortex:
            return view3DSettings_.vortexLevel;
        default:
            return planeFraction(sliceIndex_, viewTrackPlanes(track));
        }
    }

    std::string viewTrackLabel(std::size_t track) const {
        const auto planeText = [this](const char* name, std::size_t index,
                                      std::size_t count) {
            return std::string(name) + " " + std::to_string(index + 1) + "/" +
                std::to_string(count);
        };
        switch (track) {
        case TrackSliceX:
            return planeText("Slice X", view3DSettings_.sliceIndexX,
                             viewTrackPlanes(track));
        case TrackSliceY:
            return planeText("Slice Y", view3DSettings_.sliceIndexY,
                             viewTrackPlanes(track));
        case TrackSliceZ:
            return planeText("Slice Z", view3DSettings_.sliceIndexZ,
                             viewTrackPlanes(track));
        case TrackIso:
            return "Isosurface " +
                formatValue(view3DSettings_.isoLevel, false, std::string());
        case TrackVortex:
            return "Vortex Q " +
                formatValue(view3DSettings_.vortexLevel, false, std::string());
        default:
            return planeText(
                (std::string("Slice ") + sliceAxisName()).c_str(),
                sliceIndex_, viewTrackPlanes(track));
        }
    }

    void setViewTrackFromX(std::size_t track, float mouseX) {
        const sf::FloatRect& rail = viewTracks_[track];
        const double fraction = std::clamp(
            static_cast<double>((mouseX - rail.position.x) / rail.size.x),
            0.0, 1.0);
        const auto planeOf = [&fraction](std::size_t count) {
            return count <= 1
                ? std::size_t(0)
                : static_cast<std::size_t>(std::llround(
                      fraction * static_cast<double>(count - 1u)));
        };
        switch (track) {
        case TrackSliceX:
            view3DSettings_.sliceIndexX = planeOf(viewTrackPlanes(track));
            break;
        case TrackSliceY:
            view3DSettings_.sliceIndexY = planeOf(viewTrackPlanes(track));
            break;
        case TrackSliceZ:
            view3DSettings_.sliceIndexZ = planeOf(viewTrackPlanes(track));
            break;
        case TrackIso:
            view3DSettings_.isoLevel = static_cast<float>(fraction);
            break;
        case TrackVortex:
            view3DSettings_.vortexLevel = static_cast<float>(fraction);
            break;
        default:
            setSlicePlane(sliceAxis_, planeOf(viewTrackPlanes(track)));
            return;
        }
        syncViewportSettings();
    }

    bool handleViewControl(std::size_t control) {
        switch (control) {
        case ControlFrameAll:
            viewport3D_.frameAll();
            return true;
        case ControlOrtho:
            viewport3D_.camera().orthographic =
                !viewport3D_.camera().orthographic;
            status_ = viewport3D_.camera().orthographic
                ? "Isometric: parallel projection, no perspective."
                : "Perspective projection.";
            return true;
        case ControlRotateTool:
            moveTool_ = false;
            status_ = "Left drag turns the view around the data.";
            return true;
        case ControlMoveTool:
            moveTool_ = true;
            status_ = "Left drag slides the view. Holding Shift does the same "
                      "either way.";
            return true;
        case ControlBox:
            view3DSettings_.showBox = !view3DSettings_.showBox;
            break;
        case ControlGrid:
            view3DSettings_.showGrid = !view3DSettings_.showGrid;
            break;
        case ControlSolid:
            view3DSettings_.showSolid = !view3DSettings_.showSolid;
            break;
        case ControlWire:
            view3DSettings_.wireframeSolid = !view3DSettings_.wireframeSolid;
            break;
        case ControlSliceX:
            view3DSettings_.sliceX = !view3DSettings_.sliceX;
            break;
        case ControlSliceY:
            view3DSettings_.sliceY = !view3DSettings_.sliceY;
            break;
        case ControlSliceZ:
            view3DSettings_.sliceZ = !view3DSettings_.sliceZ;
            break;
        case ControlIso:
            view3DSettings_.showIsosurface = !view3DSettings_.showIsosurface;
            view3DSettings_.isoField = view3DSettings_.colourBy;
            break;
        case ControlVortices:
            view3DSettings_.showVortices = !view3DSettings_.showVortices;
            break;
        case ControlStreamlines:
            view3DSettings_.showStreamlines =
                !view3DSettings_.showStreamlines;
            break;
        case ControlTracers:
            view3DSettings_.animateTracers = !view3DSettings_.animateTracers;
            break;
        case ControlColour:
            cycleVolumeField();
            updateLayout(layoutSize_);
            return true;
        case ControlFront:
            viewport3D_.setView(2, false);
            return true;
        case ControlBack:
            viewport3D_.setView(2, true);
            return true;
        case ControlLeft:
            viewport3D_.setView(0, true);
            return true;
        case ControlRight:
            viewport3D_.setView(0, false);
            return true;
        case ControlTop:
            viewport3D_.setView(1, false);
            return true;
        case ControlBottom:
            viewport3D_.setView(1, true);
            return true;
        case ControlAxisX:
            setSlicePlane(SliceAxis::X, sliceIndex_);
            return true;
        case ControlAxisY:
            setSlicePlane(SliceAxis::Y, sliceIndex_);
            return true;
        default:
            setSlicePlane(SliceAxis::Z, sliceIndex_);
            return true;
        }
        syncViewportSettings();
        updateLayout(layoutSize_);
        return true;
    }

    void applyPickSelection(const Viewport3D::Pick& pick) {
        const PickSelection selection = selectionForPick(pick);
        if (selection.target == PickTarget::Body) {
            sliders_[BodySelect].value = selection.body;
            loadBodyRows();
            focusedSlider_ = BodySelect;
            scrollRowIntoView(BodySelect);
            status_ = "Selected body " + std::to_string(selection.body) +
                ". The BODIES rows on the right are about it now.";
            return;
        }
        if (selection.target == PickTarget::Boundary) {
            const std::size_t row = boundaryKindRow(selection.side);
            focusedSlider_ = row;
            scrollRowIntoView(row);
            status_ = std::string("Selected the ") + sliders_[row].label +
                ": it is " + sliders_[row].choice() + ", and " +
                sliders_[boundarySpeedRow(selection.side)].label +
                " is under it.";
            return;
        }
        status_ = "Nothing under the cursor there.";
    }

    std::string pickDescription(const Viewport3D::Pick& pick) const {
        if (!pick.hit || !activeFrame_) {
            return std::string();
        }
        const VtkFrame& frame = *activeFrame_;
        std::ostringstream text;
        text << "cell " << pick.i << ", " << pick.j << ", " << pick.k
             << "   x " << formatValue(pick.x, false, "m")
             << "   y " << formatValue(pick.y, false, "m")
             << "   z " << formatValue(pick.z, false, "m");
        const std::size_t index = frame.cellIndex(pick.i, pick.j, pick.k);
        if (pick.solidHit) {
            text << "   solid, object " << pick.objectId;
            return text.str();
        }
        if (index < frame.pressure.size()) {
            text << "   p " << formatValue(frame.pressure[index], false, "Pa")
                 << "   speed "
                 << formatValue(frame.velocityMagnitude[index], false, "m/s");
        }
        if (pick.face >= 0 && pick.face < 6) {
            text << "   " << sliders_[boundaryKindRow(pick.face)].label;
        }
        return text.str();
    }

    // Which numbers the colour scale is stretched between.
    //
    // "Series" is what every frame of the run holds together, so the colours
    // mean the same thing from one frame to the next; "Frame" rescales to the
    // frame on screen, which shows more detail and less of the story. The
    // trimmed pair leave out the outermost half-percent at each end, which is
    // what keeps one stagnation cell - or one frame caught mid-transient -
    // from mapping everything else onto three shades. Trimmed series is the
    // default because that is the view that is readable without being asked
    // for.
    static const std::vector<std::string>& emptyScalarNames() {
        static const std::vector<std::string> none;
        return none;
    }

    DataRange resultDisplayRange() const {
        if (!sliceCache_.view()) {
            return {};
        }
        if (resultQuantity_ == ResultQuantity::Scalar) {
            const auto trimmed =
                displayFrame().scalarTrimmedRanges.find(activeScalarName_);
            if (trimmedRange_ &&
                trimmed != displayFrame().scalarTrimmedRanges.end() &&
                trimmed->second.available)
                return trimmed->second;
            const auto full =
                displayFrame().scalarRanges.find(activeScalarName_);
            return full == displayFrame().scalarRanges.end() ? DataRange{}
                                                             : full->second;
        }
        const bool pressure = resultQuantity_ == ResultQuantity::Pressure;
        DataRange range;
        if (useSeriesRange_) {
            range = trimmedRange_
                ? (pressure ? pressureTrimmedRange_ : velocityTrimmedRange_)
                : (pressure ? pressureRange_ : velocityRange_);
            if (!range.available) {
                range = pressure ? pressureRange_ : velocityRange_;
            }
        } else {
            range = trimmedRange_
                ? (pressure ? displayFrame().pressureTrimmedRange
                            : displayFrame().velocityMagnitudeTrimmedRange)
                : (pressure ? displayFrame().pressureRange
                            : displayFrame().velocityMagnitudeRange);
            if (!range.available) {
                range = pressure ? displayFrame().pressureRange
                                 : displayFrame().velocityMagnitudeRange;
            }
        }
        return range;
    }

    // The velocity half of resultDisplayRange(), for the arrows - they are
    // drawn over the pressure view as well, where the display range is a
    // pressure and means nothing to them.
    DataRange velocityDisplayRange() const {
        if (!sliceCache_.view()) {
            return {};
        }
        DataRange range;
        if (useSeriesRange_) {
            range = trimmedRange_ ? velocityTrimmedRange_ : velocityRange_;
            if (!range.available) {
                range = velocityRange_;
            }
        } else {
            range = trimmedRange_
                ? displayFrame().velocityMagnitudeTrimmedRange
                : displayFrame().velocityMagnitudeRange;
            if (!range.available) {
                range = displayFrame().velocityMagnitudeRange;
            }
        }
        return range;
    }

    void drawResultCells() {
        const VtkFrame& frame = displayFrame();
        const DataRange range = resultDisplayRange();
        const ResultImageTransform transform = resultImageTransform(frame);
        const float cellWidth = static_cast<float>(transform.pixelWidth);
        const float cellHeight = static_cast<float>(transform.pixelHeight);
        const sf::Vector2f origin{
            static_cast<float>(transform.screenOriginX),
            static_cast<float>(transform.screenOriginY)
        };

        if (!resultTextureCacheValid_) {
            const unsigned int maximumTextureSize =
                sf::Texture::getMaximumSize();
            if (frame.nx > maximumTextureSize ||
                frame.ny > maximumTextureSize) {
                status_ = "VTK grid exceeds the GPU texture-size limit.";
                return;
            }
            const sf::Vector2u textureSize{
                static_cast<unsigned int>(frame.nx),
                static_cast<unsigned int>(frame.ny)
            };
            if (resultTexture_.getSize() != textureSize &&
                !resultTexture_.resize(textureSize)) {
                status_ = "Cannot allocate the VTK result texture.";
                return;
            }
            // Kept between frames so a series of same-sized frames does not
            // allocate and free a few megabytes on every single step.
            resultPixels_.assign(frame.nx * frame.ny * 4u, 0);
            std::uint8_t* const pixelData = resultPixels_.data();
            const bool showPressure =
                resultQuantity_ == ResultQuantity::Pressure;
            const std::vector<float>* scalarValues = nullptr;
            if (resultQuantity_ == ResultQuantity::Scalar) {
                const auto found = frame.scalars.find(activeScalarName_);
                if (found != frame.scalars.end())
                    scalarValues = &found->second;
            }
            if (scalarValues) {
                scalarFinite_.assign(scalarValues->size(), 1);
                for (std::size_t id = 0; id < scalarValues->size(); ++id)
                    scalarFinite_[id] =
                        std::isfinite((*scalarValues)[id]) ? 1 : 0;
            }
            const float* const values =
                scalarValues ? scalarValues->data()
                             : showPressure ? frame.pressure.data()
                                            : frame.velocityMagnitude.data();
            const std::uint8_t* const finiteMask =
                scalarValues ? scalarFinite_.data()
                             : showPressure ? frame.pressureFinite.data()
                                            : frame.velocityFinite.data();
            const std::uint8_t* const solidMask = frame.solid.data();
            const std::size_t nx = frame.nx;
            const std::size_t ny = frame.ny;
            const bool rangeAvailable = range.available;
            const double rangeMinimum = range.minimum;
            const double rangeMaximum = range.maximum;

            const bool asPhase =
                resultQuantity_ == ResultQuantity::Scalar &&
                activeScalarName_ == "phase";

            // A stretched frame is resampled onto an even image of the same
            // size: pixel to metres, metres to cell. Without it a cell a
            // hundredth the size of its neighbour is drawn the same width and
            // the picture is a lie. On an even frame the lookup is the
            // identity and the branch is hoisted out of the loop.
            const bool stretchedFrame = frame.rectilinear();
            const double evenWidth =
                frame.spanX() / std::max<std::size_t>(1, frame.nx);
            const double evenHeight =
                frame.spanY() / std::max<std::size_t>(1, frame.ny);

            // One row per thread. Rows are independent and the map is pure
            // arithmetic, so this is the cheapest parallelism in the UI. The
            // per-pixel branch on the displayed quantity is gone with it: the
            // choice is the same for the whole image, so it is made once.
#if defined(_OPENMP)
#pragma omp parallel for schedule(static) if (nx * ny >= 32768)
#endif
            for (std::ptrdiff_t row = 0;
                 row < static_cast<std::ptrdiff_t>(ny);
                 ++row) {
                const std::size_t j = static_cast<std::size_t>(row);
                const std::size_t sourceRow =
                    (stretchedFrame ? frame.rowAt(
                                          frame.originY +
                                          (row + 0.5) * evenHeight)
                                    : j) *
                    nx;
                const std::size_t pixelRow = (ny - 1u - j) * nx * 4u;
                for (std::size_t i = 0; i < nx; ++i) {
                    const std::size_t column =
                        stretchedFrame
                            ? frame.columnAt(frame.originX +
                                             (i + 0.5) * evenWidth)
                            : i;
                    const std::size_t dataIndex = sourceRow + column;
                    sf::Color color = SOLID_COLOR;
                    if (solidMask[dataIndex] == 0) {
                        if (finiteMask[dataIndex] == 0 || !rangeAvailable) {
                            color = INVALID_COLOR;
                        } else {
                            color = asPhase
                                        ? phaseColour(values[dataIndex])
                                        : scalarColor(values[dataIndex],
                                                      rangeMinimum,
                                                      rangeMaximum);
                        }
                    }

                    const std::size_t pixel = pixelRow + i * 4u;
                    pixelData[pixel] = color.r;
                    pixelData[pixel + 1u] = color.g;
                    pixelData[pixel + 2u] = color.b;
                    pixelData[pixel + 3u] = color.a;
                }
            }
            resultTexture_.update(resultPixels_.data());
            resultTexture_.setSmooth(false);
            resultTextureCacheValid_ = true;
        }

        sf::Sprite resultSprite(resultTexture_);
        resultSprite.setPosition(origin);
        resultSprite.setScale({cellWidth, cellHeight});
        const sf::View previousView = window_->getView();
        sf::View clippedView = previousView;
        const sf::Vector2f windowSize{
            static_cast<float>(window_->getSize().x),
            static_cast<float>(window_->getSize().y)
        };
        clippedView.setScissor({
            {
                resultViewport_.position.x / windowSize.x,
                resultViewport_.position.y / windowSize.y
            },
            {
                resultViewport_.size.x / windowSize.x,
                resultViewport_.size.y / windowSize.y
            }
        });
        window_->setView(clippedView);
        window_->draw(resultSprite);
        window_->setView(previousView);
        if (showVelocityVectors_) {
            drawVelocityVectors(frame, origin, cellWidth, cellHeight);
        }


        window_->draw(makeText(
            font_,
            frameProgressLabel(frame),
            14,
            {
                resultViewport_.position.x + 10.0f,
                resultViewport_.position.y +
                    (resultsWarning_.empty() ? 8.0f : 42.0f)
            },
            TEXT));
    }

    void drawResultControls() {
        playbackButton_.draw(*window_, font_, lastMouse_);
        recoverSetupButton_.draw(*window_, font_, lastMouse_);
        viewModeButton_.draw(*window_, font_, lastMouse_);
        for (const Button& control : viewControls_)
            if (control.enabled)
                control.draw(*window_, font_, lastMouse_);
        for (std::size_t track = 0; track < viewTracks_.size(); ++track) {
            if (viewTracks_[track].position.y < 0.0f)
                continue;
            drawSimpleTrack(
                viewTracks_[track],
                viewTrackFraction(track),
                viewTrackLabel(track));
        }
    }

    void drawViewportOverlay() {
        window_->draw(makeText(
            font_,
            frameProgressLabel(*activeFrame_),
            14,
            {
                resultViewport_.position.x + 10.0f,
                resultViewport_.position.y +
                    (resultsWarning_.empty() ? 8.0f : 42.0f)
            },
            TEXT));
        std::ostringstream counts;
        counts << viewport3D_.triangleCount() << " triangles, "
               << viewport3D_.lineCount() << " lines, coloured by "
               << volumeFieldName(view3DSettings_.colourBy);
        window_->draw(makeText(
            font_,
            counts.str(),
            12,
            {
                resultViewport_.position.x + 10.0f,
                resultViewport_.position.y + resultViewport_.size.y - 20.0f
            },
            MUTED));
        if (!pickText_.empty()) {
            window_->draw(makeText(
                font_,
                pickText_,
                12,
                {
                    resultViewport_.position.x + 10.0f,
                    resultViewport_.position.y + resultViewport_.size.y - 38.0f
                },
                ACCENT));
        }
    }

    void drawResultTooltip() {
        if (!activeFrame_ || panningResults_ || draggingFrame_ ||
            draggingZoom_ || !resultViewport_.contains(lastMouse_)) {
            return;
        }
        const VtkFrame& frame = displayFrame();
        const std::optional<VtkPixelSample> sample = sampleVtkPixel(
            frame,
            resultImageTransform(frame),
            lastMouse_.x,
            lastMouse_.y);
        if (!sample) {
            return;
        }

        const auto scalarText = [](float value, bool finite,
                                   const std::string& unit) {
            return finite ? formatValue(value, false, unit) : "non-finite";
        };
        std::ostringstream value;
        value << "Pixel X: " << sample->x << "   Y: " << sample->y << '\n'
              << "Position x: "
              << formatValue(sample->physicalX, false, "m")
              << "   y: "
              << formatValue(sample->physicalY, false, "m") << '\n';
        if (sample->solid) {
            value << "u: n/a   v: n/a\n"
                  << "Speed: n/a (solid)\nPressure: n/a (solid)";
        } else {
            value << "u: "
                  << scalarText(
                         sample->velocityX,
                         sample->speedFinite,
                         "m/s")
                  << "   v: "
                  << scalarText(
                         sample->velocityY,
                         sample->speedFinite,
                         "m/s")
                  << "\nSpeed: "
                  << scalarText(sample->speed, sample->speedFinite, "m/s")
                  << "\nPressure: "
                  << scalarText(
                         sample->pressure,
                         sample->pressureFinite,
                         "Pa");
        }

        if (sliceCache_.slicing()) {
            value << "\nSlice " << sliceAxisName() << ' '
                  << (sliceIndex_ + 1) << '/' << slicePlaneCount();
        }

        constexpr float tooltipWidth = 270.0f;
        const float tooltipHeight = sliceCache_.slicing() ? 126.0f : 110.0f;
        sf::Vector2f position = lastMouse_ + sf::Vector2f{14.0f, 14.0f};
        position.x = std::clamp(
            position.x,
            4.0f,
            static_cast<float>(layoutSize_.x) - tooltipWidth - 4.0f);
        position.y = std::clamp(
            position.y,
            52.0f,
            static_cast<float>(layoutSize_.y) - tooltipHeight - 28.0f);

        sf::RectangleShape background({tooltipWidth, tooltipHeight});
        background.setPosition(position);
        background.setFillColor(OVERLAY_BACKGROUND);
        background.setOutlineColor(ACCENT);
        background.setOutlineThickness(1.0f);
        window_->draw(background);
        window_->draw(makeText(
            font_,
            value.str(),
            12,
            position + sf::Vector2f{9.0f, 7.0f},
            TEXT));
    }

    void drawVelocityVectors(
        const VtkFrame& frame,
        sf::Vector2f origin,
        float cellWidth,
        float cellHeight) {
        // Always the velocity range, whatever the colours happen to be
        // showing, and trimmed when the colours are: that shortens the handful
        // of arrows which would otherwise reach across the picture and leave
        // every other one too small to see.
        const DataRange reference = velocityDisplayRange();
        const double referenceMagnitude =
            reference.available ? std::max(0.0, reference.maximum) : 0.0;
        const std::vector<VelocityArrow> arrows =
            velocityOverlayPlanner_.plan(
                frame,
                cellWidth,
                cellHeight,
                referenceMagnitude);
        sf::VertexArray vertices{sf::PrimitiveType::Triangles};
        vertices.resize(arrows.size() * 9u);
        std::size_t vertexIndex = 0;
        const sf::Color color{245, 248, 252, 230};

        for (const VelocityArrow& arrow : arrows) {
            const sf::Vector2f direction{
                static_cast<float>(arrow.unitX),
                static_cast<float>(-arrow.unitY)
            };
            const sf::Vector2f perpendicular{
                -direction.y,
                direction.x
            };
            const sf::Vector2f center{
                origin.x +
                    (static_cast<float>(arrow.i) + 0.5f) * cellWidth,
                origin.y +
                    (static_cast<float>(frame.ny - 1 - arrow.j) + 0.5f) *
                        cellHeight
            };
            const float length =
                10.0f +
                18.0f * std::sqrt(
                    static_cast<float>(arrow.relativeMagnitude));
            const sf::Vector2f tail =
                center - direction * (length * 0.5f);
            const sf::Vector2f tip =
                center + direction * (length * 0.5f);
            const float headLength = std::min(7.0f, length * 0.36f);
            const float headHalfWidth = headLength * 0.62f;
            const sf::Vector2f shaftTip =
                tip - direction * (headLength * 0.58f);
            const sf::Vector2f shaftOffset = perpendicular * 1.25f;
            const sf::Vector2f headLeft =
                tip - direction * headLength +
                perpendicular * headHalfWidth;
            const sf::Vector2f headRight =
                tip - direction * headLength -
                perpendicular * headHalfWidth;
            const std::array<sf::Vector2f, 9> points{{
                tail + shaftOffset,
                tail - shaftOffset,
                shaftTip - shaftOffset,
                tail + shaftOffset,
                shaftTip - shaftOffset,
                shaftTip + shaftOffset,
                tip,
                headLeft,
                headRight
            }};
            const bool inside = std::all_of(
                points.begin(),
                points.end(),
                [this](sf::Vector2f point) {
                    return resultViewport_.contains(point);
                });
            if (!inside) {
                continue;
            }
            for (const sf::Vector2f point : points) {
                vertices[vertexIndex++] = sf::Vertex{point, color};
            }
        }
        vertices.resize(vertexIndex);
        window_->draw(vertices);
    }

    void drawLegend() {
        const DataRange range = resultDisplayRange();
        const int strips = 120;
        for (int strip = 0; strip < strips; ++strip) {
            const double normalized =
                static_cast<double>(strip) /
                static_cast<double>(strips - 1);
            sf::RectangleShape rectangle({
                legendBounds_.size.x,
                legendBounds_.size.y / static_cast<float>(strips) + 1.0f
            });
            rectangle.setPosition({
                legendBounds_.position.x,
                legendBounds_.position.y +
                    legendBounds_.size.y *
                        static_cast<float>(1.0 - normalized)
            });
            rectangle.setFillColor(
                (resultQuantity_ == ResultQuantity::Scalar &&
                 activeScalarName_ == "phase")
                    ? phaseColour(static_cast<float>(normalized))
                    : scalarColor(normalized, 0.0, 1.0));
            window_->draw(rectangle);
        }

        if (resultQuantity_ == ResultQuantity::Scalar &&
            activeScalarName_ == "phase") {
            window_->draw(makeText(
                font_, "fluid 1", 12,
                {legendBounds_.position.x - 4.0f,
                 legendBounds_.position.y - 16.0f}, MUTED));
            window_->draw(makeText(
                font_, "fluid 2", 12,
                {legendBounds_.position.x - 4.0f,
                 legendBounds_.position.y + legendBounds_.size.y + 2.0f},
                MUTED));
        }

        const std::string unit =
            resultQuantity_ == ResultQuantity::Scalar
                ? std::string()
                : resultQuantity_ == ResultQuantity::Pressure ? "Pa" : "m/s";
        window_->draw(makeText(
            font_,
            resultQuantity_ == ResultQuantity::Scalar
                ? activeScalarName_
                : resultQuantity_ == ResultQuantity::Pressure
                      ? "Pressure"
                      : "Velocity",
            14,
            {legendBounds_.position.x - 18.0f,
             legendBounds_.position.y - 28.0f}));
        if (range.available) {
            window_->draw(makeText(
                font_,
                formatValue(range.maximum, false, unit),
                11,
                {legendBounds_.position.x - 18.0f,
                 legendBounds_.position.y - 14.0f},
                MUTED));
            window_->draw(makeText(
                font_,
                formatValue(range.minimum, false, unit),
                11,
                {
                    legendBounds_.position.x - 18.0f,
                    legendBounds_.position.y +
                        legendBounds_.size.y + 5.0f
                },
                MUTED));
        }
    }

    void drawResultSliders() {
        if (!view3D_) {
            drawSimpleTrack(
                zoomTrack_,
                static_cast<float>(
                    std::log(resultZoom_ / 0.5f) / std::log(16.0)),
                "Zoom " + formatValue(resultZoom_, false, "x"));
        }
        const std::size_t displayedFrame =
            desiredFrame_.value_or(selectedFrame_);
        const float frameFraction =
            frames_.size() <= 1
                ? 0.0f
                : static_cast<float>(displayedFrame) /
                      static_cast<float>(frames_.size() - 1);
        drawSimpleTrack(
            frameTrack_,
            frameFraction,
            "Saved frame " +
                std::to_string(displayedFrame + 1) + "/" +
                std::to_string(frames_.size()) + " (solver step " +
                std::to_string(frames_[displayedFrame].frameNumber) + ")");
    }

    void drawResultWarning() {
        if (resultsWarning_.empty()) {
            return;
        }
        sf::RectangleShape banner({
            resultViewport_.size.x,
            34.0f
        });
        banner.setPosition(resultViewport_.position);
        banner.setFillColor(WARNING_BACKGROUND);
        banner.setOutlineColor(WARNING_OUTLINE);
        banner.setOutlineThickness(1.0f);
        window_->draw(banner);

        std::string display = resultsWarning_;
        if (display.size() > 150) {
            display.resize(147);
            display += "...";
        }
        window_->draw(makeText(
            font_,
            display,
            12,
            {
                resultViewport_.position.x + 8.0f,
                resultViewport_.position.y + 8.0f
            },
            WARNING_TEXT));
    }

    void drawSimpleTrack(
        const sf::FloatRect& track,
        float fraction,
        const std::string& label) {
        fraction = clampFloat(fraction, 0.0f, 1.0f);
        window_->draw(makeText(
            font_,
            label,
            12,
            {track.position.x, track.position.y - 22.0f},
            MUTED));
        sf::RectangleShape rail(track.size);
        rail.setPosition(track.position);
        rail.setFillColor(CONTROL_RAIL);
        window_->draw(rail);
        sf::CircleShape handle(7.0f);
        handle.setOrigin({7.0f, 7.0f});
        handle.setPosition({
            track.position.x + track.size.x * fraction,
            track.position.y + track.size.y / 2.0f
        });
        handle.setFillColor(ACCENT);
        window_->draw(handle);
    }

    void drawArea(const sf::FloatRect& bounds, const sf::Color& fill) {
        sf::RectangleShape area(bounds.size);
        area.setPosition(bounds.position);
        area.setFillColor(fill);
        window_->draw(area);
    }

    void drawDivider(float x, float y, float width, float height) {
        sf::RectangleShape line({width, height});
        line.setPosition({x, y});
        line.setFillColor(BORDER);
        window_->draw(line);
    }

    void drawAreas() {
        const float width = static_cast<float>(layoutSize_.x);
        const float height = static_cast<float>(layoutSize_.y);
        drawArea({{0.0f, 0.0f}, {width, HEADER_HEIGHT}}, HEADER);
        drawDivider(0.0f, HEADER_HEIGHT - 1.0f, width, 1.0f);
        drawArea(
            {{panelX_, HEADER_HEIGHT},
             {width - panelX_, height - HEADER_HEIGHT}},
            PANEL);
        drawDivider(panelX_, HEADER_HEIGHT, 1.0f, height - HEADER_HEIGHT);
        const sf::FloatRect outliner = outlinerBounds();
        drawDivider(
            outliner.position.x,
            outliner.position.y + outliner.size.y,
            outliner.size.x,
            1.0f);
        drawArea({{0.0f, height - 108.0f}, {panelX_, 84.0f}}, HEADER);
        drawDivider(0.0f, height - 108.0f, panelX_, 1.0f);
    }

    void drawTopTabs() {
        setupTab_.draw(*window_, font_, lastMouse_);
        resultsTab_.draw(*window_, font_, lastMouse_);
        openVtkButton_.draw(*window_, font_, lastMouse_);
        stopSimulationButton_.draw(*window_, font_, lastMouse_);
        revealVtkButton_.draw(*window_, font_, lastMouse_);
        solverExeButton_.draw(*window_, font_, lastMouse_);
        importButton_.draw(*window_, font_, lastMouse_);
        outputFolderButton_.draw(*window_, font_, lastMouse_);
        drawOutliner();
    }

    void rebuildOutliner() {
        outlinerRows_.clear();
        const auto split = [](const std::string& line) {
            std::vector<std::string> parts;
            std::size_t at = 0;
            while (at <= line.size()) {
                const std::size_t mark = line.find(';', at);
                const std::string piece = line.substr(
                    at, mark == std::string::npos ? std::string::npos
                                                  : mark - at);
                if (!piece.empty())
                    parts.push_back(piece);
                if (mark == std::string::npos)
                    break;
                at = mark + 1;
            }
            return parts;
        };
        const std::size_t bodies = std::max<std::size_t>(solidBodyCount_, 1u);
        for (std::size_t body = 1; body <= bodies; ++body) {
            OutlinerRow row;
            row.label = "Body " + std::to_string(body);
            const std::string travel = motionEntryOf(
                sliders_[BodyMotionLine].text, static_cast<int>(body));
            const std::string wall = motionEntryOf(
                sliders_[WallMotionLine].text, static_cast<int>(body));
            if (!travel.empty())
                row.label += motionSetting(travel, "free", 0.0) >= 0.5
                    ? "  free" : "  travel";
            else if (!wall.empty())
                row.label += motionSetting(wall, "slip", 0.0) >= 0.5
                    ? "  slip" : "  drag";
            row.row = BodySelect;
            row.body = static_cast<int>(body);
            outlinerRows_.push_back(row);
        }
        std::size_t number = 1;
        for (const std::string& source : split(sliders_[SourceLine].text)) {
            OutlinerRow row;
            row.label = "Source " + std::to_string(number++) + "  " + source;
            row.row = SourceLine;
            outlinerRows_.push_back(row);
        }
        number = 1;
        for (const std::string& mic : split(sliders_[MicrophoneLine].text)) {
            OutlinerRow row;
            row.label = "Microphone " + std::to_string(number++) + "  " + mic;
            row.row = MicrophoneLine;
            outlinerRows_.push_back(row);
        }
        const int sides = volumeRun() ? 6 : 4;
        for (int side = 0; side < sides; ++side) {
            const std::size_t kind = boundaryKindRow(side);
            OutlinerRow row;
            row.label = sliders_[kind].label + "  " + sliders_[kind].choice();
            row.row = kind;
            outlinerRows_.push_back(row);
        }
    }

    sf::FloatRect outlinerBounds() const {
        return {{panelX_, OUTLINER_TOP}, {LEFT_PANEL_WIDTH, OUTLINER_HEIGHT}};
    }

    void drawOutliner() {
        const sf::FloatRect bounds = outlinerBounds();
        window_->draw(makeText(
            font_, "SCENE", 11,
            {bounds.position.x + 20.0f, bounds.position.y + 6.0f}, ACCENT));
        const float rowHeight = 17.0f;
        const float top = bounds.position.y + 24.0f;
        const float visible = bounds.size.y - 28.0f;
        const std::size_t first = static_cast<std::size_t>(
            std::max(0.0f, outlinerScroll_) / rowHeight);
        for (std::size_t index = first; index < outlinerRows_.size(); ++index) {
            const float y = top +
                static_cast<float>(index) * rowHeight - outlinerScroll_;
            if (y < top - rowHeight)
                continue;
            if (y > top + visible - rowHeight)
                break;
            const bool selected =
                focusedSlider_.has_value() &&
                *focusedSlider_ == outlinerRows_[index].row &&
                (outlinerRows_[index].body == 0 ||
                 outlinerRows_[index].body == selectedBody());
            if (selected) {
                sf::RectangleShape band(
                    {bounds.size.x - 24.0f, rowHeight - 1.0f});
                band.setPosition({bounds.position.x + 12.0f, y});
                band.setFillColor(ACCENT_DARK);
                window_->draw(band);
            }
            std::string label = outlinerRows_[index].label;
            if (label.size() > 42) {
                label.resize(39);
                label += "...";
            }
            window_->draw(makeText(
                font_, label, 12,
                {bounds.position.x + 20.0f, y + 1.0f},
                selected ? TEXT : MUTED));
        }
    }

    bool handleOutlinerClick(sf::Vector2f position) {
        const sf::FloatRect bounds = outlinerBounds();
        if (!bounds.contains(position))
            return false;
        const float rowHeight = 17.0f;
        const float top = bounds.position.y + 24.0f;
        if (position.y < top)
            return true;
        const std::size_t index = static_cast<std::size_t>(
            (position.y - top + outlinerScroll_) / rowHeight);
        if (index >= outlinerRows_.size())
            return true;
        const OutlinerRow& row = outlinerRows_[index];
        if (row.body > 0) {
            sliders_[BodySelect].value = row.body;
            loadBodyRows();
        }
        focusedSlider_ = row.row;
        scrollRowIntoView(row.row);
        status_ = row.label + " selected.";
        return true;
    }



    void drawLoadingIndicator() {
        std::string label;
        if (solverProcess_.active) {
            label = "SIMULATION RUNNING";
        } else if (resultCatalogFuture_.valid()) {
            label = "INDEXING VTK";
        } else if (!inFlightFrames_.empty()) {
            label = desiredFrame_
                        ? "LOADING VTK"
                        : "PREFETCHING VTK";
        } else {
            return;
        }

        constexpr float width = 204.0f;
        constexpr float height = 38.0f;
        const sf::Vector2f position{
            std::max(8.0f, panelX_ - width - 12.0f),
            std::max(58.0f, static_cast<float>(layoutSize_.y) - height - 118.0f)
        };
        sf::RectangleShape background({width, height});
        background.setPosition(position);
        background.setFillColor(OVERLAY_BACKGROUND);
        background.setOutlineColor(BORDER);
        background.setOutlineThickness(1.0f);
        window_->draw(background);

        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const std::size_t activeDot =
            static_cast<std::size_t>((elapsed / 100) % 8);
        const sf::Vector2f center = position + sf::Vector2f{22.0f, 19.0f};
        for (std::size_t dot = 0; dot < 8; ++dot) {
            const double angle =
                static_cast<double>(dot) * PI / 4.0 - PI / 2.0;
            sf::CircleShape circle(dot == activeDot ? 2.8f : 2.1f);
            const float radius = circle.getRadius();
            circle.setOrigin({radius, radius});
            circle.setPosition(center + sf::Vector2f{
                static_cast<float>(std::cos(angle) * 9.0),
                static_cast<float>(std::sin(angle) * 9.0)
            });
            circle.setFillColor(
                dot == activeDot ? ACCENT : sf::Color{90, 90, 90, 150});
            window_->draw(circle);
        }
        window_->draw(makeText(
            font_,
            label,
            12,
            position + sf::Vector2f{42.0f, 10.0f},
            TEXT));
    }

    // A bar across the bottom of the status strip while a run is going. It is
    // drawn from the simulated time the solver has printed, not from the frame
    // count: frames arrive every saveInterval steps and dt moves, so counting
    // them tells you almost nothing about how much is left.
    void drawRunProgressBar(float stripTop) {
        if (!solverProcess_.active) {
            return;
        }
        const float width = static_cast<float>(layoutSize_.x);
        const float height = 4.0f;
        const float y = stripTop - height;

        sf::RectangleShape rail({width, height});
        rail.setPosition({0.0f, y});
        rail.setFillColor(CONTROL_RAIL);
        window_->draw(rail);

        if (runProgress_ >= 0.0) {
            sf::RectangleShape fill({
                width * static_cast<float>(std::clamp(runProgress_, 0.0, 1.0)),
                height
            });
            fill.setPosition({0.0f, y});
            fill.setFillColor(ACCENT);
            window_->draw(fill);
            return;
        }

        // The solver has not said how far along it is - an older build, or the
        // first half-second of a new one. A sliver sliding along says "running"
        // without claiming to know more than that.
        const float sliver = std::max(60.0f, width * 0.08f);
        const float travel = width + sliver;
        const float phase = std::fmod(
            static_cast<float>(
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count()) *
                0.45f,
            1.0f);
        sf::RectangleShape marquee({sliver, height});
        marquee.setPosition({phase * travel - sliver, y});
        marquee.setFillColor(ACCENT_DARK);
        window_->draw(marquee);
    }

    void drawStatus() {
        const float y =
            static_cast<float>(layoutSize_.y) - 24.0f;
        drawRunProgressBar(y);
        if (status_.empty()) {
            return;
        }
        sf::RectangleShape background({
            static_cast<float>(layoutSize_.x),
            24.0f
        });
        background.setPosition({0.0f, y});
        background.setFillColor(HEADER);
        window_->draw(background);
        drawDivider(0.0f, y, static_cast<float>(layoutSize_.x), 1.0f);

        std::string display = status_;
        if (display.size() > 180) {
            display.resize(177);
            display += "...";
        }
        window_->draw(makeText(
            font_,
            display,
            12,
            {10.0f, y + 3.0f},
            MUTED));
        if (mode_ == DisplayMode::Results && view3D_ && !pickText_.empty()) {
            sf::Text under = makeText(
                font_, pickText_, 12, {0.0f, y + 3.0f}, TEXT);
            under.setOrigin({under.getLocalBounds().size.x, 0.0f});
            under.setPosition({panelX_ - 10.0f, y + 3.0f});
            window_->draw(under);
        }
    }

    std::filesystem::path executablePath_;
    std::filesystem::path initialModelPath_;
    std::filesystem::path solverSelectionFile_;
    std::filesystem::path preferencesFile_;
    std::filesystem::path fluidSolverExecutable_;
    std::filesystem::path outputRoot_;
    SolverExecutableInfo solverInfo_;
    sf::RenderWindow* window_ = nullptr;
    sf::Font font_;
    GeometryProcessor geometry_;
    std::array<Slider, ParameterCount> sliders_;
    std::optional<std::size_t> editingSlider_;
    std::string sliderEditText_;
    std::optional<std::size_t> lastValueClickSlider_;
    std::optional<std::size_t> invalidSlider_;
    std::chrono::steady_clock::time_point lastValueClickTime_{};
    bool invertSection_ = false;
    // How many bodies the last generated mask had. wallMotion is written for
    // 1..this, and a continuation reuses it because the frame it continues
    // does not carry the number.
    std::size_t solidBodyCount_ = 1;

    DisplayMode mode_ = DisplayMode::Setup;
    ResultQuantity resultQuantity_ = ResultQuantity::Pressure;

    std::string activeScalarName_;
    std::vector<std::uint8_t> scalarFinite_;
    Button setupTab_{"Setup"};
    Button resultsTab_{"Results"};
    Button openVtkButton_{"Open frames"};
    Button stopSimulationButton_{"Stop simulation"};
    Button revealVtkButton_{"Show output folder"};
    Button solverExeButton_{"Select solver"};
    Button importButton_{"Import STL / OBJ"};
    Button outputFolderButton_{"Output folder"};
    Button resetDefaultsButton_{"Reset defaults"};
    Button saveConfigButton_{"Save config"};
    Button loadConfigButton_{"Load config"};
    Button generateButton_{"Run simulation"};
    Button pressureButton_{"Pressure"};
    Button velocityButton_{"Velocity"};
    Button continueRunButton_{"Continue run"};
    Button fieldButton_{"Field"};
    Button vectorButton_{"Vectors: Off"};
    Button rangeButton_{"Range: Series"};
    Button playbackButton_{"Play"};
    Button runDetailsButton_{"Run details"};
    Button recoverSetupButton_{"Recover setup"};
    Button viewModeButton_{"View: 2D"};
    struct OutlinerRow {
        std::string label;
        std::size_t row = ParameterCount;
        int body = 0;
    };
    std::vector<OutlinerRow> outlinerRows_;
    float outlinerScroll_ = 0.0f;
    std::array<Button, ViewControlCount> viewControls_;
    std::array<sf::FloatRect, ViewTrackCount> viewTracks_{};
    std::optional<std::size_t> draggingViewTrack_;
    Viewport3D viewport3D_;
    Viewport3DSettings view3DSettings_;
    int viewDefaultsFor_ = -1;
    bool moveTool_ = false;
    bool dragSlides_ = false;
    int transformMode_ = 0;
    int transformAxis_ = 0;
    std::string transformText_;
    bool view3D_ = false;
    SliceCache sliceCache_;
    SliceAxis sliceAxis_ = SliceAxis::Z;
    std::size_t sliceIndex_ = 0;
    bool orbiting3D_ = false;
    bool orbitMoved_ = false;
    bool panning3D_ = false;
    std::string pickText_;
    float panelX_ = 0.0f;
    float resultBarBottom_ = 0.0f;

    sf::FloatRect setupViewport_{{0.0f, 0.0f}, {1.0f, 1.0f}};
    sf::FloatRect resultViewport_{{0.0f, 0.0f}, {1.0f, 1.0f}};
    sf::FloatRect legendBounds_{{0.0f, 0.0f}, {1.0f, 1.0f}};
    sf::FloatRect zoomTrack_{{0.0f, 0.0f}, {1.0f, 1.0f}};
    sf::FloatRect frameTrack_{{0.0f, 0.0f}, {1.0f, 1.0f}};
    sf::Vector2u layoutSize_{0u, 0u};
    float parameterScrollOffset_ = 0.0f;
    float maxParameterScroll_ = 0.0f;
    bool draggingParameterScrollbar_ = false;
    float parameterScrollbarGrabOffset_ = 0.0f;
    std::optional<std::size_t> activeSlider_;
    bool draggingHorizontalSlice_ = false;
    bool draggingVerticalSlice_ = false;
    bool rotatingObject_ = false;
    bool rotatingRoll_ = false;
    bool draggingZoom_ = false;
    bool draggingFrame_ = false;
    bool panningResults_ = false;
    sf::Vector2f lastMouse_{0.0f, 0.0f};

    float setupZoom_ = 1.0f;

    std::vector<Snapshot> undoStack_;
    std::vector<Snapshot> redoStack_;

    std::optional<std::size_t> focusedSlider_;
    bool searchActive_ = false;
    std::string searchQuery_;
    std::size_t activeTab_ = 0;
    std::array<Button, PARAMETER_TABS.size()> tabButtons_;

    bool layoutMode_ = false;
    int layoutSelected_ = 0;
    int layoutObjects_ = 0;
    double layoutTime_ = 0.0;
    bool layoutDragging_ = false;
    bool layoutRotating_ = false;
    bool draggingLayoutTime_ = false;
    std::vector<int> layoutOwner_;
    int layoutNx_ = 0;
    int layoutNy_ = 0;
    std::vector<double> layoutCentreX_;
    std::vector<double> layoutCentreY_;
    std::string layoutSignature_;
    Button layoutButton_;
    Button layoutKeyButton_;
    Button layoutDropButton_;
    Button layoutInterpButton_;
    Button layoutClearButton_;
    std::size_t layoutInterp_ = 0;
    std::size_t layoutEase_ = 0;

    bool painting_ = false;
    int paintBrush_ = 3;
    int paintTarget_ = 0;
    int paintNx_ = 0;
    int paintNy_ = 0;
    std::vector<float> paintField_;
    std::vector<std::vector<float>> paintUndo_;
    bool paintStroke_ = false;
    Button paintButton_;
    Button paintFluid1Button_;
    Button paintFluid2Button_;
    Button paintSourceButton_;
    Button paintFillButton_;
    Button paintClearButton_;
    Button paintUndoButton_;
    float resultZoom_ = 1.0f;
    sf::Vector2f resultPan_{0.0f, 0.0f};
    std::vector<SectionSegment> sectionSegments_;
    double sectionSegmentsSliceX_ =
        std::numeric_limits<double>::quiet_NaN();
    double sectionSegmentsSliceZ_ =
        std::numeric_limits<double>::quiet_NaN();

    ChildProcess solverProcess_;
    std::filesystem::path currentRunDirectory_;
    std::chrono::steady_clock::time_point nextSolverProgressUpdate_{};
    bool currentRunRequiresComputedFrame_ = false;
    bool currentRunIsContinuation_ = false;
    int continuationSourceStep_ = -1;

    // ---- the tray, and the progress that goes in it ------------------------
    TrayIcon tray_;
    std::string windowTitle_;
    std::string publishedTitle_;
    std::string runProgressText_;
    // Negative means "running, but how far along is not known" - which is what
    // a solver too old to print its simulated time leaves us with.
    double runProgress_ = -1.0;
    double runStartSeconds_ = 0.0;    // where this run began; not 0 on a
                                      // continuation
    double runTargetSeconds_ = 0.0;   // where it is heading
    double runElapsedSeconds_ = 0.0;
    bool windowHidden_ = false;
    std::string visibilitySignature_;
    std::array<bool, ParameterCount> rowHidden_{};
    std::array<bool, PARAMETER_GROUPS.size()> groupShown_{};
    std::array<std::size_t, PARAMETER_GROUPS.size()> groupFirstShown_{};
    std::array<float, PARAMETER_GROUPS.size()> groupHeaderY_{};

    std::vector<std::uint8_t> previewSolid_;
    std::size_t previewNx_ = 0;
    std::size_t previewNy_ = 0;
    std::vector<VtkFrameDescriptor> frames_;
    std::shared_ptr<const VtkFrame> activeFrame_;
    std::size_t selectedFrame_ = 0;
    std::optional<std::size_t> desiredFrame_;
    std::deque<std::size_t> prefetchQueue_;
    std::vector<std::size_t> adaptiveWindowIndices_;
    std::size_t adaptiveWindowDivisor_ = 1;
    // Frames decode on several threads at once. With one at a time - which is
    // what a single future meant - flipping quickly outran the loader and every
    // step past the cached few waited for the whole file to be read again.
    struct InFlightFrame {
        std::size_t index = 0;
        std::future<std::shared_ptr<VtkFrame>> future;
        std::chrono::steady_clock::time_point started{};
    };
    std::vector<InFlightFrame> inFlightFrames_;
    std::future<VtkSeriesCatalog> resultCatalogFuture_;
    std::optional<ResultOrigin> pendingResultOrigin_;
    std::chrono::steady_clock::time_point resultCatalogStarted_{};
    DecodedFrameCache decodedFrameCache_{DECODED_FRAME_CACHE_BYTES};
    DataRange pressureRange_;
    DataRange velocityRange_;
    // The same two, with the outermost half-percent of each frame left out.
    DataRange pressureTrimmedRange_;
    DataRange velocityTrimmedRange_;
    VelocityOverlayPlanner velocityOverlayPlanner_;
    bool showVelocityVectors_ = false;
    bool useSeriesRange_ = true;
    bool trimmedRange_ = true;
    bool playingFrames_ = false;
    float playbackAccumulator_ = 0.0f;
    bool showRunDetails_ = false;
    std::string runDetailsText_;
    sf::Texture resultTexture_;
    // Scratch for the colour map, reused rather than reallocated.
    std::vector<std::uint8_t> resultPixels_;
    bool resultTextureCacheValid_ = false;
    std::string resultsWarning_;
    std::string status_ =
        "Import a model or drop solver solution VTK frames. Double-click "
        "a parameter value to type it. Input is disabled while unfocused.";
};

Application::Application(
    std::filesystem::path executablePath,
    std::filesystem::path initialModelPath)
    : implementation_(
          std::make_unique<Implementation>(
              std::move(executablePath),
              std::move(initialModelPath))) {
}

Application::~Application() = default;

int Application::run() {
    return implementation_->run();
}

} // namespace maskui
