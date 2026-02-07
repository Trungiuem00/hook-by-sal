#include "Interface/Overlay.h"
#include "Detours/Decode.h"

#include "Include/Minhook.h"

int HooksSucceeded = 0;

HMODULE(WINAPI* _LoadLibraryExWAuto)(LPCWSTR, HANDLE, DWORD) = nullptr;

int __cdecl Remove() {
    return 0;
}

extern "C" HMODULE WINAPI _LoadLibraryExWHook(LPCWSTR Module, HANDLE File, DWORD Flags) {
    if (!wcsstr(Module, L"discord_voice")) {
        return _LoadLibraryExWAuto(Module, File, Flags);
    }

    HMODULE VoiceEngine = _LoadLibraryExWAuto(Module, File, Flags);
    if (!VoiceEngine) {
        return nullptr;
    }

    if (MH_CreateHook(reinterpret_cast<void*>((uintptr_t)VoiceEngine + 0x863E90), opus_encode, nullptr) == MH_OK) {
        HooksSucceeded += 1;
    }

    if (MH_CreateHook(reinterpret_cast<void*>((uintptr_t)VoiceEngine + 0x867BA0),
        Hooks::Decoder::OpusDecodeHook,
        reinterpret_cast<void**>(&Hooks::OpusDecodeNativeOrig)) == MH_OK) {
        HooksSucceeded += 2;
    }

    MH_CreateHook(reinterpret_cast<void*>((uintptr_t)VoiceEngine + 0x46869C), Remove, nullptr); // High pass filter
    MH_CreateHook(reinterpret_cast<void*>((uintptr_t)VoiceEngine + 0x2EF820), Remove, nullptr); // ProcessStream_AudioFrame
    MH_CreateHook(reinterpret_cast<void*>((uintptr_t)VoiceEngine + 0x2EDFC0), Remove, nullptr); // ProcessStream_StreamConfig
    MH_CreateHook(reinterpret_cast<void*>((uintptr_t)VoiceEngine + 0x2F2648), Remove, nullptr); // SendProcessedData
    MH_CreateHook(reinterpret_cast<void*>((uintptr_t)VoiceEngine + 0x5D8750), Remove, nullptr); // Clipping Predictor
    MH_CreateHook(reinterpret_cast<void*>((uintptr_t)VoiceEngine + 0x4F348C), Remove, nullptr); // Limiter
    MH_CreateHook(reinterpret_cast<void*>((uintptr_t)VoiceEngine + 0x8A12E0), Remove, nullptr); // VAD

    if (HooksSucceeded > 1) {
        std::thread(utilities::ui::start).detach();
    }

    MH_EnableHook(MH_ALL_HOOKS);
    return VoiceEngine;
}

extern "C" void MainThread(HMODULE Module) {
    HMODULE Kernel32 = GetModuleHandleA("kernel32.dll");
    FARPROC LoadLibraryExW = GetProcAddress(Kernel32, "LoadLibraryExW");

    MH_Initialize();

    MH_CreateHook(
        LoadLibraryExW,
        _LoadLibraryExWHook,
        reinterpret_cast<LPVOID*>(&_LoadLibraryExWAuto)
    );

    MH_EnableHook(MH_ALL_HOOKS);
}

BOOL WINAPI DllMain(HMODULE Module, std::uintptr_t Reason, LPVOID) {
    if (Reason == DLL_PROCESS_ATTACH) {
        std::thread(MainThread, Module).detach();
    }

    return TRUE;
}