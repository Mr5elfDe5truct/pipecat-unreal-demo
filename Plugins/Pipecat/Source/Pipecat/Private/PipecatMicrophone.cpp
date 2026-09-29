//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatMicrophone.h"

THIRD_PARTY_INCLUDES_START
#include <SDL3/SDL.h>
THIRD_PARTY_INCLUDES_END

#include <cmath>

DEFINE_LOG_CATEGORY_STATIC(LogPipecatMicrophone, Log, All);

FPipecatMicrophone::~FPipecatMicrophone()
{
	Stop();
}

bool FPipecatMicrophone::Start(int32 SampleRate, FOnAudio InOnAudio)
{
	if (Stream)
	{
		return true;
	}

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		UE_LOG(LogPipecatMicrophone, Error, TEXT("Unable to start SDL audio: %hs"), SDL_GetError());
		return false;
	}

	OnAudio = MoveTemp(InOnAudio);
	FramesPerChunk = SampleRate / 100;
	Pending.Reset();

	SDL_AudioSpec Spec;
	Spec.format = SDL_AUDIO_S16;
	Spec.channels = 1;
	Spec.freq = SampleRate;
	Stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &Spec, &FPipecatMicrophone::OnStreamData, this);
	if (!Stream)
	{
		UE_LOG(LogPipecatMicrophone, Error, TEXT("Unable to open the microphone: %hs"), SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	SDL_ResumeAudioStreamDevice(Stream);
	UE_LOG(LogPipecatMicrophone, Log, TEXT("Microphone started at %d Hz"), SampleRate);
	return true;
}

void FPipecatMicrophone::Stop()
{
	if (!Stream)
	{
		return;
	}
	// Waits for the audio thread, so OnStreamData isn't called after this.
	SDL_DestroyAudioStream(Stream);
	Stream = nullptr;
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	Level = 0.0f;
}

void FPipecatMicrophone::OnStreamData(void* UserData, SDL_AudioStream* Stream, int AdditionalAmount, int TotalAmount)
{
	FPipecatMicrophone* Self = static_cast<FPipecatMicrophone*>(UserData);

	int16 Buffer[4096];
	int Available = SDL_GetAudioStreamAvailable(Stream);
	while (Available > 0)
	{
		int Read = SDL_GetAudioStreamData(Stream, Buffer, FMath::Min<int>(Available, sizeof(Buffer)));
		if (Read <= 0)
		{
			break;
		}
		Self->HandleAudio(Buffer, Read / static_cast<int>(sizeof(int16)));
		Available -= Read;
	}
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
