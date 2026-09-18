#include "Config.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
std::string toLower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// --- values, wherever they come from ---------------------------------------
// strtof and atoi report nothing at all: "nu=0,002" quietly became 0.0 and the
// run went on inviscid, "useCuda=true" turned CUDA off. std::cin >> value was
// worse: it set failbit, left the junk in the buffer and every prompt after it
// answered itself. Everything below refuses the value instead and says how it
// should have been written.

constexpr double kTiny = 1e-30;            // "anything except zero"
constexpr double kHuge = 1e30;             // "no upper limit worth naming"
constexpr double kIntMax = 2147483647.0;

// Every key the command line, the prompts and the frame header accept, in the
// spelling print() and serialize() use. Keep in sync with printUsage().
const char* const kKeys[] = {
    "Lx", "Ly", "Lz", "nx", "ny", "nz", "U0", "nu", "ro",
    "gravityEnabled", "gravityAccel", "gravityAngle", "gravityTilt",
    "CFL", "totalTime", "dtUpdateInterval", "dtSafety",
    "omega", "smootherOmega",
    "mgIterations", "mgTolerance", "mgMinCoarseSize",
    "useCuda", "saveInterval", "outputDir", "extraFields", "frameState",
    "runName", "vortices",
    "geometryFile", "sliceAngleX", "sliceAngleY", "sliceAngleZ",
    "sliceRotation",
    "invertSection", "wallMotion", "profiles",
    "bodyMotion", "bodyCoupling", "bodyIterations",
    "bodyCollisions", "bodyRestitution", "bodyForceReport",
    "turbulence", "Cs", "turbIntensity", "turbLengthScale",
    "regime", "gamma", "R", "gamma2", "R2", "T0", "pInf", "machInlet",
    "speciesMode", "acousticFields", "acousticWindow", "acousticRef",
    "microphones", "micInterval", "micAudio", "micAudioRate",
    "micAudioSpeed", "gridStretch", "stretchRatio", "refineNear",
    "amrLevels", "amrEvery", "amrThreshold", "amrBuffer", "amrMinPatch",
    "amrMaxPatch", "amrCriterion",
    "restart", "restartFile", "addTime",
    "gravityMode", "convection", "limiter", "timeScheme",
    "caseType", "lidSpeed", "steadyTolerance",
    "bcLeft", "bcRight", "bcBottom", "bcTop", "bcFront", "bcBack",
    "bcLeftSpeed", "bcRightSpeed", "bcBottomSpeed", "bcTopSpeed",
    "bcFrontSpeed", "bcBackSpeed",
    "inletFrom", "inletTo", "inletFrom2", "inletTo2", "inletProfile",
    "phases", "rho1", "rho2", "nu1", "nu2",
    "phaseInit", "phaseLevel", "phaseX", "phaseY", "phaseZ",
    "initialPhaseFile", "vofScheme", "sources",
    "mixing", "diffusivity", "surfaceTension", "contactAngle",
};

std::string trimSpace(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// argv on Windows can keep the quotes the shell did not eat, and a path typed
// into a prompt usually arrives with them too.
std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

std::string cleanValue(const std::string& raw) {
    return trimSpace(stripQuotes(trimSpace(raw)));
}

std::string badValue(const std::string& key,
                     const std::string& value,
                     const std::string& why) {
    return "not a right way to write " + key + "=" + value + ": " + why;
}

bool parseNumber(const std::string& key, const std::string& raw, bool integer,
                 double& out, std::string& error) {
    const std::string text = cleanValue(raw);

    if (text.empty()) {
        error = badValue(key, text,
                         "the value is missing, write " + key + "=<number>");
        return false;
    }
    if (text.find(',') != std::string::npos) {
        std::string dotted = text;
        std::replace(dotted.begin(), dotted.end(), ',', '.');
        error = badValue(key, text,
                         "the decimal separator is a dot, write " + key + "=" +
                         dotted);
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);

    if (end == text.c_str()) {
        error = badValue(key, text, "that is not a number");
        return false;
    }
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (*end != '\0') {
        error = badValue(key, text,
                         std::string("'") + end + "' is stuck to the number; "
                         "units and extra characters are not part of the value");
        return false;
    }
    if (errno == ERANGE || !std::isfinite(parsed)) {
        error = badValue(key, text, "that number is out of range");
        return false;
    }
    if (integer) {
        double whole = 0.0;
        if (std::modf(parsed, &whole) != 0.0) {
            error = badValue(key, text,
                             key + " counts things, so it has to be a whole "
                             "number");
            return false;
        }
        if (parsed < -kIntMax || parsed > kIntMax) {
            error = badValue(key, text, "that number is out of range");
            return false;
        }
    }
    out = parsed;
    return true;
}

bool inRange(const std::string& key, const std::string& raw, double value,
             double lo, double hi, const char* rule, std::string& error) {
    if (value >= lo && value <= hi)
        return true;
    error = badValue(key, cleanValue(raw), rule);
    return false;
}

bool assignFloat(float& target, const std::string& key, const std::string& raw,
                 double lo, double hi, const char* rule, std::string& error) {
    double v = 0.0;
    if (!parseNumber(key, raw, false, v, error)) return false;
    if (!inRange(key, raw, v, lo, hi, rule, error)) return false;
    target = static_cast<float>(v);
    return true;
}

bool assignDouble(double& target, const std::string& key, const std::string& raw,
                  double lo, double hi, const char* rule, std::string& error) {
    double v = 0.0;
    if (!parseNumber(key, raw, false, v, error)) return false;
    if (!inRange(key, raw, v, lo, hi, rule, error)) return false;
    target = v;
    return true;
}

bool assignInt(int& target, const std::string& key, const std::string& raw,
               double lo, double hi, const char* rule, std::string& error) {
    double v = 0.0;
    if (!parseNumber(key, raw, true, v, error)) return false;
    if (!inRange(key, raw, v, lo, hi, rule, error)) return false;
    target = static_cast<int>(v);
    return true;
}

bool assignBool(bool& target, const std::string& key, const std::string& raw,
                std::string& error) {
    const std::string text = toLower(cleanValue(raw));
    if (text == "1" || text == "true" || text == "yes" || text == "on") {
        target = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "no" || text == "off") {
        target = false;
        return true;
    }
    error = badValue(key, cleanValue(raw),
                     key + " is a switch, write " + key + "=1 or " + key +
                     "=0 (true/false, yes/no and on/off work too)");
    return false;
}

int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        prev.swap(cur);
    }
    return prev[b.size()];
}
}

namespace {

struct EnumEntry {
    const char* name;
    int value;
};

bool assignEnumValue(int& target,
                     const std::string& key,
                     const std::string& value,
                     const std::vector<EnumEntry>& entries,
                     std::string& error) {
    std::string wanted = trimSpace(cleanValue(value));
    for (char& c : wanted)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const EnumEntry& entry : entries) {
        std::string name = entry.name;
        for (char& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name == wanted) {
            target = entry.value;
            return true;
        }
    }

    std::string list;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i)
            list += (i + 1 == entries.size()) ? " or " : ", ";
        list += entries[i].name;
    }
    error = badValue(key, cleanValue(value),
                     "this one takes a name, not a number: " + list);
    return false;
}

const char* enumName(int value, const std::vector<EnumEntry>& entries) {
    for (const EnumEntry& entry : entries)
        if (entry.value == value)
            return entry.name;
    return "?";
}

const std::vector<EnumEntry> kGravityModes{
    {"reduced", static_cast<int>(GravityMode::Reduced)},
    {"body", static_cast<int>(GravityMode::Body)}};

const std::vector<EnumEntry> kConvectionSchemes{
    {"upwind", static_cast<int>(ConvectionScheme::Upwind)},
    {"central", static_cast<int>(ConvectionScheme::Central)},
    {"muscl", static_cast<int>(ConvectionScheme::Muscl)}};

const std::vector<EnumEntry> kLimiters{
    {"minmod", static_cast<int>(LimiterKind::Minmod)},
    {"vanleer", static_cast<int>(LimiterKind::VanLeer)},
    {"superbee", static_cast<int>(LimiterKind::Superbee)}};

const std::vector<EnumEntry> kMixingKinds{
    {"immiscible", static_cast<int>(MixingKind::Immiscible)},
    {"miscible", static_cast<int>(MixingKind::Miscible)}};

const std::vector<EnumEntry> kVofSchemes{
    {"upwind", static_cast<int>(VofScheme::Upwind)},
    {"hric", static_cast<int>(VofScheme::Hric)},
    {"cicsam", static_cast<int>(VofScheme::Cicsam)}};

const std::vector<EnumEntry> kPhaseInits{
    {"layer", static_cast<int>(PhaseInit::Layer)},
    {"drop", static_cast<int>(PhaseInit::Drop)},
    {"column", static_cast<int>(PhaseInit::Column)},
    {"file", static_cast<int>(PhaseInit::File)}};

const std::vector<EnumEntry> kTimeSchemes{
    {"euler", static_cast<int>(TimeScheme::Euler)},
    {"rk2", static_cast<int>(TimeScheme::RK2)},
    {"rk3", static_cast<int>(TimeScheme::RK3)}};

bool assignSideKind(BoundarySpec& spec,
                    const std::string& key,
                    const std::string& value,
                    std::string& error) {
    std::string why;
    BoundaryKind kind = spec.kind;
    if (!parseBoundaryKind(trimSpace(cleanValue(value)), kind, why)) {
        error = badValue(key, cleanValue(value), why);
        return false;
    }
    spec.kind = kind;
    return true;
}

}

bool parseTurbulenceKind(const std::string& text, TurbulenceKind& out,
                         std::string& error) {
    const std::string name = toLower(cleanValue(text));
    if (name == "none" || name == "off" || name == "laminar") {
        out = TurbulenceKind::None;
        return true;
    }
    if (name == "smagorinsky" || name == "smag" || name == "les") {
        out = TurbulenceKind::Smagorinsky;
        return true;
    }
    if (name == "komega" || name == "k-omega" || name == "sst" ||
        name == "komegasst") {
        out = TurbulenceKind::KOmegaSST;
        return true;
    }
    error = badValue("turbulence", cleanValue(text),
                     "it is none, smagorinsky or kOmegaSST. none is a laminar "
                     "run and is what every run before this did; smagorinsky "
                     "reads the eddy viscosity straight off the local strain "
                     "and carries nothing; kOmegaSST is Menter's SST, two "
                     "transport equations, which is what a wall bounded flow "
                     "needs");
    return false;
}

bool parseRegime(const std::string& text, Regime& out, std::string& error) {
    const std::string name = toLower(cleanValue(text));
    if (name == "incompressible" || name == "projection" || name == "low") {
        out = Regime::Incompressible;
        return true;
    }
    if (name == "compressible" || name == "euler" || name == "gas") {
        out = Regime::Compressible;
        return true;
    }
    error = badValue("regime", cleanValue(text),
                     "it is incompressible or compressible. incompressible is "
                     "the projection solver and every run before this one; "
                     "compressible is a second solver entirely - density is a "
                     "variable, there is no pressure solve at all, and the "
                     "keys the pressure solve needed are refused rather than "
                     "quietly ignored");
    return false;
}

// A run name has to survive being a folder name on three filesystems, so
// anything that is not a letter, a digit, a space or one of - _ . becomes a
// dash, runs of dashes collapse, and Windows' reserved trailing dot and space
// are trimmed. An empty result means the name was all punctuation, and the
// caller falls back to plain "output".
std::string Config::runNameFolder() const {
    std::string out;
    out.reserve(runName.size());
    for (const char c : runName) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool plain = std::isalnum(u) || c == ' ' || c == '-' ||
                           c == '_' || c == '.' || u >= 0x80;
        if (plain)
            out.push_back(c);
        else if (!out.empty() && out.back() != '-')
            out.push_back('-');
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.' ||
                            out.back() == '-'))
        out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '-'))
        out.erase(out.begin());
    return out;
}

const char* frameStateName(FrameState state) {
    if (state == FrameState::Full)
        return "full";
    return state == FrameState::Minimal ? "minimal" : "slim";
}

bool parseFrameState(const std::string& text, FrameState& out) {
    const std::string name = toLower(cleanValue(text));
    if (name == "slim" || name == "small" || name == "0" || name.empty()) {
        out = FrameState::Slim;
        return true;
    }
    if (name == "minimal" || name == "smallest" || name == "2") {
        out = FrameState::Minimal;
        return true;
    }
    if (name == "full" || name == "exact" || name == "1") {
        out = FrameState::Full;
        return true;
    }
    return false;
}

std::string stretchKindName(StretchKind kind) {
    switch (kind) {
    case StretchKind::Edges:
        return "edges";
    case StretchKind::Body:
        return "body";
    case StretchKind::Wake:
        return "wake";
    case StretchKind::Off:
    default:
        return "off";
    }
}

bool parseStretchKind(const std::string& text, StretchKind& out) {
    const std::string name = toLower(cleanValue(text));
    if (name == "off" || name == "none" || name == "0" || name.empty()) {
        out = StretchKind::Off;
        return true;
    }
    if (name == "edges" || name == "walls") {
        out = StretchKind::Edges;
        return true;
    }
    if (name == "body" || name == "profile" || name == "1") {
        out = StretchKind::Body;
        return true;
    }
    if (name == "wake" || name == "downstream") {
        out = StretchKind::Wake;
        return true;
    }
    return false;
}

std::string amrHelp() {
    return "--- amrLevels --------------------------------------------------\n"
           "Adaptive mesh refinement, compressible only. amrLevels=0 is off "
           "and is\n"
           "every run before this one. Each level halves the cell size over "
           "the patches\n"
           "it covers, so amrLevels=2 resolves four times finer where it "
           "matters and\n"
           "nowhere else.\n"
           "amrCriterion picks what counts as matters: density is shocks and "
           "contacts,\n"
           "  vorticity is wakes and shear, species is where two gases meet, "
           "body is the\n"
           "  cells against a solid, everything is all four and is the "
           "default.\n"
           "amrThreshold is how steep a feature has to be before it is worth "
           "refining,\n"
           "  as a fraction of the steepest one in the domain.\n"
           "amrBuffer pads the tagged region so a feature cannot run off its "
           "own patch\n"
           "  between regrids, and amrEvery is how many base steps pass "
           "between them.\n"
           "amrMinPatch and amrMaxPatch bound the patch side in base cells.\n"
           "Frames come out as an ordinary .vtk on the base grid plus a .vtm "
           "with one\n"
           ".vtr per patch, which ParaView opens as a hierarchy.\n";
}

std::string gridStretchHelp() {
    return "--- gridStretch ------------------------------------------------\n"
           "The compressible solver does not need every cell to be the same "
           "size.\n"
           "gridStretch=off  every cell the same, which is every run before "
           "this one.\n"
           "gridStretch=body cells shrink toward the body and grow toward the "
           "far field.\n"
           "gridStretch=wake same, but the fine band follows the wake "
           "downstream too.\n"
           "gridStretch=edges cells shrink toward the walls, for a duct or a "
           "boundary layer.\n"
           "stretchRatio is how much one cell may grow over its neighbour. "
           "1.05 is\n"
           "  five percent, which is the usual ceiling; over about 1.2 the "
           "scheme starts\n"
           "  losing the second order it is supposed to have.\n"
           "refineNear is how wide the fine band is, as a fraction of the "
           "domain.\n"
           "The frames come out as RECTILINEAR_GRID, which ParaView reads "
           "natively.\n";
}

const char* regimeName(Regime regime) {
    return regime == Regime::Compressible ? "compressible" : "incompressible";
}

bool parseSpeciesMode(const std::string& text, SpeciesMode& out,
                      std::string& error) {
    const std::string name = toLower(cleanValue(text));
    if (name == "active" || name == "mixture" || name == "1") {
        out = SpeciesMode::Active;
        return true;
    }
    if (name == "passive" || name == "tracer" || name == "0") {
        out = SpeciesMode::Passive;
        return true;
    }
    error = badValue("speciesMode", cleanValue(text),
                     "it is active or passive. active lets the composition set "
                     "gamma and R, so the speed of sound changes with it; "
                     "passive carries the fraction along and nothing else");
    return false;
}

const char* speciesModeName(SpeciesMode mode) {
    return mode == SpeciesMode::Passive ? "passive" : "active";
}

std::string microphoneHelp() {
    return "microphones is a list of points, semicolon separated:\n"
           "  x=<m>,y=<m>[,z=<m>];x=<m>,y=<m>[,z=<m>]\n"
           "z is the one you may leave out: without it the point sits at 0, "
           "which is the single plane a nz=1 run has and is where every "
           "microphone written before this one was.\n"
           "Each one records the pressure at that cell every micInterval "
           "steps, and at the end the run writes microphones.txt next to the "
           "frames with the trace and a peak frequency for each.\n"
           "micAudio=1 writes each one as a .wav as well, so you can play it "
           "back instead of reading a column of numbers. micAudioRate sets "
           "the file's sample rate and micAudioSpeed stretches the timebase - "
           "0.05 plays it twenty times slower, which is how you hear "
           "something that happened in two milliseconds.";
}

bool parseMicrophones(const std::string& text,
                      std::vector<Microphone>& out,
                      std::string& error) {
    out.clear();
    std::string body = cleanValue(text);
    if (body.empty())
        return true;

    std::size_t position = 0;
    while (position <= body.size()) {
        const std::size_t semicolon = body.find(';', position);
        std::string entry = body.substr(
            position, semicolon == std::string::npos ? std::string::npos
                                                     : semicolon - position);
        position = semicolon == std::string::npos ? body.size() + 1
                                                  : semicolon + 1;
        while (!entry.empty() && std::isspace(
                   static_cast<unsigned char>(entry.front())))
            entry.erase(entry.begin());
        while (!entry.empty() && std::isspace(
                   static_cast<unsigned char>(entry.back())))
            entry.pop_back();
        if (entry.empty())
            continue;

        Microphone mic;
        bool sawX = false, sawY = false;
        std::size_t inner = 0;
        while (inner <= entry.size()) {
            const std::size_t comma = entry.find(',', inner);
            std::string token = entry.substr(
                inner, comma == std::string::npos ? std::string::npos
                                                  : comma - inner);
            inner = comma == std::string::npos ? entry.size() + 1 : comma + 1;
            const std::size_t assign = token.find('=');
            if (assign == std::string::npos) {
                if (token.empty())
                    continue;
                error = "'" + token + "' is not a microphone setting. " +
                        microphoneHelp();
                return false;
            }
            const std::string name = toLower(token.substr(0, assign));
            const std::string value = token.substr(assign + 1);
            float number = 0.0f;
            try {
                number = std::stof(value);
            } catch (...) {
                error = "'" + value + "' is not a number. " + microphoneHelp();
                return false;
            }
            if (name == "x") { mic.x = number; sawX = true; }
            else if (name == "y") { mic.y = number; sawY = true; }
            else if (name == "z") { mic.z = number; }
            else {
                error = "'" + name + "' is not a microphone setting. " +
                        microphoneHelp();
                return false;
            }
        }
        if (!sawX || !sawY) {
            error = "a microphone needs both x and y. " + microphoneHelp();
            return false;
        }
        out.push_back(mic);
    }

    if (out.size() > 64) {
        error = "64 microphones is already more than anybody reads; this line "
                "has " + std::to_string(out.size()) + ".";
        return false;
    }
    return true;
}

const char* turbulenceKindName(TurbulenceKind kind) {
    switch (kind) {
    case TurbulenceKind::Smagorinsky: return "smagorinsky";
    case TurbulenceKind::KOmegaSST:   return "kOmegaSST";
    default:                          return "none";
    }
}

bool parseMixingKind(const std::string& text, MixingKind& out,
                     std::string& error) {
    const std::string key = toLower(trimSpace(text));
    if (key == "immiscible" || key == "sharp")
        { out = MixingKind::Immiscible; return true; }
    if (key == "miscible" || key == "mixing" || key == "mixed")
        { out = MixingKind::Miscible; return true; }
    error = "'" + text +
            "' is not a mixing kind. Use immiscible (oil and water, with a "
            "surface) or miscible (ink and water, with none).";
    return false;
}

const char* mixingKindName(MixingKind kind) {
    return kind == MixingKind::Miscible ? "miscible" : "immiscible";
}

bool parseVofScheme(const std::string& text, VofScheme& out,
                    std::string& error) {
    const std::string key = toLower(trimSpace(text));
    if (key == "upwind") { out = VofScheme::Upwind; return true; }
    if (key == "hric")   { out = VofScheme::Hric;   return true; }
    if (key == "cicsam") { out = VofScheme::Cicsam; return true; }
    error = "'" + text + "' is not a VOF scheme. Use upwind, hric or cicsam.";
    return false;
}

bool parsePhaseInit(const std::string& text, PhaseInit& out,
                    std::string& error) {
    const std::string key = toLower(trimSpace(text));
    if (key == "layer")  { out = PhaseInit::Layer;  return true; }
    if (key == "drop")   { out = PhaseInit::Drop;   return true; }
    if (key == "column") { out = PhaseInit::Column; return true; }
    if (key == "file")   { out = PhaseInit::File;   return true; }
    error = "'" + text +
            "' is not an initial shape. Use layer, drop, column or file.";
    return false;
}

const char* vofSchemeName(VofScheme scheme) {
    switch (scheme) {
    case VofScheme::Upwind: return "upwind";
    case VofScheme::Cicsam: return "cicsam";
    default:                return "hric";
    }
}

const char* phaseInitName(PhaseInit init) {
    switch (init) {
    case PhaseInit::Drop:   return "drop";
    case PhaseInit::Column: return "column";
    case PhaseInit::File:   return "file";
    default:                return "layer";
    }
}

std::string sourcesHelp() {
    return
        "\n--- How to write sources -----------------------------------------\n"
        "  sources=x=<m>,y=<m>[,z=<m>],r=<m>,rate=<m/s>[,angle=<deg>]\n"
        "          [,elev=<deg>][,phase=<0..1>];...\n"
        "\n"
        "  A source is a disc inside the domain that pushes fluid out of itself,\n"
        "  a ball of one once nz>1.\n"
        "  Unlike an inlet it is not on a side, so it needs a direction:\n"
        "    x, y, z  centre, in metres. Left out, z is 0.\n"
        "    r        radius, in metres. Under one cell nothing comes out.\n"
        "    rate     speed the fluid leaves at, m/s. Negative drains instead.\n"
        "    angle    degrees, 0 is +x and it turns counter-clockwise\n"
        "    elev     degrees out of the xy plane towards +z. angle still aims\n"
        "             the jet inside the plane and elev lifts it out of it, so\n"
        "             elev=90 fires straight along +z whatever angle says.\n"
        "    phase    which fluid comes out, 1 or 0. Ignored at one phase.\n"
        "\n"
        "  Everything a source adds has to leave somewhere, so a case with one\n"
        "  needs an outlet exactly as an inlet does, and is refused without one.\n"
        "\n"
        "    sources=\"x=0.5,y=0.2,r=0.05,rate=2,angle=90,phase=1\"\n"
        "    sources=\"x=0.5,y=0.2,z=0.3,r=0.05,rate=2,angle=90,elev=30\"\n"
        "------------------------------------------------------------------\n";
}


std::string vorticesHelp() {
    return "--- vortices ---------------------------------------------------\n"
           "Vortices put into the flow at the start, rather than waited for.\n"
           "\n"
           "    vortices=x=0.5,y=0.5,z=0.5,radius=0.05,strength=60,axis=z\n"
           "\n"
           "  x, y, z    where the axis of the tube passes through, in metres\n"
           "  radius     the distance from that axis at which the swirl is\n"
           "             fastest, in metres. Keep it to at least eight cells\n"
           "             or the scheme will smear the vortex away before it\n"
           "             has gone anywhere.\n"
           "  strength   that fastest swirl speed, in m/s. Positive turns\n"
           "             anticlockwise looking down the axis.\n"
           "  axis       x, y or z - which way the tube runs. Default z.\n"
           "\n"
           "Several are separated by semicolons. A compressible run gets the\n"
           "isentropic vortex, which is an exact steady solution of the Euler\n"
           "equations: left alone in a uniform stream it is carried along\n"
           "without changing shape, so how much of it survives crossing the\n"
           "box is a direct measurement of the scheme's own dissipation. An\n"
           "incompressible run gets the same velocity profile, which is\n"
           "divergence free as it stands and so needs no projection.\n"
           "\n"
           "The core of a compressible vortex is colder and thinner than the\n"
           "air around it, and there is a limit to how fast it can swirl\n"
           "before the middle would have to have no temperature left. Past\n"
           "that the strength is reduced and the run says so.\n";
}

bool parseVortices(const std::string& text,
                   std::vector<SeedVortex>& out,
                   std::string& error) {
    out.clear();
    const std::string body = trimSpace(text);
    if (body.empty() || toLower(body) == "none")
        return true;

    size_t start = 0;
    while (start <= body.size()) {
        const size_t end = body.find(';', start);
        const std::string token =
            trimSpace(body.substr(start, end == std::string::npos
                                             ? std::string::npos
                                             : end - start));
        start = (end == std::string::npos) ? body.size() + 1 : end + 1;
        if (token.empty())
            continue;

        SeedVortex vortex;
        bool sawRadius = false;
        bool sawStrength = false;
        size_t at = 0;
        while (at <= token.size()) {
            const size_t comma = token.find(',', at);
            const std::string piece =
                trimSpace(token.substr(at, comma == std::string::npos
                                               ? std::string::npos
                                               : comma - at));
            at = (comma == std::string::npos) ? token.size() + 1 : comma + 1;
            if (piece.empty())
                continue;

            const size_t equals = piece.find('=');
            if (equals == std::string::npos) {
                error = "'" + piece +
                        "' has no '=' in it. Every setting of a vortex is "
                        "<name>=<value>.";
                return false;
            }
            const std::string name =
                toLower(trimSpace(piece.substr(0, equals)));
            const std::string valueText = trimSpace(piece.substr(equals + 1));

            if (name == "axis") {
                const std::string axis = toLower(valueText);
                if (axis == "x" || axis == "0") {
                    vortex.axis = 0;
                } else if (axis == "y" || axis == "1") {
                    vortex.axis = 1;
                } else if (axis == "z" || axis == "2") {
                    vortex.axis = 2;
                } else {
                    error = "axis of a vortex is x, y or z, not '" +
                            valueText + "'.";
                    return false;
                }
                continue;
            }

            double value = 0.0;
            std::string why;
            if (!parseNumber(name, valueText, false, value, why)) {
                error = why;
                return false;
            }
            if (name == "x") {
                vortex.x = static_cast<float>(value);
            } else if (name == "y") {
                vortex.y = static_cast<float>(value);
            } else if (name == "z") {
                vortex.z = static_cast<float>(value);
            } else if (name == "radius" || name == "r") {
                vortex.radius = static_cast<float>(value);
                sawRadius = true;
            } else if (name == "strength" || name == "swirl") {
                vortex.strength = static_cast<float>(value);
                sawStrength = true;
            } else {
                error = "'" + name +
                        "' is not a setting of a vortex. They are x, y, z, "
                        "radius, strength and axis.";
                return false;
            }
        }

        if (!sawRadius || !(vortex.radius > 0.0f)) {
            error = "a vortex needs a radius greater than zero - the distance "
                    "from its axis at which the swirl is fastest.";
            return false;
        }
        if (!sawStrength) {
            error = "a vortex needs a strength - how fast it swirls at that "
                    "radius, in m/s.";
            return false;
        }
        out.push_back(vortex);
    }
    return true;
}

bool parseSources(const std::string& text,
                  std::vector<FlowSource>& out,
                  std::string& error) {
    out.clear();
    const std::string body = trimSpace(text);
    if (body.empty() || toLower(body) == "none")
        return true;

    size_t start = 0;
    while (start <= body.size()) {
        const size_t end = body.find(';', start);
        const std::string token =
            trimSpace(body.substr(start, end == std::string::npos
                                             ? std::string::npos
                                             : end - start));
        start = (end == std::string::npos) ? body.size() + 1 : end + 1;
        if (token.empty())
            continue;

        FlowSource source;
        bool sawRate = false;
        size_t at = 0;
        while (at <= token.size()) {
            const size_t comma = token.find(',', at);
            const std::string piece =
                trimSpace(token.substr(at, comma == std::string::npos
                                               ? std::string::npos
                                               : comma - at));
            at = (comma == std::string::npos) ? token.size() + 1 : comma + 1;
            if (piece.empty())
                continue;

            const size_t equals = piece.find('=');
            if (equals == std::string::npos) {
                error = "'" + piece +
                        "' has no '=' in it. Every setting of a source is "
                        "<name>=<number>.";
                return false;
            }
            const std::string name = toLower(trimSpace(piece.substr(0, equals)));
            const std::string valueText = trimSpace(piece.substr(equals + 1));
            double value = 0.0;
            std::string why;
            if (!parseNumber(name, valueText, false, value, why)) {
                error = why;
                return false;
            }
            if (name == "x") source.x = static_cast<float>(value);
            else if (name == "y") source.y = static_cast<float>(value);
            else if (name == "z") source.z = static_cast<float>(value);
            else if (name == "r" || name == "radius")
                source.radius = static_cast<float>(value);
            else if (name == "rate" || name == "speed") {
                source.rate = static_cast<float>(value);
                sawRate = true;
            }
            else if (name == "angle") source.angle = static_cast<float>(value);
            else if (name == "elev" || name == "elevation")
                source.elevation = static_cast<float>(value);
            else if (name == "phase")
                source.phase = static_cast<float>(std::min(1.0, std::max(0.0, value)));
            else if (name == "body" || name == "on") {
                if (value < 0.0 || value != std::floor(value)) {
                    error = "body=<object number> says which body the source "
                            "rides, so it is a whole number from 1, or 0 for "
                            "one bolted to the domain.";
                    return false;
                }
                source.body = static_cast<int>(value);
            }
            else {
                error = "'" + name +
                        "' is not a setting of a source. Use x, y, z, r, rate, "
                        "angle, elev, phase or body.";
                return false;
            }
        }

        if (!(source.radius > 0.0f)) {
            error = "a source needs a radius: r=<metres>.";
            return false;
        }
        if (!sawRate) {
            error = "a source with no rate= does nothing at all.";
            return false;
        }
        out.push_back(source);
    }
    return true;
}

std::string profilesHelp() {
    return
        "\n--- How to write profiles ----------------------------------------\n"
        "  profiles=<file>@<setting>=<value>,<setting>=<value>;<next file>@...\n"
        "\n"
        "  The separator between the file and its settings is '@' and not ':',\n"
        "  because a Windows path already owns the colon.\n"
        "\n"
        "  Settings, all optional:\n"
        "    x, y, z  where the centre of this model lands, in metres. z is\n"
        "             read once nz>1 and ignored while the run is one plane\n"
        "    size     the larger side of its section, in metres\n"
        "    rot      turn it in the plane, degrees\n"
        "    ax, ay, az  slice angles for this model alone, degrees. angleX,\n"
        "             angleY and angleZ are the same three written out\n"
        "    invert   1 mirrors it, same as invertSection but per model\n"
        "\n"
        "  A file with no '@' keeps the old behaviour: centred in the domain\n"
        "  at a fifth of its smaller side. Anything landing on or outside the\n"
        "  domain edge is refused with the number it missed by, because a body\n"
        "  touching the border is a wall, not an obstacle.\n"
        "\n"
        "    profiles=\"wing.stl@x=0.6,y=0.5,size=0.3;ball.obj@x=1.6,y=0.5\"\n"
        "    profiles=\"wing.stl@x=0.6,y=0.5,z=0.5,size=0.3,ay=15\"\n"
        "------------------------------------------------------------------\n";
}

bool parseProfiles(const std::string& text,
                   std::vector<Profile>& out,
                   std::string& error) {
    out.clear();
    error.clear();
    const std::string body = trimSpace(text);
    if (body.empty())
        return true;

    size_t pos = 0;
    while (pos <= body.size()) {
        const size_t end = body.find(';', pos);
        const std::string token =
            trimSpace(body.substr(pos, end == std::string::npos
                                           ? std::string::npos
                                           : end - pos));
        pos = (end == std::string::npos) ? body.size() + 1 : end + 1;
        if (token.empty())
            continue;

        Profile profile;
        const size_t at = token.find('@');
        profile.file = trimSpace(token.substr(0, at));
        if (profile.file.empty()) {
            error = badValue("profiles", token,
                             "there is no file name in front of the '@'");
            return false;
        }

        if (at != std::string::npos) {
            std::string settings = token.substr(at + 1);
            size_t sub = 0;
            while (sub <= settings.size()) {
                const size_t comma = settings.find(',', sub);
                const std::string pair =
                    trimSpace(settings.substr(sub, comma == std::string::npos
                                                       ? std::string::npos
                                                       : comma - sub));
                sub = (comma == std::string::npos) ? settings.size() + 1
                                                   : comma + 1;
                if (pair.empty())
                    continue;

                const size_t eq = pair.find('=');
                if (eq == std::string::npos || eq == 0) {
                    error = badValue("profiles", pair,
                                     "every setting is name=value, e.g. x=1.5");
                    return false;
                }
                std::string name = trimSpace(pair.substr(0, eq));
                for (char& c : name)
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                const std::string raw = trimSpace(pair.substr(eq + 1));

                double number = 0.0;
                std::string why;
                if (!parseNumber("profiles", raw, false, number, why)) {
                    error = badValue("profiles", pair, why);
                    return false;
                }

                if (name == "x")           { profile.x = float(number); profile.placed = true; }
                else if (name == "y")      { profile.y = float(number); profile.placed = true; }
                else if (name == "z")      { profile.z = float(number); profile.placed = true; }
                else if (name == "size")   { profile.size = float(number); }
                else if (name == "rot")    { profile.rotation = float(number); }
                else if (name == "ax" || name == "anglex")  { profile.angleX = float(number); profile.angleSet = true; }
                else if (name == "ay" || name == "angley")  { profile.angleY = float(number); profile.angleSet = true; }
                else if (name == "az" || name == "anglez")  { profile.angleZ = float(number); profile.angleSet = true; }
                else if (name == "invert") { profile.invert = number != 0.0; profile.invertSet = true; }
                else if (name == "attach") { profile.attach = number != 0.0; }
                else {
                    error = badValue("profiles", pair,
                                     "'" + name +
                                         "' is not a profile setting. Use x, y,"
                                         " z, size, rot, ax, ay, az or invert");
                    return false;
                }
            }
        }

        if (profile.size < 0.0f) {
            error = badValue("profiles", token,
                             "size is a length in metres, it cannot be "
                             "negative");
            return false;
        }
        out.push_back(std::move(profile));
    }

    return true;
}

std::string wallMotionHelp() {
    return
        "\n"
        "--- How to write wallMotion --------------------------------------\n"
        "Shape of the line:   <object>:<setting>=<value>,<setting>=<value>\n"
        "                     and ';' in front of the next object's number\n"
        "  Object numbers are printed with the mesh, right after this screen.\n"
        "  ',' and ';' both just separate settings; a new object starts\n"
        "  wherever a number and a colon appear, and each object is listed\n"
        "  once, with everything it does inside that one entry.\n"
        "\n"
        "Every object picks ONE of the two groups below.\n"
        "\n"
        "  A. The wall holds the fluid (no-slip) and now drags it along:\n"
        "     rot=<deg/s>    the surface turns about this object's own centre,\n"
        "                    counter-clockwise. rot=90 is a quarter turn a\n"
        "                    second; a spinning cylinder, a blade, a valve\n"
        "     rotX=<deg/s>   the same about the x axis through that centre,\n"
        "                    right handed, and rotY=<deg/s> about the y one.\n"
        "                    rotZ= is another way of writing rot=\n"
        "     slideX=<m/s>   the surface runs along +x, like a conveyor belt\n"
        "     slideY=<m/s>   the same along +y\n"
        "     slideZ=<m/s>   the same along +z\n"
        "     The body itself never moves, only the velocity its surface hands\n"
        "     to the fluid. All six add up, so one object can take every one of\n"
        "     them at once.\n"
        "\n"
        "  B. The wall stops holding the fluid at all:\n"
        "     slip=1         free-slip. The fluid slides past the surface and\n"
        "                    the wall exerts no drag on it, so no boundary\n"
        "                    layer grows. slip=0 is the default no-slip wall\n"
        "\n"
        "  A and B cannot be mixed on the same object: rot and slide push the\n"
        "  fluid through the grip that slip=1 removes, so 'slip=1,rot=90' asks\n"
        "  for a surface that turns and touches nothing. That is refused.\n"
        "\n"
        "Empty line = every wall stands still and holds the fluid (no-slip).\n"
        "\n"
        "Examples:\n"
        "  1:rot=90              object 1 spins at 90 deg/s counter-clockwise\n"
        "  1:slideX=0.5          its surface runs along +x at 0.5 m/s\n"
        "  1:rot=90,slideX=0.5   both at once - one object, one entry\n"
        "  1:rotX=45,slideZ=0.2  it rolls about x and its surface runs along +z\n"
        "  1:slip=1              object 1 is frictionless instead\n"
        "  1:rot=90;2:slip=1     object 1 spins, object 2 slips\n"
        "------------------------------------------------------------------\n";
}

bool parseWallMotion(const std::string& text,
                     std::vector<WallMotion>& out,
                     std::string& error) {
    out.clear();
    error.clear();

    const std::string body = cleanValue(text);
    if (body.empty())
        return true;

    std::vector<std::string> tokens;
    for (size_t pos = 0; pos < body.size();) {
        size_t end = body.find_first_of(",;", pos);
        if (end == std::string::npos)
            end = body.size();
        tokens.push_back(trimSpace(body.substr(pos, end - pos)));
        pos = end + 1;
    }

    int current = -1;
    for (std::string token : tokens) {
        if (token.empty())
            continue;

        const size_t colon = token.find(':');
        if (colon != std::string::npos) {
            const std::string idText = trimSpace(token.substr(0, colon));
            double id = 0.0;
            std::string why;
            if (!parseNumber("object", idText, true, id, why) || id < 1.0) {
                error = badValue("wallMotion", body,
                                 "'" + idText + "' is not an object number; "
                                 "objects are numbered from 1 and the number "
                                 "comes first, e.g. 1:rot=90");
                return false;
            }
            for (const WallMotion& done : out) {
                if (done.object != static_cast<int>(id))
                    continue;
                error = badValue("wallMotion", body,
                                 "object " + idText + " is listed twice. One "
                                 "object gets one entry, with everything it "
                                 "does inside it: write " + idText +
                                 ":rot=90,slideX=0.5, not " + idText +
                                 ":rot=90;" + idText + ":slideX=0.5");
                return false;
            }
            out.push_back(WallMotion());
            out.back().object = static_cast<int>(id);
            current = static_cast<int>(out.size()) - 1;
            token = trimSpace(token.substr(colon + 1));
            if (token.empty())
                continue;
        }

        if (current < 0) {
            error = badValue("wallMotion", body,
                             "'" + token + "' comes before any object number. "
                             "The line starts with the object it is about and "
                             "a colon, so this reads 1:" + token +
                             " if object 1 is the one meant");
            return false;
        }

        const size_t eq = token.find('=');
        if (eq == std::string::npos || eq == 0) {
            double stray = 0.0;
            std::string why;
            const bool bareNumber =
                parseNumber("wallMotion", token, false, stray, why);
            error = badValue("wallMotion", body,
                             bareNumber
                                 ? "'" + token + "' is a bare number; a comma "
                                   "separates settings here, so the decimal "
                                   "separator inside one is a dot"
                                 : "'" + token + "' is not a setting. Every "
                                   "setting is name=value, e.g. rot=90, and "
                                   "the names are rot, rotX, rotY, slideX, "
                                   "slideY, slideZ and slip");
            return false;
        }

        const std::string name = toLower(trimSpace(token.substr(0, eq)));
        const std::string value = token.substr(eq + 1);

        if (name == "slip") {
            if (!assignBool(out[current].slip, name, value, error))
                return false;
            continue;
        }

        double parsed = 0.0;
        if (!parseNumber(name, value, false, parsed, error))
            return false;

        if (name == "rot" || name == "rotation" || name == "rotz")
            out[current].rotation = static_cast<float>(parsed);
        else if (name == "rotx")
            out[current].rotationX = static_cast<float>(parsed);
        else if (name == "roty")
            out[current].rotationY = static_cast<float>(parsed);
        else if (name == "slidex")
            out[current].slideX = static_cast<float>(parsed);
        else if (name == "slidey")
            out[current].slideY = static_cast<float>(parsed);
        else if (name == "slidez")
            out[current].slideZ = static_cast<float>(parsed);
        else {
            error = badValue("wallMotion", body,
                             "'" + name + "' is not a wall setting. There are "
                             "seven: rot=<deg/s> spins the surface about the "
                             "object's own centre counter-clockwise, rotX and "
                             "rotY do the same about the other two axes, "
                             "slideX=<m/s>, slideY=<m/s> and slideZ=<m/s> drag "
                             "it in a straight line, and slip=1 makes the wall "
                             "frictionless instead of dragging anything");
            return false;
        }
    }

    for (const WallMotion& done : out) {
        if (!done.slip)
            continue;
        if (done.rotation == 0.0f && done.rotationX == 0.0f &&
            done.rotationY == 0.0f && done.slideX == 0.0f &&
            done.slideY == 0.0f && done.slideZ == 0.0f)
            continue;

        // Quoting back the two lines the object could have been given beats
        // naming the rule: whichever of them was meant can be copied straight
        // into the answer.
        const std::string id = std::to_string(done.object);
        std::ostringstream moving;
        moving << id << ":";
        const char* separator = "";
        if (done.rotation != 0.0f) {
            moving << "rot=" << done.rotation;
            separator = ",";
        }
        if (done.rotationX != 0.0f) {
            moving << separator << "rotX=" << done.rotationX;
            separator = ",";
        }
        if (done.rotationY != 0.0f) {
            moving << separator << "rotY=" << done.rotationY;
            separator = ",";
        }
        if (done.slideX != 0.0f) {
            moving << separator << "slideX=" << done.slideX;
            separator = ",";
        }
        if (done.slideY != 0.0f) {
            moving << separator << "slideY=" << done.slideY;
            separator = ",";
        }
        if (done.slideZ != 0.0f)
            moving << separator << "slideZ=" << done.slideZ;

        error = badValue("wallMotion", body,
                         "object " + id + " is asked to slip and to move its "
                         "surface at once, and those are opposites. A moving "
                         "wall pushes the fluid by holding on to it, and "
                         "slip=1 is exactly the setting that lets go, so "
                         "together they leave a surface that turns and touches "
                         "nothing. Keep one of the two: " + id + ":slip=1 for "
                         "a frictionless wall, or " + moving.str() + " for a "
                         "wall that drags the flow");
        return false;
    }

    return true;
}

namespace {
struct NamedInterp {
    const char* name;
    InterpKind kind;
};
const NamedInterp kInterpKinds[] = {
    {"constant", InterpKind::Constant}, {"step", InterpKind::Constant},
    {"hold", InterpKind::Constant},
    {"linear", InterpKind::Linear},
    {"bezier", InterpKind::Bezier}, {"smooth", InterpKind::Bezier},
    {"sine", InterpKind::Sine}, {"sinusoidal", InterpKind::Sine},
    {"quad", InterpKind::Quad}, {"quadratic", InterpKind::Quad},
    {"cubic", InterpKind::Cubic},
    {"quart", InterpKind::Quart}, {"quartic", InterpKind::Quart},
    {"quint", InterpKind::Quint}, {"quintic", InterpKind::Quint},
    {"expo", InterpKind::Expo}, {"exponential", InterpKind::Expo},
    {"circ", InterpKind::Circ}, {"circular", InterpKind::Circ},
    {"back", InterpKind::Back},
    {"bounce", InterpKind::Bounce},
    {"elastic", InterpKind::Elastic}
};
}

bool parseInterpKind(const std::string& text, InterpKind& out,
                     std::string& error) {
    const std::string name = toLower(cleanValue(text));
    for (const NamedInterp& known : kInterpKinds)
        if (name == known.name) {
            out = known.kind;
            return true;
        }
    error = badValue("interp", cleanValue(text),
                     "the ones a 3D package has: constant, linear, bezier, "
                     "and the easings sine, quad, cubic, quart, quint, expo, "
                     "circ, back, bounce and elastic");
    return false;
}

bool parseEaseKind(const std::string& text, EaseKind& out,
                   std::string& error) {
    const std::string name = toLower(cleanValue(text));
    if (name == "auto")  { out = EaseKind::Auto;  return true; }
    if (name == "in")    { out = EaseKind::In;    return true; }
    if (name == "out")   { out = EaseKind::Out;   return true; }
    if (name == "inout" || name == "in-out" || name == "both") {
        out = EaseKind::InOut;
        return true;
    }
    error = badValue("ease", cleanValue(text),
                     "it is in, out, inout, or auto - which is out for the "
                     "ordinary easings and in for back, bounce and elastic, "
                     "the same choice a 3D package makes");
    return false;
}

const char* interpKindName(InterpKind kind) {
    switch (kind) {
    case InterpKind::Constant: return "constant";
    case InterpKind::Bezier:   return "bezier";
    case InterpKind::Sine:     return "sine";
    case InterpKind::Quad:     return "quad";
    case InterpKind::Cubic:    return "cubic";
    case InterpKind::Quart:    return "quart";
    case InterpKind::Quint:    return "quint";
    case InterpKind::Expo:     return "expo";
    case InterpKind::Circ:     return "circ";
    case InterpKind::Back:     return "back";
    case InterpKind::Bounce:   return "bounce";
    case InterpKind::Elastic:  return "elastic";
    default:                   return "linear";
    }
}

const char* easeKindName(EaseKind kind) {
    switch (kind) {
    case EaseKind::In:    return "in";
    case EaseKind::Out:   return "out";
    case EaseKind::InOut: return "inout";
    default:              return "auto";
    }
}

std::string bodyMotionHelp() {
    return
        "\n"
        "--- How to write bodyMotion --------------------------------------\n"
        "Shape of the line:   <object>:<setting>=<value>,<setting>=<value>\n"
        "                     and ';' in front of the next object's number\n"
        "  Same shape as wallMotion, same object numbers - the ones printed\n"
        "  with the mesh. The difference is what moves. wallMotion moves the\n"
        "  SURFACE and leaves the body where it is; bodyMotion moves the\n"
        "  BODY, and the mask is rebuilt every step.\n"
        "\n"
        "  A. You say where it goes:\n"
        "     vx=<m/s>       the body travels along +x\n"
        "     vy=<m/s>       the same along +y\n"
        "     vz=<m/s>       the same along +z\n"
        "     omega=<deg/s>  it turns about its own centre, counter-clockwise\n"
        "     omegaX=<deg/s> it turns about the x axis through that centre,\n"
        "                    right handed, and omegaY=<deg/s> about the y one.\n"
        "                    omegaZ= is another way of writing omega=\n"
        "\n"
        "  B. The flow decides where it goes:\n"
        "     free=1         the body is let go and the fluid carries it\n"
        "     mass=<kg/m>    per metre of depth while the run is one plane,\n"
        "                    and kilograms outright once nz>1\n"
        "     density=<kg/m3>  instead of mass, if the shape is easier to\n"
        "                    weigh than to mass: m = density * its own area,\n"
        "                    or its own volume once nz>1\n"
        "     inertia=<kg m2/m>  about z through the centre. Left out it is\n"
        "                    taken as m*r^2/2, which is what a disc of that\n"
        "                    rim has, and inertiaX= and inertiaY= are the same\n"
        "                    two numbers about the other axes\n"
        "     vx, vy, vz, omega, omegaX, omegaY  become the velocity it is let\n"
        "                    go WITH\n"
        "     pinX=1 pinY=1 pinZ=1 pinRot=1   hold one degree of freedom\n"
        "                    still, and pinRotX=1 pinRotY=1 the two turns that\n"
        "                    leave the plane. A cylinder free to spin but not\n"
        "                    to drift is free=1,pinX=1,pinY=1\n"
        "\n"
        "  C. Keyframes, for a prescribed path that is not a constant:\n"
        "     @<t>           opens a keyframe at t seconds; everything after\n"
        "                    it belongs to that keyframe until the next @.\n"
        "     Times go forwards. Between two keyframes the velocity is\n"
        "     interpolated; before the first and after the last it is held.\n"
        "     interp=<kind>  how the segment STARTING at this key is shaped,\n"
        "                    the same set a 3D package offers:\n"
        "                      constant  hold until the next key\n"
        "                      linear    a straight line (the default)\n"
        "                      bezier    a cubic with auto-clamped handles,\n"
        "                                so it does not overshoot a turn\n"
        "                      sine quad cubic quart quint expo circ\n"
        "                      back bounce elastic\n"
        "     ease=<in|out|inout|auto>  which end of the segment the easing\n"
        "                    is applied at. auto is out for the ordinary ones\n"
        "                    and in for back, bounce and elastic\n"
        "\n"
        "Examples:\n"
        "  1:vx=0.2                    object 1 drifts right at 0.2 m/s\n"
        "  1:vx=0.2,omega=45           and turns while it goes\n"
        "  1:vz=0.1,omegaX=30          it leaves the plane and rolls about x\n"
        "  1:free=1,mass=2             let go, 2 kg per metre of depth\n"
        "  1:free=1,density=2700       the same, weighed as aluminium\n"
        "  1:free=1,density=2700,pinX=1,pinY=1   pinned, free to spin\n"
        "  1:@0,vx=0,@1,vx=0.5,@2,vx=0 speeds up, then stops\n"
        "  1:vx=0.1;2:free=1,mass=5    one of each\n"
        "\n"
        "Empty line = every body stays where it is, which is every run\n"
        "written before this existed.\n"
        "------------------------------------------------------------------\n";
}

bool parseBodyMotion(const std::string& text,
                     std::vector<BodyMotion>& out,
                     std::string& error) {
    out.clear();
    error.clear();

    const std::string body = cleanValue(text);
    if (body.empty())
        return true;

    std::vector<std::string> tokens;
    for (size_t pos = 0; pos < body.size();) {
        size_t end = body.find_first_of(",;", pos);
        if (end == std::string::npos)
            end = body.size();
        tokens.push_back(trimSpace(body.substr(pos, end - pos)));
        pos = end + 1;
    }

    const auto timeText = [](float value) {
        std::ostringstream out;
        out << value;
        return out.str();
    };

    int current = -1;
    for (std::string token : tokens) {
        if (token.empty())
            continue;

        const size_t colon = token.find(':');
        const size_t equals = token.find('=');
        if (colon != std::string::npos &&
            (equals == std::string::npos || colon < equals)) {
            const std::string idText = trimSpace(token.substr(0, colon));
            double id = 0.0;
            std::string why;
            if (!parseNumber("object", idText, true, id, why) || id < 1.0) {
                error = badValue("bodyMotion", body,
                                 "'" + idText + "' is not an object number; "
                                 "objects are numbered from 1 and the number "
                                 "comes first, e.g. 1:vx=0.2");
                return false;
            }
            for (const BodyMotion& done : out) {
                if (done.object != static_cast<int>(id))
                    continue;
                error = badValue("bodyMotion", body,
                                 "object " + idText + " is listed twice. One "
                                 "object gets one entry, with everything it "
                                 "does inside it: write " + idText +
                                 ":vx=0.2,omega=45, not " + idText +
                                 ":vx=0.2;" + idText + ":omega=45");
                return false;
            }
            out.push_back(BodyMotion());
            out.back().object = static_cast<int>(id);
            current = static_cast<int>(out.size()) - 1;
            token = trimSpace(token.substr(colon + 1));
            if (token.empty())
                continue;
        }

        if (current < 0) {
            error = badValue("bodyMotion", body,
                             "'" + token + "' comes before any object number. "
                             "The line starts with the object it is about and "
                             "a colon, so this reads 1:" + token +
                             " if object 1 is the one meant");
            return false;
        }

        BodyMotion& target = out[current];

        if (token[0] == '@') {
            double when = 0.0;
            std::string why;
            if (!parseNumber("keyframe time", token.substr(1), false, when,
                             why)) {
                error = badValue("bodyMotion", body,
                                 "'" + token + "' opens a keyframe, so what "
                                 "follows the @ is a time in seconds: @0, "
                                 "@1.5, @2");
                return false;
            }
            if (when < 0.0) {
                error = badValue("bodyMotion", body,
                                 "a keyframe at " + token.substr(1) +
                                 " s is before the run starts");
                return false;
            }
            if (!target.keys.empty() &&
                static_cast<float>(when) <= target.keys.back().time) {
                error = badValue("bodyMotion", body,
                                 "keyframes go forwards in time, and " + token +
                                 " does not come after @" +
                                 timeText(target.keys.back().time) +
                                 ". List them in order");
                return false;
            }
            BodyKeyframe frame;
            frame.time = static_cast<float>(when);
            if (!target.keys.empty()) {
                frame.vx = target.keys.back().vx;
                frame.vy = target.keys.back().vy;
                frame.vz = target.keys.back().vz;
                frame.omegaX = target.keys.back().omegaX;
                frame.omegaY = target.keys.back().omegaY;
                frame.omega = target.keys.back().omega;
                frame.free = target.keys.back().free;
                frame.interp = target.keys.back().interp;
                frame.ease = target.keys.back().ease;
            } else {
                frame.vx = target.vx;
                frame.vy = target.vy;
                frame.vz = target.vz;
                frame.omegaX = target.omegaX;
                frame.omegaY = target.omegaY;
                frame.omega = target.omega;
                frame.free = target.free;
            }
            target.keys.push_back(frame);
            continue;
        }

        const size_t assign = token.find('=');
        if (assign == std::string::npos || assign == 0) {
            error = badValue("bodyMotion", body,
                             "'" + token + "' is not a setting. Every setting "
                             "is name=value, e.g. vx=0.2, and a keyframe opens "
                             "with @<seconds>");
            return false;
        }

        const std::string name = toLower(trimSpace(token.substr(0, assign)));
        const std::string value = token.substr(assign + 1);

        if (name == "free") {
            bool& flag = target.keys.empty() ? target.free
                                             : target.keys.back().free;
            if (!assignBool(flag, name, value, error))
                return false;
            continue;
        }
        if (name == "interp") {
            if (target.keys.empty()) {
                error = badValue("bodyMotion", body,
                                 "interp belongs to a keyframe and there is "
                                 "no keyframe yet: open one with @<seconds> "
                                 "first");
                return false;
            }
            if (!parseInterpKind(value, target.keys.back().interp, error))
                return false;
            continue;
        }
        if (name == "ease") {
            if (target.keys.empty()) {
                error = badValue("bodyMotion", body,
                                 "ease belongs to a keyframe and there is no "
                                 "keyframe yet: open one with @<seconds> "
                                 "first");
                return false;
            }
            if (!parseEaseKind(value, target.keys.back().ease, error))
                return false;
            continue;
        }
        if (name == "pinx") {
            if (!assignBool(target.pinX, name, value, error))
                return false;
            continue;
        }
        if (name == "piny") {
            if (!assignBool(target.pinY, name, value, error))
                return false;
            continue;
        }
        if (name == "pinz") {
            if (!assignBool(target.pinZ, name, value, error))
                return false;
            continue;
        }
        if (name == "pinrot" || name == "pinrotz") {
            if (!assignBool(target.pinRot, name, value, error))
                return false;
            continue;
        }
        if (name == "pinrotx") {
            if (!assignBool(target.pinRotX, name, value, error))
                return false;
            continue;
        }
        if (name == "pinroty") {
            if (!assignBool(target.pinRotY, name, value, error))
                return false;
            continue;
        }

        double parsed = 0.0;
        if (!parseNumber(name, value, false, parsed, error))
            return false;

        const float number = static_cast<float>(parsed);
        BodyKeyframe* frame =
            target.keys.empty() ? nullptr : &target.keys.back();

        if (name == "vx") {
            (frame ? frame->vx : target.vx) = number;
        } else if (name == "vy") {
            (frame ? frame->vy : target.vy) = number;
        } else if (name == "vz") {
            (frame ? frame->vz : target.vz) = number;
        } else if (name == "omega" || name == "rot" || name == "omegaz") {
            (frame ? frame->omega : target.omega) = number;
        } else if (name == "omegax") {
            (frame ? frame->omegaX : target.omegaX) = number;
        } else if (name == "omegay") {
            (frame ? frame->omegaY : target.omegaY) = number;
        } else if (name == "mass") {
            if (!(number > 0.0f)) {
                error = badValue("bodyMotion", body,
                                 "a body has to weigh something, and mass=" +
                                 cleanValue(value) + " is not a positive "
                                 "number");
                return false;
            }
            target.mass = number;
        } else if (name == "density") {
            if (!(number > 0.0f)) {
                error = badValue("bodyMotion", body,
                                 "density=" + cleanValue(value) + " is not a "
                                 "positive number, and the mass is worked out "
                                 "from it");
                return false;
            }
            target.density = number;
        } else if (name == "inertia" || name == "inertiaz") {
            if (!(number > 0.0f)) {
                error = badValue("bodyMotion", body,
                                 "inertia=" + cleanValue(value) + " is not a "
                                 "positive number. Leave it out and it is "
                                 "taken as m*r^2/2, which is what a disc of "
                                 "that rim would have");
                return false;
            }
            target.inertia = number;
        } else if (name == "inertiax") {
            if (!(number > 0.0f)) {
                error = badValue("bodyMotion", body,
                                 "inertiaX=" + cleanValue(value) + " is not a "
                                 "positive number. Leave it out and it is "
                                 "taken from the shape, exactly as inertia is");
                return false;
            }
            target.inertiaX = number;
        } else if (name == "inertiay") {
            if (!(number > 0.0f)) {
                error = badValue("bodyMotion", body,
                                 "inertiaY=" + cleanValue(value) + " is not a "
                                 "positive number. Leave it out and it is "
                                 "taken from the shape, exactly as inertia is");
                return false;
            }
            target.inertiaY = number;
        } else {
            error = badValue("bodyMotion", body,
                             "'" + name + "' is not a body setting. The "
                             "prescribed ones are vx, vy, vz, omega, omegaX "
                             "and omegaY; free=1 hands the trajectory to the "
                             "flow and then mass or density, inertia, "
                             "inertiaX, inertiaY, pinX, pinY, pinZ, pinRot, "
                             "pinRotX and pinRotY apply; @<seconds> opens a "
                             "keyframe");
            return false;
        }
    }

    for (const BodyMotion& done : out) {
        const std::string id = std::to_string(done.object);
        bool everFree = done.free;
        for (const BodyKeyframe& frame : done.keys)
            if (frame.free)
                everFree = true;
        if (everFree && done.mass <= 0.0f && done.density <= 0.0f) {
            error = badValue("bodyMotion", body,
                             "object " + id + " is let go without a weight, "
                             "and the fluid cannot accelerate something whose "
                             "mass it does not know. Give it mass=<kg per "
                             "metre of depth> or density=<kg/m3>");
            return false;
        }
        if (!everFree &&
            (done.mass > 0.0f || done.density > 0.0f || done.inertia > 0.0f ||
             done.inertiaX > 0.0f || done.inertiaY > 0.0f ||
             done.pinX || done.pinY || done.pinZ || done.pinRot ||
             done.pinRotX || done.pinRotY)) {
            error = badValue("bodyMotion", body,
                             "object " + id + " is given a weight or a pin but "
                             "not free=1, and a body whose path you prescribe "
                             "goes where you said whatever it weighs. Add "
                             "free=1 to let the flow move it, or drop the "
                             "settings only a free body reads");
            return false;
        }
    }

    return true;
}

bool parseBodyCoupling(const std::string& text, BodyCoupling& out,
                       std::string& error) {
    const std::string name = toLower(cleanValue(text));
    if (name == "weak") {
        out = BodyCoupling::Weak;
        return true;
    }
    if (name == "added") {
        out = BodyCoupling::Added;
        return true;
    }
    if (name == "strong") {
        out = BodyCoupling::Strong;
        return true;
    }
    error = badValue("bodyCoupling", cleanValue(text),
                     "it is weak, added or strong. weak evaluates the force "
                     "once a step and goes unstable as soon as the body is not "
                     "much heavier than the fluid it displaces; added carries "
                     "an estimate of the fluid that moves with it on the left "
                     "hand side, which is where that instability comes from, "
                     "and costs nothing; strong iterates until force and "
                     "motion agree, which is what a body lighter than the "
                     "fluid needs");
    return false;
}

const char* bodyCouplingName(BodyCoupling coupling) {
    switch (coupling) {
    case BodyCoupling::Weak:   return "weak";
    case BodyCoupling::Strong: return "strong";
    default:                   return "added";
    }
}

std::string Config::canonicalKey(const std::string& key) {
    std::string name = cleanValue(key);
    while (!name.empty() && (name.front() == '-' || name.front() == '/'))
        name.erase(name.begin());

    const std::string lower = toLower(name);
    for (const char* known : kKeys)
        if (toLower(known) == lower)
            return known;
    return std::string();
}

std::string Config::suggestKey(const std::string& key) {
    const std::string exact = canonicalKey(key);
    if (!exact.empty())
        return exact;

    std::string name = cleanValue(key);
    while (!name.empty() && (name.front() == '-' || name.front() == '/'))
        name.erase(name.begin());
    const std::string lower = toLower(name);
    if (lower.empty())
        return std::string();

    std::string best;
    int bestDistance = 0;
    for (const char* known : kKeys) {
        const int d = editDistance(lower, toLower(known));
        if (best.empty() || d < bestDistance) {
            best = known;
            bestDistance = d;
        }
    }
    const int limit = std::max(2, static_cast<int>(lower.size()) / 3);
    return bestDistance <= limit ? best : std::string();
}

std::string Config::currentValue(const std::string& key) const {
    const std::string wanted = canonicalKey(key);
    if (wanted.empty())
        return std::string();

    // Not part of serialize(), they describe the run and not the physics.
    if (wanted == "restart")
        return restart ? "1" : "0";
    if (wanted == "restartFile")
        return restartFile;
    if (wanted == "addTime") {
        std::ostringstream out;
        out << addTime;
        return out.str();
    }

    std::istringstream text(serialize());
    std::string line;
    while (std::getline(text, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        if (canonicalKey(line.substr(0, eq)) != wanted)
            continue;

        const std::string value = line.substr(eq + 1);
        // serialize() prints at full float precision, which reads terribly in
        // a prompt ("0.00999999978"). Round it back to what print() shows.
        char* end = nullptr;
        const double number = std::strtod(value.c_str(), &end);
        if (end != value.c_str() && *end == '\0') {
            std::ostringstream out;
            out << number;
            return out.str();
        }
        return value;
    }
    return std::string();
}

bool Config::ask(const std::string& key, const std::string& prompt) {
    const std::string canon = canonicalKey(key);
    const std::string name = canon.empty() ? key : canon;

    // wallMotion is the one answer with a grammar of its own, so its rules go
    // on the screen before the cursor gets there and not only after a refusal.
    // Here rather than at the call site, so re-entering it from the
    // confirmation menu shows the same block.
    if (name == "wallMotion")
        std::cout << wallMotionHelp();
    if (name == "gridStretch")
        std::cout << gridStretchHelp();
    if (name == "amrLevels" || name == "amrCriterion")
        std::cout << amrHelp();
    if (name == "bodyMotion")
        std::cout << bodyMotionHelp();
    if (name == "profiles")
        std::cout << profilesHelp();

    for (;;) {
        std::cout << prompt;
        const std::string shown = currentValue(name);
        if (!shown.empty())
            std::cout << " [" << shown << "]";
        std::cout << ": ";

        std::string line;
        if (!std::getline(std::cin, line)) {
            // Ctrl+Z, Ctrl+D, or a script that ran out of input. Reading on a
            // dead stream returns instantly, so asking anything else would
            // just scroll the remaining questions past without an answer.
            std::cout << "\nEnd of input. Keeping the rest of the "
                         "configuration as it is.\n";
            return false;
        }

        line = trimSpace(line);
        if (line.empty())
            return true;   // Enter keeps what is already there

        std::string error, warning;
        if (setParam(name, line, error, &warning)) {
            if (!warning.empty())
                std::cout << "  Warning: " << warning << "\n";
            return true;
        }
        std::cout << "  " << error << "\n";
    }
}

void Config::readFromConsole() {
    std::cout << "=== Fluid Solver Configuration ===\n";
    std::cout << "Press Enter to keep the value shown in brackets.\n\n";
    std::cout << "Start a new simulation or continue an old one?\n";
    std::cout << "  0 = new simulation\n";
    std::cout << "  1 = continue from a saved .vtk\n";
    if (!ask("restart", "Your choice"))
        return;
    if (restart) {
        while (restartFile.empty()) {
            if (!ask("restartFile",
                     "Enter path to the .vtk to continue from (or the folder "
                     "with the frames, newest one wins)"))
                return;
        }
        std::cout << "Configuration will be restored from that frame.\n";
        return;
    }
    if (!ask("Lx", "Enter domain width Lx (m)")) return;
    if (!ask("Ly", "Enter domain height Ly (m)")) return;
    if (!ask("Lz", "Enter domain depth Lz (m)")) return;
    if (!ask("nx", "Enter number of cells in x-direction nx")) return;
    if (!ask("ny", "Enter number of cells in y-direction ny")) return;
    if (!ask("nz", "Enter number of cells in z-direction nz (1 is the single "
                   "plane every earlier version solved; anything above it is "
                   "what makes the run three-dimensional)")) return;
    if (!ask("U0", "Enter inlet velocity U0 (m/s)")) return;
    if (!ask("phases", "How many fluids share the domain (1 or 2)")) return;
    if (phases > 1) {
        std::cout << "  Two fluids: nu and ro are ignored and each fluid gets "
                     "its own.\n  Fluid 1 is what the initial shape is made "
                     "of, fluid 2 fills the rest.\n";
        if (!ask("rho1", "Density of fluid 1 (kg/m^3, water is 1000)")) return;
        if (!ask("nu1", "Kinematic viscosity of fluid 1 (m^2/s, water is 1e-6)"))
            return;
        if (!ask("rho2", "Density of fluid 2 (kg/m^3, air is 1.225)")) return;
        if (!ask("nu2", "Kinematic viscosity of fluid 2 (m^2/s, air is 1.5e-5)"))
            return;
        if (!ask("phaseInit",
                 "What is in the domain at the start: layer, drop, column or "
                 "file"))
            return;
        if (phaseInit == PhaseInit::File) {
            if (!ask("initialPhaseFile",
                     "File holding one fraction per cell, row 0 first"))
                return;
        } else {
            if (!ask("phaseLevel",
                     phaseInit == PhaseInit::Drop
                         ? "Drop diameter as a fraction of the smaller side"
                         : "Height of fluid 1 as a fraction of Ly"))
                return;
            if (phaseInit != PhaseInit::Layer)
                if (!ask("phaseX",
                         phaseInit == PhaseInit::Column
                             ? "Width of the column as a fraction of Lx"
                             : "Drop centre x as a fraction of Lx"))
                    return;
            if (phaseInit == PhaseInit::Drop)
                if (!ask("phaseY", "Drop centre y as a fraction of Ly")) return;
            if (phaseInit == PhaseInit::Drop && volumetric())
                if (!ask("phaseZ", "Drop centre z as a fraction of Lz")) return;
        }
        if (!ask("mixing",
                 "Do the two mix? immiscible (oil and water, a surface "
                 "between them) or miscible (ink and water, no surface)"))
            return;
        if (mixing == MixingKind::Miscible) {
            if (!ask("diffusivity",
                     "How fast one spreads through the other (m^2/s; salt in "
                     "water is about 1.5e-9, ink about 1e-9)"))
                return;
        } else {
            if (!ask("vofScheme",
                     "How the interface is carried: hric, cicsam or upwind"))
                return;
            if (!ask("surfaceTension",
                     "Surface tension (N/m; water against air is 0.072, 0 "
                     "turns it off)"))
                return;
            if (surfaceTension > 0.0f)
                if (!ask("contactAngle",
                         "Angle the interface meets a wall at, measured inside "
                         "fluid 1 (degrees; 90 is neutral)"))
                    return;
        }
    } else {
        if (!ask("nu", "Enter kinematic viscosity nu (m^2/s)")) return;
        if (!ask("ro", "Enter density ro. Make sure that the gas/liquid is "
                       "incompressible (meaning for air speed its less than "
                       "0.3M) (kg/m^3)")) return;
    }
    if (!ask("gravityEnabled", "Enable gravity? (0 = no, 1 = yes)")) return;
    if (gravityEnabled) {
        if (!ask("gravityAccel",
                 "Enter gravitational acceleration (m/s^2, 9.81 on Earth)"))
            return;
        if (!ask("gravityAngle",
                 "Enter gravity direction (degrees clockwise from straight "
                 "down: 0 = down, 90 = towards the inlet, 180 = up)"))
            return;
        if (volumetric())
            if (!ask("gravityTilt",
                     "Enter how far gravity leans out of that plane towards +z "
                     "(degrees: 0 keeps it in the plane, 90 points it straight "
                     "along +z)"))
                return;
        if (phases > 1)
            std::cout << "Note: with two fluids the weight difference is what "
                         "moves them, so the force goes\n  into the solve and "
                         "gravityMode is body whether it is asked for or not.\n";
        else
            std::cout << "Note: at constant density gravity only adds "
                         "hydrostatic pressure, the velocity field is "
                         "unchanged.\n";
    }
    if (gravityEnabled && phases == 1) {
        if (!ask("gravityMode",
                 "How gravity is applied: reduced (exact at one density, the "
                 "head is added on output) or body (real force in the solve)"))
            return;
    }
    std::cout << boundaryHelp();
    if (!ask("caseType", "Case: channel or cavity")) return;
    if (caseType == CaseType::Cavity) {
        if (!ask("lidSpeed", "Speed the lid slides at (m/s)")) return;
    } else {
    if (!ask("bcLeft", "Left boundary")) return;
    if (boundaries[BoundarySide::Left].kind == BoundaryKind::MovingWall)
        if (!ask("bcLeftSpeed", "Speed the left wall slides at (m/s)")) return;
    if (!ask("bcRight", "Right boundary")) return;
    if (boundaries[BoundarySide::Right].kind == BoundaryKind::MovingWall)
        if (!ask("bcRightSpeed", "Speed the right wall slides at (m/s)")) return;
    if (!ask("bcBottom", "Bottom boundary")) return;
    if (boundaries[BoundarySide::Bottom].kind == BoundaryKind::MovingWall)
        if (!ask("bcBottomSpeed", "Speed the bottom wall slides at (m/s)")) return;
    if (!ask("bcTop", "Top boundary")) return;
    if (boundaries[BoundarySide::Top].kind == BoundaryKind::MovingWall)
        if (!ask("bcTopSpeed", "Speed the top wall slides at (m/s)")) return;
    if (volumetric()) {
        if (!ask("bcFront", "Front boundary (z = 0)")) return;
        if (boundaries[BoundarySide::Front].kind == BoundaryKind::MovingWall)
            if (!ask("bcFrontSpeed", "Speed the front wall slides at (m/s)"))
                return;
        if (!ask("bcBack", "Back boundary (z = Lz)")) return;
        if (boundaries[BoundarySide::Back].kind == BoundaryKind::MovingWall)
            if (!ask("bcBackSpeed", "Speed the back wall slides at (m/s)"))
                return;
    }
    }
    if (!ask("steadyTolerance",
             "Stop when the field stops changing? Give the rate, or 0 to run "
             "the whole of totalTime (1e-5 is a good number for a cavity)"))
        return;
    if (!ask("turbulence",
             "Turbulence model: none (solve what is on the grid and nothing "
             "else), smagorinsky (large eddy, wants a fine grid) or kOmegaSST "
             "(two transport equations, what a wall bounded flow at a high "
             "Reynolds number needs)")) return;
    if (turbulence == TurbulenceKind::Smagorinsky)
        if (!ask("Cs", "Smagorinsky constant (0.17 isotropic, 0.1 with a wall "
                       "in it)")) return;
    if (turbulence == TurbulenceKind::KOmegaSST) {
        if (!ask("turbIntensity",
                 "How turbulent the inlet is, as a fraction of its speed "
                 "(0.01 a wind tunnel, 0.05 a pipe, 0.1 behind something)"))
            return;
        if (!ask("turbLengthScale",
                 "Size of the largest eddy coming in (m), or 0 for a tenth of "
                 "Ly")) return;
    }
    if (!ask("convection",
             "Convective scheme: upwind (first order, what this solver has "
             "always used), muscl or central")) return;
    if (convection == ConvectionScheme::Muscl)
        if (!ask("limiter", "Limiter for muscl: minmod, vanLeer or superbee"))
            return;
    if (!ask("timeScheme",
             "Time scheme: euler, rk2 or rk3. Anything but upwind wants rk2 or "
             "rk3, euler alone is only conditionally stable there")) return;
    if (!ask("CFL", "Enter CFL number (recommended 0.3-0.5)")) return;
    if (!ask("totalTime", "Enter total simulation time (seconds)")) return;
    if (!ask("dtUpdateInterval",
             "Enter steps between dt recomputations (recommended 5)")) return;
    if (!ask("omega", "Enter SOR relaxation parameter omega (coarsest multigrid "
                      "level, 1.6-1.85)")) return;
    if (!ask("smootherOmega",
             "Enter SOR relaxation parameter smootherOmega (V-cycle smoother, "
             "1.0-1.3 recommended)")) return;
    if (!ask("mgIterations",
             "Enter multigrid V-cycles per step (2 by default, 4-10 max "
             "recommended)")) return;
    if (!ask("mgTolerance",
             "Enter multigrid relative residual tolerance (1e-4 HEAVILY "
             "recommended)")) return;
    if (!ask("mgMinCoarseSize",
             "Enter minimum coarse grid size (8 recommended)")) return;
    if (!ask("saveInterval",
             "Enter VTK save interval in steps (1 = every step, 20 "
             "recommended)")) return;
    if (!ask("geometryFile",
             "Enter path to 3D model (or 'none' for circle)")) return;
    if (!ask("sliceAngleX",
             "Enter around the axis going towards the observer (degrees)"))
        return;
    if (volumetric())
        if (!ask("sliceAngleY",
                 "Enter around the horizontal axis across the screen (degrees)"))
            return;
    if (!ask("sliceAngleZ", "Enter around a vertical axis (degrees)")) return;
    if (!ask("sliceRotation",
             "Enter rotation in the simulation plane (degrees)")) return;
    if (!ask("invertSection", "Mirror the section? (0 = no, 1 = yes)")) return;
    if (!ask("extraFields",
             "Extra fields to write into every frame, comma separated, empty "
             "for none (vorticity, divergence, speed, objectId, density, "
             "source, curvature, nuT, k, omega, wallDistance, strain)"))
        return;
    if (!ask("profiles",
             "Extra models and where they sit, empty for just geometryFile "
             "(the rules are above)"))
        return;
    if (!ask("wallMotion",
             "Wall behaviour (Enter leaves every wall stationary and no-slip)"))
        return;
    if (!ask("bodyMotion",
             "Bodies that travel through the grid (Enter leaves every body "
             "where it is)"))
        return;
    if (bodiesMove()) {
        if (!ask("bodyCoupling",
                 "How a free body is coupled to the fluid (weak / added / "
                 "strong)"))
            return;
        if (bodyCoupling == BodyCoupling::Strong &&
            !ask("bodyIterations",
                 "Most force/motion iterations inside one step"))
            return;
        if (!ask("bodyForceReport",
                 "Also work out the fluid force on bodies whose path you set? "
                 "(it never changes where they go)"))
            return;
        if (!ask("bodyCollisions",
                 "Do bodies bounce off each other and off the walls? "
                 "(0 = they pass through)"))
            return;
        if (bodyCollisions &&
            !ask("bodyRestitution",
                 "How much of the closing speed survives a bounce (0 to 1)"))
            return;
    }
    if (!ask("useCuda",
             "Use cuda? (0 = no, 1 = yes, ignored on a CPU-only build)"))
        return;
    std::cout << "Configuration read.\n";
}
void Config::print() const {
    std::cout << "\n--- Current Configuration ---\n";
    std::cout << "  mode             = " << (restart ? "CONTINUE" : "NEW") << "\n";
    if (restart) {
        std::cout << "  restartFile      = " << restartFile << "\n";
        std::cout << "  addTime          = " << addTime
                  << " s (0 = totalTime is used as is)\n";
    }
    if (volumetric()) {
        std::cout << "  domain           = " << Lx << " x " << Ly << " x " << Lz
                  << " m\n";
        std::cout << "  cells            = " << nx << " x " << ny << " x " << nz
                  << "\n";
    } else {
        std::cout << "  Lx               = " << Lx << " m\n";
        std::cout << "  Ly               = " << Ly << " m\n";
        std::cout << "  nx               = " << nx << "\n";
        std::cout << "  ny               = " << ny << "\n";
    }
    std::cout << "  U0               = " << U0 << " m/s\n";
    if (phases > 1) {
        std::cout << "  phases           = 2\n";
        std::cout << "  fluid 1          = rho " << rho1 << " kg/m^3, nu "
                  << nu1 << " m^2/s\n";
        std::cout << "  fluid 2          = rho " << rho2 << " kg/m^3, nu "
                  << nu2 << " m^2/s\n";
        std::cout << "  phaseInit        = " << phaseInitName(phaseInit);
        if (phaseInit == PhaseInit::File)
            std::cout << " (" << initialPhaseFile << ")";
        else if (volumetric())
            std::cout << ", level " << phaseLevel << ", at (" << phaseX << ", "
                      << phaseY << ", " << phaseZ << ")";
        else
            std::cout << ", level " << phaseLevel << ", at (" << phaseX << ", "
                      << phaseY << ")";
        std::cout << "\n";
        std::cout << "  mixing           = " << mixingKindName(mixing);
        if (mixing == MixingKind::Miscible)
            std::cout << ", diffusivity " << diffusivity << " m^2/s";
        std::cout << "\n";
        if (mixing == MixingKind::Immiscible) {
            std::cout << "  vofScheme        = " << vofSchemeName(vofScheme)
                      << "\n";
            std::cout << "  surfaceTension   = " << surfaceTension << " N/m";
            if (surfaceTension > 0.0f)
                std::cout << ", contact angle " << contactAngle << " deg";
            std::cout << "\n";
        }
    } else {
        std::cout << "  nu               = " << nu << " m^2/s\n";
        std::cout << "  ro               = " << ro << " kg/m^3\n";
    }
    if (!sources.empty())
        std::cout << "  sources          = " << resolvedSources().size()
                  << " (" << sources << ")\n";
    std::cout << "  gravity          = " << (gravityEnabled ? "ON" : "OFF") << "\n";
    if (gravityEnabled) {
        std::cout << "  gravityAccel     = " << gravityAccel << " m/s^2\n";
        std::cout << "  gravityAngle     = " << gravityAngle
                  << " deg (clockwise, 0 = down)\n";
        if (gravityTilt != 0.0f)
            std::cout << "  gravityTilt      = " << gravityTilt
                      << " deg (out of the xy plane, towards +z)\n";
        std::cout << "  gravityMode      = "
                  << enumName(static_cast<int>(gravityMode), kGravityModes)
                  << (gravityMode == GravityMode::Reduced
                          ? " (head added on output only)"
                          : " (body force, p is the total pressure)")
                  << "\n";
    }
    std::cout << "  convection       = "
              << enumName(static_cast<int>(convection), kConvectionSchemes);
    if (convection == ConvectionScheme::Muscl)
        std::cout << " (" << enumName(static_cast<int>(limiter), kLimiters)
                  << ")";
    std::cout << "\n";
    std::cout << "  timeScheme       = "
              << enumName(static_cast<int>(timeScheme), kTimeSchemes) << "\n";
    std::cout << "  caseType         = " << caseTypeName(caseType);
    if (caseType == CaseType::Cavity)
        std::cout << ", lid at " << lidSpeed << " m/s";
    std::cout << "\n";
    if (steadyTolerance > 0.0f)
        std::cout << "  steadyTolerance  = " << steadyTolerance
                  << " (stops when the field stops changing)\n";
    std::cout << "  boundaries       = "
              << boundaryKindName(boundaries[BoundarySide::Left].kind) << " | "
              << boundaryKindName(boundaries[BoundarySide::Right].kind) << " | "
              << boundaryKindName(boundaries[BoundarySide::Bottom].kind) << " | "
              << boundaryKindName(boundaries[BoundarySide::Top].kind);
    if (volumetric())
        std::cout << " | "
                  << boundaryKindName(boundaries[BoundarySide::Front].kind)
                  << " | "
                  << boundaryKindName(boundaries[BoundarySide::Back].kind)
                  << "   (left | right | bottom | top | front | back)\n";
    else
        std::cout << "   (left | right | bottom | top)\n";
    for (int side = 0; side < kBoundarySides; ++side) {
        const BoundarySpec& spec = boundaries.side[side];
        if (spec.kind != BoundaryKind::Inlet)
            continue;
        std::cout << "  inlet ("
                  << boundarySideName(static_cast<BoundarySide>(side))
                  << ")"
                  << std::string(std::max<size_t>(
                         1, 9 - std::string(boundarySideName(
                                    static_cast<BoundarySide>(side))).size()),
                                 ' ')
                  << "= " << (spec.speedSet ? spec.speed : U0) << " m/s, "
                  << inletProfileName(spec.profile);
        if (spec.from > 0.0f || spec.to < 1.0f)
            std::cout << ", band " << spec.from << ".." << spec.to
                      << " of the side";
        if (spec.from2 > 0.0f || spec.to2 < 1.0f)
            std::cout << ", " << spec.from2 << ".." << spec.to2
                      << " along its second axis";
        std::cout << "\n";
    }
    std::cout << "  CFL              = " << CFL << "\n";
    std::cout << "  totalTime        = " << totalTime << " s\n";
    std::cout << "  dtUpdateInterval = " << dtUpdateInterval << " steps\n";
    std::cout << "  omega            = " << omega << " (coarsest level)\n";
    std::cout << "  smootherOmega    = " << smootherOmega << " (V-cycle smoother)\n";
    std::cout << "  mgIterations     = " << mgIterations << " V-cycles/step\n";
    std::cout << "  mgTolerance      = " << mgTolerance << " (relative)\n";
    std::cout << "  mgMinCoarseSize  = " << mgMinCoarseSize << " cells/axis\n";
    std::cout << "  saveInterval     = " << saveInterval << " steps\n";
    if (!runName.empty())
        std::cout << "  runName          = " << runName << "\n";
    std::cout << "  extraFields      = "
              << (extraFields.empty() ? "none" : extraFields) << "\n";
    std::cout << "  frameState       = " << frameStateName(frameState);
    if (compressible())
        std::cout << (frameState == FrameState::Full
                          ? "  (conserved variables written out as well)"
                          : "  (half the file; the conserved variables are "
                            "rebuilt exactly on restart)");
    else
        std::cout << (frameState == FrameState::Minimal
                          ? "  (a sixth smaller; a continuation is projected "
                            "rather than exact)"
                          : "  (face velocities kept, so a continuation is "
                            "exact)");
    std::cout << "\n";
    std::cout << "  outputDir        = " << outputDir << "\n";
    std::cout << "  geometryFile     = " << geometryFile << "\n";
    std::cout << "  sliceAngleX      = " << sliceAngleX << " deg\n";
    if (volumetric())
        std::cout << "  sliceAngleY      = " << sliceAngleY << " deg\n";
    std::cout << "  sliceAngleZ      = " << sliceAngleZ << " deg\n";
    std::cout << "  invertSection    = " << invertSection << "\n";
    std::cout << "  sliceRotation    = " << sliceRotation << " deg\n";
    std::cout << "  wallMotion       = "
              << (wallMotion.empty() ? "none" : wallMotion) << "\n";
    std::cout << "  bodyMotion       = "
              << (bodyMotion.empty() ? "none (nothing travels)" : bodyMotion)
              << "\n";
    if (bodiesMove()) {
        std::cout << "  bodyCoupling     = " << bodyCouplingName(bodyCoupling);
        if (bodyCoupling == BodyCoupling::Strong)
            std::cout << ", up to " << bodyIterations << " iterations a step";
        std::cout << "\n";
        std::cout << "  bodyCollisions   = "
                  << (bodyCollisions ? "on" : "off, bodies pass through each "
                                              "other and through the walls")
                  << "\n";
        if (bodyCollisions)
            std::cout << "  bodyRestitution  = " << bodyRestitution
                      << " of the closing speed survives a bounce\n";
        if (bodyForceReport)
            std::cout << "  bodyForceReport  = on, and it still changes "
                         "nothing about where a set path goes\n";
    }
    std::cout << "  regime           = " << regimeName(regime);
    if (compressible())
        std::cout << " (density is a variable and there is no pressure solve)";
    std::cout << "\n";
    if (compressible()) {
        std::cout << "  gamma            = " << gamma << "\n";
        std::cout << "  R                = " << R << " J/(kg K)\n";
        std::cout << "  T0               = " << T0 << " K\n";
        std::cout << "  pInf             = " << pInf << " Pa\n";
        std::cout << "  machInlet        = " << machInlet << "\n";
        if (twoSpecies()) {
            std::cout << "  gamma2 / R2      = " << gamma2 << " / " << R2
                      << " J/(kg K)\n";
            std::cout << "  speciesMode      = "
                      << speciesModeName(speciesMode) << "\n";
            if (speciesMode == SpeciesMode::Passive)
                std::cout << "  !! WARNING: the composition is carried along "
                             "but does NOT set gamma and R.\n"
                             "     Both are frozen at the first gas, so the "
                             "speed of sound, the temperature\n"
                             "     and every wave speed in the run are air's "
                             "wherever the second gas is.\n"
                             "     Use speciesMode=active unless you are "
                             "deliberately comparing against this.\n";
        }
        std::cout << "  acousticFields   = "
                  << (acousticFields ? "on" : "off") << "\n";
        if (acousticFields)
            std::cout << "  acousticWindow   = " << acousticWindow << " s, 0 dB at "
                      << acousticRef << " Pa\n";
        std::cout << "  amrLevels        = " << amrLevels;
        if (amrLevels > 0)
            std::cout << ", " << amrCriterion << ", threshold "
                      << amrThreshold << ", regrid every " << amrEvery
                      << " steps";
        std::cout << "\n";
        std::cout << "  gridStretch      = " << stretchKindName(gridStretch);
        if (gridStretch != StretchKind::Off)
            std::cout << ", ratio " << stretchRatio << ", band "
                      << refineNear;
        std::cout << "\n";
        std::cout << "  microphones      = "
                  << (microphones.empty() ? "none" : microphones) << "\n";
        if (!microphones.empty()) {
            std::cout << "  micAudio         = "
                      << (micAudio ? "on" : "off");
            if (micAudio) {
                std::cout << ", " << micAudioRate << " Hz";
                if (micAudioSpeed != 1.0f)
                    std::cout << ", played at " << micAudioSpeed
                              << " of real speed";
            }
            std::cout << "\n";
        }
        if (micAudio && microphones.empty())
            std::cout << "\n  !!! micAudio=1 with no microphones= is nothing "
                         "to record. Add microphones=x=..,y=..\n\n";
    }
    std::cout << "  turbulence       = " << turbulenceKindName(turbulence);
    if (turbulence == TurbulenceKind::None)
        std::cout << " (only what the grid can hold is solved)";
    std::cout << "\n";
    if (turbulence == TurbulenceKind::Smagorinsky)
        std::cout << "  Cs               = " << Cs << "\n";
    if (turbulence == TurbulenceKind::KOmegaSST) {
        std::cout << "  turbIntensity    = " << turbIntensity
                  << " of the inlet speed\n";
        std::cout << "  turbLengthScale  = ";
        if (turbLengthScale > 0.0f)
            std::cout << turbLengthScale << " m\n";
        else
            std::cout << 0.1f * Ly << " m (a tenth of Ly)\n";
    }
    std::cout << "  profiles         = "
              << (profiles.empty() ? "none (geometryFile only)" : profiles)
              << "\n";
    std::cout << " CUDA? Yes/No:       " << (useCuda ? "Yes" : "No") << "\n";
    std::cout << "--------------------------------\n";
}

bool Config::emptyDomain() const {
    if (!profiles.empty())
        return false;
    std::string lowered = geometryFile;
    for (char& c : lowered)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lowered == "empty";
}

std::vector<Profile> Config::resolvedProfiles() const {
    std::vector<Profile> list;
    std::string ignored;
    if (!profiles.empty())
        parseProfiles(profiles, list, ignored);

    if (list.empty()) {
        std::string name = geometryFile;
        std::string lowered = name;
        for (char& c : lowered)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name.empty() || lowered == "none" || lowered == "empty")
            return list;
        Profile only;
        only.file = std::move(name);
        list.push_back(std::move(only));
    }

    for (Profile& profile : list) {
        if (!profile.angleSet) {
            profile.angleX = sliceAngleX;
            profile.angleY = sliceAngleY;
            profile.angleZ = sliceAngleZ;
        }
        if (!profile.invertSet)
            profile.invert = invertSection;
        if (profile.rotation == 0.0f)
            profile.rotation = sliceRotation;
    }
    return list;
}

void Config::gravityVector(float& gx, float& gy, float& gz) const {
    if (!gravityEnabled) {
        gx = gy = gz = 0.0f;
        return;
    }
    constexpr float degToRad = 3.14159265358979f / 180.0f;
    const float rad = gravityAngle * degToRad;
    const float tilt = gravityTilt * degToRad;
    const float planar = gravityAccel * std::cos(tilt);
    gx = -planar * std::sin(rad);
    gy = -planar * std::cos(rad);
    gz = gravityAccel * std::sin(tilt);
}

bool Config::regimeConsistent(std::string& error) const {
    if (!compressible()) {
        if (amrLevels > 0) {
            error = "amrLevels is compressible only. Adaptive refinement here "
                    "means patches of a finer grid over the base one, and the "
                    "projection solver's pressure solve is global - a "
                    "multigrid hierarchy over a patch hierarchy is a "
                    "different solver, not a setting. Drop amrLevels=, or add "
                    "regime=compressible.";
            return false;
        }
        if (gridStretch != StretchKind::Off) {
            error = "gridStretch is compressible only. The projection solver "
                    "indexes a flat array with one dx and one dy, and the "
                    "multigrid halves that grid to build its hierarchy - "
                    "neither survives cells of different sizes. Drop "
                    "gridStretch=, or add regime=compressible.";
            return false;
        }
        if (acousticFields || !microphones.empty()) {
            error = "acousticFields and microphones are compressible only. "
                    "An incompressible run has no acoustics to record: the "
                    "projection method makes the speed of sound infinite by "
                    "construction, so a pressure change is everywhere at once "
                    "and there is nothing travelling to listen to. Add "
                    "regime=compressible, or drop them.";
            return false;
        }
        return true;
    }

    if (turbulent()) {
        error = "turbulence is incompressible only in this build. The "
                "compressible solver is inviscid Euler - it has no viscous "
                "term for an eddy viscosity to be added to. Drop "
                "turbulence=, or drop regime=compressible.";
        return false;
    }
    if (hasSurfaceTension()) {
        error = "surfaceTension is incompressible only. Two gases share a "
                "composition rather than an interface, so there is no surface "
                "for it to pull on. Drop surfaceTension=.";
        return false;
    }
    if (!sources.empty()) {
        error = "sources are incompressible only. A source that pushes fluid "
                "out of itself needs a pressure solve to make room for it, "
                "and there is not one here. Drop sources=.";
        return false;
    }
    if (amrLevels > 0 && gridStretch != StretchKind::Off) {
        error = "amrLevels and gridStretch do not go together. Refinement "
                "halves a cell to make a patch, and it has nothing to halve "
                "when every cell is already a different size. Pick one: "
                "gridStretch puts the small cells where you say, amrLevels "
                "puts them where the flow says and moves them.";
        return false;
    }
    if (multiphase() && mixing == MixingKind::Immiscible) {
        error = "two gases always mix: there is no interface to carry and no "
                "scheme here that would carry one. Set mixing=miscible, or "
                "drop back to phases=1.";
        return false;
    }
    if (gravityEnabled) {
        error = "gravity is incompressible only in this build. It would be a "
                "source term in the momentum and the energy of a compressible "
                "run, and neither is written. Drop gravityEnabled=1.";
        return false;
    }
    if (caseType == CaseType::Cavity) {
        error = "the cavity preset drives the flow with a sliding lid, which "
                "is a low speed case by definition. Use caseType=channel or "
                "caseType=shockTube.";
        return false;
    }
    return true;
}

std::vector<Microphone> Config::resolvedMicrophones() const {
    std::vector<Microphone> out;
    std::string ignored;
    parseMicrophones(microphones, out, ignored);
    return out;
}

std::vector<FlowSource> Config::resolvedSources() const {
    std::vector<FlowSource> list;
    std::string ignored;
    if (!sources.empty())
        parseSources(sources, list, ignored);
    return list;
}

std::string Config::serialize() const {
    std::ostringstream out;

    out << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "regime=" << regimeName(regime) << "\n"
        << "Lx=" << Lx << "\n"
        << "Ly=" << Ly << "\n"
        << "Lz=" << Lz << "\n"
        << "nx=" << nx << "\n"
        << "ny=" << ny << "\n"
        << "nz=" << nz << "\n"
        << "U0=" << U0 << "\n"
        << "nu=" << nu << "\n"
        << "ro=" << ro << "\n"

        << "phases=" << phases << "\n"
        << "rho1=" << rho1 << "\n"
        << "rho2=" << rho2 << "\n"
        << "nu1=" << nu1 << "\n"
        << "nu2=" << nu2 << "\n"
        << "vofScheme=" << vofSchemeName(vofScheme) << "\n"
        << "mixing=" << mixingKindName(mixing) << "\n"
        << "diffusivity=" << diffusivity << "\n"
        << "surfaceTension=" << surfaceTension << "\n"
        << "contactAngle=" << contactAngle << "\n"
        << "phaseInit=" << phaseInitName(phaseInit) << "\n"
        << "phaseLevel=" << phaseLevel << "\n"
        << "phaseX=" << phaseX << "\n"
        << "phaseY=" << phaseY << "\n"
        << "phaseZ=" << phaseZ << "\n"
        << "initialPhaseFile=" << initialPhaseFile << "\n"
        // Frames written before gravity existed simply do not carry these keys,
        // and setParam is never called for them, so the defaults leave gravity
        // off. Old frames stay loadable, new frames stay readable by old builds.
        << "gravityEnabled=" << (gravityEnabled ? 1 : 0) << "\n"
        << "gravityAccel=" << gravityAccel << "\n"
        << "gravityAngle=" << gravityAngle << "\n"
        << "gravityTilt=" << gravityTilt << "\n"
        << "gravityMode="
        << enumName(static_cast<int>(gravityMode), kGravityModes) << "\n"
        << "convection="
        << enumName(static_cast<int>(convection), kConvectionSchemes) << "\n"
        << "limiter="
        << enumName(static_cast<int>(limiter), kLimiters) << "\n"
        << "timeScheme="
        << enumName(static_cast<int>(timeScheme), kTimeSchemes) << "\n"
        << "caseType=" << caseTypeName(caseType) << "\n"
        << "lidSpeed=" << lidSpeed << "\n"
        << "steadyTolerance=" << steadyTolerance << "\n"
        << "bcLeft=" << boundaryKindName(boundaries[BoundarySide::Left].kind) << "\n"
        << "bcRight=" << boundaryKindName(boundaries[BoundarySide::Right].kind) << "\n"
        << "bcBottom=" << boundaryKindName(boundaries[BoundarySide::Bottom].kind) << "\n"
        << "bcTop=" << boundaryKindName(boundaries[BoundarySide::Top].kind) << "\n"
        << "bcFront=" << boundaryKindName(boundaries[BoundarySide::Front].kind) << "\n"
        << "bcBack=" << boundaryKindName(boundaries[BoundarySide::Back].kind) << "\n"
        << "inletFrom=" << boundaries[BoundarySide::Left].from << "\n"
        << "inletTo=" << boundaries[BoundarySide::Left].to << "\n"
        << "inletFrom2=" << boundaries[BoundarySide::Left].from2 << "\n"
        << "inletTo2=" << boundaries[BoundarySide::Left].to2 << "\n"
        << "inletProfile="
        << inletProfileName(boundaries[BoundarySide::Left].profile) << "\n";

    for (int side = 0; side < kBoundarySides; ++side) {
        const BoundarySpec& spec = boundaries.side[side];
        if (!spec.speedSet)
            continue;
        static const char* const kNames[kBoundarySides] = {
            "bcLeftSpeed", "bcRightSpeed", "bcBottomSpeed", "bcTopSpeed",
            "bcFrontSpeed", "bcBackSpeed"};
        out << kNames[side] << "=" << spec.speed << "\n";
    }

    out
        << "CFL=" << CFL << "\n"
        << "dtUpdateInterval=" << dtUpdateInterval << "\n"
        << "dtSafety=" << dtSafety << "\n"
        << "omega=" << omega << "\n"
        << "smootherOmega=" << smootherOmega << "\n"
        << "mgIterations=" << mgIterations << "\n"
        << "mgTolerance=" << mgTolerance << "\n"
        << "mgMinCoarseSize=" << mgMinCoarseSize << "\n"
        << "saveInterval=" << saveInterval << "\n"
        << "useCuda=" << (useCuda ? 1 : 0) << "\n"
        << "sliceAngleX=" << sliceAngleX << "\n"
        << "sliceAngleY=" << sliceAngleY << "\n"
        << "sliceAngleZ=" << sliceAngleZ << "\n"
        << "sliceRotation=" << sliceRotation << "\n"
        << "invertSection=" << (invertSection ? 1 : 0) << "\n";

    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "totalTime=" << totalTime << "\n";

    out << "outputDir=" << outputDir << "\n"
        << "geometryFile=" << geometryFile << "\n"
        << "wallMotion=" << wallMotion << "\n"
        << "bodyMotion=" << bodyMotion << "\n"
        << "bodyCoupling=" << bodyCouplingName(bodyCoupling) << "\n"
        << "bodyIterations=" << bodyIterations << "\n"
        << "bodyCollisions=" << (bodyCollisions ? 1 : 0) << "\n"
        << "bodyRestitution=" << bodyRestitution << "\n"
        << "bodyForceReport=" << (bodyForceReport ? 1 : 0) << "\n"
        << "gamma=" << gamma << "\n"
        << "R=" << R << "\n"
        << "gamma2=" << gamma2 << "\n"
        << "R2=" << R2 << "\n"
        << "T0=" << T0 << "\n"
        << "pInf=" << pInf << "\n"
        << "machInlet=" << machInlet << "\n"
        << "speciesMode=" << speciesModeName(speciesMode) << "\n"
        << "acousticFields=" << (acousticFields ? 1 : 0) << "\n"
        << "acousticWindow=" << acousticWindow << "\n"
        << "acousticRef=" << acousticRef << "\n"
        << "microphones=" << microphones << "\n"
        << "micInterval=" << micInterval << "\n"
        << "micAudio=" << (micAudio ? 1 : 0) << "\n"
        << "micAudioRate=" << micAudioRate << "\n"
        << "micAudioSpeed=" << micAudioSpeed << "\n"
        << "gridStretch=" << stretchKindName(gridStretch) << "\n"
        << "stretchRatio=" << stretchRatio << "\n"
        << "refineNear=" << refineNear << "\n"
        << "amrLevels=" << amrLevels << "\n"
        << "amrEvery=" << amrEvery << "\n"
        << "amrThreshold=" << amrThreshold << "\n"
        << "amrBuffer=" << amrBuffer << "\n"
        << "amrMinPatch=" << amrMinPatch << "\n"
        << "amrMaxPatch=" << amrMaxPatch << "\n"
        << "amrCriterion=" << amrCriterion << "\n"
        << "turbulence=" << turbulenceKindName(turbulence) << "\n"
        << "Cs=" << Cs << "\n"
        << "turbIntensity=" << turbIntensity << "\n"
        << "turbLengthScale=" << turbLengthScale << "\n"
        << "sources=" << sources << "\n"
        << "profiles=" << profiles << "\n"
        << "extraFields=" << extraFields << "\n"
        << "frameState=" << frameStateName(frameState) << "\n"
        << "runName=" << runName << "\n"
        << "vortices=" << vortices << "\n";

    return out.str();
}

bool Config::setParam(const std::string& key, const std::string& value) {
    std::string ignored;
    return setParam(key, value, ignored, nullptr);
}

bool Config::setParam(const std::string& key,
                      const std::string& value,
                      std::string& error,
                      std::string* warning) {
    error.clear();
    if (warning)
        warning->clear();

    const std::string k = canonicalKey(key);
    if (k.empty()) {
        const std::string guess = suggestKey(key);
        error = badValue(trimSpace(key), cleanValue(value),
                         guess.empty()
                             ? "there is no such parameter, run with --help "
                               "for the list"
                             : "there is no such parameter. Did you mean " +
                                   guess + "?");
        return false;
    }

    bool ok = false;
    if      (k == "Lx") ok = assignFloat(Lx, k, value, kTiny, kHuge,
             "the domain width must be a positive length in metres", error);
    else if (k == "Ly") ok = assignFloat(Ly, k, value, kTiny, kHuge,
             "the domain height must be a positive length in metres", error);
    else if (k == "Lz") ok = assignFloat(Lz, k, value, kTiny, kHuge,
             "the domain depth must be a positive length in metres", error);
    else if (k == "nx") ok = assignInt(nx, k, value, 8, kIntMax,
             "the grid needs at least 8 cells per axis, the multigrid has "
             "nothing to coarsen below that", error);
    else if (k == "ny") ok = assignInt(ny, k, value, 8, kIntMax,
             "the grid needs at least 8 cells per axis, the multigrid has "
             "nothing to coarsen below that", error);
    else if (k == "nz") ok = assignInt(nz, k, value, 1, kIntMax,
             "nz counts the cells across the depth and the smallest number of "
             "them is 1, which is the single plane every earlier version "
             "solved; raising it is what makes the run three-dimensional",
             error);
    else if (k == "U0") ok = assignFloat(U0, k, value, -kHuge, kHuge,
             "the inlet velocity must be a finite number", error);
    else if (k == "nu") ok = assignFloat(nu, k, value, 0.0, kHuge,
             "viscosity cannot be negative (0 means inviscid)", error);
    else if (k == "ro") ok = assignFloat(ro, k, value, kTiny, kHuge,
             "density must be positive", error);
    else if (k == "gravityEnabled") ok = assignBool(gravityEnabled, k, value, error);
    else if (k == "gravityAccel") ok = assignFloat(gravityAccel, k, value, 0.0, kHuge,
             "this is a magnitude, it cannot be negative; to point gravity the "
             "other way use gravityAngle=180", error);
    else if (k == "gravityAngle") ok = assignFloat(gravityAngle, k, value, -kHuge, kHuge,
             "the angle must be a finite number of degrees", error);
    else if (k == "gravityTilt") ok = assignFloat(gravityTilt, k, value, -kHuge, kHuge,
             "the tilt must be a finite number of degrees; it leans gravity out "
             "of the xy plane towards +z, and 0 leaves it in the plane where "
             "gravityAngle alone puts it", error);
    else if (k == "gravityMode") {
        int mode = static_cast<int>(gravityMode);
        ok = assignEnumValue(mode, k, value, kGravityModes, error);
        if (ok && phases > 1 && !compressible() &&
            static_cast<GravityMode>(mode) == GravityMode::Reduced) {
            error = badValue(k, cleanValue(value),
                             "at two phases the weight of the fluid is what "
                             "moves it, so it has to be inside the solve. "
                             "gravityMode=body is the only one that is");
            ok = false;
        }
        if (ok) gravityMode = static_cast<GravityMode>(mode);
    }
    else if (k == "convection") {
        int scheme = static_cast<int>(convection);
        ok = assignEnumValue(scheme, k, value, kConvectionSchemes, error);
        if (ok) convection = static_cast<ConvectionScheme>(scheme);
    }
    else if (k == "limiter") {
        int kind = static_cast<int>(limiter);
        ok = assignEnumValue(kind, k, value, kLimiters, error);
        if (ok) limiter = static_cast<LimiterKind>(kind);
    }
    else if (k == "timeScheme") {
        int scheme = static_cast<int>(timeScheme);
        ok = assignEnumValue(scheme, k, value, kTimeSchemes, error);
        if (ok) timeScheme = static_cast<TimeScheme>(scheme);
    }
    else if (k == "phases") {
        ok = assignInt(phases, k, value, 1, 2,
                       "this solver carries one phase field, so it does one or "
                       "two fluids. Three would need a second field and a rule "
                       "for what happens where all three meet", error);

        if (ok && phases > 1)
            gravityMode = GravityMode::Body;
    }
    else if (k == "rho1") ok = assignFloat(rho1, k, value, 1e-9, kHuge,
             "a density has to be positive", error);
    else if (k == "rho2") ok = assignFloat(rho2, k, value, 1e-9, kHuge,
             "a density has to be positive", error);
    else if (k == "nu1") ok = assignFloat(nu1, k, value, 0.0, kHuge,
             "viscosity cannot be negative", error);
    else if (k == "nu2") ok = assignFloat(nu2, k, value, 0.0, kHuge,
             "viscosity cannot be negative", error);
    else if (k == "phaseLevel") ok = assignFloat(phaseLevel, k, value, 0.0, 1.0,
             "this is a fraction of the domain, so it lives between 0 and 1", error);
    else if (k == "phaseX") ok = assignFloat(phaseX, k, value, 0.0, 1.0,
             "this is a fraction of Lx, so it lives between 0 and 1", error);
    else if (k == "phaseY") ok = assignFloat(phaseY, k, value, 0.0, 1.0,
             "this is a fraction of Ly, so it lives between 0 and 1", error);
    else if (k == "phaseZ") ok = assignFloat(phaseZ, k, value, 0.0, 1.0,
             "this is a fraction of Lz, so it lives between 0 and 1", error);
    else if (k == "initialPhaseFile") { initialPhaseFile = cleanValue(value); ok = true; }
    else if (k == "sources") {
        std::vector<FlowSource> parsed;
        std::string why;
        ok = parseSources(cleanValue(value), parsed, why);
        if (!ok) error = badValue(k, cleanValue(value), why);
        else sources = cleanValue(value);
    }
    else if (k == "mixing") {
        int kind = static_cast<int>(mixing);
        ok = assignEnumValue(kind, k, value, kMixingKinds, error);
        if (ok) {
            mixing = static_cast<MixingKind>(kind);

            if (mixing == MixingKind::Miscible && surfaceTension > 0.0f) {
                surfaceTension = 0.0f;
                if (warning)
                    *warning = "fluids that mix have no interface, so "
                               "surfaceTension has been set back to 0";
            }
        }
    }
    else if (k == "diffusivity") ok = assignFloat(diffusivity, k, value, 0.0, kHuge,
             "a diffusivity cannot be negative; zero means the two only mix "
             "by being stirred together", error);
    else if (k == "surfaceTension") {
        ok = assignFloat(surfaceTension, k, value, 0.0, kHuge,
                         "surface tension is a magnitude in N/m and cannot be "
                         "negative - a negative one would make the interface "
                         "grow instead of shrink, which is not a fluid", error);
        if (ok && surfaceTension > 0.0f && mixing == MixingKind::Miscible) {
            error = badValue(k, cleanValue(value),
                             "these two fluids mix, so there is no interface "
                             "for tension to pull on. Set mixing=immiscible "
                             "first");
            ok = false;
        }
    }
    else if (k == "contactAngle") ok = assignFloat(contactAngle, k, value, 0.0, 180.0,
             "a contact angle is between 0 and 180 degrees, measured inside "
             "fluid 1: under 90 it wets the wall, over 90 the other one does",
             error);
    else if (k == "vofScheme") {
        int scheme = static_cast<int>(vofScheme);
        ok = assignEnumValue(scheme, k, value, kVofSchemes, error);
        if (ok) vofScheme = static_cast<VofScheme>(scheme);
    }
    else if (k == "phaseInit") {
        int init = static_cast<int>(phaseInit);
        ok = assignEnumValue(init, k, value, kPhaseInits, error);
        if (ok) phaseInit = static_cast<PhaseInit>(init);
    }
    else if (k == "caseType") {
        CaseType type = caseType;
        std::string why;
        ok = parseCaseType(trimSpace(cleanValue(value)), type, why);
        if (!ok) {
            error = badValue(k, cleanValue(value), why);
        } else {
            caseType = type;
            boundaries = (type == CaseType::Cavity)
                             ? cavityBoundaries(lidSpeed)
                             : defaultChannelBoundaries();

            if (type == CaseType::Cavity && geometryFile == "none")
                geometryFile = "empty";
        }
    }
    else if (k == "lidSpeed") {
        ok = assignFloat(lidSpeed, k, value, -kHuge, kHuge,
             "the lid slides at a finite number of m/s", error);
        if (ok && caseType == CaseType::Cavity) {
            boundaries[BoundarySide::Top].speed = lidSpeed;
            boundaries[BoundarySide::Top].speedSet = true;
        }
    }
    else if (k == "steadyTolerance") ok = assignFloat(steadyTolerance, k, value, 0.0, kHuge,
             "this is a rate of change measured against the driving speed, so "
             "it cannot be negative; 0 turns the check off", error);
    else if (k == "bcLeft")   ok = assignSideKind(boundaries[BoundarySide::Left], k, value, error);
    else if (k == "bcRight")  ok = assignSideKind(boundaries[BoundarySide::Right], k, value, error);
    else if (k == "bcBottom") ok = assignSideKind(boundaries[BoundarySide::Bottom], k, value, error);
    else if (k == "bcTop")    ok = assignSideKind(boundaries[BoundarySide::Top], k, value, error);
    else if (k == "bcFront")  ok = assignSideKind(boundaries[BoundarySide::Front], k, value, error);
    else if (k == "bcBack")   ok = assignSideKind(boundaries[BoundarySide::Back], k, value, error);
    else if (k == "bcLeftSpeed" || k == "bcRightSpeed" ||
             k == "bcBottomSpeed" || k == "bcTopSpeed" ||
             k == "bcFrontSpeed" || k == "bcBackSpeed") {
        BoundarySide side = BoundarySide::Left;
        if (k == "bcRightSpeed")       side = BoundarySide::Right;
        else if (k == "bcBottomSpeed") side = BoundarySide::Bottom;
        else if (k == "bcTopSpeed")    side = BoundarySide::Top;
        else if (k == "bcFrontSpeed")  side = BoundarySide::Front;
        else if (k == "bcBackSpeed")   side = BoundarySide::Back;
        ok = assignFloat(boundaries[side].speed, k, value, -kHuge, kHuge,
             "the speed this side imposes must be a finite number of m/s", error);
        if (ok) boundaries[side].speedSet = true;
    }
    else if (k == "inletFrom" || k == "inletTo") {
        float target = 0.0f;
        ok = assignFloat(target, k, value, 0.0, 1.0,
             "this is a fraction of the side measured from its low end, so it "
             "lives between 0 and 1", error);
        if (ok)
            for (int side = 0; side < kBoundarySides; ++side) {
                if (k == "inletFrom") boundaries.side[side].from = target;
                else                  boundaries.side[side].to = target;
            }
    }
    else if (k == "inletFrom2" || k == "inletTo2") {
        float target = 0.0f;
        ok = assignFloat(target, k, value, 0.0, 1.0,
             "this is a fraction of the second axis of the side, measured from "
             "its low end, so it lives between 0 and 1", error);
        if (ok)
            for (int side = 0; side < kBoundarySides; ++side) {
                if (k == "inletFrom2") boundaries.side[side].from2 = target;
                else                   boundaries.side[side].to2 = target;
            }
    }
    else if (k == "inletProfile") {
        InletProfile profile = InletProfile::Uniform;
        std::string why;
        ok = parseInletProfile(trimSpace(cleanValue(value)), profile, why);
        if (!ok) error = badValue(k, cleanValue(value), why);
        else for (int side = 0; side < kBoundarySides; ++side)
            boundaries.side[side].profile = profile;
    }
    else if (k == "CFL") ok = assignFloat(CFL, k, value, kTiny, kHuge,
             "the CFL number must be positive (0.3-0.5 is the usual range)", error);
    else if (k == "totalTime") ok = assignDouble(totalTime, k, value, kTiny, kHuge,
             "the simulated time must be positive", error);
    else if (k == "dtUpdateInterval") ok = assignInt(dtUpdateInterval, k, value, 1, kIntMax,
             "dt is recomputed every N steps, so N is at least 1", error);
    else if (k == "dtSafety") ok = assignFloat(dtSafety, k, value, kTiny, kHuge,
             "this is the fraction of the stable dt that is actually taken, so "
             "it must be positive (0.9 = 90%)", error);
    else if (k == "omega") ok = assignFloat(omega, k, value, kTiny, 2.0 - 1e-6,
             "SOR only converges for 0 < omega < 2", error);
    else if (k == "smootherOmega") ok = assignFloat(smootherOmega, k, value, kTiny, 2.0 - 1e-6,
             "SOR only converges for 0 < smootherOmega < 2", error);
    else if (k == "mgIterations") ok = assignInt(mgIterations, k, value, 1, kIntMax,
             "at least one V-cycle per pressure solve", error);
    else if (k == "mgTolerance") ok = assignFloat(mgTolerance, k, value, kTiny, 1.0,
             "this is a relative residual, so it lives between 0 and 1 "
             "(1e-4 recommended)", error);
    else if (k == "mgMinCoarseSize") ok = assignInt(mgMinCoarseSize, k, value, 2, kIntMax,
             "the coarsest grid needs at least 2 cells per axis", error);
    else if (k == "useCuda") ok = assignBool(useCuda, k, value, error);
    else if (k == "saveInterval") ok = assignInt(saveInterval, k, value, 1, kIntMax,
             "a frame is written every N steps, so N is at least 1", error);
    else if (k == "frameState") {
        ok = parseFrameState(value, frameState);
        if (!ok)
            error = "frameState is slim, minimal or full. Slim is the "
                    "default and drops only what can be put back exactly - "
                    "half of a compressible frame, nothing of an "
                    "incompressible one. Minimal also drops the packed face "
                    "velocities, which is a sixth off an incompressible "
                    "frame and makes a continuation close rather than exact. "
                    "Full writes everything down.";
    }
    else if (k == "outputDir")    { outputDir = cleanValue(value); ok = true; }
    else if (k == "runName")      { runName = cleanValue(value); ok = true; }
    else if (k == "vortices") {
        std::vector<SeedVortex> parsed;
        ok = parseVortices(value, parsed, error);
        if (ok)
            vortices = cleanValue(value);
    }
    else if (k == "geometryFile") { geometryFile = cleanValue(value); ok = true; }
    else if (k == "sliceAngleX") ok = assignFloat(sliceAngleX, k, value, -kHuge, kHuge,
             "the angle must be a finite number of degrees", error);
    else if (k == "sliceAngleY") ok = assignFloat(sliceAngleY, k, value, -kHuge, kHuge,
             "the angle must be a finite number of degrees", error);
    else if (k == "sliceAngleZ") ok = assignFloat(sliceAngleZ, k, value, -kHuge, kHuge,
             "the angle must be a finite number of degrees", error);
    else if (k == "sliceRotation") ok = assignFloat(sliceRotation, k, value, -kHuge, kHuge,
             "the angle must be a finite number of degrees", error);
    else if (k == "invertSection") ok = assignBool(invertSection, k, value, error);
    else if (k == "extraFields") {
        const std::string wanted = trimSpace(cleanValue(value));
        std::string bad;
        size_t pos = 0;
        while (pos <= wanted.size() && bad.empty()) {
            const size_t comma = wanted.find(',', pos);
            std::string name = trimSpace(
                wanted.substr(pos, comma == std::string::npos
                                       ? std::string::npos
                                       : comma - pos));
            pos = (comma == std::string::npos) ? wanted.size() + 1 : comma + 1;
            if (name.empty())
                continue;
            for (char& c : name)
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            if (name != "vorticity" && name != "divergence" &&
                name != "objectid" && name != "speed" &&
                name != "density" && name != "source" &&
                name != "curvature" && name != "nut" && name != "k" &&
                name != "omega" && name != "walldistance" &&
                name != "strain" && name != "temperature" &&
                name != "mach" && name != "speedofsound" &&
                name != "entropy" && name != "pfluct" && name != "spl" &&
                name != "pitch")
                bad = name;
        }
        if (!bad.empty()) {
            error = badValue(k, cleanValue(value),
                             "'" + bad +
                                 "' is not a field this build can write. Use "
                                 "vorticity, divergence, speed, objectId, "
                                 "density, source, curvature, nuT, k, omega, "
                                 "wallDistance, strain, and in the "
                                 "compressible regime temperature, mach, "
                                 "speedOfSound, entropy, pFluct, SPL or "
                                 "pitch, comma separated");
            ok = false;
        } else {
            extraFields = wanted;
            ok = true;
        }
    }
    else if (k == "profiles") {
        std::vector<Profile> parsed;
        ok = parseProfiles(cleanValue(value), parsed, error);
        if (ok)
            profiles = cleanValue(value);
    }
    else if (k == "wallMotion") {
        std::vector<WallMotion> parsed;
        ok = parseWallMotion(value, parsed, error);
        if (ok)
            wallMotion = cleanValue(value);
    }
    else if (k == "bodyMotion") {
        std::vector<BodyMotion> parsed;
        ok = parseBodyMotion(value, parsed, error);
        if (ok)
            bodyMotion = cleanValue(value);
    }
    else if (k == "bodyCoupling")
        ok = parseBodyCoupling(value, bodyCoupling, error);
    else if (k == "regime")
        ok = parseRegime(value, regime, error);
    else if (k == "gamma")
        ok = assignFloat(gamma, k, value, 1.01f, 3.0f,
                         "gamma is the ratio of specific heats: 1.4 for air, "
                         "1.667 for a monatomic gas, 1.3 for steam", error);
    else if (k == "R")
        ok = assignFloat(R, k, value, 1.0f, 100000.0f,
                         "R is the specific gas constant in J/(kg K): 287 for "
                         "air, 2077 for helium", error);
    else if (k == "gamma2")
        ok = assignFloat(gamma2, k, value, 1.01f, 3.0f,
                         "gamma of the second gas, read only when phases = 2", error);
    else if (k == "R2")
        ok = assignFloat(R2, k, value, 1.0f, 100000.0f,
                         "R of the second gas in J/(kg K), read only when "
                         "phases = 2", error);
    else if (k == "T0")
        ok = assignFloat(T0, k, value, 1.0f, 100000.0f,
                         "T0 is the reference temperature in kelvin, and it is "
                         "what the inlet and the initial field are built from", error);
    else if (k == "pInf")
        ok = assignFloat(pInf, k, value, 1.0f, 1e12f,
                         "pInf is the ambient pressure in pascals; 101325 is "
                         "one atmosphere", error);
    else if (k == "machInlet")
        ok = assignFloat(machInlet, k, value, 0.0f, 20.0f,
                         "machInlet is the inlet speed as a multiple of the "
                         "speed of sound there", error);
    else if (k == "speciesMode")
        ok = parseSpeciesMode(value, speciesMode, error);
    else if (k == "acousticFields")
        ok = assignBool(acousticFields, k, value, error);
    else if (k == "acousticWindow")
        ok = assignFloat(acousticWindow, k, value, 1e-6f, 1000.0f,
                         "acousticWindow is how far back the running mean and "
                         "RMS look, in seconds; it has to be several periods "
                         "of whatever you are listening for", error);
    else if (k == "acousticRef")
        ok = assignFloat(acousticRef, k, value, 1e-12f, 1e6f,
                         "acousticRef is the pressure that counts as 0 dB; "
                         "2e-5 Pa is the human hearing threshold everybody "
                         "quotes SPL against", error);
    else if (k == "micInterval")
        ok = assignInt(micInterval, k, value, 1, 1000000,
                       "micInterval is how many steps pass between samples, "
                       "and it sets the sampling rate: every step is the "
                       "highest frequency the run can resolve at all", error);
    else if (k == "amrLevels")
        ok = assignInt(amrLevels, k, value, 0, 4,
                       "amrLevels is how many refinement levels sit over the "
                       "base grid. 0 is off, each one halves the cell size "
                       "where it is needed, and four is already 16 times "
                       "finer", error);
    else if (k == "amrEvery")
        ok = assignInt(amrEvery, k, value, 1, 100000,
                       "amrEvery is how many base steps pass between "
                       "regrids. Too rarely and a shock walks off its own "
                       "patch; too often and the regrid costs more than it "
                       "saves", error);
    else if (k == "amrThreshold")
        ok = assignFloat(amrThreshold, k, value, 0.001f, 1.0f,
                         "amrThreshold is how steep a feature has to be "
                         "before it is worth refining, as a fraction of the "
                         "steepest one in the domain", error);
    else if (k == "amrBuffer")
        ok = assignInt(amrBuffer, k, value, 0, 64,
                       "amrBuffer is how many cells of padding go round the "
                       "tagged region, so a feature cannot leave its patch "
                       "between regrids", error);
    else if (k == "amrMinPatch")
        ok = assignInt(amrMinPatch, k, value, 2, 4096,
                       "amrMinPatch is the smallest side a patch may have, "
                       "in base cells. Below about 8 the ghost layers cost "
                       "more than the interior", error);
    else if (k == "amrMaxPatch")
        ok = assignInt(amrMaxPatch, k, value, 4, 8192,
                       "amrMaxPatch is the largest side a patch may have, in "
                       "base cells", error);
    else if (k == "amrCriterion") {
        const std::string name = toLower(cleanValue(value));
        ok = name == "density" || name == "shock" || name == "vorticity" ||
             name == "wake" || name == "species" || name == "mixing" ||
             name == "body" || name == "wall" || name == "everything" ||
             name == "all";
        if (ok)
            amrCriterion = name;
        else
            error = badValue("amrCriterion", cleanValue(value),
                             "it is density, vorticity, species, body or "
                             "everything");
    }
    else if (k == "gridStretch") {
        ok = parseStretchKind(value, gridStretch);
        if (!ok)
            error = badValue("gridStretch", cleanValue(value),
                             "it is off, body, wake or edges. off is the even "
                             "grid every run before this one used");
    }
    else if (k == "stretchRatio")
        ok = assignFloat(stretchRatio, k, value, 1.0f, 1.5f,
                         "stretchRatio is how much one cell may grow over its "
                         "neighbour. 1 is off, 1.05 is the usual ceiling, and "
                         "past about 1.2 the scheme quietly drops to first "
                         "order", error);
    else if (k == "refineNear")
        ok = assignFloat(refineNear, k, value, 0.01f, 1.0f,
                         "refineNear is how wide the fine band is, as a "
                         "fraction of the domain", error);
    else if (k == "micAudio")
        ok = assignBool(micAudio, k, value, error);
    else if (k == "micAudioRate")
        ok = assignInt(micAudioRate, k, value, 1000, 384000,
                       "micAudioRate is the sample rate of the .wav files; "
                       "44100 is what everything plays, and there is no point "
                       "above twice the highest frequency in the run", error);
    else if (k == "micAudioSpeed")
        ok = assignFloat(micAudioSpeed, k, value, 1e-4f, 1000.0f,
                         "micAudioSpeed stretches the .wav timebase: 1 is "
                         "real time, 0.05 plays it twenty times slower and "
                         "drops every frequency by the same factor", error);
    else if (k == "microphones") {
        std::vector<Microphone> parsed;
        if (!parseMicrophones(value, parsed, error)) {
            ok = false;
        } else {
            microphones = cleanValue(value);
            ok = true;
        }
    }
    else if (k == "turbulence")
        ok = parseTurbulenceKind(value, turbulence, error);
    else if (k == "Cs")
        ok = assignFloat(Cs, k, value, 0.0f, 1.0f,
                         "Cs is the Smagorinsky constant, between 0 and 1; "
                         "0.17 is isotropic turbulence and 0.1 is what a wall "
                         "bounded flow usually wants", error);
    else if (k == "turbIntensity")
        ok = assignFloat(turbIntensity, k, value, 0.0f, 1.0f,
                         "turbIntensity is the fraction of the inlet speed "
                         "that is fluctuation, between 0 and 1; 0.05 is a "
                         "fairly ordinary wind tunnel", error);
    else if (k == "turbLengthScale")
        ok = assignFloat(turbLengthScale, k, value, 0.0f, kHuge,
                         "turbLengthScale is the size of the largest eddy "
                         "coming in, in metres; zero lets it be taken as a "
                         "tenth of the domain height", error);
    else if (k == "bodyForceReport")
        ok = assignBool(bodyForceReport, k, value, error);
    else if (k == "bodyCollisions")
        ok = assignBool(bodyCollisions, k, value, error);
    else if (k == "bodyRestitution")
        ok = assignFloat(bodyRestitution, k, value, 0.0f, 1.0f,
                         "bodyRestitution is how much of the closing speed "
                         "survives a bounce, from 0 (they stop dead) to 1 "
                         "(they bounce back at the speed they met)", error);
    else if (k == "bodyIterations")
        ok = assignInt(bodyIterations, k, value, 1, 64,
                       "bodyIterations is how many times force and motion may "
                       "be recomputed inside one step, from 1 to 64", error);
    else if (k == "restart")       ok = assignBool(restart, k, value, error);
    else if (k == "restartFile")  { restartFile = cleanValue(value); ok = true; }
    else if (k == "addTime") ok = assignDouble(addTime, k, value, -kHuge, kHuge,
             "addTime must be a finite number of seconds", error);

    if (!ok)
        return false;

    if (warning) {
        const std::string shown = k + "=" + cleanValue(value);
        if (k == "CFL" && CFL > 1.0f)
            *warning = shown + " is above 1; advection here is explicit, so "
                               "the run will most likely blow up";
        else if (k == "dtSafety" && dtSafety > 1.0f)
            *warning = shown + " takes a bigger step than the stability "
                               "estimate allows";
        else if ((k == "omega" && omega >= 1.95f) ||
                 (k == "smootherOmega" && smootherOmega >= 1.95f))
            *warning = shown + " is very close to 2, where SOR stops being "
                               "reliable";
        else if (k == "mgTolerance" && mgTolerance > 0.1f)
            *warning = shown + " is a very loose tolerance; the pressure solve "
                               "will stop long before the field is divergence "
                               "free";
        else if (k == "nu" && nu == 0.0f)
            *warning = shown + " is inviscid; nothing damps the smallest scales";
    }
    return true;
}

bool Config::modifyParam(const std::string& name) {
    const std::string canon = canonicalKey(name);
    if (canon.empty()) {
        const std::string guess = suggestKey(name);
        std::cout << "There is no parameter called '" << trimSpace(name) << "'";
        if (!guess.empty())
            std::cout << ". Did you mean " << guess << "?";
        std::cout << "\n";
        return false;
    }
    // confirm() reprints the whole configuration on the next turn, so there is
    // nothing to announce here.
    return ask(canon, "New " + canon);
}
bool Config::confirm() {
    print();
    std::cout << "\nTo change a parameter, type its name (e.g. 'nx'), or the\n"
                 "whole thing at once ('nx=256'), and press Enter.\n";
    std::cout << "To confirm all parameters and proceed, just press Enter (empty line).\n";
    std::cout << "> ";

    std::string input;
    if (!std::getline(std::cin, input)) {
        std::cout << "\nEnd of input, going with the configuration above.\n";
        return true;
    }
    input = trimSpace(input);

    if (input.empty()) {
        return true;   // confirmed
    }

    const size_t eq = input.find('=');
    if (eq != std::string::npos && eq > 0) {
        const std::string key = input.substr(0, eq);
        std::string error, warning;
        if (setParam(key, input.substr(eq + 1), error, &warning)) {
            if (!warning.empty())
                std::cout << "Warning: " << warning << "\n";
        } else {
            std::cout << error << "\n";
            // This branch never went through ask(), so the rules have not been
            // printed and a one-line refusal is all there would be to go on.
            if (canonicalKey(key) == "wallMotion")
                std::cout << wallMotionHelp();
            if (canonicalKey(key) == "bodyMotion")
                std::cout << bodyMotionHelp();
            if (canonicalKey(key) == "vortices")
                std::cout << vorticesHelp();
        }
        return false;
    }

    modifyParam(input);
    return false;  // not confirmed yet, loop again
}
