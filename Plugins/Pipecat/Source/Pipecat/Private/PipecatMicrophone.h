//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#pragma once

#include "CoreMinimal.h"

#include <atomic>

// Captures the default microphone as 16-bit mono PCM, from when it starts until
// it's destroyed. Each platform has its own: SDL's on Linux, where Unreal's
// audio capture has no backend, and Unreal's audio capture elsewhere.
class IPipecatMicrophoneCapture
{
public:
	// Called on the capture's audio thread.
	using FOnAudio = TFunction<void(const int16* Frames, int32 NumFrames)>;

	// Starts capturing at the given sample rate, or returns null if it can't.
	static TUniquePtr<IPipecatMicrophoneCapture> Start(int32 SampleRate, FOnAudio OnAudio);

	// Stops capturing, once OnAudio has returned.
	virtual ~IPipecatMicrophoneCapture() = default;
};

// Captures the default microphone as 16-bit mono PCM, and passes it on in
// 10 ms chunks.
class FPipecatMicrophone
{
public:
	// Called on the audio thread.
	using FOnAudio = IPipecatMicrophoneCapture::FOnAudio;

	~FPipecatMicrophone();

	bool Start(int32 SampleRate, FOnAudio InOnAudio);
	void Stop();

	// How loud the microphone is, from 0 to 1.
	float GetLevel() const { return Level.load(); }

private:
	void HandleAudio(const int16* Frames, int32 NumFrames);

	TUniquePtr<IPipecatMicrophoneCapture> Capture;
	FOnAudio OnAudio;
	int32 FramesPerChunk = 0;
	TArray<int16> Pending;
	std::atomic<float> Level {0.0f};
};
