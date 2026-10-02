//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

#if PLATFORM_WINDOWS
#include "AudioCaptureCore.h"
#include "AudioResampler.h"
#endif

#include <atomic>

struct SDL_AudioStream;

// Captures the default microphone as 16-bit mono PCM, and passes it on in
// 10 ms chunks. It uses Unreal's audio capture on Windows, and SDL on Linux,
// where Unreal's audio capture has no backend.
class FPipecatMicrophone
{
public:
	// Called on the audio thread.
	using FOnAudio = TFunction<void(const int16* Frames, int32 NumFrames)>;

	~FPipecatMicrophone();

	bool Start(int32 SampleRate, FOnAudio InOnAudio);
	void Stop();

	// How loud the microphone is, from 0 to 1.
	float GetLevel() const { return Level.load(); }

private:
	void HandleAudio(const int16* Frames, int32 NumFrames);

#if PLATFORM_WINDOWS
	void OnCapture(const float* Samples, int32 NumFrames, int32 NumChannels, int32 CaptureSampleRate);

	Audio::FAudioCapture Capture;
	Audio::FResampler Resampler;
	int32 SampleRate = 0;
	// The microphone's sample rate the resampler converts from.
	int32 ResampledSampleRate = 0;
	// Only used on the audio thread.
	TArray<float> Mono;
	TArray<float> Resampled;
	TArray<int16> Converted;
#else
	static void OnStreamData(void* UserData, SDL_AudioStream* Stream, int AdditionalAmount, int TotalAmount);

	SDL_AudioStream* Stream = nullptr;
#endif

	FOnAudio OnAudio;
	int32 FramesPerChunk = 0;
	TArray<int16> Pending;
	std::atomic<float> Level {0.0f};
};
