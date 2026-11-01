// Paula's Revenge - INTERNET RADIO: stations from all over the world (Radio Browser), by genre,
// by country or by typing; listen, and GRAB the last 5 / 10 / 20 / 30 seconds straight into a new
// instrument (no effects, as it came off the stream) - or record it on the SAMPLE page (RADIO).
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "Panels.h"

namespace prv::ui
{
class RadioPage : public juce::Component
{
public:
    explicit RadioPage (PaulaProcessor&);
    ~RadioPage() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onStatus;
    std::function<void (int instrument)> onGrabbed;   // GRAB made this instrument: the sample editor

    void opened();
    void tick();          // (the window's timer, while it's showing)

    // ---- what's listed: a genre (tag), a country (code), a search, the starred ones or the recent ones
    enum class Left { Genres, Countries };
    void showGenres();
    void showCountries();
    void showGenre (int index);
    void showCountry (const juce::String& code, const juce::String& name);
    void showSearch (const juce::String& text);
    void showFavourites();
    void showRecent();

    // ---- stations
    int  stationCount() const { return (int) stations.size(); }
    const RadioStation& stationAt (int row) const { return stations[(size_t) row]; }
    juce::String stationRow (int row) const;          // as the list shows it
    void playRow (int row);
    void stopRadio();
    void toggleFavourite (const RadioStation&);
    static bool isFavourite (const RadioStation&);
    static std::vector<RadioStation> favourites();
    static std::vector<RadioStation> recent();

    // ---- sampling
    void setBuffer (int seconds);                    // 5 / 10 / 20 / 30 (remembered)
    int  grab();                                     // the held seconds into the first empty instrument: its index, or -1

    // (tests)
    static const juce::StringArray& genreNames();
    static const juce::StringArray& genreTags();
    juce::String listTitle() const { return listHeading; }
    juce::String searchText() const { return query; }
    bool isLoading() const { return loading > 0; }
    PixList& stationList() { return list; }
    PixList& leftList() { return left; }
    juce::String nowText() const;                    // the NOW PLAYING box's lines, joined
    juce::String bufferText() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    PaulaProcessor& proc;
    Left leftMode = Left::Genres;
    PixList left, list;
    std::vector<RadioStation> stations;
    juce::StringArray countryNames, countryCodes;
    juce::String listHeading, listKey, query, problemText;
    int loading = 0;
    int request = 0;     // (the latest fetch wins)
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    PixButton genresBtn { "GENRE", true }, countriesBtn { "COUNTRY", true }, favsBtn { "STARRED", true }, recentBtn { "RECENT", true };
    PixButton playBtn { "PLAY" }, starBtn { "STAR" }, closeBtn { "CLOSE" }, grabBtn { "GRAB" };
    PixButton bufBtn[4] { PixButton ("5", true), PixButton ("10", true), PixButton ("20", true), PixButton ("30", true) };
    PixSlider volSlider { "VOL" };
    juce::Rectangle<float> titleArea, searchArea, headArea, nowBox, nowArea, meterArea, bufferBox, bufferBar, noteArea, bufLabel;
    float meterL = 0, meterR = 0;

    void fetch (RadioDirectory::Kind, const juce::String& what, const juce::String& heading, const juce::String& key);
    void setStations (std::vector<RadioStation>, const juce::String& heading);
    void refreshButtons();
    void status (const juce::String& m) { if (onStatus) onStatus (m); }
    void remember (const RadioStation&);   // RECENT
    int  selectedRow() const { return list.selected >= 0 && list.selected < (int) stations.size() ? list.selected : -1; }
};
} // namespace prv::ui
