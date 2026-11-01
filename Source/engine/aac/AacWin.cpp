// Paula's Revenge - AAC / AAC+ on Windows: Media Foundation's own AAC decoder (part of Windows),
// fed the ADTS frames as they are; HE-AAC's SBR / PS make it change its output, which it says.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <mutex>
#include <algorithm>
#include <cstring>
#include "../RadioFormats.h"

namespace prv::radio
{
namespace
{
    template <typename T> struct Com
    {
        T* p = nullptr;
        ~Com() { if (p != nullptr) p->Release(); }
        T** put() { if (p != nullptr) { p->Release(); p = nullptr; } return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    bool startMf()
    {
        static std::once_flag once;
        static bool ok = false;
        std::call_once (once, [] { ok = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE)); });
        return ok;
    }

    struct WinAac final : AacBackend
    {
        Com<IMFTransform> mft;
        bool ready = false, isFloat = false, providesSamples = false;
        int rate = 0, channels = 2, bits = 16;
        DWORD outSize = 0;

        WinAac() { CoInitializeEx (nullptr, COINIT_MULTITHREADED); }   // (this thread: it may already be set up)

        bool open (const AdtsHeader& h)
        {
            if (! startMf()) return false;
            if (FAILED (CoCreateInstance (CLSID_CMSAACDecMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (mft.put())))) return false;
            Com<IMFMediaType> t;
            if (FAILED (MFCreateMediaType (t.put()))) return false;
            t->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            t->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_AAC);
            t->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) h.sampleRate());
            t->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, (UINT32) (h.channels > 0 ? h.channels : 2));
            t->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            t->SetUINT32 (MF_MT_AAC_PAYLOAD_TYPE, 1);   // ADTS
            t->SetUINT32 (MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0xFE);
            // (the part of HEAACWAVEINFO after the WAVEFORMATEX: payload type 1, profile unknown)
            BYTE user[12] = { 1, 0, 0xFE, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
            t->SetBlob (MF_MT_USER_DATA, user, sizeof (user));
            if (FAILED (mft->SetInputType (0, t.p, 0))) return false;
            if (! chooseOutput()) return false;
            mft->ProcessMessage (MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
            mft->ProcessMessage (MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
            ready = true;
            return true;
        }

        bool chooseOutput()
        {
            // floats if it offers them, else 16-bit
            int pick = -1;
            for (DWORD i = 0; ; ++i)
            {
                Com<IMFMediaType> o;
                if (FAILED (mft->GetOutputAvailableType (0, i, o.put()))) break;
                GUID sub {};
                o->GetGUID (MF_MT_SUBTYPE, &sub);
                if (sub == MFAudioFormat_Float) { pick = (int) i; break; }
                if (sub == MFAudioFormat_PCM && pick < 0) pick = (int) i;
            }
            if (pick < 0) return false;
            Com<IMFMediaType> o;
            if (FAILED (mft->GetOutputAvailableType (0, (DWORD) pick, o.put())) || FAILED (mft->SetOutputType (0, o.p, 0))) return false;
            return readOutput();
        }

        bool readOutput()
        {
            Com<IMFMediaType> o;
            if (FAILED (mft->GetOutputCurrentType (0, o.put()))) return false;
            GUID sub {};
            o->GetGUID (MF_MT_SUBTYPE, &sub);
            isFloat = sub == MFAudioFormat_Float;
            UINT32 v = 0;
            if (SUCCEEDED (o->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &v))) rate = (int) v;
            if (SUCCEEDED (o->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &v))) channels = std::max (1, (int) v);
            if (SUCCEEDED (o->GetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, &v))) bits = (int) v;
            MFT_OUTPUT_STREAM_INFO si {};
            if (SUCCEEDED (mft->GetOutputStreamInfo (0, &si)))
            {
                providesSamples = (si.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
                outSize = std::max<DWORD> (si.cbSize, 2048 * 8 * 8);
            }
            return rate > 0;
        }

        void drain (std::vector<float>& left, std::vector<float>& right)
        {
            for (int guard = 0; guard < 64; ++guard)
            {
                Com<IMFSample> s;
                Com<IMFMediaBuffer> b;
                MFT_OUTPUT_DATA_BUFFER db {};
                if (! providesSamples)
                {
                    if (FAILED (MFCreateSample (s.put())) || FAILED (MFCreateMemoryBuffer (outSize, b.put()))) return;
                    s->AddBuffer (b.p);
                    db.pSample = s.p;
                }
                DWORD status = 0;
                const HRESULT hr = mft->ProcessOutput (0, 1, &db, &status);
                if (db.pEvents != nullptr) db.pEvents->Release();
                if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) return;
                if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
                {
                    // (HE-AAC found: the output's rate / channels changed)
                    if (! chooseOutput()) return;
                    continue;
                }
                if (FAILED (hr) || db.pSample == nullptr) return;
                Com<IMFMediaBuffer> cb;
                if (SUCCEEDED (db.pSample->ConvertToContiguousBuffer (cb.put())))
                {
                    BYTE* data = nullptr;
                    DWORD len = 0;
                    if (SUCCEEDED (cb->Lock (&data, nullptr, &len)))
                    {
                        const int bytesPer = isFloat ? 4 : std::max (1, bits / 8);
                        const int frames = (int) (len / (DWORD) (bytesPer * channels));
                        for (int i = 0; i < frames; ++i)
                        {
                            auto at = [&] (int c) -> float
                            {
                                const BYTE* q = data + ((size_t) i * (size_t) channels + (size_t) c) * (size_t) bytesPer;
                                if (isFloat) { float f; std::memcpy (&f, q, 4); return f; }
                                int16_t v; std::memcpy (&v, q, 2); return v / 32768.0f;
                            };
                            left.push_back (at (0));
                            right.push_back (at (channels > 1 ? 1 : 0));
                        }
                        cb->Unlock();
                    }
                }
                if (providesSamples) db.pSample->Release();
            }
        }

        bool decode (const uint8_t* frame, size_t n, std::vector<float>& left, std::vector<float>& right) override
        {
            if (! ready)
            {
                AdtsHeader h;
                if (! parseAdts (frame, n, h) || ! open (h)) return false;
            }
            Com<IMFSample> s;
            Com<IMFMediaBuffer> b;
            if (FAILED (MFCreateSample (s.put())) || FAILED (MFCreateMemoryBuffer ((DWORD) n, b.put()))) return false;
            BYTE* data = nullptr;
            if (FAILED (b->Lock (&data, nullptr, nullptr))) return false;
            std::memcpy (data, frame, n);
            b->Unlock();
            b->SetCurrentLength ((DWORD) n);
            s->AddBuffer (b.p);
            HRESULT hr = mft->ProcessInput (0, s.p, 0);
            if (hr == MF_E_NOTACCEPTING) { drain (left, right); hr = mft->ProcessInput (0, s.p, 0); }
            drain (left, right);
            return SUCCEEDED (hr);
        }
        int sampleRate() const override { return rate; }
    };
}

std::unique_ptr<AacBackend> makeAacBackend() { return std::make_unique<WinAac>(); }
bool aacAvailable() { return true; }
} // namespace prv::radio
