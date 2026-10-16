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
    FString HapticsName = TEXT("70%");

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
    bool HasDuelStarted() const { return bDuelStarted; }
    float GetRoundTime() const { return RoundTime; }
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
    float RoundTime = 0.f;
    int32 QualityIndex = 2;
    int32 FrameCapIndex = 0;
    int32 HapticsIndex = 2;
    bool bRoundScored = false;
    bool bDuelStarted = false;
    bool bGuardHeld = false;
    float SmoothedFrameTime = 1.f/60.f;
};
