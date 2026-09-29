//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

#include <atomic>

struct SDL_AudioStream;

// Captures the default microphone as 16-bit mono PCM, with SDL, and passes it
// on in 10 ms chunks. Unreal's audio capture has no Linux backend.
class FPipecatMicrophone
{
public:
	// Called on SDL's audio thread.
	using FOnAudio = TFunction<void(const int16* Frames, int32 NumFrames)>;

	~FPipecatMicrophone();

	bool Start(int32 SampleRate, FOnAudio InOnAudio);
	void Stop();

	// How loud the microphone is, from 0 to 1.
	float GetLevel() const { return Level.load(); }

private:
	static void OnStreamData(void* UserData, SDL_AudioStream* Stream, int AdditionalAmount, int TotalAmount);

	void HandleAudio(const int16* Frames, int32 NumFrames);

	SDL_AudioStream* Stream = nullptr;
	FOnAudio OnAudio;
	int32 FramesPerChunk = 0;
	TArray<int16> Pending;
	std::atomic<float> Level {0.0f};
};
