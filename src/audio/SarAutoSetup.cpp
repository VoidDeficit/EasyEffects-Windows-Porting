#include "SarAutoSetup.h"

namespace eeval::sar {

juce::File configFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("SynchronousAudioRouter")
               .getChildFile("default.json");
}

juce::String clsidOfAsioDriver(const juce::String& name)
{
#if JUCE_WINDOWS
    for (auto* root : { "HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\", "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\ASIO\\" }) {
        const auto v = juce::WindowsRegistry::getValue(juce::String(root) + name + "\\CLSID");
        if (v.isNotEmpty())
            return v.trim();
    }
#else
    juce::ignoreUnused(name);
#endif
    return {};
}

namespace {

juce::var makeEndpoint(const char* id, const char* description, const char* type)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", id);
    obj->setProperty("description", description);
    obj->setProperty("type", type);
    obj->setProperty("channelCount", 2.0);   // SAR expects a JSON number
    return juce::var(obj);
}

bool hasEndpoint(const juce::Array<juce::var>& endpoints, const char* id, const char* description)
{
    for (const auto& ep : endpoints)
        if (ep.getProperty("id", {}).toString() == id
            || ep.getProperty("description", {}).toString() == description)
            return true;
    return false;
}

} // namespace

Result ensureConfig(const juce::StringArray& asioDrivers, const juce::String& preferredHardware)
{
    Result result;

    // Hardware candidates = every installed ASIO driver except SAR itself.
    juce::StringArray hardware;
    for (const auto& d : asioDrivers)
        if (!isSarDriver(d) && clsidOfAsioDriver(d).isNotEmpty())
            hardware.add(d);

    // Load the existing config (keep unknown fields: app routing, defaults...).
    const auto file = configFile();
    juce::var root;
    if (file.existsAsFile())
        root = juce::JSON::parse(file);
    if (!root.isObject())
        root = juce::var(new juce::DynamicObject());
    auto* obj = root.getDynamicObject();

    // --- hardware driver ---------------------------------------------------
    const auto currentClsid = obj->getProperty("driverClsid").toString().trim();
    juce::String currentHardware;
    for (const auto& h : hardware)
        if (clsidOfAsioDriver(h).equalsIgnoreCase(currentClsid))
            currentHardware = h;

    juce::String wanted = currentHardware;                       // keep a valid choice
    if (preferredHardware.isNotEmpty() && hardware.contains(preferredHardware))
        wanted = preferredHardware;                              // user's explicit choice wins
    if (wanted.isEmpty() && !hardware.isEmpty())
        wanted = hardware[0];

    if (wanted.isEmpty()) {
        result.error = "No audio interface ASIO driver was found for Synchronous Audio Router to use.";
        return result;
    }
    result.hardware = wanted;

    const auto wantedClsid = clsidOfAsioDriver(wanted);
    if (!wantedClsid.equalsIgnoreCase(currentClsid)) {
        obj->setProperty("driverClsid", wantedClsid);
        result.changed = true;
    }

    if (!obj->hasProperty("enableApplicationRouting")) {
        obj->setProperty("enableApplicationRouting", false);
        result.changed = true;
    }

    // --- endpoints ---------------------------------------------------------
    juce::Array<juce::var> endpoints;
    if (auto* arr = obj->getProperty("endpoints").getArray())
        endpoints = *arr;

    if (!hasEndpoint(endpoints, speakersId, speakersEndpoint)) {
        endpoints.add(makeEndpoint(speakersId, speakersEndpoint, "playback"));
        result.changed = result.endpointsAdded = true;
    }
    if (!hasEndpoint(endpoints, microphoneId, microphoneEndpoint)) {
        endpoints.add(makeEndpoint(microphoneId, microphoneEndpoint, "recording"));
        result.changed = result.endpointsAdded = true;
    }
    obj->setProperty("endpoints", endpoints);

    if (result.changed) {
        file.getParentDirectory().createDirectory();
        // Keep one backup of whatever was there before we first touched it.
        const auto backup = file.getSiblingFile("default.json.before-easyeffects");
        if (file.existsAsFile() && !backup.existsAsFile())
            file.copyFileTo(backup);

        if (!file.replaceWithText(juce::JSON::toString(root, false)))
            result.error = "Could not write " + file.getFullPathName();
    }
    return result;
}

} // namespace eeval::sar
