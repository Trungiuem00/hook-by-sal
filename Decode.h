#pragma once

#include <memory>
#include <deque>
#include <mutex>
#include <windows.h>

#include "opus.h"
#include "opus_types.h"

#include "arch.h"

#include "Interface/Overlay.h"

namespace Hooks {
	using OpusDecodeNativePtr = __int64(__fastcall*)(
		OpusDecoder*, const unsigned char*, opus_int32, 
		opus_val16*, int, int, int, opus_int32*, int);
	extern OpusDecodeNativePtr OpusDecodeNativeOrig;

	class Decoder {
	public: 
		static __int64 OpusDecodeHook(OpusDecoder* st, const unsigned char* data, opus_int32 len,
			opus_val16* pcm, int frame_size, int decode_fec, int self_delimited, opus_int32* packet_offset,
			int soft_clip);
	};
}