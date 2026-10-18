#pragma once

#include <juce_core/juce_core.h>
#include <map>

namespace Harmonizer
{

enum class Language
{
    English = 0,
    SimplifiedChinese = 1
};

class Localization
{
public:
    static Localization& getInstance();

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void languageChanged() = 0;
    };

    void addListener(Listener* l)       { listeners.add(l); }
    void removeListener(Listener* l)    { listeners.remove(l); }

    Language getLanguage() const        { return currentLang; }
    void setLanguage(Language lang);

    void saveConfig();
    void loadConfig();

    juce::String get(const juce::String& key) const;

private:
    Localization();
    Language currentLang = Language::SimplifiedChinese;
    juce::ListenerList<Listener> listeners;

    std::map<juce::String, juce::String> zhStrings;

    void initDictionary();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Localization)
};

inline juce::String tr(const juce::String& key)
{
    return Localization::getInstance().get(key);
}

inline juce::String tr(const char* key)
{
    return Localization::getInstance().get(juce::String::fromUTF8(key));
}

} // namespace Harmonizer
