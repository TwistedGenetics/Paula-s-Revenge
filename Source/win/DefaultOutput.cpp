// Paula's Revenge - Windows' default sound output's name (the same "friendly name" JUCE's
// Windows Audio (WASAPI) devices are listed by). No JUCE in this file: windows.h stays apart.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <string>

#pragma comment (lib, "ole32.lib")

std::string prvWinDefaultOutputName()
{
    // (COM may already be set up on this thread, in either mode: that's fine)
    const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    std::string result;
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en)) && en != nullptr)
    {
        IMMDevice* dev = nullptr;
        if (SUCCEEDED (en->GetDefaultAudioEndpoint (eRender, eConsole, &dev)) && dev != nullptr)
        {
            IPropertyStore* props = nullptr;
            if (SUCCEEDED (dev->OpenPropertyStore (STGM_READ, &props)) && props != nullptr)
            {
                PROPVARIANT v;
                PropVariantInit (&v);
                if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal != nullptr)
                {
                    const int len = WideCharToMultiByte (CP_UTF8, 0, v.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 1)
                    {
                        result.resize ((size_t) len - 1);
                        WideCharToMultiByte (CP_UTF8, 0, v.pwszVal, -1, result.data(), len, nullptr, nullptr);
                    }
                }
                PropVariantClear (&v);
                props->Release();
            }
            dev->Release();
        }
        en->Release();
    }
    if (init == S_OK || init == S_FALSE) CoUninitialize();
    return result;
}
