//
// Copyright (c) 2026, Daily
//
// SPDX-License-Identifier: BSD-2-Clause
//

#include "PipecatVoiceComponent.h"

#include "PipecatMicrophone.h"

#include "Async/Async.h"
#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundWaveProcedural.h"

THIRD_PARTY_INCLUDES_START
#include <pipecat/daily/transport.h>
#include <pipecat/pipecat.h>
THIRD_PARTY_INCLUDES_END

#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <string>
#include <thread>
#include <vector>

DEFINE_LOG_CATEGORY_STATIC(LogPipecat, Log, All);

namespace
{
// The microphone is sent at 16 kHz, like most speech-to-text services use, and
// the bot's voice is played at 48 kHz.
const uint32 UserSampleRate = 16000;
const uint32 BotSampleRate = 48000;

// The most bot voice Unreal can have waiting to be played. Unreal doesn't play
// sounds while the game is paused, or in the background in the editor, and
// anything beyond this is dropped, so the voice is never further behind.
const int32 MaxQueuedBotVoiceBytes = BotSampleRate * 3 / 10 * sizeof(int16);

FString ToFString(const std::string& Text)
{
	return FString(UTF8_TO_TCHAR(Text.c_str()));
}

std::string ToStdString(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}

float Loudness(const int16* Frames, int32 NumFrames)
{
	double Sum = 0.0;
	for (int32 i = 0; i < NumFrames; ++i)
	{
		Sum += static_cast<double>(Frames[i]) * Frames[i];
	}
	float Rms = NumFrames > 0 ? static_cast<float>(std::sqrt(Sum / NumFrames) / 32768.0) : 0.0f;
	return FMath::Clamp(Rms * 4.0f, 0.0f, 1.0f);
}
} // namespace

// Everything that talks to Pipecat. Pipecat calls its callbacks on the client's
// own thread, and they're passed on to the component on the game thread.
class FPipecatSession : public pipecat::PipecatClientCallbacks
{
public:
	FPipecatSession(
		UPipecatVoiceComponent* InOwner,
		USoundWaveProcedural* InWave,
		const TMap<FString, FPipecatFunctionHandler>& Functions)
		: Owner(InOwner), Wave(InWave)
	{
		pipecat::DailyTransportOptions TransportOptions;
		TransportOptions.user_audio_sample_rate = UserSampleRate;
		TransportOptions.user_audio_channels = 1;
		TransportOptions.bot_audio_sample_rate = BotSampleRate;
		TransportOptions.bot_audio_channels = 1;

		pipecat::PipecatClientOptions Options;
		Options.transport = std::make_unique<pipecat::DailyTransport>(TransportOptions);
		Options.callbacks = this;
		Client = std::make_unique<pipecat::PipecatClient>(std::move(Options));

		for (const TPair<FString, FPipecatFunctionHandler>& Function : Functions)
		{
			RegisterFunction(Function.Key, Function.Value);
		}

		Running = true;
		Reader = std::thread([this] { PlayBotVoice(); });
	}

	virtual ~FPipecatSession() override
	{
		Stop();
	}

	void Start(const FString& StartUrl, const FString& ApiKey, bool bUseMicrophone)
	{
		if (ConnectThread.joinable())
		{
			return;
		}

		pipecat::APIRequest Request;
		Request.endpoint = ToStdString(StartUrl);
		Request.request_data = {{"createDailyRoom", true}};
		if (!ApiKey.IsEmpty())
		{
			Request.headers["Authorization"] = "Bearer " + ToStdString(ApiKey);
		}

		// Starting the bot and connecting take a few seconds.
		ConnectThread = std::thread([this, Request, bUseMicrophone] {
			try
			{
				UE_LOG(LogPipecat, Log, TEXT("Starting the bot at %hs"), Request.endpoint.c_str());
				Client->start_bot_and_connect(Request);
			}
			catch (const std::exception& Error)
			{
				if (Running)
				{
					ReportError(FString::Printf(TEXT("Unable to connect: %hs"), Error.what()));
				}
				return;
			}

			if (bUseMicrophone && Running)
			{
				Microphone.Start(UserSampleRate, [this](const int16* Frames, int32 NumFrames) {
					Client->send_user_audio(Frames, NumFrames);
				});
			}
		});
	}

	void Stop()
	{
		if (!Running.exchange(false))
		{
			return;
		}
		// Also stops a connection in progress, and wakes up the reader.
		Client->disconnect();
		if (ConnectThread.joinable())
		{
			ConnectThread.join();
		}
		Microphone.Stop();
		if (Reader.joinable())
		{
			Reader.join();
		}
		Ready = false;
	}

	void SendText(const FString& Text)
	{
		try
		{
			Client->send_text(ToStdString(Text));
		}
		catch (const std::exception& Error)
		{
			ReportError(FString::Printf(TEXT("Unable to send text: %hs"), Error.what()));
		}
	}

	bool IsReady() const { return Ready; }
	float GetBotVoiceLevel() const { return BotLevel; }
	float GetMicrophoneLevel() const { return Microphone.GetLevel(); }

	//
	// pipecat::PipecatClientCallbacks, on the client's thread.
	//

	virtual void on_bot_ready(const pipecat::rtvi::BotReadyData&) override
	{
		Ready = true;
		Post([](UPipecatVoiceComponent& C) { C.OnBotReady.Broadcast(); });
	}

	virtual void on_disconnected() override
	{
		Ready = false;
		Post([](UPipecatVoiceComponent& C) { C.OnDisconnected.Broadcast(); });
	}

	virtual void on_error(const pipecat::rtvi::ErrorData& Error) override
	{
		ReportError(ToFString(Error.error));
	}

	virtual void on_user_started_speaking() override
	{
		Post([](UPipecatVoiceComponent& C) { C.OnUserStartedSpeaking.Broadcast(); });
	}

	virtual void on_user_stopped_speaking() override
	{
		Post([](UPipecatVoiceComponent& C) { C.OnUserStoppedSpeaking.Broadcast(); });
	}

	virtual void on_user_transcript(const pipecat::rtvi::TranscriptData& Data) override
	{
		FString Text = ToFString(Data.text);
		bool bFinal = Data.final;
		Post([Text, bFinal](UPipecatVoiceComponent& C) { C.OnUserTranscript.Broadcast(Text, bFinal); });
	}

	virtual void on_bot_started_speaking() override
	{
		Post([](UPipecatVoiceComponent& C) { C.OnBotStartedSpeaking.Broadcast(); });
	}

	virtual void on_bot_stopped_speaking() override
	{
		Post([](UPipecatVoiceComponent& C) { C.OnBotStoppedSpeaking.Broadcast(); });
	}

	virtual void on_bot_interrupted() override
	{
		Post([](UPipecatVoiceComponent& C) { C.OnBotInterrupted.Broadcast(); });
	}

	virtual void on_bot_output(const pipecat::rtvi::BotOutputData& Data) override
	{
		// Text the bot speaks comes first when it's written, and then again as
		// it's spoken. Pass it on once, when the bot starts speaking it, so it
		// matches the voice.
		if (Data.will_be_spoken.value_or(false) && Data.spoken_status == std::optional<std::string>("new"))
		{
			return;
		}
		if (Data.segment_id && !OutputSegments.insert(*Data.segment_id).second)
		{
			return;
		}
		FString Text = ToFString(Data.text).TrimStartAndEnd();
		if (Text.IsEmpty())
		{
			// E.g. a paragraph break.
			return;
		}
		Post([Text](UPipecatVoiceComponent& C) { C.OnBotOutput.Broadcast(Text); });
	}

private:
	template <typename F>
	void Post(F&& Fn)
	{
		AsyncTask(ENamedThreads::GameThread, [Owner = Owner, Fn = MoveTemp(Fn)]() {
			if (UPipecatVoiceComponent* Component = Owner.Get())
			{
				Fn(*Component);
			}
		});
	}

	void ReportError(const FString& Error)
	{
		UE_LOG(LogPipecat, Warning, TEXT("%s"), *Error);
		Post([Error](UPipecatVoiceComponent& C) { C.OnError.Broadcast(Error); });
	}

	void RegisterFunction(const FString& Name, FPipecatFunctionHandler Handler)
	{
		TWeakObjectPtr<UPipecatVoiceComponent> WeakOwner = Owner;
		Client->register_function_call_handler(
			ToStdString(Name),
			[WeakOwner, Handler](const pipecat::FunctionCallParams& Params, pipecat::FunctionCallResultCallback Respond) {
				FString Arguments = ToFString(Params.arguments.dump());
				AsyncTask(ENamedThreads::GameThread, [WeakOwner, Handler, Arguments, Respond]() {
					if (!WeakOwner.IsValid())
					{
						Respond({{"error", "The game isn't running"}});
						return;
					}
					Handler(Arguments, [Respond](const FString& ResultJson) {
						nlohmann::json Result = nlohmann::json::parse(ToStdString(ResultJson), nullptr, false);
						if (Result.is_discarded())
						{
							Result = ToStdString(ResultJson);
						}
						Respond(Result);
					});
				});
			});
	}

	// Reads the bot's voice and queues it on the sound wave the owner plays.
	void PlayBotVoice()
	{
		std::vector<int16> Frames(BotSampleRate / 100);
		while (Running)
		{
			int32 Read = Client->read_bot_audio(Frames.data(), Frames.size());
			if (Read <= 0)
			{
				// Not connected.
				BotLevel = 0.0f;
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}
			BotLevel = Loudness(Frames.data(), Read);
			if (Wave->GetAvailableAudioByteCount() < MaxQueuedBotVoiceBytes)
			{
				Wave->QueueAudio(reinterpret_cast<const uint8*>(Frames.data()), Read * sizeof(int16));
			}

		}
	}

	TWeakObjectPtr<UPipecatVoiceComponent> Owner;
	// Kept alive by the component, which stops the session first.
	USoundWaveProcedural* Wave;
	std::unique_ptr<pipecat::PipecatClient> Client;
	FPipecatMicrophone Microphone;

	std::atomic<bool> Running {false};
	std::atomic<bool> Ready {false};
	std::atomic<float> BotLevel {0.0f};
	std::thread ConnectThread;
	std::thread Reader;

	// Only used on the client's thread.
	std::set<int64_t> OutputSegments;
};

UPipecatVoiceComponent::UPipecatVoiceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UPipecatVoiceComponent::BeginPlay()
{
	Super::BeginPlay();

	// The bot's voice, played from the owner's position.
	VoiceWave = NewObject<USoundWaveProcedural>(this);
	VoiceWave->SetSampleRate(BotSampleRate);
	VoiceWave->NumChannels = 1;
	VoiceWave->Duration = INDEFINITELY_LOOPING_DURATION;
	VoiceWave->SoundGroup = SOUNDGROUP_Voice;
	VoiceWave->bLooping = false;
	// Unreal stops sounds it virtualizes, e.g. when they can't be heard, and
	// a stream can't be restarted.
	VoiceWave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;

	AActor* Owner = GetOwner();
	VoiceAudio = NewObject<UAudioComponent>(Owner, TEXT("PipecatVoice"));
	if (USceneComponent* Root = Owner->GetRootComponent())
	{
		VoiceAudio->SetupAttachment(Root);
	}
	VoiceAudio->bAutoActivate = false;
	VoiceAudio->bAllowSpatialization = true;
	VoiceAudio->bOverrideAttenuation = true;
	VoiceAudio->AttenuationOverrides.bAttenuate = true;
	VoiceAudio->AttenuationOverrides.bSpatialize = true;
	// Full volume within 3 meters, and fading out after that.
	VoiceAudio->AttenuationOverrides.AttenuationShapeExtents = FVector(300.0f);
	VoiceAudio->AttenuationOverrides.FalloffDistance = VoiceFalloffDistance;
	VoiceAudio->SetSound(VoiceWave);
	VoiceAudio->RegisterComponent();
	VoiceAudio->Play();

	if (bConnectOnBeginPlay)
	{
		Connect();
	}
}

void UPipecatVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Disconnect();
	if (VoiceAudio)
	{
		VoiceAudio->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

void UPipecatVoiceComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// If something stops the bot's voice, play it again, without what was
	// waiting to be played.
	if (VoiceAudio && IsReady() && !VoiceAudio->IsPlaying())
	{
		UE_LOG(LogPipecat, Warning, TEXT("The bot's voice stopped playing, playing it again"));
		VoiceWave->ResetAudio();
		VoiceAudio->Play();
	}
}

void UPipecatVoiceComponent::Connect()
{
	if (!Session)
	{
		Session = MakeShared<FPipecatSession>(this, VoiceWave, Functions);
	}
	Session->Start(StartUrl, ApiKey, bUseMicrophone);
}

void UPipecatVoiceComponent::Disconnect()
{
	if (Session)
	{
		Session->Stop();
		Session.Reset();
	}
}

void UPipecatVoiceComponent::SendText(const FString& Text)
{
	if (Session)
	{
		Session->SendText(Text);
	}
}

bool UPipecatVoiceComponent::IsReady() const
{
	return Session && Session->IsReady();
}

float UPipecatVoiceComponent::GetBotVoiceLevel() const
{
	return Session ? Session->GetBotVoiceLevel() : 0.0f;
}

float UPipecatVoiceComponent::GetMicrophoneLevel() const
{
	return Session ? Session->GetMicrophoneLevel() : 0.0f;
}

void UPipecatVoiceComponent::RegisterFunction(const FString& Name, FPipecatFunctionHandler Handler)
{
	Functions.Add(Name, MoveTemp(Handler));
}
