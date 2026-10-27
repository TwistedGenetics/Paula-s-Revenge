// Paula's Revenge - native song format, sample import and WAV rendering
#include "SongFile.h"
#include "engine/ModuleIO.h"

namespace prv
{
namespace
{
    constexpr uint32_t kMagic = 0x50525631; // "PRV1"
    constexpr int kVersion = 4;   // v2: stereo samples, v3: shaping + track pans, v4: 2nd effect column + sample rates

    void writeStr (juce::OutputStream& o, const std::string& s)
    {
        const auto n = (uint16_t) std::min<size_t> (s.size(), 255);
        o.writeShortBigEndian ((short) n);
        o.write (s.data(), n);
    }

    std::string readStr (juce::InputStream& in)
    {
        const int n = (uint16_t) in.readShortBigEndian();
        std::string s ((size_t) std::clamp (n, 0, 255), ' ');
        in.read (s.data(), (int) s.size());
        return s;
    }
}

juce::MemoryBlock writeSong (const Song& song)
{
    juce::MemoryOutputStream raw;
    raw.writeIntBigEndian ((int) kMagic);
    raw.writeIntBigEndian (kVersion);
    writeStr (raw, song.name);
    raw.writeByte ((char) song.numTracks);
    raw.writeShortBigEndian ((short) song.tempo);
    raw.writeByte ((char) song.speed);
    raw.writeByte (song.ledFilterAtStart ? 1 : 0);

    raw.writeShortBigEndian ((short) song.blocks.size());
    for (const auto& b : song.blocks)
    {
        raw.writeShortBigEndian ((short) b.lines);
        for (int l = 0; l < b.lines; ++l)
            for (int t = 0; t < kMaxTracks; ++t)
            {
                const auto& c = b.at (l, t);
                raw.writeByte ((char) c.note); raw.writeByte ((char) c.instr);
                raw.writeByte ((char) c.cmd);  raw.writeByte ((char) c.param);
                raw.writeByte ((char) c.cmd2); raw.writeByte ((char) c.param2);
            }
    }
    raw.writeShortBigEndian ((short) song.sequence.size());
    for (int s : song.sequence) raw.writeShortBigEndian ((short) s);

    for (const auto& in : song.instruments)
    {
        writeStr (raw, in.name);
        raw.writeByte ((char) in.volume);
        raw.writeByte ((char) in.finetune);
        raw.writeByte ((char) in.transpose);
        raw.writeIntBigEndian (in.loopStart);
        raw.writeIntBigEndian (in.loopLength);
        raw.writeIntBigEndian ((int) in.data.size());
        for (auto s : in.data) raw.writeShortBigEndian (s);
        raw.writeByte (in.isStereo() ? 1 : 0);
        if (in.isStereo()) for (auto s : in.dataR) raw.writeShortBigEndian (s);
    }

    // v3 extras, appended so older sections keep their layout
    for (int t = 0; t < kMaxTracks; ++t) raw.writeShortBigEndian ((short) song.trackPan[t]);
    auto writeEnv = [&raw] (const Envelope& e)
    {
        raw.writeByte (e.on ? 1 : 0);
        raw.writeFloatBigEndian (e.attack); raw.writeFloatBigEndian (e.decay);
        raw.writeFloatBigEndian (e.sustain); raw.writeFloatBigEndian (e.release);
    };
    for (const auto& in : song.instruments)
    {
        writeEnv (in.volEnv);
        raw.writeByte ((char) in.filter);
        raw.writeFloatBigEndian (in.cutoff);
        raw.writeFloatBigEndian (in.resonance);
        writeEnv (in.filterEnv);
        raw.writeFloatBigEndian (in.filterEnvAmount);
        raw.writeFloatBigEndian (in.keyTrack);
        raw.writeByte (in.amigaPan ? 1 : 0);
        raw.writeShortBigEndian ((short) in.pan);
    }
    // v4: per-instrument C-3 rate
    for (const auto& in : song.instruments) raw.writeDoubleBigEndian (in.rate);

    juce::MemoryOutputStream packed;
    {
        juce::GZIPCompressorOutputStream gz (packed, 6);
        gz.write (raw.getData(), raw.getDataSize());
        gz.flush();
    }
    return packed.getMemoryBlock();
}

bool readSong (const void* data, size_t size, Song& out, juce::String& error)
{
    juce::MemoryInputStream packed (data, size, false);
    juce::GZIPDecompressorInputStream gz (packed);
    juce::MemoryBlock rawBlock;
    gz.readIntoMemoryBlock (rawBlock);
    juce::MemoryInputStream in (rawBlock, false);

    if (rawBlock.getSize() < 16 || (uint32_t) in.readIntBigEndian() != kMagic) { error = "Not a Paula's Revenge song file."; return false; }
    const int version = in.readIntBigEndian();
    if (version > kVersion) { error = "This song was saved by a newer version."; return false; }

    Song s;
    s.name = readStr (in);
    s.numTracks = juce::jlimit (1, kMaxTracks, (int) (uint8_t) in.readByte());
    s.tempo = juce::jlimit (32, 255, (int) in.readShortBigEndian());
    s.speed = juce::jlimit (1, 31, (int) (uint8_t) in.readByte());
    s.ledFilterAtStart = in.readByte() != 0;

    const int nb = juce::jlimit (1, kMaxBlocks, (int) (uint16_t) in.readShortBigEndian());
    s.blocks.assign ((size_t) nb, Block{});
    for (auto& b : s.blocks)
    {
        b.lines = juce::jlimit (1, kMaxLines, (int) (uint16_t) in.readShortBigEndian());
        for (int l = 0; l < b.lines; ++l)
            for (int t = 0; t < kMaxTracks; ++t)
            {
                auto& c = b.at (l, t);
                c.note = (uint8_t) in.readByte(); c.instr = (uint8_t) in.readByte();
                c.cmd = (uint8_t) in.readByte(); c.param = (uint8_t) in.readByte();
                if (version >= 4) { c.cmd2 = (uint8_t) in.readByte(); c.param2 = (uint8_t) in.readByte(); }
                if (c.cmd >= kNumCommands) c.cmd = 0;
                if (c.cmd2 >= kNumCommands) c.cmd2 = 0;
                if (c.instr > kMaxInstruments) c.instr = 0;
                if (c.note > kNoteMax && c.note != kNoteOff) c.note = 0;
            }
    }
    const int ns = juce::jlimit (0, kMaxSequence, (int) (uint16_t) in.readShortBigEndian());
    s.sequence.clear();
    for (int i = 0; i < ns; ++i) s.sequence.push_back (juce::jlimit (0, nb - 1, (int) (uint16_t) in.readShortBigEndian()));
    if (s.sequence.empty()) s.sequence.push_back (0);

    for (auto& ins : s.instruments)
    {
        if (in.isExhausted()) break;
        ins.name = readStr (in);
        ins.volume = juce::jlimit (0, 64, (int) (uint8_t) in.readByte());
        ins.finetune = juce::jlimit (-8, 7, (int) (int8_t) in.readByte());
        ins.transpose = juce::jlimit (-48, 48, (int) (int8_t) in.readByte());
        ins.loopStart = in.readIntBigEndian();
        ins.loopLength = in.readIntBigEndian();
        const int len = juce::jlimit (0, 1 << 24, in.readIntBigEndian());
        ins.data.resize ((size_t) len);
        for (auto& v : ins.data) v = in.readShortBigEndian();
        if (version >= 2 && in.readByte() != 0)
        {
            ins.dataR.resize ((size_t) len);
            for (auto& v : ins.dataR) v = in.readShortBigEndian();
        }
        if (! ins.isLooped()) { ins.loopStart = 0; ins.loopLength = 0; }
    }
    if (version >= 3 && ! in.isExhausted())
    {
        auto readF = [&in] { const float f = in.readFloatBigEndian(); return std::isfinite (f) ? f : 0.0f; };
        for (int t = 0; t < kMaxTracks; ++t) s.trackPan[t] = juce::jlimit (-1, 255, (int) in.readShortBigEndian());
        auto readEnv = [&in, &readF] (Envelope& e)
        {
            e.on = in.readByte() != 0;
            e.attack  = juce::jlimit (0.0f, 30.0f, readF());
            e.decay   = juce::jlimit (0.0f, 30.0f, readF());
            e.sustain = juce::jlimit (0.0f, 1.0f,  readF());
            e.release = juce::jlimit (0.0f, 30.0f, readF());
        };
        for (auto& ins : s.instruments)
        {
            if (in.isExhausted()) break;
            readEnv (ins.volEnv);
            ins.filter = (FilterType) juce::jlimit (0, 3, (int) (uint8_t) in.readByte());
            ins.cutoff = juce::jlimit (20.0f, 20000.0f, readF());
            ins.resonance = juce::jlimit (0.0f, 1.0f, readF());
            readEnv (ins.filterEnv);
            ins.filterEnvAmount = juce::jlimit (-8.0f, 8.0f, readF());
            ins.keyTrack = juce::jlimit (0.0f, 1.0f, readF());
            ins.amigaPan = in.readByte() != 0;
            ins.pan = juce::jlimit (0, 255, (int) in.readShortBigEndian());
        }
    }
    if (version >= 4 && ! in.isExhausted())
        for (auto& ins : s.instruments)
        {
            const double r = in.readDoubleBigEndian();
            ins.rate = std::isfinite (r) ? juce::jlimit (500.0, 192000.0, r) : kAmigaC3Rate;
        }
    out = std::move (s);
    return true;
}

bool loadSongFile (const juce::File& file, Song& out, juce::String& error, juce::StringArray& notes)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() == 0) { error = "Couldn't read " + file.getFileName(); return false; }

    // native format is gzip (starts 1F 8B)
    const auto* b = static_cast<const uint8_t*> (mb.getData());
    if (mb.getSize() > 2 && b[0] == 0x1F && b[1] == 0x8B)
        return readSong (mb.getData(), mb.getSize(), out, error);

    std::vector<uint8_t> bytes (b, b + mb.getSize());
    auto r = loadModule (bytes, out);
    for (auto& w : r.warnings) notes.add (w);
    if (! r.ok) { error = r.error; return false; }
    if (out.name.empty() || out.name == "UNTITLED") out.name = file.getFileNameWithoutExtension().toStdString();
    return true;
}

bool decodeAudioFile (const juce::File& file, std::vector<float>& left, std::vector<float>& right,
                      double& rate, Instrument& meta, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) { error = "Couldn't read " + file.getFileName(); return false; }
    left.clear(); right.clear();
    meta.clear();
    std::vector<uint8_t> bytes ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());

    Instrument svx;
    if (load8SVX (bytes, svx, rate))
    {
        left.resize (svx.data.size());
        for (size_t i = 0; i < left.size(); ++i) left[i] = svx.data[i] / 32768.0f;
        meta = svx; meta.data.clear();
        return ! left.empty();
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { error = "Unsupported audio file: " + file.getFileName(); return false; }
    rate = reader->sampleRate;
    const auto maxLen = (juce::int64) (rate * 30.0); // 30 seconds is plenty for a tracker sample
    const int len = (int) std::min (reader->lengthInSamples, maxLen);
    if (len <= 0) { error = "The sample is empty."; return false; }
    const int chans = (int) std::max (1u, reader->numChannels);
    juce::AudioBuffer<float> buf (chans, len);
    reader->read (&buf, 0, len, 0, true, true);
    left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
    if (chans >= 2)
    {
        right.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len);
        // a "stereo" file with identical channels is really mono
        bool same = true;
        for (int i = 0; i < len && same; ++i) same = std::abs (left[(size_t) i] - right[(size_t) i]) < 1.0e-5f;
        if (same) right.clear();
    }
    meta.name = file.getFileNameWithoutExtension().toStdString();
    meta.volume = 64;
    return true;
}

namespace
{
    std::vector<float> resampleTo (std::vector<float> in, double ratio)
    {
        if (std::abs (ratio - 1.0) <= 1e-4) return in;
        if (ratio > 1.0)
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                juce::IIRFilter f;
                f.setCoefficients (juce::IIRCoefficients::makeLowPass (ratio * kSampleRateAtC3, kSampleRateAtC3 * 0.45, 0.7071));
                f.processSamples (in.data(), (int) in.size());
            }
        }
        const int outLen = std::max (1, (int) std::floor ((double) in.size() / ratio));
        std::vector<float> res ((size_t) outLen);
        in.resize (in.size() + 8, 0.0f);
        juce::LagrangeInterpolator interp;
        interp.process (ratio, in.data(), res.data(), outLen);
        return res;
    }

    std::vector<int16_t> toInt16 (const std::vector<float>& v)
    {
        std::vector<int16_t> o (v.size());
        for (size_t i = 0; i < v.size(); ++i)
            o[i] = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (v[i] * 32767.0f));
        return o;
    }
}

bool importSample (const juce::File& file, Instrument& out, juce::String& error, bool keepRate)
{
    std::vector<float> L, R;
    double rate = 0;
    Instrument meta;
    if (! decodeAudioFile (file, L, R, rate, meta, error)) return false;
    if (L.empty() || rate <= 0) { error = "The sample is empty."; return false; }

    // resample so C-3 plays the sample at its original pitch (or keep the data as it is)
    const double ratio = keepRate ? 1.0 : rate / kSampleRateAtC3;
    L = resampleTo (std::move (L), ratio);
    if (! R.empty()) { R = resampleTo (std::move (R), ratio); R.resize (L.size(), 0.0f); }

    out.clear();
    out.name = juce::String (meta.name).toUpperCase().substring (0, 22).toStdString();
    out.volume = meta.volume;
    out.data = toInt16 (L);
    if (! R.empty()) out.dataR = toInt16 (R);
    out.rate = keepRate ? juce::jlimit (500.0, 192000.0, rate) : kAmigaC3Rate;
    out.loopStart = (int) std::lround (meta.loopStart / ratio);
    out.loopLength = (int) std::lround (meta.loopLength / ratio);
    if (! out.isLooped()) { out.loopStart = 0; out.loopLength = 0; }
    return true;
}

bool renderSongToWav (const Song& song, const PaulaOptions& options, double sampleRate,
                      const juce::File& file, juce::String& error)
{
    Player p;
    p.options = options;
    p.setSampleRate (sampleRate);
    p.setSong (&song);
    p.playSong();

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
    if (os == nullptr) { error = "Couldn't write " + file.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (os.get(), sampleRate, 2, 24, {}, 0));
    if (writer == nullptr) { error = "Couldn't create the WAV writer."; return false; }
    os.release();

    const int block = 4096;
    juce::AudioBuffer<float> buf (2, block);
    const auto maxSamples = (juce::int64) (sampleRate * 60.0 * 20.0); // stop after 20 minutes
    juce::int64 done = 0;
    while (p.songWraps.load() == 0 && p.isPlaying() && done < maxSamples)
    {
        p.render (buf.getWritePointer (0), buf.getWritePointer (1), block);
        writer->writeFromAudioSampleBuffer (buf, 0, block);
        done += block;
    }
    // one second tail for releases and filter ring-out
    p.stop();
    for (int i = 0; i < (int) sampleRate / block + 1; ++i)
    {
        p.render (buf.getWritePointer (0), buf.getWritePointer (1), block);
        writer->writeFromAudioSampleBuffer (buf, 0, block);
    }
    return true;
}
} // namespace prv
