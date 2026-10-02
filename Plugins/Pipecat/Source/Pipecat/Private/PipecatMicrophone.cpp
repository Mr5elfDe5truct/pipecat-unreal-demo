//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatMicrophone.h"

#if !PLATFORM_WINDOWS
THIRD_PARTY_INCLUDES_START
#include <SDL3/SDL.h>
THIRD_PARTY_INCLUDES_END
#endif

#include <cmath>

DEFINE_LOG_CATEGORY_STATIC(LogPipecatMicrophone, Log, All);

FPipecatMicrophone::~FPipecatMicrophone()
{
	Stop();
}

#if PLATFORM_WINDOWS

bool FPipecatMicrophone::Start(int32 InSampleRate, FOnAudio InOnAudio)
{
	if (Capture.IsStreamOpen())
	{
		return true;
	}

	OnAudio = MoveTemp(InOnAudio);
	SampleRate = InSampleRate;
	FramesPerChunk = SampleRate / 100;
	ResampledSampleRate = 0;
	Pending.Reset();

	// The default microphone, as it captures: 32-bit float, with its own
	// channels and sample rate.
	Audio::FAudioCaptureDeviceParams Params;
	Audio::FOnAudioCaptureFunction OnCaptured = [this](
		const void* Samples, int32 NumFrames, int32 NumChannels, int32 CaptureSampleRate, double StreamTime, bool bOverflow) {
		OnCapture(static_cast<const float*>(Samples), NumFrames, NumChannels, CaptureSampleRate);
	};
	if (!Capture.OpenAudioCaptureStream(Params, MoveTemp(OnCaptured), 1024) || !Capture.StartStream())
	{
		UE_LOG(LogPipecatMicrophone, Error, TEXT("Unable to open the microphone"));
		Capture.AbortStream();
		return false;
	}

	UE_LOG(LogPipecatMicrophone, Log, TEXT("Microphone started at %d Hz"), SampleRate);
	return true;
}

void FPipecatMicrophone::Stop()
{
	if (!Capture.IsStreamOpen())
	{
		return;
	}
	// Waits for the audio thread, so OnCapture isn't called after this.
	Capture.AbortStream();
	Level = 0.0f;
}

void FPipecatMicrophone::OnCapture(const float* Samples, int32 NumFrames, int32 NumChannels, int32 CaptureSampleRate)
{
	if (NumFrames <= 0 || NumChannels <= 0 || CaptureSampleRate <= 0)
	{
		return;
	}

	// To mono.
	Mono.SetNumUninitialized(NumFrames, EAllowShrinking::No);
	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		float Sum = 0.0f;
		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			Sum += Samples[Frame * NumChannels + Channel];
		}
		Mono[Frame] = Sum / NumChannels;
	}

	// To the sample rate asked for.
	const float* Output = Mono.GetData();
	int32 NumOutput = NumFrames;
	if (CaptureSampleRate != SampleRate)
	{
		const float Ratio = static_cast<float>(SampleRate) / CaptureSampleRate;
		if (ResampledSampleRate != CaptureSampleRate)
		{
			Resampler.Init(Audio::EResamplingMethod::FastSinc, Ratio, 1);
			ResampledSampleRate = CaptureSampleRate;
		}
		Resampled.SetNumUninitialized(FMath::CeilToInt(NumFrames * Ratio) + 16, EAllowShrinking::No);
		NumOutput = 0;
		Resampler.ProcessAudio(Mono.GetData(), NumFrames, false, Resampled.GetData(), Resampled.Num(), NumOutput);
		Output = Resampled.GetData();
	}

	// To 16-bit.
	Converted.SetNumUninitialized(NumOutput, EAllowShrinking::No);
	for (int32 i = 0; i < NumOutput; ++i)
	{
		Converted[i] = static_cast<int16>(FMath::Clamp(Output[i], -1.0f, 1.0f) * 32767.0f);
	}
	HandleAudio(Converted.GetData(), NumOutput);
}

#else

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

#endif

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
