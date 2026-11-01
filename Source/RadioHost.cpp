// Paula's Revenge - INTERNET RADIO: the directory and the player (see RadioHost.h)
#include "RadioHost.h"
#include "Prefs.h"

namespace prv
{
namespace
{
    const char* kUserAgent = "PaulasRevenge/0.31 (github.com/TwistedGenetics/Paula-s-Revenge)";

    // the pixel font is plain ASCII: accented letters lose their accents, other scripts go
    juce::String plain (const juce::String& s)
    {
        static const char* from = "\xC0\xC1\xC2\xC3\xC4\xC5\xC7\xC8\xC9\xCA\xCB\xCC\xCD\xCE\xCF\xD1\xD2\xD3\xD4\xD5\xD6\xD8\xD9\xDA\xDB\xDC\xDD"
                                  "\xE0\xE1\xE2\xE3\xE4\xE5\xE7\xE8\xE9\xEA\xEB\xEC\xED\xEE\xEF\xF1\xF2\xF3\xF4\xF5\xF6\xF8\xF9\xFA\xFB\xFC\xFD\xFF";
        static const char* to   = "AAAAAACEEEEIIIINOOOOOOUUUUY"
                                  "aaaaaaceeeeiiiinoooooouuuuyy";
        juce::String out;
        bool space = false;
        for (auto p = s.getCharPointer(); ! p.isEmpty(); ++p)
        {
            juce::juce_wchar c = *p;
            if (c >= 0xC0 && c <= 0xFF)
            {
                const char* k = std::strchr (from, (char) c);
                c = k != nullptr ? (juce::juce_wchar) to[k - from] : (c == 0xDF ? 's' : 0);
            }
            else if (c == 0x2019 || c == 0x2018) c = '\'';
            else if (c == 0x2013 || c == 0x2014) c = '-';
            if (c == '\t' || c == '\n' || c == '\r') c = ' ';
            if (c < 32 || c >= 127) continue;
            if (c == ' ') { if (space || out.isEmpty()) continue; space = true; }
            else space = false;
            out << juce::String::charToString (c);
        }
        return out.trimEnd();
    }
    int letters (const juce::String& s) { int n = 0; for (auto c : s) if (juce::CharacterFunctions::isLetterOrDigit (c)) ++n; return n; }

    struct WebStream : RadioNet::Stream
    {
        std::unique_ptr<juce::WebInputStream> s;
        int read (void* d, int max) override
        {
            if (s->isError()) return -1;
            const int r = s->read (d, max);
            if (r < 0) return -1;
            if (r == 0 && s->isExhausted()) return -1;
            return r;
        }
        void cancel() override { s->cancel(); }
    };

    struct WebNet : RadioNet
    {
        static std::unique_ptr<juce::WebInputStream> connect (const juce::String& url, bool icy, juce::String& error, juce::StringPairArray& headers)
        {
            auto ws = std::make_unique<juce::WebInputStream> (juce::URL (url), false);
            ws->withExtraHeaders (juce::String (icy ? "Icy-MetaData: 1\r\n" : "") + "User-Agent: " + kUserAgent + "\r\n")
               .withConnectionTimeout (10000)
               .withNumRedirectsToFollow (5);
            if (! ws->connect (nullptr)) { error = "COULDN'T CONNECT"; return nullptr; }
            const int code = ws->getStatusCode();
            if (code >= 400) { error = "THE SERVER SAID " + juce::String (code); return nullptr; }
            const auto h = ws->getResponseHeaders();
            for (int i = 0; i < h.size(); ++i) headers.set (h.getAllKeys()[i].toLowerCase().trim(), h.getAllValues()[i].trim());
            return ws;
        }
        juce::String get (const juce::String& url, juce::String& error) override
        {
            juce::StringPairArray h;
            auto ws = connect (url, false, error, h);
            if (ws == nullptr) return {};
            juce::MemoryOutputStream mo;
            char buf[16384];
            while (! ws->isExhausted() && mo.getDataSize() < (8u << 20))
            {
                const int r = ws->read (buf, (int) sizeof (buf));
                if (r <= 0) break;
                mo.write (buf, (size_t) r);
            }
            return mo.toUTF8();
        }
        std::unique_ptr<Stream> open (const juce::String& url, juce::String& error) override
        {
            auto st = std::make_unique<WebStream>();
            st->s = connect (url, true, error, st->headers);
            if (st->s == nullptr) return nullptr;
            return st;
        }
    };
}

std::unique_ptr<RadioNet> makeWebRadioNet() { return std::make_unique<WebNet>(); }

// ---------------------------------------------------------------------------- stations
juce::var RadioStation::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("stationuuid", uuid); o->setProperty ("name", name); o->setProperty ("url_resolved", url);
    o->setProperty ("country", country); o->setProperty ("countrycode", countryCode); o->setProperty ("codec", codec);
    o->setProperty ("tags", tags); o->setProperty ("homepage", homepage);
    o->setProperty ("hls", hls ? 1 : 0);
    o->setProperty ("bitrate", bitrate); o->setProperty ("votes", votes); o->setProperty ("clickcount", clicks);
    return juce::var (o);
}

RadioStation RadioStation::fromVar (const juce::var& v)
{
    RadioStation s;
    s.uuid = v["stationuuid"].toString();
    s.url = v["url_resolved"].toString().trim();
    if (s.url.isEmpty()) s.url = v["url"].toString().trim();
    s.homepage = v["homepage"].toString().trim();
    s.country = plain (v["country"].toString()).toUpperCase();
    if (s.country.startsWith ("THE ")) s.country = s.country.substring (4);
    s.country = s.country.replace ("UNITED KINGDOM OF GREAT BRITAIN AND NORTHERN IRELAND", "UNITED KINGDOM").replace ("UNITED STATES OF AMERICA", "UNITED STATES");
    s.countryCode = v["countrycode"].toString().toUpperCase().trim();
    s.codec = v["codec"].toString().toUpperCase().trim();
    s.hls = (int) v["hls"] != 0 || s.url.upToFirstOccurrenceOf ("?", false, false).toLowerCase().endsWith (".m3u8");
    s.tags = plain (v["tags"].toString()).toUpperCase();
    s.bitrate = (int) v["bitrate"];
    s.votes = (int) v["votes"];
    s.clicks = (int) v["clickcount"];
    s.name = plain (v["name"].toString()).toUpperCase();
    if (letters (s.name) < 2)
    {
        // (a name in a script the pixel font hasn't got: its web site, or where it's from)
        auto host = juce::URL (s.homepage.isNotEmpty() ? s.homepage : s.url).getDomain().fromFirstOccurrenceOf ("www.", false, false);
        if (host.isEmpty()) host = juce::URL (s.homepage.isNotEmpty() ? s.homepage : s.url).getDomain();
        s.name = host.isNotEmpty() ? host.toUpperCase() : "STATION " + s.countryCode;
    }
    return s;
}

juce::String RadioStation::format() const
{
    if (hls) return "HLS";
    if (codec == "HE-AAC") return "AAC+";
    return codec.isNotEmpty() ? codec : juce::String ("MP3");
}

std::vector<RadioStation> RadioDirectory::parseStations (const juce::String& json)
{
    std::vector<RadioStation> out;
    const auto v = juce::JSON::parse (json);
    if (! v.isArray()) return out;
    std::set<juce::String> seen;
    for (const auto& e : *v.getArray())
    {
        if (! v.isArray() || ! e.isObject()) continue;
        if (e.hasProperty ("lastcheckok") && (int) e["lastcheckok"] == 0) continue;
        auto s = RadioStation::fromVar (e);
        // MP3 everywhere; AAC / AAC+ and HLS where the computer can decode AAC
        const bool aac = s.codec == "AAC" || s.codec == "AAC+" || s.codec == "HE-AAC";
        const bool other = s.codec.contains ("OGG") || s.codec.contains ("OPUS") || s.codec.contains ("FLAC") || s.codec.contains ("WMA")
                         || s.codec.contains ("H.264") || s.codec.contains ("MP4V");
        if (s.hls ? (other || ! (radio::aacAvailable() || s.codec == "MP3")) : ! (s.codec == "MP3" || (aac && radio::aacAvailable()))) continue;
        const auto u = s.url.toLowerCase();
        if (! (u.startsWith ("http://") || u.startsWith ("https://"))) continue;
        const auto key = s.uuid.isNotEmpty() ? s.uuid : s.url;
        if (! seen.insert (key).second || ! seen.insert (s.name + "|" + s.url).second) continue;
        out.push_back (s);
    }
    return out;
}

// ---------------------------------------------------------------------------- the directory
RadioDirectory::RadioDirectory (RadioNet& n) : net (n) {}

RadioDirectory::~RadioDirectory()
{
    alive->store (false);
    for (int i = 0; i < 200 && running.load() > 0; ++i) juce::Thread::sleep (10);   // (let a fetch see it)
}

juce::String RadioDirectory::server() const { const juce::ScopedLock l (cs); return lastGood; }

juce::String RadioDirectory::pathFor (Kind k, const juce::String& what)
{
    const juce::String common = "&hidebroken=true&order=clickcount&reverse=true&limit=500";
    const auto w = juce::URL::addEscapeChars (what.trim().toLowerCase(), true);
    switch (k)
    {
        case Kind::Genre:   return "/json/stations/search?tag=" + w + common;
        case Kind::Country: return "/json/stations/search?countrycode=" + juce::URL::addEscapeChars (what.trim().toUpperCase(), true) + common;
        case Kind::Search:  return "/json/stations/search?name=" + w + common;
    }
    return {};
}

juce::String RadioDirectory::query (const juce::String& path, juce::String& error)
{
    // the servers: the one that answered last, then the list Radio Browser gives, then a few known ones
    juce::StringArray order;
    {
        const juce::ScopedLock l (cs);
        if (lastGood.isNotEmpty()) order.add (lastGood);
        if (servers.isEmpty())
        {
            juce::String e;
            const auto list = juce::JSON::parse (net.get ("https://all.api.radio-browser.info/json/servers", e));
            if (list.isArray()) for (const auto& s : *list.getArray()) servers.addIfNotAlreadyThere (s["name"].toString().trim());
            servers.removeEmptyStrings();
            juce::Random r;
            for (int i = servers.size() - 1; i > 0; --i) servers.getReference (i).swapWith (servers.getReference (r.nextInt (i + 1)));
            for (auto f : { "de1.api.radio-browser.info", "nl1.api.radio-browser.info", "at1.api.radio-browser.info",
                            "de2.api.radio-browser.info", "all.api.radio-browser.info" })
                servers.addIfNotAlreadyThere (f);
        }
        for (auto& s : servers) order.addIfNotAlreadyThere (s);
    }
    for (auto& s : order)
    {
        juce::String e;
        const auto text = net.get ("https://" + s + path, e);
        if (text.trimStart().startsWith ("[") || text.trimStart().startsWith ("{"))
        {
            const juce::ScopedLock l (cs);
            lastGood = s;
            return text;
        }
        error = e.isNotEmpty() ? e : juce::String ("NO ANSWER");
    }
    error = "THE STATION DIRECTORY ISN'T ANSWERING (" + error + ") - CHECK THE INTERNET CONNECTION";
    return {};
}

void RadioDirectory::run (std::function<void()> job)
{
    ++running;
    juce::Thread::launch ([this, job = std::move (job), a = alive]
    {
        if (a->load()) job();
        --running;
    });
}

void RadioDirectory::fetch (Kind k, const juce::String& what, Done done)
{
    auto a = alive;
    run ([this, k, what, done = std::move (done), a]
    {
        juce::String err;
        std::vector<RadioStation> list;
        const auto text = query (pathFor (k, what), err);
        if (text.isNotEmpty()) list = parseStations (text);
        if (k == Kind::Search && text.isNotEmpty() && a->load())
        {
            // a search looks in the genres too
            juce::String e2;
            const auto byTag = parseStations (query (pathFor (Kind::Genre, what), e2));
            for (auto& s : byTag) if (std::find_if (list.begin(), list.end(), [&] (const RadioStation& x) { return x.uuid == s.uuid; }) == list.end()) list.push_back (s);
            std::stable_sort (list.begin(), list.end(), [] (const RadioStation& x, const RadioStation& y) { return x.clicks > y.clicks; });
        }
        juce::MessageManager::callAsync ([a, done, list = std::move (list), err, text]
        {
            if (a->load() && done) done (list, text.isEmpty() ? err : juce::String());
        });
    });
}

void RadioDirectory::fetchCountries (std::function<void (juce::StringArray, juce::StringArray, juce::String)> done)
{
    auto a = alive;
    run ([this, done = std::move (done), a]
    {
        juce::String err;
        const auto text = query ("/json/countries?hidebroken=true&order=stationcount&reverse=true", err);
        juce::StringArray names, codes;
        const auto v = juce::JSON::parse (text);
        if (v.isArray())
            for (const auto& c : *v.getArray())
            {
                const auto code = c["iso_3166_1"].toString().toUpperCase().trim();
                auto name = plain (c["name"].toString()).toUpperCase();
                if (name.startsWith ("THE ")) name = name.substring (4);
                name = name.replace ("UNITED KINGDOM OF GREAT BRITAIN AND NORTHERN IRELAND", "UNITED KINGDOM")
                           .replace ("UNITED STATES OF AMERICA", "UNITED STATES");
                if (code.length() != 2 || (int) c["stationcount"] < 5 || codes.contains (code)) continue;
                names.add (name.isNotEmpty() ? name : code);
                codes.add (code);
            }
        juce::MessageManager::callAsync ([a, done, names, codes, err, ok = v.isArray()]
        {
            if (a->load() && done) done (names, codes, ok ? juce::String() : err);
        });
    });
}

void RadioDirectory::countClick (const RadioStation& s)
{
    if (s.uuid.isEmpty()) return;
    const auto path = "/json/url/" + juce::URL::addEscapeChars (s.uuid, true);
    run ([this, path] { juce::String e; query (path, e); });
}

juce::File RadioDirectory::cacheFile() { return prefs().getFile().getSiblingFile ("RadioCache.json"); }

void RadioDirectory::cacheStore (const juce::String& key, const std::vector<RadioStation>& list)
{
    auto all = juce::JSON::parse (cacheFile());
    if (! all.isObject()) all = juce::var (new juce::DynamicObject());
    juce::Array<juce::var> arr;
    for (auto& s : list) arr.add (s.toVar());
    all.getDynamicObject()->setProperty (key, arr);
    // (the last 40 lists at most)
    auto& props = all.getDynamicObject()->getProperties();
    while (props.size() > 40) props.remove (props.getName (0));
    cacheFile().replaceWithText (juce::JSON::toString (all, true));
}

std::vector<RadioStation> RadioDirectory::cacheLoad (const juce::String& key)
{
    std::vector<RadioStation> out;
    const auto all = juce::JSON::parse (cacheFile());
    const auto arr = all[juce::Identifier (key)];
    if (arr.isArray()) for (const auto& v : *arr.getArray()) out.push_back (RadioStation::fromVar (v));
    return out;
}

// ---------------------------------------------------------------------------- the player
RadioPlayer::RadioPlayer (RadioNet& n) : juce::Thread ("Radio"), net (n) {}

RadioPlayer::~RadioPlayer()
{
    wanted = false;
    ++generation;
    { const juce::ScopedLock l (streamLock); if (liveStream != nullptr) liveStream->cancel(); }
    signalThreadShouldExit();
    notify();
    stopThread (6000);
}

juce::String RadioPlayer::stateName (State s)
{
    switch (s)
    {
        case State::Stopped:      return "STOPPED";
        case State::Connecting:   return "CONNECTING";
        case State::Buffering:    return "BUFFERING";
        case State::Playing:      return "PLAYING";
        case State::Reconnecting: return "RECONNECTING";
        case State::Failed:       return "FAILED";
    }
    return {};
}

void RadioPlayer::prepare (double sampleRate)
{
    if (sampleRate <= 0 || sampleRate == rate) return;
    const bool was = wanted.load();
    const auto s = station();
    // (the thread stops while the FIFO changes size)
    wanted = false;
    ++generation;
    { const juce::ScopedLock l (streamLock); if (liveStream != nullptr) liveStream->cancel(); }
    signalThreadShouldExit();
    notify();
    stopThread (6000);
    rate = sampleRate;
    fifo.setSize ((int) (sampleRate * 8));   // 8 s: room for the 3 s it waits for and a margin
    {
        const juce::SpinLock::ScopedLockType l (grabLock);
        grabBuf.setLength (grabSecs.load(), sampleRate);
    }
    setState (State::Stopped);
    if (was) play (s);
}

void RadioPlayer::play (const RadioStation& s)
{
    {
        const juce::ScopedLock l (cs);
        current = s;
        title.clear();
        why.clear();
    }
    tries = 0;
    wanted = true;
    ++generation;
    flush = true;
    waitingForBuffer = true;
    dry = 0;
    { const juce::ScopedLock l (streamLock); if (liveStream != nullptr) liveStream->cancel(); }
    setState (State::Connecting);
    if (rate > 0 && ! isThreadRunning()) startThread();
    notify();
}

void RadioPlayer::stop()
{
    wanted = false;
    ++generation;
    flush = true;
    { const juce::ScopedLock l (streamLock); if (liveStream != nullptr) liveStream->cancel(); }
    setState (State::Stopped);
    tries = 0;
    notify();
}

RadioPlayer::State RadioPlayer::state() const
{
    const auto s = (State) st.load();
    if (s == State::Playing || s == State::Buffering) return waitingForBuffer.load() ? State::Buffering : State::Playing;
    return s;
}

juce::String RadioPlayer::nowPlaying() const { const juce::ScopedLock l (cs); return title; }
juce::String RadioPlayer::problem() const    { const juce::ScopedLock l (cs); return why; }
RadioStation RadioPlayer::station() const    { const juce::ScopedLock l (cs); return current; }
double RadioPlayer::bufferedSeconds() const  { return rate > 0 ? fifo.available() / rate : 0.0; }

void RadioPlayer::setGrabSeconds (int seconds)
{
    grabSecs = juce::jlimit (1, 60, seconds);
    if (rate <= 0) return;
    // (the copy and the new buffer are made here; the audio thread skips writing for that moment)
    const juce::SpinLock::ScopedLockType l (grabLock);
    grabBuf.setLength (grabSecs.load(), rate);
}

int RadioPlayer::grabSeconds() const { return grabSecs.load(); }

double RadioPlayer::grabHeldSeconds() const
{
    const juce::SpinLock::ScopedLockType l (const_cast<juce::SpinLock&> (grabLock));
    return grabBuf.heldSeconds();
}

bool RadioPlayer::grab (std::vector<float>& left, std::vector<float>& right, double& r)
{
    const juce::SpinLock::ScopedLockType l (grabLock);
    grabBuf.copy (left, right);
    r = grabBuf.rate();
    return ! left.empty();
}

void RadioPlayer::clearGrab()
{
    const juce::SpinLock::ScopedLockType l (grabLock);
    grabBuf.clear();
}

float RadioPlayer::peak (int ch) { return (ch == 0 ? peakL : peakR).exchange (0.0f); }

void RadioPlayer::process (float* outL, float* outR, float* rawL, float* rawR, int n)
{
    std::fill (rawL, rawL + n, 0.0f);
    std::fill (rawR, rawR + n, 0.0f);
    if (flush.exchange (false)) { fifo.clear(); waitingForBuffer = true; }
    if (! wanted.load() || rate <= 0) { gain = 0; return; }
    if (waitingForBuffer.load())
    {
        const double need = (dry.load() > 0 ? std::min (2.0, prebufferSeconds) : prebufferSeconds) * rate;
        if (fifo.available() < need) return;
        waitingForBuffer = false;
    }
    const int k = fifo.pop (rawL, rawR, n);
    if (k < n) { ++dry; waitingForBuffer = true; }   // (ran dry: silence, then it waits for 2 s again)
    const float target = juce::jlimit (0.0f, 1.0f, volume.load());
    const float step = (float) (1.0 / (0.02 * rate));
    float pl = 0, pr = 0;
    for (int i = 0; i < k; ++i)
    {
        gain = target > gain ? std::min (target, gain + step) : std::max (target, gain - step);
        outL[i] += rawL[i] * gain;
        outR[i] += rawR[i] * gain;
        pl = std::max (pl, std::abs (rawL[i])); pr = std::max (pr, std::abs (rawR[i]));
    }
    if (pl > peakL.load()) peakL = pl;
    if (pr > peakR.load()) peakR = pr;
    // GRAB: what came off the stream (if the message thread is copying it right now, this bit waits out)
    const juce::SpinLock::ScopedTryLockType tl (grabLock);
    if (tl.isLocked() && k > 0) grabBuf.write (rawL, rawR, k);
}

void RadioPlayer::run()
{
    while (! threadShouldExit())
    {
        if (! wanted.load()) { wait (-1); continue; }
        const int gen = generation.load();
        const auto s = station();
        setState (State::Connecting);
        const auto r = playOnce (s, gen);
        if (threadShouldExit()) break;
        if (gen != generation.load() || ! wanted.load()) continue;   // (a new station, or STOP)
        if (r == Result::Unplayable)
        {
            setState (State::Failed);
            wanted = false;
            continue;
        }
        const int t = ++tries;
        const int d = radio::retryDelayMs (t);
        if (d < 0)
        {
            setWhy ("GAVE UP AFTER " + juce::String (radio::kMaxRetries) + " TRIES: " + problem());
            setState (State::Failed);
            wanted = false;
            tries = 0;
            continue;
        }
        setState (State::Reconnecting);
        wait (juce::jmax (1, (int) (d * retryScale)));
    }
}

bool RadioPlayer::pushAudio (std::vector<float>& L, std::vector<float>& R, int srcRate, radio::Resampler& rs, int gen, int64_t& played)
{
    if (L.empty() || srcRate <= 0) return true;
    rs.setRates (srcRate, rate);
    std::vector<float> oL, oR;
    rs.process (L.data(), R.data(), (int) L.size(), oL, oR);
    // into the FIFO: when it's full, wait for the audio to make room
    size_t off = 0;
    while (off < oL.size())
    {
        off += (size_t) fifo.push (oL.data() + off, oR.data() + off, (int) (oL.size() - off));
        if (off < oL.size())
        {
            if (threadShouldExit() || gen != generation.load()) return false;
            juce::Thread::sleep (15);
        }
    }
    played += (int64_t) oL.size();
    if (played > (int64_t) (rate * 5)) tries = 0;   // (5 s playing: a later drop gets all its tries again)
    if ((State) st.load() == State::Connecting) setState (State::Buffering);
    return true;
}

bool RadioPlayer::download (const juce::String& url, std::vector<uint8_t>& out, size_t maxBytes, int gen, juce::String& err)
{
    // (a whole file: a playlist or an HLS segment; STOP / a new station cuts it short)
    out.clear();
    auto stream = net.open (url, err);
    if (stream == nullptr) return false;
    { const juce::ScopedLock l (streamLock); liveStream = stream.get(); }
    struct Clear { RadioPlayer& p; ~Clear() { const juce::ScopedLock l (p.streamLock); p.liveStream = nullptr; } } clearLive { *this };
    uint8_t buf[16384];
    double quietMs = 0;
    while (out.size() < maxBytes)
    {
        if (threadShouldExit() || gen != generation.load()) return false;
        const int r = stream->read (buf, (int) sizeof (buf));
        if (r < 0) break;
        if (r == 0)
        {
            quietMs += 10;
            if (quietMs > 15000) { err = "THE STATION STOPPED SENDING"; return false; }
            juce::Thread::sleep (10);
            continue;
        }
        quietMs = 0;
        out.insert (out.end(), buf, buf + r);
    }
    return true;
}

RadioPlayer::Result RadioPlayer::playOnce (const RadioStation& s, int gen)
{
    auto stale = [&] { return threadShouldExit() || gen != generation.load(); };
    juce::String url = s.url, err;
    std::unique_ptr<RadioNet::Stream> stream;
    juce::String ct;
    auto isHlsUrl = [] (const juce::String& u) { return u.upToFirstOccurrenceOf ("?", false, false).toLowerCase().endsWith (".m3u8"); };
    for (int hop = 0; hop < 4; ++hop)
    {
        // HLS: a playlist of short pieces, fetched one after another
        if (isHlsUrl (url))
        {
            std::vector<uint8_t> text;
            if (! download (url, text, 1u << 20, gen, err)) { if (stale()) return Result::Stopped; setWhy (err.isNotEmpty() ? err : "COULDN'T GET THE STATION'S PLAYLIST"); return Result::Failed; }
            return playHls (url, std::string (text.begin(), text.end()), gen);
        }
        stream = net.open (url, err);
        if (stale()) return Result::Stopped;
        if (stream == nullptr) { setWhy (err); return Result::Failed; }
        ct = stream->headers["content-type"].toLowerCase();
        if (! radio::isPlaylist (url.toStdString(), ct.toStdString()) && ! ct.contains ("mpegurl")) break;
        // a playlist: its first stream (or HLS after all)
        std::string text;
        char buf[4096];
        for (int i = 0; i < 256 && text.size() < 262144; ++i)
        {
            const int r = stream->read (buf, (int) sizeof (buf));
            if (r < 0) break;
            if (r == 0) { juce::Thread::sleep (20); continue; }
            text.append (buf, (size_t) r);
        }
        stream.reset();
        if (radio::looksLikeHls (text)) return playHls (url, text, gen);
        const auto urls = radio::parsePlaylist (text);
        if (urls.empty()) { setWhy ("THE STATION'S PLAYLIST IS EMPTY"); return Result::Unplayable; }
        url = juce::String (urls.front());
    }
    if (stream == nullptr) { setWhy ("TOO MANY PLAYLISTS"); return Result::Unplayable; }
    for (auto other : { "ogg", "opus", "flac", "text/html" })
        if (ct.contains (other))
        {
            setWhy ("THIS STATION SENDS " + (ct.contains ("text/html") ? juce::String ("A WEB PAGE") : ct.fromFirstOccurrenceOf ("/", false, false).toUpperCase())
                    + " - MP3 AND AAC ONLY FOR NOW, TRY ANOTHER");
            return Result::Unplayable;
        }
    auto hint = radio::StreamDecoder::Kind::Unknown;
    if (ct.contains ("audio/mpeg") || ct.contains ("audio/mp3")) hint = radio::StreamDecoder::Kind::Mp3;
    else if (ct.contains ("aac")) hint = radio::StreamDecoder::Kind::Aac;
    else if (ct.contains ("mp2t")) hint = radio::StreamDecoder::Kind::Ts;
    if (hint == radio::StreamDecoder::Kind::Aac && ! radio::aacAvailable()) { setWhy ("AAC CAN'T BE PLAYED ON THIS COMPUTER"); return Result::Unplayable; }

    radio::IcyParser icy;
    icy.reset (stream->headers["icy-metaint"].getIntValue());
    radio::StreamDecoder dec;
    dec.reset (hint);
    radio::Resampler rs;
    { const juce::ScopedLock l (streamLock); liveStream = stream.get(); }
    struct Clear { RadioPlayer& p; ~Clear() { const juce::ScopedLock l (p.streamLock); p.liveStream = nullptr; } } clearLive { *this };
    if (stale()) return Result::Stopped;
    setState (State::Buffering);

    std::vector<uint8_t> bytes;
    std::vector<float> L, R;
    uint8_t buf[4096];
    int64_t played = 0;
    double quietMs = 0;
    bool any = false;
    while (! stale())
    {
        const int r = stream->read (buf, (int) sizeof (buf));
        if (stale()) return Result::Stopped;
        if (r < 0) { setWhy ("THE STREAM STOPPED"); return Result::Failed; }
        if (r == 0)
        {
            quietMs += 10;
            if (quietMs > 10000) { setWhy ("NOTHING FROM THE STATION FOR 10 SECONDS"); return Result::Failed; }
            juce::Thread::sleep (10);
            continue;
        }
        quietMs = 0;
        bytes.clear();
        if (icy.feed (buf, (size_t) r, bytes)) { const juce::ScopedLock l (cs); title = plain (juce::String::fromUTF8 (icy.title().c_str())).toUpperCase(); }
        L.clear(); R.clear();
        dec.feed (bytes.data(), bytes.size(), L, R);
        if (! dec.problem().empty()) { setWhy (juce::String (dec.problem())); return Result::Unplayable; }
        if (! any && dec.sampleRate() == 0 && dec.badBytes() > 256 * 1024) { setWhy ("THIS ISN'T AN MP3 OR AAC STREAM - TRY ANOTHER"); return Result::Unplayable; }
        if (L.empty() || dec.sampleRate() <= 0) continue;
        any = true;
        if (! pushAudio (L, R, dec.sampleRate(), rs, gen, played)) return Result::Stopped;
    }
    return Result::Stopped;
}

RadioPlayer::Result RadioPlayer::playHls (juce::String url, std::string text, int gen)
{
    auto stale = [&] { return threadShouldExit() || gen != generation.load(); };
    juce::String err;
    auto fetchText = [&] (const juce::String& u, std::string& out)
    {
        std::vector<uint8_t> b;
        if (! download (u, b, 1u << 20, gen, err)) return false;
        out.assign (b.begin(), b.end());
        return true;
    };
    auto pl = radio::parseHls (text);
    if (pl.valid && pl.master)
    {
        // the best audio variant (or the audio-only rendition)
        const int v = radio::chooseVariant (pl);
        std::string pick = v >= 0 ? pl.variants[(size_t) v].uri : std::string();
        if (! pl.audioUri.empty() && (pick.empty() || pl.variants[(size_t) v].codecs.find ("avc") != std::string::npos)) pick = pl.audioUri;
        if (pick.empty()) { setWhy ("THE STATION'S HLS PLAYLIST HAS NOTHING TO PLAY"); return Result::Unplayable; }
        url = juce::String (radio::resolveUrl (url.toStdString(), pick));
        if (! fetchText (url, text)) { if (stale()) return Result::Stopped; setWhy (err.isNotEmpty() ? err : "COULDN'T GET THE STATION'S PLAYLIST"); return Result::Failed; }
        pl = radio::parseHls (text);
    }
    if (! pl.valid || pl.segments.empty()) { setWhy ("THE STATION'S HLS PLAYLIST IS EMPTY"); return Result::Failed; }
    if (! pl.keyMethod.empty() && juce::String (pl.keyMethod).toUpperCase() != "NONE") { setWhy ("THIS STATION IS ENCRYPTED - IT CAN'T BE PLAYED HERE"); return Result::Unplayable; }

    radio::StreamDecoder dec;
    radio::Resampler rs;
    const auto base = url.toStdString();
    if (! pl.mapUri.empty())
    {
        std::vector<uint8_t> init;
        if (! download (juce::String (radio::resolveUrl (base, pl.mapUri)), init, 4u << 20, gen, err)) { if (stale()) return Result::Stopped; setWhy (err); return Result::Failed; }
        if (! dec.setInit (init.data(), init.size())) { setWhy (juce::String (dec.problem())); return Result::Unplayable; }
    }
    setState (State::Buffering);
    // live: start 3 pieces from the end (as players do); a finished one: from the start
    size_t first = pl.ended ? 0 : (pl.segments.size() > 3 ? pl.segments.size() - 3 : 0);
    int64_t next = pl.segments[first].sequence;
    int64_t played = 0;
    int fails = 0, reloadFails = 0;
    std::vector<float> L, R;
    std::vector<uint8_t> seg;
    auto isMp4 = [] (const std::vector<uint8_t>& b)
    {
        if (b.size() < 8) return false;
        const std::string t (b.begin() + 4, b.begin() + 8);
        return t == "styp" || t == "moof" || t == "ftyp" || t == "sidx";
    };
    while (! stale())
    {
        for (const auto& sg : pl.segments)
        {
            if (sg.sequence < next) continue;
            if (stale()) return Result::Stopped;
            if (! download (juce::String (radio::resolveUrl (base, sg.uri)), seg, 16u << 20, gen, err))
            {
                if (stale()) return Result::Stopped;
                if (++fails >= 3) { setWhy (err.isNotEmpty() ? err : "THE STATION'S PIECES WON'T DOWNLOAD"); return Result::Failed; }
                break;   // (the playlist again, then this piece again)
            }
            fails = 0;
            L.clear(); R.clear();
            if (isMp4 (seg))
            {
                if (! pl.mapUri.empty() || dec.setInit (seg.data(), seg.size())) dec.feedMp4Segment (seg.data(), seg.size(), L, R);
            }
            else dec.feed (seg.data(), seg.size(), L, R);
            if (! dec.problem().empty()) { setWhy (juce::String (dec.problem())); return Result::Unplayable; }
            if (! pushAudio (L, R, dec.sampleRate(), rs, gen, played)) return Result::Stopped;
            next = sg.sequence + 1;
        }
        if (pl.ended && next > pl.segments.back().sequence) { setWhy ("THE STATION'S STREAM ENDED"); return Result::Failed; }
        // the next pieces: the playlist again after half a piece's time
        const int waitMs = juce::jlimit (500, 5000, (int) (pl.targetDuration * 500));
        for (int t = 0; t < waitMs; t += 20) { if (stale()) return Result::Stopped; juce::Thread::sleep (20); }
        std::string again;
        if (! fetchText (url, again))
        {
            if (stale()) return Result::Stopped;
            if (++reloadFails >= 3) { setWhy (err.isNotEmpty() ? err : "THE STATION'S PLAYLIST STOPPED COMING"); return Result::Failed; }
            continue;
        }
        auto fresh = radio::parseHls (again);
        if (! fresh.valid || fresh.segments.empty())
        {
            if (++reloadFails >= 3) { setWhy ("THE STATION'S PLAYLIST STOPPED COMING"); return Result::Failed; }
            continue;
        }
        reloadFails = 0;
        pl = std::move (fresh);
        // fell behind (the oldest piece is newer than the next one wanted), or the numbering started again
        if (next < pl.segments.front().sequence) next = pl.segments.front().sequence;
        if (next > pl.segments.back().sequence + 50) next = pl.segments[pl.segments.size() > 3 ? pl.segments.size() - 3 : 0].sequence;
    }
    return Result::Stopped;
}
} // namespace prv
