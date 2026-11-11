#pragma once
#include <functional>

namespace eeval {
namespace app {

/**
 * Keeps EasyEffects to one running copy per Windows session, including across
 * administrator / normal copies (where JUCE's own single-instance messaging is
 * blocked by Windows).
 *
 *  - acquireSingleInstance(): takes the session-wide lock; false if another
 *    EasyEffects already holds it.
 *  - requestShowRunningInstance(): asks the running copy to show its window.
 *  - listenForShowRequests(): the running copy calls this; the callback runs on
 *    a background thread.
 *  - releaseSingleInstance(): gives the lock up, e.g. right before relaunching
 *    with administrator rights so the new copy can take it.
 */
bool acquireSingleInstance();
void releaseSingleInstance();
bool requestShowRunningInstance();
void listenForShowRequests(std::function<void()> onShowRequested);
void stopListeningForShowRequests();

/** Plain Windows message box, usable before any JUCE window exists. */
void showAlreadyRunningMessage(bool windowShown);

} // namespace app
} // namespace eeval
