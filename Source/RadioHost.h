// Paula's Revenge - INTERNET RADIO: the station directory (Radio Browser, radio-browser.info) and
// the player. A background thread opens the stream, reads the ICY titles, decodes the MP3 and
// changes its rate into a FIFO; the audio thread only plays out of the FIFO (silence when it's
// empty, never a wait). The radio goes straight to the output with its own VOL (no mixer, no
// effects, not in renders or bounces) and the GRAB buffer keeps the last 5 / 10 / 20 / 30 seconds.
#pragma once

#include <JuceHeader.h>
#include "engine/Radio.h"
#include "engine/RadioFormats.h"

namespace prv
{
struct RadioStation
{
    juce::String uuid, name, url, country, countryCode, codec, tags, homepage;
    int bitrate = 0, votes = 0, clicks = 0;
    bool hls = false;                       // (an HLS stream: a playlist of short pieces)
    juce::String format() const;            // "MP3", "AAC", "AAC+", "HLS"
    juce::var toVar() const;
    static RadioStation fromVar (const juce::var&);
    bool operator== (const RadioStation& o) const { return uuid == o.uuid && url == o.url; }
};

// the network: the real one (JUCE's web streams), or a stand-in for the tests
struct RadioNet
{
    struct Stream
    {
        virtual ~Stream() = default;
        juce::StringPairArray headers;          // (lower-case keys: content-type, icy-metaint, ...)
        virtual int  read (void* dest, int max) = 0;   // bytes read; 0 = none yet; -1 = it ended / failed
        virtual void cancel() {}                // (from another thread: read() returns soon)
    };
    virtual ~RadioNet() = default;
    virtual juce::String get (const juce::String& url, juce::String& error) = 0;   // a whole (short) text reply
    virtual std::unique_ptr<Stream> open (const juce::String& url, juce::String& error) = 0;
};
std::unique_ptr<RadioNet> makeWebRadioNet();
// the one the app uses: the web, or (tests) whatever is put in it before the radio is used
struct RadioNetSwitch : RadioNet
{
    std::unique_ptr<RadioNet> inner = makeWebRadioNet();
    juce::String get (const juce::String& url, juce::String& error) override { return inner->get (url, error); }
    std::unique_ptr<Stream> open (const juce::String& url, juce::String& error) override { return inner->open (url, error); }
};

// ---- the directory: lists fetched on a thread; the answers arrive on the message thread
class RadioDirectory
{
public:
    explicit RadioDirectory (RadioNet&);
    ~RadioDirectory();
    enum class Kind { Genre, Country, Search };
    using Done = std::function<void (std::vector<RadioStation>, juce::String error)>;
    void fetch (Kind, const juce::String& what, Done);
    void fetchCountries (std::function<void (juce::StringArray names, juce::StringArray codes, juce::String error)>);
    void countClick (const RadioStation&);   // (the directory's popularity count: a play)
    juce::String server() const;              // the one that answered last
    bool busy() const { return running.load() > 0; }

    // (the parts the tests use directly)
    static std::vector<RadioStation> parseStations (const juce::String& json);   // MP3 that worked, no HLS, no repeats
    static juce::String pathFor (Kind, const juce::String& what);
    juce::String query (const juce::String& path, juce::String& error);          // tries the servers in turn

    // the last lists, kept on disk so the page opens with something even offline
    static juce::File cacheFile();
    static void cacheStore (const juce::String& key, const std::vector<RadioStation>&);
    static std::vector<RadioStation> cacheLoad (const juce::String& key);

private:
    RadioNet& net;
    juce::StringArray servers;
    mutable juce::CriticalSection cs;
    juce::String lastGood;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<int> running { 0 };
    void run (std::function<void()> job);
};

// ---- the player
class RadioPlayer : private juce::Thread
{
public:
    explicit RadioPlayer (RadioNet&);
    ~RadioPlayer() override;

    enum class State { Stopped, Connecting, Buffering, Playing, Reconnecting, Failed };
    static juce::String stateName (State);

    // ---- the message thread
    void prepare (double sampleRate);         // the app's rate (stops and starts again if it changed)
    void play (const RadioStation&);
    void stop();
    State state() const;
    int  attempt() const { return tries.load(); }       // RECONNECTING n / 5
    juce::String nowPlaying() const;                     // the stream's title ("" if it sends none)
    juce::String problem() const;                        // why it stopped / is reconnecting
    RadioStation station() const;
    double bufferedSeconds() const;                      // in the FIFO, ready to play
    std::atomic<float> volume { 0.8f };                  // its own VOL (0..1)
    double retryScale = 1.0;                             // (tests: shorter waits)
    double prebufferSeconds = 3.0;                       // before it starts (and 2 after running dry)

    // GRAB: the last `seconds` that played, as it came off the stream (before VOL)
    void setGrabSeconds (int seconds);
    int  grabSeconds() const;
    double grabHeldSeconds() const;
    bool grab (std::vector<float>& left, std::vector<float>& right, double& rate);   // false: nothing held
    void clearGrab();

    // ---- the audio thread: plays into out (VOL), and gives what played in raw (silence if nothing)
    void process (float* outL, float* outR, float* rawL, float* rawR, int n);
    float peak (int channel);                            // since the last call (the meter)
    int64_t underruns() const { return dry.load(); }

private:
    RadioNet& net;
    double rate = 0;
    radio::Fifo fifo;
    juce::SpinLock grabLock;
    radio::GrabBuffer grabBuf;
    std::atomic<int> grabSecs { 10 };

    mutable juce::CriticalSection cs;          // the strings and the station
    RadioStation current;
    juce::String title, why;
    std::atomic<int> st { (int) State::Stopped };
    std::atomic<int> tries { 0 };
    std::atomic<int> generation { 0 };          // a new play() / stop()
    std::atomic<bool> wanted { false };
    std::atomic<bool> flush { false };          // (the audio thread empties the FIFO)
    std::atomic<bool> waitingForBuffer { true };
    std::atomic<int64_t> dry { 0 };
    std::atomic<float> peakL { 0 }, peakR { 0 };
    float gain = 0;                             // (audio thread: VOL, smoothed)
    juce::CriticalSection streamLock;
    RadioNet::Stream* liveStream = nullptr;

    void run() override;
    enum class Result { Stopped, Failed, Unplayable };
    Result playOnce (const RadioStation&, int gen);
    Result playHls (juce::String url, std::string playlist, int gen);
    bool download (const juce::String& url, std::vector<uint8_t>& out, size_t maxBytes, int gen, juce::String& err);
    bool pushAudio (std::vector<float>& L, std::vector<float>& R, int srcRate, radio::Resampler&, int gen, int64_t& played);
    void setState (State s) { st.store ((int) s); }
    void setWhy (const juce::String& w) { const juce::ScopedLock l (cs); why = w; }
};
} // namespace prv
