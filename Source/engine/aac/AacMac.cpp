// Paula's Revenge - AAC / AAC+ on a Mac: AudioToolbox's own decoder. An AudioFileStream reads the
// ADTS frames (and finds HE-AAC's SBR / PS in them); an AudioConverter turns them into stereo floats.
#include "../RadioFormats.h"
#include <AudioToolbox/AudioToolbox.h>

namespace prv::radio
{
namespace
{
    constexpr OSStatus kNoMoreInput = 'pRvN';

    struct MacAac final : AacBackend
    {
        AudioFileStreamID stream = nullptr;
        AudioConverterRef conv = nullptr;
        AudioStreamBasicDescription in {}, out {};
        int rate = 0;
        bool broken = false;
        std::vector<float>* L = nullptr;
        std::vector<float>* R = nullptr;
        std::vector<float> tmp;

        MacAac()
        {
            if (AudioFileStreamOpen (this, &MacAac::property, &MacAac::packets, kAudioFileAAC_ADTSType, &stream) != noErr) stream = nullptr;
        }
        ~MacAac() override
        {
            if (conv != nullptr) AudioConverterDispose (conv);
            if (stream != nullptr) AudioFileStreamClose (stream);
        }

        static void property (void* self, AudioFileStreamID, AudioFileStreamPropertyID id, AudioFileStreamPropertyFlags*)
        {
            if (id == kAudioFileStreamProperty_ReadyToProducePackets) static_cast<MacAac*> (self)->setup();
        }

        void setup()
        {
            UInt32 size = sizeof (in);
            if (AudioFileStreamGetProperty (stream, kAudioFileStreamProperty_DataFormat, &size, &in) != noErr) { broken = true; return; }
            // HE-AAC (v2): the best format in the list the stream gives that this Mac can play
            UInt32 listSize = 0;
            Boolean writable = false;
            if (AudioFileStreamGetPropertyInfo (stream, kAudioFileStreamProperty_FormatList, &listSize, &writable) == noErr && listSize >= sizeof (AudioFormatListItem))
            {
                std::vector<AudioFormatListItem> items (listSize / sizeof (AudioFormatListItem));
                if (AudioFileStreamGetProperty (stream, kAudioFileStreamProperty_FormatList, &listSize, items.data()) == noErr)
                {
                    UInt32 index = 0, isz = sizeof (index);
                    if (AudioFormatGetProperty (kAudioFormatProperty_FirstPlayableFormatFromList, listSize, items.data(), &isz, &index) == noErr && index < items.size())
                        in = items[index].mASBD;
                }
            }
            out = {};
            out.mSampleRate = in.mSampleRate;
            out.mFormatID = kAudioFormatLinearPCM;
            out.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
            out.mChannelsPerFrame = 2;
            out.mBitsPerChannel = 32;
            out.mBytesPerFrame = 8;
            out.mFramesPerPacket = 1;
            out.mBytesPerPacket = 8;
            if (conv != nullptr) { AudioConverterDispose (conv); conv = nullptr; }
            if (AudioConverterNew (&in, &out, &conv) != noErr) { conv = nullptr; broken = true; return; }
            UInt32 cookieSize = 0;
            if (AudioFileStreamGetPropertyInfo (stream, kAudioFileStreamProperty_MagicCookieData, &cookieSize, &writable) == noErr && cookieSize > 0)
            {
                std::vector<char> cookie (cookieSize);
                if (AudioFileStreamGetProperty (stream, kAudioFileStreamProperty_MagicCookieData, &cookieSize, cookie.data()) == noErr)
                    AudioConverterSetProperty (conv, kAudioConverterDecompressionMagicCookie, cookieSize, cookie.data());
            }
            rate = (int) out.mSampleRate;
        }

        struct Feed { const void* data; UInt32 bytes; AudioStreamPacketDescription desc; bool used; };

        static OSStatus input (AudioConverterRef, UInt32* ioPackets, AudioBufferList* io, AudioStreamPacketDescription** descs, void* user)
        {
            auto* f = static_cast<Feed*> (user);
            if (f->used) { *ioPackets = 0; return kNoMoreInput; }
            f->used = true;
            *ioPackets = 1;
            io->mNumberBuffers = 1;
            io->mBuffers[0].mData = const_cast<void*> (f->data);
            io->mBuffers[0].mDataByteSize = f->bytes;
            io->mBuffers[0].mNumberChannels = 0;
            if (descs != nullptr) *descs = &f->desc;
            return noErr;
        }

        static void packets (void* self, UInt32, UInt32 count, const void* data, AudioStreamPacketDescription* descs)
        {
            auto* s = static_cast<MacAac*> (self);
            if (s->conv == nullptr || descs == nullptr) return;
            for (UInt32 i = 0; i < count; ++i)
            {
                Feed f { static_cast<const uint8_t*> (data) + descs[i].mStartOffset, descs[i].mDataByteSize, descs[i], false };
                f.desc.mStartOffset = 0;
                s->convert (f);
            }
        }

        void convert (Feed& f)
        {
            const UInt32 maxFrames = 4096;
            tmp.resize (maxFrames * 2);
            for (int guard = 0; guard < 8; ++guard)
            {
                UInt32 frames = maxFrames;
                AudioBufferList abl;
                abl.mNumberBuffers = 1;
                abl.mBuffers[0].mNumberChannels = 2;
                abl.mBuffers[0].mDataByteSize = maxFrames * 8;
                abl.mBuffers[0].mData = tmp.data();
                const OSStatus e = AudioConverterFillComplexBuffer (conv, &MacAac::input, &f, &frames, &abl, nullptr);
                for (UInt32 k = 0; k < frames; ++k) { L->push_back (tmp[k * 2]); R->push_back (tmp[k * 2 + 1]); }
                if (e != noErr || frames < maxFrames) break;
            }
        }

        bool decode (const uint8_t* frame, size_t n, std::vector<float>& left, std::vector<float>& right) override
        {
            if (stream == nullptr || broken) return false;
            L = &left; R = &right;
            return AudioFileStreamParseBytes (stream, (UInt32) n, frame, 0) == noErr && ! broken;
        }
        int sampleRate() const override { return rate; }
    };
}

std::unique_ptr<AacBackend> makeAacBackend()
{
    auto b = std::make_unique<MacAac>();
    if (b->stream == nullptr) return nullptr;
    return b;
}
bool aacAvailable() { return true; }
} // namespace prv::radio
