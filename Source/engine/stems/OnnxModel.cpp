// Paula's Revenge - STEMS: the Demucs network in ONNX Runtime (Microsoft, MIT licence), on the CPU
#include "Stems.h"
#include <onnxruntime_cxx_api.h>
#include <thread>
#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace prv::stems
{
namespace
{
    Ort::Env& env()
    {
        static Ort::Env e (ORT_LOGGING_LEVEL_ERROR, "PaulasRevenge");
        return e;
    }

    struct OnnxDemucs final : DemucsModel
    {
        Ort::Session session { nullptr };
        std::vector<std::string> inNames, outNames;
        int nsrc = 0;
        int timeIn = 0, specIn = 1, specOut = 0, timeOut = 1;

        int sources() const override { return nsrc; }

        bool run (const float* xt, const float* x, float* xOut, float* xtOut, std::string& error) override
        {
            try
            {
                auto mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
                const int64_t xtShape[3] = { 1, 2, kSegment };
                const int64_t xShape[4] = { 1, 4, kBins, kFrames };
                std::vector<Ort::Value> inputs;
                inputs.emplace_back (nullptr); inputs.emplace_back (nullptr);
                inputs[(size_t) timeIn] = Ort::Value::CreateTensor<float> (mem, const_cast<float*> (xt), (size_t) 2 * kSegment, xtShape, 3);
                inputs[(size_t) specIn] = Ort::Value::CreateTensor<float> (mem, const_cast<float*> (x), (size_t) 4 * kBins * kFrames, xShape, 4);
                const int64_t xoShape[5] = { 1, nsrc, 4, kBins, kFrames };
                const int64_t xtoShape[4] = { 1, nsrc, 2, kSegment };
                std::vector<Ort::Value> outputs;
                outputs.emplace_back (nullptr); outputs.emplace_back (nullptr);
                outputs[(size_t) specOut] = Ort::Value::CreateTensor<float> (mem, xOut, (size_t) nsrc * 4 * kBins * kFrames, xoShape, 5);
                outputs[(size_t) timeOut] = Ort::Value::CreateTensor<float> (mem, xtOut, (size_t) nsrc * 2 * kSegment, xtoShape, 4);
                const char* in[2] = { inNames[0].c_str(), inNames[1].c_str() };
                const char* out[2] = { outNames[0].c_str(), outNames[1].c_str() };
                session.Run (Ort::RunOptions { nullptr }, in, inputs.data(), 2, out, outputs.data(), 2);
                return true;
            }
            catch (const std::exception& e) { error = std::string ("THE MODEL FAILED: ") + e.what(); return false; }
        }
    };
}

bool onnxAvailable() { return true; }

std::unique_ptr<DemucsModel> loadOnnxModel (const std::string& path, int threads, std::string& error)
{
    try
    {
        Ort::SessionOptions so;
        const int hw = (int) std::max (1u, std::thread::hardware_concurrency());
        so.SetIntraOpNumThreads (threads > 0 ? threads : std::max (1, hw - 1));   // (one core left for the music and the window)
        so.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        auto m = std::make_unique<OnnxDemucs>();
       #ifdef _WIN32
        std::wstring wpath;
        const int len = MultiByteToWideChar (CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        wpath.resize ((size_t) std::max (0, len - 1));
        MultiByteToWideChar (CP_UTF8, 0, path.c_str(), -1, wpath.data(), len);
        m->session = Ort::Session (env(), wpath.c_str(), so);
       #else
        m->session = Ort::Session (env(), path.c_str(), so);
       #endif
        Ort::AllocatorWithDefaultOptions alloc;
        if (m->session.GetInputCount() != 2 || m->session.GetOutputCount() != 2) { error = "THE MODEL DOESN'T LOOK LIKE DEMUCS"; return nullptr; }
        for (size_t i = 0; i < 2; ++i) m->inNames.emplace_back (m->session.GetInputNameAllocated (i, alloc).get());
        for (size_t i = 0; i < 2; ++i) m->outNames.emplace_back (m->session.GetOutputNameAllocated (i, alloc).get());
        // which input / output is which: the time ones are 3-D / 4-D, the spectrograms 4-D / 5-D
        const auto in0 = m->session.GetInputTypeInfo (0).GetTensorTypeAndShapeInfo().GetShape();
        m->timeIn = in0.size() == 3 ? 0 : 1; m->specIn = 1 - m->timeIn;
        const auto out0 = m->session.GetOutputTypeInfo (0).GetTensorTypeAndShapeInfo().GetShape();
        m->specOut = out0.size() == 5 ? 0 : 1; m->timeOut = 1 - m->specOut;
        const auto so5 = m->session.GetOutputTypeInfo ((size_t) m->specOut).GetTensorTypeAndShapeInfo().GetShape();
        if (so5.size() != 5 || so5[2] != 4 || so5[3] != kBins || so5[4] != kFrames) { error = "THE MODEL'S SHAPE ISN'T DEMUCS V4'S"; return nullptr; }
        m->nsrc = (int) so5[1];
        return m;
    }
    catch (const std::exception& e) { error = std::string ("COULDN'T LOAD THE MODEL: ") + e.what(); return nullptr; }
}
} // namespace prv::stems
