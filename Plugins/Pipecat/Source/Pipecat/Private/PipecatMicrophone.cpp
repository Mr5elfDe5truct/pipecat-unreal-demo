//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatMicrophone.h"

#include <cmath>

FPipecatMicrophone::~FPipecatMicrophone()
{
	Stop();
}

bool FPipecatMicrophone::Start(int32 SampleRate, FOnAudio InOnAudio)
{
	if (Capture)
	{
		return true;
	}

	OnAudio = MoveTemp(InOnAudio);
	FramesPerChunk = SampleRate / 100;
	Pending.Reset();

	Capture = IPipecatMicrophoneCapture::Start(
		SampleRate, [this](const int16* Frames, int32 NumFrames) { HandleAudio(Frames, NumFrames); });
	return Capture.IsValid();
}

void FPipecatMicrophone::Stop()
{
	// Waits for the audio thread, so HandleAudio isn't called after this.
	Capture.Reset();
	Level = 0.0f;
}

void FPipecatMicrophone::HandleAudio(const int16* Frames, int32 NumFrames)
{
	double Sum = 0.0;
	for (int32 i = 0; i < NumFrames; ++i)
	{
		Sum += static_cast<double>(Frames[i]) * Frames[i];
	}
	float Rms = NumFrames > 0 ? static_cast<float>(std::sqrt(Sum / NumFrames) / 32768.0) : 0.0f;
	Level = FMath::Clamp(Rms * 4.0f, 0.0f, 1.0f);

	// The transport sends 10 ms at a time.
	Pending.Append(Frames, NumFrames);
	int32 Sent = 0;
	while (Pending.Num() - Sent >= FramesPerChunk)
	{
		OnAudio(Pending.GetData() + Sent, FramesPerChunk);
		Sent += FramesPerChunk;
	}
	if (Sent > 0)
	{
		Pending.RemoveAt(0, Sent, EAllowShrinking::No);
	}
}
