// Paula's Revenge - STEMS in the app (see StemsJob.h)
#include "StemsJob.h"
#include "Diagnostics.h"

namespace prv
{
namespace
{
    // the files' SHA-256 (from the release's SHA256SUMS.txt, read when the models are downloaded)
    juce::String sha256Of (const juce::File& f)
    {
        juce::FileInputStream in (f);
        if (! in.openedOk()) return {};
        return juce::SHA256 (in).toHexString().toLowerCase();
    }
}

StemsJob::StemsJob() : juce::Thread ("Stems") {}

StemsJob::~StemsJob()
{
    stop = true;
    stopThread (30000);
}

juce::File StemsJob::modelFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Paula's Revenge").getChildFile ("Stem Models");
}

juce::String StemsJob::baseUrl() { return "https://github.com/TwistedGenetics/Paula-s-Revenge/releases/download/stem-models-1/"; }

bool StemsJob::haveModels (stems::Quality q)
{
    for (const auto& m : stems::modelsFor (q))
        if (! modelFolder().getChildFile (juce::String (m)).existsAsFile()) return false;
    return true;
}

int64_t StemsJob::downloadSize (stems::Quality q)
{
    int64_t total = 0;
    for (const auto& m : stems::modelsFor (q))
        if (! modelFolder().getChildFile (juce::String (m)).existsAsFile()) total += 170LL << 20;
    return total;
}

bool StemsJob::start (std::vector<float> left, std::vector<float> right, double r, stems::Quality q, Done d)
{
    if (isThreadRunning()) return false;
    L = std::move (left); R = std::move (right);
    rate = r; quality = q; done = std::move (d);
    stop = false; prog = 0; sepStarted = 0;
    started = juce::Time::getMillisecondCounterHiRes();
    setStage ("STARTING");
    startThread (juce::Thread::Priority::low);
    return true;
}

void StemsJob::cancel() { stop = true; }

juce::String StemsJob::stage() const { const juce::ScopedLock l (cs); return what; }

double StemsJob::secondsLeft() const
{
    const double p = prog.load(), t0 = sepStarted.load();
    if (t0 <= 0 || p < 0.04) return -1;
    const double spent = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    return spent * (1.0 - p) / p;
}

bool StemsJob::webDownload (const juce::String& name, const juce::File& to, std::function<void (double)> progress, juce::String& error)
{
    juce::WebInputStream in (juce::URL (baseUrl() + name), false);
    in.withNumRedirectsToFollow (10).withConnectionTimeout (20000);
    if (! in.connect (nullptr) || in.getStatusCode() >= 400)
    {
        error = "COULDN'T DOWNLOAD " + name.toUpperCase() + " (CHECK THE INTERNET CONNECTION)";
        return false;
    }
    const auto total = in.getTotalLength();
    const auto part = to.getSiblingFile (to.getFileName() + ".part");
    part.deleteFile();
    {
        juce::FileOutputStream out (part);
        if (! out.openedOk()) { error = "COULDN'T WRITE " + part.getFullPathName(); return false; }
        juce::HeapBlock<char> buf (1 << 16);
        int64_t got = 0;
        while (! in.isExhausted())
        {
            if (stop.load() || threadShouldExit()) { out.flush(); part.deleteFile(); error = "CANCELLED"; return false; }
            const int r = in.read (buf.get(), 1 << 16);
            if (r < 0) break;
            if (r == 0) { if (in.isExhausted()) break; juce::Thread::sleep (5); continue; }
            out.write (buf.get(), (size_t) r);
            got += r;
            if (total > 0 && progress) progress ((double) got / (double) total);
        }
        out.flush();
        if (total > 0 && got != total) { part.deleteFile(); error = "THE DOWNLOAD OF " + name.toUpperCase() + " WAS CUT SHORT - TRY AGAIN"; return false; }
    }
    return part.moveFileTo (to);
}

bool StemsJob::fetchModels (juce::String& error)
{
    const auto folder = modelFolder();
    folder.createDirectory();
    const auto files = stems::modelsFor (quality);
    juce::StringArray missing;
    for (auto& m : files) if (! folder.getChildFile (juce::String (m)).existsAsFile()) missing.add (juce::String (m));
    if (missing.isEmpty()) return true;
    auto get = download ? download : [this] (const juce::String& n, const juce::File& to, std::function<void (double)> p, juce::String& e) { return webDownload (n, to, p, e); };
    // the checksums first
    const auto sums = folder.getChildFile ("SHA256SUMS.txt");
    if (! get ("SHA256SUMS.txt", sums, nullptr, error)) return false;
    juce::StringPairArray expected;
    for (auto& line : juce::StringArray::fromLines (sums.loadFileAsString()))
    {
        const auto t = juce::StringArray::fromTokens (line.trim(), " *", "");
        if (t.size() >= 2) expected.set (t[t.size() - 1].trim(), t[0].trim().toLowerCase());
    }
    for (int i = 0; i < missing.size(); ++i)
    {
        const auto& name = missing[i];
        setStage ("DOWNLOADING THE MODELS (ONCE): " + juce::String (i + 1) + " OF " + juce::String (missing.size()));
        const auto to = folder.getChildFile (name);
        const double base = (double) i / missing.size(), span = 1.0 / missing.size();
        if (! get (name, to, [this, base, span] (double f) { prog = base + f * span; }, error)) return false;
        const auto want = expected[name];
        if (want.isNotEmpty() && sha256Of (to) != want)
        {
            to.deleteFile();
            error = "THE DOWNLOADED " + name.toUpperCase() + " ISN'T RIGHT (A BAD DOWNLOAD?) - TRY AGAIN";
            return false;
        }
        Diagnostics::get().log ("STEMS: DOWNLOADED " + name + " (" + juce::String (to.getSize() / 1048576.0, 1) + " MB)");
    }
    return true;
}

void StemsJob::run()
{
    juce::String error;
    auto result = std::make_shared<stems::StemResult>();
    bool ok = fetchModels (error);
    if (ok && ! stop.load())
    {
        prog = 0;
        sepStarted = juce::Time::getMillisecondCounterHiRes();
        setStage (quality == stems::Quality::Best ? "SPLITTING (BEST: 4 MODELS)" : "SPLITTING (FAST)");
        stems::Progress p;
        p.cancel = &stop;
        p.report = [this] (double f) { prog = f; };
        std::string err;
        const auto path = [] (const std::string& name)
        {
            const auto f = modelFolder().getChildFile (juce::String (name));
            return f.existsAsFile() ? f.getFullPathName().toStdString() : std::string();
        };
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        ok = separator ? separator (quality, path, L, R, rate, *result, p, err)
                       : stems::separateSample (quality, path, L, R, rate, *result, p, err);
        if (! ok) error = juce::String (err);
        Diagnostics::get().log ("STEMS (" + juce::String (quality == stems::Quality::Best ? "BEST" : "FAST") + ") " + (ok ? "DONE" : "FAILED: " + error)
                                + " - " + juce::String ((double) L.size() / rate, 1) + " S OF AUDIO IN "
                                + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) + " S");
    }
    if (stop.load()) { ok = false; error = "CANCELLED"; }
    L.clear(); R.clear();
    auto d = done;
    juce::MessageManager::callAsync ([d, ok, error, result] { if (d) d (ok, error, result); });
}
} // namespace prv
