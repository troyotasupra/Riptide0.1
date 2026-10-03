#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/VoiceConfig.h"
#include "Subsystems/WorldSubsystem.h"
#include "RiptideVoice.generated.h"

class APlayerState;
class ARiptideBoat;
class ARiptideCharacter;
class UInputAction;
class UInputMappingContext;
class USoundAttenuation;
class USoundBase;
class USoundEffectSourcePresetChain;

/** What a boat's hand mic is switched to. */
UENUM(BlueprintType)
enum class ERiptideMicMode : uint8
{
	Radio,          // the CB, on the boat's channel
	Loudhailer,     // the horn on the T-top
};

/** Where a player's voice comes out, for one listener. */
UENUM(BlueprintType)
enum class ERiptideVoiceRoute : uint8
{
	Proximity,      // their own voice, from where they stand, fading out over some 25 m
	Radio,          // into the radio mic on the CB: out of the listener's boat's radio, if it's on the same channel
	Loudhailer,     // into the mic on the loudhailer: out of the horn on their boat's T-top, heard a long way off
};

/**
 * One remote player's voice on this machine: where it plays from is set by the listener's voice component before
 * each transmission starts. Plays the radio's squelch at the start and end of a transmission heard over the radio.
 */
UCLASS()
class RIPTIDE_API URiptideVoiceTalker : public UVOIPTalker
{
	GENERATED_BODY()

public:
	virtual void OnTalkingBegin(UAudioComponent* AudioComponent) override;
	virtual void OnTalkingEnd() override;

	ERiptideVoiceRoute Route = ERiptideVoiceRoute::Proximity;
	/** The squelch sounds, and where the radio's speaker is (for a transmission heard over the radio). */
	TWeakObjectPtr<USceneComponent> Speaker;
	TObjectPtr<USoundBase> SquelchOpen;
	TObjectPtr<USoundBase> SquelchClose;
	bool bTalking = false;
};

/**
 * Voice chat and the boats' radios, for one player (on their player controller).
 *
 * - V is push-to-talk. Spoken without the radio mic, it's heard around you, fading over about 25 m.
 * - Holding the boat's hand mic, what you say goes out on the boat's CB channel (heard from the radio of every other
 *   boat tuned to it, through a radio's tinny speaker), or through the loudhailer horn if the mic's switched to it.
 * - While holding the mic: [ and ] (or the mouse wheel) change the channel, B switches between CB and loudhailer.
 * - NPC crews talk on the same channels and over their loudhailers (URiptideRadioSubsystem): heard here as radio
 *   chatter with subtitles.
 *
 * Every machine decides for itself where each other player's voice comes out (everyone's voice reaches everyone):
 * from who is holding which boat's mic, which way it's switched and the boats' channels, all replicated before
 * anyone speaks, so a transmission comes out of the right place from its first word.
 */
UCLASS(ClassGroup = (Riptide))
class RIPTIDE_API URiptideVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URiptideVoiceComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Push-to-talk (what V does; also for tests). */
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void SetPushToTalk(bool bDown);

	UFUNCTION(BlueprintPure, Category = "Voice")
	bool IsPushingToTalk() const { return bPushToTalk; }

	/** The microphones this machine has, by device name (the settings menu's list). */
	UFUNCTION(BlueprintCallable, Category = "Voice")
	static TArray<FString> ListMicrophones();

	/** Points the voice capture at the microphone the settings name (empty: the system's default). Works once the
	 * player has started talking at least once (that's when the capture exists). True if it was taken. */
	UFUNCTION(BlueprintCallable, Category = "Voice", meta = (WorldContext = "WorldContext"))
	static bool ApplyMicrophone(const UObject* WorldContext);

	/** How loud the microphone is right now while talking, 0..1 (0 when not capturing). For the mic test. */
	UFUNCTION(BlueprintPure, Category = "Voice", meta = (WorldContext = "WorldContext"))
	static float MicrophoneLevel(const UObject* WorldContext);

	/** Turns the radio's channel knob (while holding the mic, or at the helm). */
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void ChangeChannel(int32 Delta);

	/** Switches the held mic between the CB and the loudhailer. */
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void ToggleMicMode();

	/** Where Talker's voice comes out for Listener (any machine can work it out; for tests too). */
	UFUNCTION(BlueprintPure, Category = "Voice")
	static ERiptideVoiceRoute RouteFor(const APlayerState* Talker, const APlayerController* Listener);

	/** The crew member a player is (their pawn, or the one at the helm of the boat they're driving). */
	static ARiptideCharacter* CrewOf(const APlayerState* Player);

	/** A line heard on the radio or over a loudhailer, from an NPC (sent by URiptideRadioSubsystem, through the
	 * player's controller). */
	void HearRadioLine(int32 Channel, ARiptideBoat* FromBoat, const FString& Speaker, const FString& Line, bool bLoudhailer);

	/** The last radio or loudhailer line heard here (for tests). */
	UFUNCTION(BlueprintPure, Category = "Voice")
	FString GetLastHeardLine() const { return LastHeardLine; }

	/** Radio channels run 1 to 40, like a CB's. */
	static constexpr int32 MaxChannel = 40;

	/** Tunes Boat's radio and switches its mic, on the server, if Player is at that radio (holding its mic, or at its
	 * helm). */
	static void SetRadioFor(const APlayerController* Player, ARiptideBoat* Boat, int32 Channel, ERiptideMicMode Mode);

private:
	void RequestRadio(ARiptideBoat* Boat, int32 Channel, ERiptideMicMode Mode);
	void BuildInput();
	void BuildAudio();
	/** The boat whose radio this player can work now: the one whose mic they hold, or whose helm they have. */
	ARiptideBoat* RadioInReach() const;
	void UpdateTalkers();
	void DrawHud() const;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> VoiceMapping;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> Actions;

	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerState>, TObjectPtr<URiptideVoiceTalker>> Talkers;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> VoiceFalloff;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RadioFalloff;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> HailerFalloff;

	UPROPERTY(Transient)
	TObjectPtr<USoundEffectSourcePresetChain> RadioEffects;

	UPROPERTY(Transient)
	TObjectPtr<USoundEffectSourcePresetChain> HailerEffects;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> SquelchOpen;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> SquelchClose;

	bool bPushToTalk = false;

	FString LastHeardLine;
};

/**
 * The radio and loudhailers for the game's own voices (NPC crews): lines go out on a channel, heard by every player
 * whose boat's radio is tuned to it, or over a boat's loudhailer, heard by everyone near it. Server only.
 */
UCLASS()
class RIPTIDE_API URiptideRadioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Says Line on the radio, on Channel (1-40), as Speaker. */
	UFUNCTION(BlueprintCallable, Category = "Radio")
	void Broadcast(int32 Channel, const FString& Speaker, const FString& Line);

	/** Broadcast, from anywhere with a world (scripts and tests). */
	UFUNCTION(BlueprintCallable, Category = "Radio", meta = (WorldContext = "WorldContextObject"))
	static void RadioCall(const UObject* WorldContextObject, int32 Channel, const FString& Speaker, const FString& Line);

	/** Says Line over Boat's loudhailer, as Speaker: heard by players within about 150 m of it. */
	UFUNCTION(BlueprintCallable, Category = "Radio")
	void Loudhailer(ARiptideBoat* Boat, const FString& Speaker, const FString& Line);
};
