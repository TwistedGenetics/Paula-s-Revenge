// Paula's Revenge - STEMS: a sample split into drums, bass, other and vocals by Demucs v4 (Meta AI
// Research's hybrid transformer, MIT licence). The network itself runs in ONNX Runtime (DemucsModel
// below); everything around it is here and has no other dependency: the spectrogram it reads
// and writes (torch.stft / istft exactly: 4096 points, hop 1024, periodic Hann, normalised),
// the 7.8 s pieces with 25% overlap and their cross-fades, the level normalisation, and a
// windowed-sinc rate change to and from the model's 44.1 kHz.
#pragma once

#include <atomic>
#include <complex>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace prv::stems
{
constexpr int kRate = 44100;
constexpr int kFft = 4096, kHop = 1024, kBins = kFft / 2;        // (the model takes 2048 of the 2049 bins)
constexpr int kSegment = 343980;                                  // int (7.8 s * 44100)
constexpr int kFrames = 336;                                      // ceil (kSegment / kHop)
constexpr int kPad = kHop / 2 * 3;                                // 1536: demucs' reflect padding before the STFT
constexpr int kPadded = kFrames * kHop + 2 * kPad;                // 347136
constexpr int kSources = 4;                                       // drums, bass, other, vocals (Demucs' order)
const char* sourceName (int s);                                   // "DRUMS" ...

// ---- a radix-2 FFT of kFft points
class Fft
{
public:
    Fft();
    void forward (std::complex<float>* data) const;   // in place
    void inverse (std::complex<float>* data) const;   // in place, scaled by 1 / n
private:
    std::vector<std::complex<float>> tw;
    std::vector<int> rev;
    void run (std::complex<float>* d, bool inv) const;
};

// torch.stft (n_fft 4096, hop 1024, periodic Hann, center = reflect, normalized = True) of one
// channel: frames x 2049 bins; and torch.istft back (with `length`)
void stft (const Fft&, const float* x, int n, std::vector<std::complex<float>>& out, int& frames);
void istft (const Fft&, const std::complex<float>* spec, int frames, int length, float* out);

// reflect padding as torch's F.pad (mode "reflect": the edge sample isn't repeated)
std::vector<float> reflectPad (const float* x, int n, int left, int right);

// windowed-sinc rate change (for the model's 44.1 kHz, and back)
std::vector<float> resample (const std::vector<float>& in, double from, double to);

// ---- the network: one 7.8 s stereo piece in (time, and the spectrogram as complex-as-channels),
// the sources out (their spectrograms and their time-domain parts)
struct DemucsModel
{
    virtual ~DemucsModel() = default;
    virtual int sources() const = 0;
    // xt: 2 x kSegment; x: 4 x kBins x kFrames (L real, L imag, R real, R imag)
    // xOut: sources x 4 x kBins x kFrames; xtOut: sources x 2 x kSegment
    virtual bool run (const float* xt, const float* x, float* xOut, float* xtOut, std::string& error) = 0;
};

// the ONNX Runtime one (models/htdemucs.onnx etc.); nullptr + error where ONNX Runtime isn't built in
std::unique_ptr<DemucsModel> loadOnnxModel (const std::string& path, int threads, std::string& error);
bool onnxAvailable();

// ---- one model over a whole stereo sample (at 44.1 kHz): its sources, each stereo
struct Progress
{
    std::function<void (double)> report;     // 0..1
    std::atomic<bool>* cancel = nullptr;     // set: stops soon (returns false)
};
// one piece through the model: the time-domain sources (sources x 2 x kSegment)
bool separatePiece (DemucsModel&, const float* left, const float* right, std::vector<float>& out, std::string& error);
// the whole thing, as Demucs' apply_model (split, 25% overlap, linear cross-fades, no random shift)
bool separate (DemucsModel&, const std::vector<float>& left, const std::vector<float>& right,
               std::vector<std::vector<float>>& outL, std::vector<std::vector<float>>& outR,
               Progress&, std::string& error);

// ---- STEMS for a sample: FAST (htdemucs) or BEST (the four fine-tuned htdemucs_ft models, each
// for its own source); any rate in, four stereo stems at 44.1 kHz out
enum class Quality { Fast, Best };
struct StemResult
{
    std::vector<float> left[kSources], right[kSources];
    double rate = kRate;
};
// modelFile (name) -> its path on this computer ("" if it isn't there)
using ModelPath = std::function<std::string (const std::string& name)>;
std::vector<std::string> modelsFor (Quality);   // the files it needs: "htdemucs.onnx" / "htdemucs_ft_drums.onnx" ...
bool separateSample (Quality, const ModelPath&, const std::vector<float>& left, const std::vector<float>& right, double rate,
                     StemResult&, Progress&, std::string& error, int threads = 0);
} // namespace prv::stems
