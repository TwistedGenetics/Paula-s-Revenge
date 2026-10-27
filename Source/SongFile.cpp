// Paula's Revenge - native song format, sample import and WAV rendering
#include "SongFile.h"
#include "engine/ModuleIO.h"

namespace prv
{
namespace
{
    constexpr uint32_t kMagic = 0x50525631; // "PRV1"
    constexpr int kVersion = 1;

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
    }

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
                c.cmd = (uint8_t) in.readByte() & 15; c.param = (uint8_t) in.readByte();
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
        if (! ins.isLooped()) { ins.loopStart = 0; ins.loopLength = 0; }
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

bool importSample (const juce::File& file, Instrument& out, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) { error = "Couldn't read " + file.getFileName(); return false; }

    std::vector<float> mono;
    double rate = 0;
    Instrument tmp;
    std::vector<uint8_t> bytes ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());

    if (load8SVX (bytes, tmp, rate))
    {
        mono.resize (tmp.data.size());
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = tmp.data[i] / 32768.0f;
    }
    else
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr) { error = "Unsupported audio file: " + file.getFileName(); return false; }
        rate = reader->sampleRate;
        const auto maxLen = (juce::int64) (rate * 30.0); // 30 seconds is plenty for a tracker sample
        const int len = (int) std::min (reader->lengthInSamples, maxLen);
        juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
        reader->read (&buf, 0, len, 0, true, true);
        mono.assign ((size_t) len, 0.0f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (ch, i) / (float) buf.getNumChannels();
        tmp.name = file.getFileNameWithoutExtension().toStdString();
        tmp.volume = 64;
    }
    if (mono.empty() || rate <= 0) { error = "The sample is empty."; return false; }

    // resample to the C-3 rate (anti-alias first when going down)
    const double ratio = rate / kSampleRateAtC3;
    if (std::abs (ratio - 1.0) > 1e-4)
    {
        if (ratio > 1.0)
        {
            const double cutoff = kSampleRateAtC3 * 0.45;
            for (int pass = 0; pass < 2; ++pass)
            {
                juce::IIRFilter f;
                f.setCoefficients (juce::IIRCoefficients::makeLowPass (rate, cutoff, 0.7071));
                f.processSamples (mono.data(), (int) mono.size());
            }
        }
        const int outLen = (int) std::floor ((double) mono.size() / ratio);
        std::vector<float> res ((size_t) std::max (1, outLen));
        mono.resize (mono.size() + 8, 0.0f);
        juce::LagrangeInterpolator interp;
        interp.process (ratio, mono.data(), res.data(), (int) res.size());
        mono.swap (res);
        tmp.loopStart = (int) std::lround (tmp.loopStart / ratio);
        tmp.loopLength = (int) std::lround (tmp.loopLength / ratio);
    }

    out.clear();
    out.name = juce::String (tmp.name).toUpperCase().substring (0, 22).toStdString();
    out.volume = tmp.volume;
    out.data.resize (mono.size());
    for (size_t i = 0; i < mono.size(); ++i)
        out.data[i] = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (mono[i] * 32767.0f));
    out.loopStart = tmp.loopStart; out.loopLength = tmp.loopLength;
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
