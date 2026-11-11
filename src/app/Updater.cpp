#include "Updater.h"

#ifndef EE_UPDATE_REPO
 #define EE_UPDATE_REPO "VoidDeficit/EasyEffects-Windows-Porting"
#endif

namespace eeval {
namespace app {

// --- Install integration ------------------------------------------------------

static juce::File exeFile()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile);
}

static juce::File installIni()
{
    return exeFile().getSiblingFile("install.ini");
}

juce::String currentVersion()
{
    return JucePlugin_VersionString;
}

juce::String updateRepository()
{
    // The installer records the repository it came from; builds default to
    // the one CMake was configured with.
    const auto ini = installIni();
    if (ini.existsAsFile()) {
        juce::StringArray lines;
        ini.readLines(lines);
        for (auto& l : lines)
            if (l.trim().startsWithIgnoreCase("Repository="))
                if (auto repo = l.fromFirstOccurrenceOf("=", false, false).trim(); repo.contains("/"))
                    return repo;
    }
    return EE_UPDATE_REPO;
}

bool isInstalledCopy()
{
    return juce::JUCEApplicationBase::isStandaloneApp() && installIni().existsAsFile();
}

int compareVersions(const juce::String& a, const juce::String& b)
{
    auto parts = [](juce::String v) {
        v = v.trim();
        if (v.startsWithIgnoreCase("v")) v = v.substring(1);
        v = v.upToFirstOccurrenceOf("-", false, false);   // ignore "-beta" style suffixes
        juce::StringArray p;
        p.addTokens(v, ".", "");
        return p;
    };
    const auto pa = parts(a), pb = parts(b);
    for (int i = 0; i < juce::jmax(pa.size(), pb.size()); ++i) {
        const int x = pa[i].getIntValue(), y = pb[i].getIntValue();
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// Runs a console tool without a window and returns its exit code (-1 if it
// could not start or timed out).
static int runHidden(const juce::StringArray& args, int timeoutMs, juce::String* output = nullptr)
{
    juce::ChildProcess p;
    if (!p.start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return -1;
    const auto out = p.readAllProcessOutput();
    if (output != nullptr) *output = out;
    if (!p.waitForProcessToFinish(timeoutMs)) {
        p.kill();
        return -1;
    }
    return (int)p.getExitCode();
}

static constexpr const char* launchTask    = "\\EasyEffects\\Launch";
static constexpr const char* autostartTask = "\\EasyEffects\\Autostart";

bool startElevatedViaTask()
{
    if (!isInstalledCopy())
        return false;
    return runHidden({ "schtasks.exe", "/Run", "/TN", launchTask }, 10000) == 0;
}

bool isAutostartEnabled()
{
    if (!isInstalledCopy())
        return false;
    return runHidden({ "schtasks.exe", "/Query", "/TN", autostartTask }, 10000) == 0;
}

juce::String setAutostartEnabled(bool on)
{
    const auto script = exeFile().getSiblingFile("tasks.ps1");
    if (!script.existsAsFile())
        return "tasks.ps1 is missing next to EasyEffects.exe. Reinstall EasyEffects to fix this.";

    juce::String out;
    const int code = runHidden({ "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                 "-File", script.getFullPathName(),
                                 "-SetAutostart", on ? "on" : "off",
                                 "-Exe", exeFile().getFullPathName() }, 30000, &out);
    if (code == 0)
        return {};
    return "Could not change the sign-in task (administrator rights are needed).\n\n" + out.trim();
}

// --- Updates ------------------------------------------------------------------

static std::unique_ptr<juce::InputStream> openGitHub(const juce::URL& url, int* status, int timeoutMs)
{
    return url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                     .withExtraHeaders("User-Agent: EasyEffects-Windows-Updater\r\n"
                                                       "Accept: application/vnd.github+json")
                                     .withConnectionTimeoutMs(timeoutMs)
                                     .withNumRedirectsToFollow(5)
                                     .withStatusCode(status));
}

Updater::Updater() : shared(std::make_shared<Shared>()) {}

Updater::~Updater()
{
    shared->alive = false;   // background threads drop their results
}

void Updater::setState(State s, const juce::String& error)
{
    state = s;
    lastError = error;
    if (onChange) onChange();
}

juce::String Updater::getStatusText() const
{
    switch (state) {
        case State::idle:        return "Version " + currentVersion();
        case State::checking:    return "Checking for updates...";
        case State::upToDate:    return "You're up to date (version " + currentVersion() + ")";
        case State::available:   return "Version " + latest.version + " is available (you have " + currentVersion() + ")";
        case State::downloading: return "Downloading version " + latest.version + "... "
                                        + juce::String(juce::roundToInt(progress * 100.0f)) + "%";
        case State::failed:      return lastError.isNotEmpty() ? lastError : juce::String("Update check failed");
    }
    return {};
}

void Updater::check()
{
    if (state == State::checking || state == State::downloading)
        return;
    setState(State::checking);

    auto sh = shared;
    const auto repo = updateRepository();
    juce::Thread::launch([this, sh, repo] {
        int status = 0;
        juce::String error, body;
        if (auto stream = openGitHub(juce::URL("https://api.github.com/repos/" + repo + "/releases/latest"), &status, 15000))
            body = stream->readEntireStreamAsString();
        else
            error = "Couldn't reach GitHub. Check your internet connection.";

        Release r;
        if (error.isEmpty()) {
            if (status == 404) {
                error = "No releases have been published yet";
            } else if (status != 200) {
                error = "GitHub answered with HTTP " + juce::String(status);
            } else {
                const auto json = juce::JSON::parse(body);
                r.version = json.getProperty("tag_name", "").toString().trimCharactersAtStart("vV");
                r.pageUrl = json.getProperty("html_url", "").toString();
                if (auto* assets = json.getProperty("assets", {}).getArray())
                    for (auto& a : *assets) {
                        const auto name = a.getProperty("name", "").toString();
                        if (name.endsWithIgnoreCase("-Setup.exe")) {
                            r.installerName = name;
                            r.installerUrl  = a.getProperty("browser_download_url", "").toString();
                            r.installerSize = (juce::int64)a.getProperty("size", 0);
                        }
                    }
                if (r.version.isEmpty())
                    error = "The latest release has no version tag";
            }
        }

        juce::MessageManager::callAsync([this, sh, r, error] {
            if (!sh->alive) return;
            if (error.isNotEmpty()) { setState(State::failed, error); return; }
            latest = r;
            if (compareVersions(r.version, currentVersion()) > 0) {
                setState(State::available);
                if (onUpdateFound) onUpdateFound(latest);
            } else {
                setState(State::upToDate);
            }
        });
    });
}

void Updater::downloadAndInstall()
{
    if (state != State::available)
        return;
    if (!isInstalledCopy() || latest.installerUrl.isEmpty()) {
        juce::URL(latest.pageUrl).launchInDefaultBrowser();
        return;
    }

    progress = 0.0f;
    setState(State::downloading);

    auto sh = shared;
    const auto release = latest;
    juce::Thread::launch([this, sh, release] {
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("EasyEffects-Update");
        dir.createDirectory();
        auto target = dir.getChildFile(release.installerName.isNotEmpty() ? release.installerName
                                                                          : juce::String("EasyEffects-Setup.exe"));
        target.deleteFile();

        juce::String error;
        int status = 0;
        auto in = openGitHub(juce::URL(release.installerUrl), &status, 30000);
        if (in == nullptr || status != 200) {
            error = "Couldn't download the installer (HTTP " + juce::String(status) + ")";
        } else {
            juce::FileOutputStream out(target);
            if (!out.openedOk()) {
                error = "Couldn't write " + target.getFullPathName();
            } else {
                const juce::int64 total = release.installerSize > 0 ? release.installerSize : in->getTotalLength();
                juce::HeapBlock<char> buffer(1 << 16);
                juce::int64 done = 0;
                int lastPercent = -1;
                while (!in->isExhausted() && sh->alive) {
                    const int n = in->read(buffer, 1 << 16);
                    if (n <= 0) break;
                    out.write(buffer, (size_t)n);
                    done += n;
                    const int percent = total > 0 ? (int)(done * 100 / total) : 0;
                    if (percent != lastPercent) {
                        lastPercent = percent;
                        juce::MessageManager::callAsync([this, sh, percent] {
                            if (!sh->alive) return;
                            progress = (float)percent / 100.0f;
                            if (onChange) onChange();
                        });
                    }
                }
                out.flush();
                if (total > 0 && done != total)
                    error = "The download was incomplete";
            }
        }

        juce::MessageManager::callAsync([this, sh, target, error] {
            if (!sh->alive) return;
            if (error.isNotEmpty()) { setState(State::failed, error); return; }

            // Silent update: the installer shows its progress, closes this app,
            // replaces it and starts the new version.
            if (!target.startAsProcess("/SILENT /SP- /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS")) {
                setState(State::failed, "Couldn't start the installer");
                return;
            }
            juce::JUCEApplicationBase::quit();
        });
    });
}

} // namespace app
} // namespace eeval
