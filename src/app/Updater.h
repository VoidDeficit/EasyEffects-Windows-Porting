#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>

namespace eeval {
namespace app {

// --- Install integration ------------------------------------------------------

/** The version this build was made from (the VERSION file). */
juce::String currentVersion();

/** "owner/name" of the GitHub repository releases are published to. */
juce::String updateRepository();

/** True for a copy installed by the setup program (install.ini next to the exe),
    false for the portable zip. Only installed copies update themselves. */
bool isInstalledCopy();

/** -1, 0 or 1 comparing dotted versions ("0.10.0" > "0.9.3"); a leading "v" is ignored. */
int compareVersions(const juce::String& a, const juce::String& b);

/** Installed copy, not elevated: start the "\EasyEffects\Launch" scheduled task,
    which runs EasyEffects with administrator rights without a UAC prompt.
    Returns true if the task was started (the caller should then quit). */
bool startElevatedViaTask();

/** Whether the "\EasyEffects\Autostart" task (start at sign-in) exists. */
bool isAutostartEnabled();

/** Turns the sign-in task on or off (needs administrator rights). Returns an
    error message, empty on success. */
juce::String setAutostartEnabled(bool on);

// --- Updates ------------------------------------------------------------------

/**
 * Checks GitHub for the latest release and, for installed copies, downloads
 * its "-Setup.exe" and runs it silently. All callbacks arrive on the message
 * thread; work happens on background threads.
 */
class Updater {
public:
    enum class State { idle, checking, upToDate, available, downloading, failed };

    struct Release {
        juce::String version;        // "0.3.0"
        juce::String pageUrl;        // release page on GitHub
        juce::String installerUrl;   // *-Setup.exe asset (may be empty)
        juce::String installerName;
        juce::int64  installerSize = 0;
    };

    Updater();
    ~Updater();

    std::function<void()> onChange;                 // state / progress changed
    std::function<void(const Release&)> onUpdateFound;  // a newer release was found

    void check();
    void downloadAndInstall();   // installed copies only; quits the app when the installer starts

    State getState() const { return state; }
    const Release& getRelease() const { return latest; }
    float getProgress() const { return progress; }
    juce::String getStatusText() const;

private:
    struct Shared { std::atomic<bool> alive { true }; };
    void setState(State s, const juce::String& error = {});

    std::shared_ptr<Shared> shared;
    State state = State::idle;
    Release latest;
    juce::String lastError;
    float progress = 0.0f;
};

} // namespace app
} // namespace eeval
