/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2026                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/webbrowser/include/cefhost.h>

#include <modules/webbrowser/include/webbrowserapp.h>
#include <openspace/filesystem/filesystem.h>
#include <openspace/format.h>
#include <openspace/logging/logmanager.h>
#include <openspace/misc/exception.h>
#include <openspace/misc/profiling.h>
#include <include/cef_app.h>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <string_view>

#ifdef WIN32
#pragma warning(push)
#pragma warning(disable : 4100)
#endif // WIN32

#include <include/wrapper/cef_helpers.h>

#ifdef WIN32
#pragma warning(pop)
#endif // WIN32

struct CefSettingsTraits;
template <typename T> class CefStructBase;
using CefSettings = CefStructBase<CefSettingsTraits>;

namespace {
    constexpr std::string_view _loggerCat = "CefHost";
} // namespace

#if defined(__linux__) && defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
#include <malloc.h>

// Workaround for https://issues.chromium.org/issues/401168177: the prebuilt CEF calls
// mallinfo(), whose int fields overflow with large heaps and trip a CHECK in
// MallocDumpProvider::OnMemoryDump. Defining the symbol in the executable interposes
// glibc's version for libcef and returns values from mallinfo2 scaled to fit in int.
extern "C" __attribute__((visibility("default"))) struct mallinfo mallinfo(void) {
    const struct mallinfo2 m2 = mallinfo2();
    // Chromium sums several fields as int, so scale everything down by a common
    // factor until even the sum of four fields fits
    size_t largest = std::max({ m2.arena, m2.hblkhd, m2.uordblks, m2.fordblks });
    constexpr size_t Max = static_cast<size_t>(std::numeric_limits<int>::max() / 4);
    int shift = 0;
    while ((largest >> shift) > Max) {
        shift++;
    }
    auto s = [shift](size_t v) { return static_cast<int>(v >> shift); };
    struct mallinfo m = {};
    m.arena = s(m2.arena);
    m.ordblks = s(m2.ordblks);
    m.smblks = s(m2.smblks);
    m.hblks = s(m2.hblks);
    m.hblkhd = s(m2.hblkhd);
    m.usmblks = s(m2.usmblks);
    m.fsmblks = s(m2.fsmblks);
    m.uordblks = s(m2.uordblks);
    m.fordblks = s(m2.fordblks);
    m.keepcost = s(m2.keepcost);
    return m;
}
#endif

namespace openspace {

CefHost::CefHost(const std::string& helperLocation, bool enableRemoteDebugging) {
    LDEBUG("Initializing CEF...");

    CefSettings settings;

#ifndef CEF_USE_SANDBOX
    LDEBUG("Disabling sandbox for CEF");
    settings.no_sandbox = 1;
#endif

    const std::filesystem::path root =
        std::filesystem::path(helperLocation).parent_path();
    const std::filesystem::path cefcache = std::format("{}/cefcache", root);
    CefString(&settings.root_cache_path).FromString(cefcache.string());
    CefString(&settings.browser_subprocess_path).FromString(helperLocation);

    // Left unset, CEF looks for required files next to whichever libcef it was loaded
    // from. This was an issue on AppImage packaging on Linux before where there were
    // multiple libcef.so present
    const std::filesystem::path resources = absPath("${BIN}");
    CefString(&settings.resources_dir_path).FromString(resources.string());
    CefString(&settings.locales_dir_path).FromString((resources / "locales").string());

    settings.windowless_rendering_enabled = 1;

    if (enableRemoteDebugging) {
        settings.remote_debugging_port = 8088;
        LDEBUG(std::format(
            "Remote WebBrowser debugging available on http://localhost:{}",
            settings.remote_debugging_port
        ));
    }

    // Suppress Chromium ERROR/WARNING/INFO messages from stderr. These include spurious
    // messages like "Network service crashed, restarting service" that appear during
    // normal shutdown
    settings.log_severity = LOGSEVERITY_FATAL;

    // cf. https://github.com/chromiumembedded/cef/issues/3685
    settings.chrome_runtime = 1;

    const CefRefPtr<WebBrowserApp> app = CefRefPtr<WebBrowserApp>(new WebBrowserApp);

    const CefMainArgs args;
    const bool success = CefInitialize(args, settings, app.get(), nullptr);
    if (!success) {
        throw RuntimeError("Failed initializing CEF Browser");
    }
    LDEBUG("Initializing CEF... done");
}

CefHost::~CefHost() {
    CefShutdown();
}

void CefHost::doMessageLoopWork() {
    ZoneScoped;

    CefDoMessageLoopWork();
}

} // namespace openspace
