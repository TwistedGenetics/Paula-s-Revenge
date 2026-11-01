// Paula's Revenge - INTERNET RADIO page (see RadioPage.h)
#include "RadioPage.h"
#include "ChoiceBox.h"
#include "../Prefs.h"
#include "../engine/Recorder.h"

namespace prv::ui
{
namespace
{
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    juce::String fit (juce::String t, float w, float px)
    {
        const int n = std::max (3, (int) (w / (px * PixelFont::kCellW)));
        return t.length() > n ? t.substring (0, n - 2) + ".." : t;
    }
    std::vector<RadioStation> loadList (const char* key)
    {
        std::vector<RadioStation> out;
        const auto v = juce::JSON::parse (prefs().getValue (key));
        if (v.isArray()) for (const auto& e : *v.getArray()) out.push_back (RadioStation::fromVar (e));
        return out;
    }
    void saveList (const char* key, const std::vector<RadioStation>& l)
    {
        juce::Array<juce::var> a;
        for (auto& s : l) a.add (s.toVar());
        prefs().setValue (key, juce::JSON::toString (juce::var (a), true));
        prefs().saveIfNeeded();
    }
}

const juce::StringArray& RadioPage::genreNames()
{
    static const juce::StringArray n { "JUNGLE", "DRUM AND BASS", "BREAKBEAT", "HARDCORE", "UK GARAGE", "DUBSTEP", "DUB", "REGGAE",
        "DANCEHALL", "TECHNO", "HOUSE", "DEEP HOUSE", "ACID", "TRANCE", "ELECTRONIC", "AMBIENT", "CHILLOUT", "DOWNTEMPO", "TRIP HOP",
        "HIP HOP", "R&B", "SOUL", "FUNK", "DISCO", "JAZZ", "BLUES", "ROCK", "INDIE", "METAL", "PUNK", "POP", "80S", "90S", "OLDIES",
        "COUNTRY", "FOLK", "LATIN", "SALSA", "AFROBEAT", "AFRICAN", "BOLLYWOOD", "K-POP", "J-POP", "ARABIC", "CLASSICAL", "OPERA",
        "WORLD", "EXPERIMENTAL", "LO-FI", "SOUNDTRACK", "NEWS", "TALK" };
    return n;
}
const juce::StringArray& RadioPage::genreTags()
{
    static const juce::StringArray t { "jungle", "drum and bass", "breakbeat", "hardcore", "uk garage", "dubstep", "dub", "reggae",
        "dancehall", "techno", "house", "deep house", "acid", "trance", "electronic", "ambient", "chillout", "downtempo", "trip hop",
        "hip hop", "rnb", "soul", "funk", "disco", "jazz", "blues", "rock", "indie", "metal", "punk", "pop", "80s", "90s", "oldies",
        "country", "folk", "latin", "salsa", "afrobeat", "african", "bollywood", "kpop", "jpop", "arabic", "classical", "opera",
        "world music", "experimental", "lofi", "soundtrack", "news", "talk" };
    return t;
}

RadioPage::RadioPage (PaulaProcessor& p) : proc (p)
{
    Theme::markDarkPage (*this);   // (dark with light text in the dark skin)
    setWantsKeyboardFocus (true);
    setOpaque (true);

    left.getNumRows = [this] { return leftMode == Left::Genres ? genreNames().size() : countryNames.size(); };
    left.getRowText = [this] (int r)
    {
        const auto t = leftMode == Left::Genres ? genreNames()[r] : countryNames[r];
        return fit (t, left.getWidth() - left.textX() - Theme::px * 10, Theme::px);
    };
    left.onClick = [this] (int r, const juce::MouseEvent&)
    {
        left.selected = r;
        if (leftMode == Left::Genres) showGenre (r);
        else if (r >= 0 && r < countryCodes.size()) showCountry (countryCodes[r], countryNames[r]);
        grabKeyboardFocus();
    };
    addAndMakeVisible (left);

    list.getNumRows = [this] { return (int) stations.size(); };
    list.getRowText = [this] (int r) { return stationRow (r); };
    list.onClick = [this] (int r, const juce::MouseEvent& e)
    {
        list.selected = r;
        if (e.mods.isPopupMenu() && r >= 0 && r < (int) stations.size())
        {
            const auto s = stations[(size_t) r];
            juce::Component::SafePointer<RadioPage> safe (this);
            showMenu (s.name, { { "PLAY", [safe, r] { if (safe) safe->playRow (r); } },
                                { isFavourite (s) ? "UNSTAR" : "STAR (STARRED LIST)", [safe, s] { if (safe) safe->toggleFavourite (s); } } });
        }
        refreshButtons();
        grabKeyboardFocus();
    };
    list.onDoubleClick = [this] (int r) { playRow (r); };
    addAndMakeVisible (list);

    genresBtn.onClick = [this] { showGenres(); };
    countriesBtn.onClick = [this] { showCountries(); };
    favsBtn.onClick = [this] { showFavourites(); };
    recentBtn.onClick = [this] { showRecent(); };
    playBtn.onClick = [this]
    {
        if (proc.radio.state() != RadioPlayer::State::Stopped && proc.radio.state() != RadioPlayer::State::Failed) stopRadio();
        else if (selectedRow() >= 0) playRow (selectedRow());
        else status ("PICK A STATION FIRST (DOUBLE-CLICK ONE TO PLAY IT)");
    };
    starBtn.onClick = [this]
    {
        if (selectedRow() >= 0) toggleFavourite (stations[(size_t) selectedRow()]);
        else if (proc.radio.station().url.isNotEmpty()) toggleFavourite (proc.radio.station());
    };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    grabBtn.onClick = [this] { grab(); };
    for (int k = 0; k < 4; ++k)
    {
        bufBtn[k].setClickingTogglesState (false);
        bufBtn[k].onClick = [this, k] { setBuffer (radio::GrabBuffer::kChoices[k]); };
    }
    volSlider.labelChars = 4;
    volSlider.valueChars = 5;
    volSlider.defaultValue = 0.8f;
    grabBtn.textScale = 1.5f;   // (the one to find in a hurry)
    volSlider.onChange = [this] (float v) { proc.radio.volume = v; prefs().setValue ("radioVol", (double) v); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &genresBtn, &countriesBtn, &favsBtn, &recentBtn, &playBtn, &starBtn,
                                                                         &closeBtn, &grabBtn, &volSlider })
        addAndMakeVisible (c);
    for (auto& b : bufBtn) addAndMakeVisible (b);

    // the remembered settings
    const float vol = (float) prefs().getDoubleValue ("radioVol", 0.8);
    proc.radio.volume = juce::jlimit (0.0f, 1.0f, vol);
    volSlider.setValue (proc.radio.volume.load());
    int secs = prefs().getIntValue ("radioBuffer", 10);
    if (std::find (std::begin (radio::GrabBuffer::kChoices), std::end (radio::GrabBuffer::kChoices), secs) == std::end (radio::GrabBuffer::kChoices)) secs = 10;
    proc.radio.setGrabSeconds (secs);
    countryNames = juce::StringArray::fromLines (prefs().getValue ("radioCountryNames"));
    countryCodes = juce::StringArray::fromLines (prefs().getValue ("radioCountryCodes"));
    countryNames.removeEmptyStrings(); countryCodes.removeEmptyStrings();
    if (countryNames.size() != countryCodes.size()) { countryNames.clear(); countryCodes.clear(); }
    refreshButtons();
}

RadioPage::~RadioPage() { *alive = false; }

void RadioPage::opened()
{
    if (stations.empty() && loading == 0)
    {
        // where it was last time: a genre (JUNGLE the first time), a country, the starred or recent ones
        const auto last = prefs().getValue ("radioLast", "genre:0");
        if (last.startsWith ("country:"))
        {
            const auto code = last.fromFirstOccurrenceOf (":", false, false);
            const int i = countryCodes.indexOf (code);
            showCountries();
            showCountry (code, i >= 0 ? countryNames[i] : code);
        }
        else if (last == "starred") showFavourites();
        else if (last == "recent") showRecent();
        else showGenre (juce::jlimit (0, genreNames().size() - 1, last.fromFirstOccurrenceOf (":", false, false).getIntValue()));
    }
    refreshButtons();
    grabKeyboardFocus();
    status ("INTERNET RADIO: DOUBLE-CLICK A STATION TO PLAY IT, TYPE TO SEARCH - GRAB TAKES THE LAST " + juce::String (proc.radio.grabSeconds()) + " SEC");
}

// ---------------------------------------------------------------- lists
void RadioPage::showGenres()
{
    leftMode = Left::Genres;
    left.selected = juce::jlimit (0, genreNames().size() - 1, left.selected);
    refreshButtons();
    left.repaint();
}

void RadioPage::showCountries()
{
    leftMode = Left::Countries;
    left.selected = 0;
    refreshButtons();
    if (countryNames.isEmpty()) problemText = "LOADING THE COUNTRIES...";
    ++loading;
    juce::Component::SafePointer<RadioPage> safe (this);
    proc.radioDirectory.fetchCountries ([safe] (juce::StringArray names, juce::StringArray codes, juce::String err)
    {
        if (safe == nullptr) return;
        --safe->loading;
        if (err.isNotEmpty() && safe->countryNames.isEmpty()) { safe->problemText = err; safe->repaint(); return; }
        if (names.isEmpty()) { safe->repaint(); return; }
        safe->countryNames = names; safe->countryCodes = codes;
        prefs().setValue ("radioCountryNames", names.joinIntoString ("\n"));
        prefs().setValue ("radioCountryCodes", codes.joinIntoString ("\n"));
        prefs().saveIfNeeded();
        if (safe->problemText.startsWith ("LOADING THE COUNTRIES")) safe->problemText.clear();
        safe->left.repaint();
        safe->repaint();
    });
    left.repaint();
    repaint();
}

void RadioPage::showGenre (int index)
{
    if (index < 0 || index >= genreTags().size()) return;
    if (leftMode != Left::Genres) showGenres();
    left.selected = index;
    left.ensureVisible (index);
    prefs().setValue ("radioLast", "genre:" + juce::String (index));
    query.clear();
    fetch (RadioDirectory::Kind::Genre, genreTags()[index], genreNames()[index], "genre:" + genreTags()[index]);
}

void RadioPage::showCountry (const juce::String& code, const juce::String& name)
{
    prefs().setValue ("radioLast", "country:" + code);
    if (countryCodes.contains (code)) { left.selected = countryCodes.indexOf (code); left.ensureVisible (left.selected); left.repaint(); }
    query.clear();
    fetch (RadioDirectory::Kind::Country, code, name, "country:" + code);
}

void RadioPage::showSearch (const juce::String& text)
{
    query = text;
    if (text.trim().length() < 2) { repaint(); return; }
    fetch (RadioDirectory::Kind::Search, text.trim(), "SEARCH: " + text.trim().toUpperCase(), {});
}

void RadioPage::showFavourites()
{
    ++request;   // (a list still on its way is no longer wanted)
    prefs().setValue ("radioLast", "starred");
    query.clear();
    problemText.clear();
    listKey = "starred";
    setStations (favourites(), "STARRED");
    if (stations.empty()) problemText = "NOTHING STARRED YET - RIGHT-CLICK A STATION, OR STAR";
    repaint();
}

void RadioPage::showRecent()
{
    ++request;
    prefs().setValue ("radioLast", "recent");
    query.clear();
    problemText.clear();
    listKey = "recent";
    setStations (recent(), "RECENT");
    if (stations.empty()) problemText = "NOTHING PLAYED YET";
    repaint();
}

void RadioPage::fetch (RadioDirectory::Kind k, const juce::String& what, const juce::String& heading, const juce::String& key)
{
    const int req = ++request;
    listKey = key;
    problemText.clear();
    // the saved list first (instant, and there when offline), then the fresh one
    auto cached = key.isNotEmpty() ? RadioDirectory::cacheLoad (key) : std::vector<RadioStation> {};
    setStations (cached, heading);
    ++loading;
    juce::Component::SafePointer<RadioPage> safe (this);
    proc.radioDirectory.fetch (k, what, [safe, req, heading, key] (std::vector<RadioStation> got, juce::String err)
    {
        if (safe == nullptr) return;
        --safe->loading;
        if (req != safe->request) { safe->repaint(); return; }
        if (err.isNotEmpty())
        {
            safe->problemText = safe->stations.empty() ? err : "OFFLINE - THE SAVED LIST";
            safe->status (err);
            safe->repaint();
            return;
        }
        if (key.isNotEmpty()) RadioDirectory::cacheStore (key, got);
        safe->setStations (std::move (got), heading);
        if (safe->stations.empty()) safe->problemText = "NO STATIONS IT CAN PLAY";
        safe->repaint();
    });
    repaint();
}

void RadioPage::setStations (std::vector<RadioStation> s, const juce::String& heading)
{
    // (the selected station stays selected if it's in the new list)
    const auto sel = selectedRow() >= 0 ? stations[(size_t) selectedRow()].uuid : juce::String();
    stations = std::move (s);
    listHeading = heading;
    list.selected = 0;
    for (int i = 0; i < (int) stations.size(); ++i) if (sel.isNotEmpty() && stations[(size_t) i].uuid == sel) list.selected = i;
    list.playing = -1;
    list.ensureVisible (list.selected);
    refreshButtons();
    list.repaint();
    repaint();
}

juce::String RadioPage::stationRow (int r) const
{
    if (r < 0 || r >= (int) stations.size()) return {};
    const auto& s = stations[(size_t) r];
    const float px = Theme::px;
    const int chars = std::max (16, (int) ((list.getWidth() - list.textX() - px * 12) / (px * PixelFont::kCellW)));
    const juce::String right = "  " + (s.countryCode.isNotEmpty() ? s.countryCode : juce::String ("--")) + " " + s.format().paddedRight (' ', 4) + " "
                             + (s.bitrate > 0 ? juce::String (s.bitrate).paddedLeft (' ', 3) + "K" : juce::String ("   -"));
    juce::String name = (isFavourite (s) ? "* " : "") + s.name;
    const int room = chars - right.length();
    if (name.length() > room) name = name.substring (0, std::max (1, room - 2)) + "..";
    return name.paddedRight (' ', room) + right;
}

// ---------------------------------------------------------------- playing
void RadioPage::playRow (int r)
{
    if (r < 0 || r >= (int) stations.size()) return;
    list.selected = r;
    const auto s = stations[(size_t) r];
    proc.radio.play (s);
    proc.radioDirectory.countClick (s);
    remember (s);
    list.playing = r;
    status ("RADIO: " + s.name + (s.country.isNotEmpty() ? " (" + s.country + ")" : juce::String()) + " - CONNECTING...");
    refreshButtons();
    repaint();
}

void RadioPage::stopRadio()
{
    proc.radio.stop();
    list.playing = -1;
    status ("RADIO STOPPED (THE GRAB BUFFER KEEPS WHAT IT HAS)");
    refreshButtons();
    repaint();
}

std::vector<RadioStation> RadioPage::favourites() { return loadList ("radioFavourites"); }
std::vector<RadioStation> RadioPage::recent()     { return loadList ("radioRecent"); }

bool RadioPage::isFavourite (const RadioStation& s)
{
    for (auto& f : favourites()) if (f.uuid == s.uuid && f.url == s.url) return true;
    return false;
}

void RadioPage::toggleFavourite (const RadioStation& s)
{
    auto f = favourites();
    auto it = std::find_if (f.begin(), f.end(), [&] (const RadioStation& x) { return x.uuid == s.uuid && x.url == s.url; });
    const bool was = it != f.end();
    if (was) f.erase (it); else f.insert (f.begin(), s);
    saveList ("radioFavourites", f);
    status (was ? s.name + " UNSTARRED" : s.name + " STARRED - IN STARRED");
    if (listKey == "starred") { setStations (f, "STARRED"); return; }
    refreshButtons();
    list.repaint();
}

void RadioPage::remember (const RadioStation& s)
{
    auto r = recent();
    r.erase (std::remove_if (r.begin(), r.end(), [&] (const RadioStation& x) { return x.uuid == s.uuid && x.url == s.url; }), r.end());
    r.insert (r.begin(), s);
    if (r.size() > 30) r.resize (30);
    saveList ("radioRecent", r);
}

// ---------------------------------------------------------------- sampling
void RadioPage::setBuffer (int seconds)
{
    proc.radio.setGrabSeconds (seconds);
    prefs().setValue ("radioBuffer", seconds);
    prefs().saveIfNeeded();
    status ("BUFFER " + juce::String (seconds) + " SEC: GRAB TAKES THE LAST " + juce::String (seconds) + " SECONDS THE RADIO PLAYED");
    refreshButtons();
    repaint();
}

int RadioPage::grab()
{
    std::vector<float> l, r;
    double rate = 0;
    if (! proc.radio.grab (l, r, rate) || rate <= 0) { status ("NOTHING TO GRAB YET - PLAY A STATION FIRST"); return -1; }
    // the first empty instrument
    int idx = -1;
    for (int i = 0; i < kMaxInstruments && idx < 0; ++i)
    {
        const auto& in = proc.song.instruments[(size_t) i];
        if (! in.hasData() && ! in.isPlugin()) idx = i;
    }
    if (idx < 0) { status ("NO EMPTY INSTRUMENT - CLEAR ONE, THEN GRAB AGAIN"); return -1; }
    TakeOptions o;
    o.channels = RecChannels::Stereo;
    o.trimSilence = false;   // (as it came off the stream: nothing trimmed, nothing normalised)
    o.normalise = false;
    const auto st = proc.radio.station();
    o.name = (st.name.isNotEmpty() ? st.name : juce::String ("RADIO")).substring (0, 22).trimEnd().toStdString();
    Instrument in;
    if (! takeToInstrument (l, r, rate, o, in)) { status ("NOTHING TO GRAB YET - PLAY A STATION FIRST"); return -1; }
    proc.history.instrument (idx, "GRAB FROM THE RADIO");
    proc.withLock ([&] { proc.player.jamAllOff(); std::swap (proc.song.instruments[(size_t) idx], in); });
    proc.editorState.instrument = idx;
    const double secs = (double) l.size() / rate;
    const auto title = proc.radio.nowPlaying();
    status ("GRABBED " + juce::String (secs, 1) + " SEC INTO INSTRUMENT " + hex2 (idx + 1) + ": " + juce::String (o.name)
            + (title.isNotEmpty() ? " (" + title.substring (0, 30) + ")" : juce::String()));
    if (onGrabbed) onGrabbed (idx);
    return idx;
}

// ---------------------------------------------------------------- the clock
void RadioPage::tick()
{
    const float l = proc.radio.peak (0), r = proc.radio.peak (1);
    meterL = std::max (l, meterL * 0.85f);
    meterR = std::max (r, meterR * 0.85f);
    refreshButtons();
    repaint (nowBox.getUnion (bufferBox).toNearestInt());
    if (loading > 0) repaint (headArea.toNearestInt());
}

void RadioPage::refreshButtons()
{
    const auto s = proc.radio.state();
    const bool on = s != RadioPlayer::State::Stopped && s != RadioPlayer::State::Failed;
    playBtn.setButtonText (on ? "STOP" : "PLAY");
    genresBtn.setToggleState (listKey != "starred" && listKey != "recent" && leftMode == Left::Genres, juce::dontSendNotification);
    countriesBtn.setToggleState (listKey != "starred" && listKey != "recent" && leftMode == Left::Countries, juce::dontSendNotification);
    favsBtn.setToggleState (listKey == "starred", juce::dontSendNotification);
    recentBtn.setToggleState (listKey == "recent", juce::dontSendNotification);
    const auto cur = proc.radio.station();
    const bool starNow = selectedRow() >= 0 ? isFavourite (stations[(size_t) selectedRow()]) : (cur.url.isNotEmpty() && isFavourite (cur));
    starBtn.setButtonText (starNow ? "UNSTAR" : "STAR");
    for (int k = 0; k < 4; ++k) bufBtn[k].setToggleState (proc.radio.grabSeconds() == radio::GrabBuffer::kChoices[k], juce::dontSendNotification);
    grabBtn.setEnabled (proc.radio.grabHeldSeconds() > 0.05);
    // (the playing station marked in the list)
    int playing = -1;
    if (on) for (int i = 0; i < (int) stations.size(); ++i) if (stations[(size_t) i].uuid == cur.uuid && stations[(size_t) i].url == cur.url) playing = i;
    if (playing != list.playing) { list.playing = playing; list.repaint(); }
}

juce::String RadioPage::nowText() const
{
    const auto s = proc.radio.station();
    const auto st = proc.radio.state();
    juce::StringArray lines;
    if (s.url.isEmpty()) return "NOTHING PLAYING";
    lines.add (s.name);
    const auto t = proc.radio.nowPlaying();
    if (t.isNotEmpty()) lines.add (t);
    juce::String info = s.format() + (s.bitrate > 0 ? " " + juce::String (s.bitrate) + "K" : juce::String());
    if (s.countryCode.isNotEmpty()) info << "  " << s.countryCode;
    if (s.country.isNotEmpty()) info << "  " << s.country;
    lines.add (info);
    juce::String state = RadioPlayer::stateName (st);
    if (st == RadioPlayer::State::Buffering) state << " " << juce::String (proc.radio.bufferedSeconds(), 1) << " SEC";
    if (st == RadioPlayer::State::Reconnecting) state << " " << juce::String (proc.radio.attempt()) << "/" << juce::String (radio::kMaxRetries);
    if ((st == RadioPlayer::State::Reconnecting || st == RadioPlayer::State::Failed) && proc.radio.problem().isNotEmpty()) state << ": " << proc.radio.problem();
    lines.add (state);
    return lines.joinIntoString ("\n");
}

juce::String RadioPage::bufferText() const
{
    return "BUFFER " + juce::String (proc.radio.grabSeconds()) + " SEC - " + juce::String (proc.radio.grabHeldSeconds(), 1) + " HELD";
}

// ---------------------------------------------------------------- keys
bool RadioPage::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    const int code = k.getKeyCode();
    if (m.isCommandDown() || m.isCtrlDown() || m.isAltDown()) return false;
    if (code == juce::KeyPress::escapeKey)
    {
        if (query.isNotEmpty()) { query.clear(); repaint(); return true; }
        if (onClose) onClose();
        return true;
    }
    if (code == juce::KeyPress::returnKey)
    {
        if (query.trim().length() >= 2 && listHeading != "SEARCH: " + query.trim().toUpperCase()) showSearch (query);
        else if (selectedRow() >= 0) playRow (selectedRow());
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        if (stations.empty()) return true;
        list.selected = juce::jlimit (0, (int) stations.size() - 1, list.selected + (code == juce::KeyPress::upKey ? -1 : 1));
        list.ensureVisible (list.selected);
        refreshButtons();
        list.repaint();
        return true;
    }
    if (code == juce::KeyPress::backspaceKey)
    {
        if (query.isEmpty()) return true;
        query = query.dropLastCharacters (1);
        repaint();
        return true;
    }
    const auto c = k.getTextCharacter();
    if (c == ' ' && query.isEmpty()) return false;   // (Space: the song, as everywhere)
    if (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-' || c == '.' || c == '\'' || c == '&')
    {
        if (query.length() < 30) query += juce::String::charToString (c);
        repaint();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- drawing
void RadioPage::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    g.fillAll (pal.panel);
    PixelFont::drawLeft (g, fit ("INTERNET RADIO", titleArea.getWidth(), px), titleArea, px, pal.ink);

    // the search line
    g.setColour (sk.bg);
    g.fillRect (searchArea);
    Theme::bevel (g, searchArea, false, std::max (1.0f, px * 0.5f), pal);
    {
        const bool blink = (juce::Time::getMillisecondCounter() / 500) % 2 == 0 && hasKeyboardFocus (false);
        const auto t = query.isEmpty() ? juce::String ("TYPE TO SEARCH, THEN ENTER") : "SEARCH: " + query.toUpperCase() + (blink ? "_" : " ");
        PixelFont::drawLeft (g, fit (t, searchArea.getWidth() - px * 4, px), searchArea.reduced (px * 2, 0), px, query.isEmpty() ? sk.dim : sk.text);
    }
    // what's listed
    {
        juce::String h = listHeading.isEmpty() ? juce::String ("STATIONS") : listHeading;
        if (loading > 0) h << " - LOADING" << juce::String ("...").substring (0, 1 + (int) (juce::Time::getMillisecondCounter() / 300) % 3);
        else if (problemText.isNotEmpty()) h << " - " << problemText;
        else h << ": " << juce::String ((int) stations.size()) << " STATIONS";
        PixelFont::drawLeft (g, fit (h, headArea.getWidth(), px), headArea, px, problemText.isNotEmpty() && loading == 0 ? sk.track[1] : pal.ink);
    }

    // NOW PLAYING
    Theme::panelBox (g, nowBox, "NOW PLAYING", pal);
    {
        auto r = nowArea;
        const float lh = px * 11;
        const auto lines = juce::StringArray::fromLines (nowText());
        for (int i = 0; i < lines.size() && r.getHeight() >= lh; ++i)
        {
            // (the stream's title can be long: up to 2 lines of it)
            auto t = lines[i];
            const int n = std::max (4, (int) (r.getWidth() / (px * PixelFont::kCellW)));
            const bool isTitle = i == 1 && lines.size() == 4;
            const auto col = i == lines.size() - 1 ? (proc.radio.state() == RadioPlayer::State::Failed ? sk.track[1] : pal.ink) : pal.ink;
            if ((isTitle || i == lines.size() - 1) && t.length() > n && r.getHeight() >= lh * 2)
            {
                int cut = t.substring (0, n).lastIndexOfChar (' ');
                if (cut < n / 2) cut = n;
                PixelFont::drawLeft (g, t.substring (0, cut).trimEnd(), r.removeFromTop (lh), px, col);
                t = t.substring (cut).trimStart();
            }
            PixelFont::drawLeft (g, fit (t, r.getWidth(), px), r.removeFromTop (lh), px, col);
        }
        // the level
        g.setColour (sk.bg);
        g.fillRect (meterArea);
        auto m = meterArea.reduced (px);
        const float h = (m.getHeight() - px) / 2;
        auto bar = [&] (juce::Rectangle<float> b, float v)
        {
            const float w = b.getWidth() * juce::jlimit (0.0f, 1.0f, std::sqrt (v));
            g.setColour (v > 0.98f ? sk.track[1] : sk.track[3]);
            g.fillRect (b.withWidth (w));
        };
        bar (m.removeFromTop (h), meterL);
        m.removeFromTop (px);
        bar (m, meterR);
        Theme::bevel (g, meterArea, false, std::max (1.0f, px * 0.5f), pal);
    }

    // the GRAB buffer
    Theme::panelBox (g, bufferBox, "GRAB BUFFER", pal);
    PixelFont::drawLeft (g, "SEC", bufLabel, px, pal.ink);
    {
        g.setColour (sk.bg);
        g.fillRect (bufferBar);
        const double held = proc.radio.grabHeldSeconds(), len = std::max (1, proc.radio.grabSeconds());
        g.setColour (sk.track[2].withAlpha (0.45f));
        g.fillRect (bufferBar.reduced (px).withWidth ((bufferBar.getWidth() - px * 2) * (float) juce::jlimit (0.0, 1.0, held / len)));
        Theme::bevel (g, bufferBar, false, std::max (1.0f, px * 0.5f), pal);
        PixelFont::drawCentred (g, fit (bufferText(), bufferBar.getWidth() - px * 2, px), bufferBar, px, sk.text);
    }
    // (copyright)
    {
        auto r = noteArea;
        const float lh = px * 10;
        for (auto line : { "STREAMS ARE COPYRIGHTED:", "CLEAR SAMPLES BEFORE", "YOU RELEASE THEM." })
            if (r.getHeight() >= lh) PixelFont::drawLeft (g, fit (line, r.getWidth(), px), r.removeFromTop (lh), px, pal.faint);
    }
}

void RadioPage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    auto r = getLocalBounds().reduced (gap * 2);

    auto head = r.removeFromTop (rowH);
    closeBtn.setBounds (head.removeFromRight (charsW (5)));
    head.removeFromRight (gap * 2);
    titleArea = head.toFloat();
    r.removeFromTop (gap * 2);

    // right: NOW PLAYING and the GRAB buffer
    auto right = r.removeFromRight (std::min (charsW (30), r.getWidth() / 3));
    r.removeFromRight (gap * 3);
    // left: genres / countries
    auto leftCol = r.removeFromLeft (std::min (charsW (20), r.getWidth() / 3));
    r.removeFromLeft (gap * 3);
    {
        auto row = leftCol.removeFromTop (rowH); leftCol.removeFromTop (gap);
        const int hw = (row.getWidth() - gap) / 2;
        genresBtn.setBounds (row.removeFromLeft (hw)); row.removeFromLeft (gap); countriesBtn.setBounds (row);
        row = leftCol.removeFromTop (rowH); leftCol.removeFromTop (gap * 2);
        favsBtn.setBounds (row.removeFromLeft (hw)); row.removeFromLeft (gap); recentBtn.setBounds (row);
        left.setBounds (leftCol);
    }
    // middle: the search, what's listed, the stations
    searchArea = r.removeFromTop (rowH).toFloat();
    r.removeFromTop (gap);
    headArea = r.removeFromTop (rowH).toFloat();
    r.removeFromTop (gap);
    list.setBounds (r);

    // NOW PLAYING: 5 lines, the level, PLAY / STAR, VOL
    const int title = (int) (px * 11);
    const int nowH = title + rowH * 5 + gap * 2 + (int) (px * 8) + gap * 2 + rowH + gap + rowH + gap * 4;
    auto nb = right.removeFromTop (std::min (right.getHeight(), nowH));
    nowBox = nb.toFloat();
    {
        auto in = nb.reduced (gap * 2).withTrimmedTop (title);
        volSlider.setBounds (in.removeFromBottom (rowH)); in.removeFromBottom (gap);
        auto row = in.removeFromBottom (rowH); in.removeFromBottom (gap * 2);
        const int hw = (row.getWidth() - gap) / 2;
        playBtn.setBounds (row.removeFromLeft (hw)); row.removeFromLeft (gap); starBtn.setBounds (row);
        meterArea = in.removeFromBottom ((int) (px * 8)).toFloat(); in.removeFromBottom (gap);
        nowArea = in.toFloat();
    }
    right.removeFromTop (gap * 3);
    // GRAB: the length, how much is held, and the button (big)
    const int bufH = title + rowH * 3 + gap * 6 + rowH;
    auto bb = right.removeFromTop (std::min (right.getHeight(), bufH));
    bufferBox = bb.toFloat();
    {
        auto in = bb.reduced (gap * 2).withTrimmedTop (title);
        auto row = in.removeFromTop (rowH); in.removeFromTop (gap);
        bufLabel = row.removeFromRight (charsW (3)).toFloat(); row.removeFromRight (gap);
        const int w = (row.getWidth() - gap * 3) / 4;
        for (auto& b : bufBtn) { b.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
        bufferBar = in.removeFromTop (rowH).toFloat(); in.removeFromTop (gap * 2);
        grabBtn.setBounds (in.removeFromTop (rowH * 2));
    }
    right.removeFromTop (gap * 2);
    noteArea = right.toFloat();
}
} // namespace prv::ui
