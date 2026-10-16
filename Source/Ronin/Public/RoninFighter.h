#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CombatCore.h"
#include "RoninFighter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UParticleSystemComponent;
class USkeletalMesh;
class ARoninGameMode;
struct FRoninAnimationState;

UCLASS()
class RONIN_API ARoninFighter : public ACharacter
{
    GENERATED_BODY()
public:
    ARoninFighter();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

    ronin::FighterId Identity = ronin::FighterId::Player;
    void ResetPose(FVector Location, FRotator Rotation);
    void BeginDodge();
    void AdvanceDodge(float SimulationDelta);
    void HitFlash(FLinearColor Color);
    void SetFrozen(bool bFrozen);
    // Copied by the animation proxy on the game thread, never read from a worker.
    void GetAnimationState(FRoninAnimationState& OutState) const;

private:
    ARoninGameMode* Game() const;
    void UpdateBladeTrail(bool bActivePhase, bool bFreeze);
    void MoveForward(float Value);
    void MoveRight(float Value);
    void Turn(float Value);
    void LookUp(float Value);
    void TurnRate(float Value);
    void LookUpRate(float Value);
    void Light(); void Heavy(); void GuardDown(); void GuardUp(); void DodgeInput();
    void ExecuteInput(); void Flow(); void Stone(); void Gale(); void Restart();
    void Pause(); void Lock(); void Quality(); void FrameCap(); void Help(); void Haptics();

    UPROPERTY() TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<USkeletalMesh> PlayerMesh;
    UPROPERTY() TObjectPtr<USkeletalMesh> EnemyMesh;
    UPROPERTY() TObjectPtr<UParticleSystemComponent> BladeTrail;
    FVector DodgeDirection = FVector::ForwardVector;
    float DodgeDistance = 180.f;
    float DodgeDuration = 0.f;
    float DodgeElapsed = 0.f;
    FVector2D MoveInput = FVector2D::ZeroVector;
    uint32 AnimationResetSerial = 0;
    bool bFrozen = false;
    bool bBladeTrailEmitting = false;
};
