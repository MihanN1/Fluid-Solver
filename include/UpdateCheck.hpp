#pragma once
#include <functional>
#include <string>

// "Is there a newer release than this one?", asked once at startup.
//
// The rule is the plain one: any published version greater than the one this
// binary was built as counts, whether it moved the major or only the minor -
// 0.2 sees 0.3, 0.10 and 1.0 alike, and does not see 0.2 or 0.1.9. The version
// is compared number by number, so 0.10 is correctly newer than 0.9 rather
// than alphabetically older.
//
// Nothing here is allowed to hold the program up: the request has a short
// timeout, every failure is silent unless asked about, and no network at all
// is the normal case rather than an error.

namespace update {

// Where the check gets its numbers. The window is its own release with its own
// version, so it cannot use the solver's: it asks about the repository it was
// built from and compares against the version stamped into this binary.
const char* currentVersion();
const char* releasesPage();

struct Result {
    bool checked = false;      // the request actually completed
    bool newer = false;        // ... and it found something newer
    std::string latest;        // the newest tag seen, without the leading "v"
    std::string url;           // where to get it
    std::string error;         // why "checked" is false, for --check-updates
};

// -1 when a < b, 0 when equal, 1 when a > b. Missing components count as zero,
// so "0.2" == "0.2.0", and anything after the numbers ("0.3-beta") is ignored
// for ordering but keeps the tag itself intact.
int compareVersions(const std::string& a, const std::string& b);

// One HTTPS GET against the releases API. timeoutSeconds covers the whole
// exchange; on Windows this is WinHTTP, elsewhere curl or wget, whichever is
// on the machine.
Result check(int timeoutSeconds = 4);

// Hands the URL to the browser. Best effort, and never blocks.
bool openUrl(const std::string& url);

// Runs check() on a thread of its own and hands the answer back through the
// callback, on that same thread. Nothing here touches the window: the caller
// stores the result and the next frame draws it. A check that never answers
// leaves the thread to time out on its own and costs nothing.
void checkInBackground(std::function<void(Result)> done);

}   // namespace update
