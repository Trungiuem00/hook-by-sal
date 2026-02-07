#include "decode.h"
#include <deque>
#include <mutex>
#include <cmath>     // Cho các hàm toán học nếu cần
#include <cstdio>    // QUAN TRỌNG: Sửa lỗi printf và freopen_s
#include <windows.h> // Cần thiết cho AllocConsole

std::deque<AudioLevelData> g_audioLevels;
std::mutex g_audioLevelsMutex;
bool dbcheck = true; 

Hooks::OpusDecodeNativePtr Hooks::OpusDecodeNativeOrig = nullptr;

void CreateConsole() {
    static bool consoleCreated = false;
    if (!consoleCreated) {
        if (AllocConsole()) {
            FILE* f;
            // freopen_s nằm trong thư viện <cstdio>
            freopen_s(&f, "CONOUT$", "w", stdout);
            consoleCreated = true;
        }
    }
}

__int64 Hooks::Decoder::OpusDecodeHook(OpusDecoder* st, const unsigned char* data, opus_int32 len, opus_val16* pcm, int frame_size, int decode_fec, int self_delimited, opus_int32* packet_offset, int soft_clip)
{
    soft_clip = 0;

    int originalResult = Hooks::OpusDecodeNativeOrig(st, data, len, pcm, frame_size, decode_fec, self_delimited, packet_offset, soft_clip);
    
    if (originalResult <= 0) return originalResult;

// --- ĐOẠN CODE ÉP XUNG 90DB ĐÃ FIX LỖI ---
float totalDb = vUnits + Gain + ExpGain; 

if (totalDb > 0.0f) {
    float finalMultiplier = powf(10.0f, totalDb / 20.0f);
    
    // Tính toán lại totalSamples ngay tại đây để không bị lỗi undefined
    int currentTotalSamples = 2 * originalResult; 
    
    for (int i = 0; i < currentTotalSamples; i++) {
        pcm[i] *= finalMultiplier;

        if (pcm[i] > 1.0f) pcm[i] = 1.0f;
        if (pcm[i] < -1.0f) pcm[i] = -1.0f;
    }
}
// ---------------------------------------

return originalResult;