#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CombatCore.h"
#include "RoninGameMode.generated.h"

class ARoninFighter;
class UParticleSystem;
class UParticleSystemComponent;
class USoundBase;

UCLASS()
class RONIN_API ARoninGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ARoninGameMode();
    virtual void StartPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick(float DeltaSeconds) override;

    ronin::CombatCore Combat;
    UPROPERTY() TObjectPtr<ARoninFighter> PlayerFighter;
    UPROPERTY() TObjectPtr<ARoninFighter> EnemyFighter;
    bool bCombatPaused = false;
    bool bShowHelp = false;
    bool bLockOn = true;
    bool bUsingGamepad = false;
    int32 Victories = 0;
    int32 PerfectParries = 0;
    FString Feedback;
    FLinearColor FeedbackColor = FLinearColor(0.88f, 0.83f, 0.68f);
    float FeedbackTime = 0.f;
    float ImpactFlash = 0.f;
    float DisplayedFPS = 0.f;
    FString QualityName = TEXT("HIGH");
    FString FrameCapName = TEXT("UNCAPPED");
    FString HapticsName = TEXT("100%");

    void StartDuel();
    void RestartDuel();
    void LightAttack();
    void HeavyAttack();
    void Guard(bool bHeld);
    void Dodge();
    void Execute();
    void ChangeStance(ronin::Stance Stance);
    void TogglePause();
    void CycleQuality();
    void CycleFrameCap();
    void CycleHaptics();
    bool CanControl() const;
    bool CanTraverse() const;
    bool HasDuelStarted() const { return bDuelStarted; }
    float GetRoundTime() const { return RoundTime; }
    // Only combat/movement/pose time is scaled; world, input and HUD time stay live.
    float GetCombatTimeScale() const { return CombatTimeScale; }
    float GetCombatDeltaSeconds() const { return CombatFrameDelta; }
    // Monotonic success identifier; the kind belongs to the most recent cue.
    uint32 GetDefenseCueSerial() const { return DefenseCueSerial; }
    bool IsPerfectDodgeCue() const { return bPerfectDodgeCue; }
    void ClearDefenseCinematic();
    const UParticleSystem* GetContactSparkTemplate() const { return ContactSparks; }
    void Notify(const FString& Text, FLinearColor Color, float Duration = 1.25f);

private:
    void Step(float DeltaSeconds);
    void ResolveAttack(ronin::FighterId Attacker);
    void PlayCue(FName Cue, FVector Location, float Volume = 1.f);
    void InitializeContactSparks();
    void SpawnContactSparks(FVector Location);
    void UpdateContactSparks(float DeltaSeconds);
    void ClearContactSparks();
    void BeginDefenseCinematic(bool bPerfectDodge);
    float SampleDefenseTimeScale() const;
    void ObservePlayerAttackStart();
    UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> Sounds;
    UPROPERTY() TObjectPtr<UParticleSystem> ContactSparkSource;
    UPROPERTY(Transient) TObjectPtr<UParticleSystem> ContactSparks;
    struct FContactSparkBurst
    {
        TWeakObjectPtr<UParticleSystemComponent> Component;
        float Age = 0.f;
    };
    TArray<FContactSparkBurst> ContactSparkBursts;
    float Accumulator = 0.f;
    float HitStop = 0.f;
    double DefenseCueStartedAt = -1.0;
    float CombatTimeScale = 1.f;
    float CombatFrameDelta = 0.f;
    uint32 DefenseCueSerial = 0;
    uint64 LastPlayerAttackId = 0;
    bool bPerfectDodgeCue = false;
    float RoundTime = 0.f;
    int32 QualityIndex = 2;
    int32 FrameCapIndex = 0;
    int32 HapticsIndex = 3;
    bool bRoundScored = false;
    bool bDuelStarted = false;
    bool bGuardHeld = false;
    float SmoothedFrameTime = 1.f/60.f;
};
