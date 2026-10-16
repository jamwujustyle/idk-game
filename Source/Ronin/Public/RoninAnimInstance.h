#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "CombatCore.h"
#include "RoninAnimInstance.generated.h"

class UAnimSequence;

// Source clips are catalogued from the locally installed, licensed Kwang package.
enum class ERoninAnimationClip : uint8
{
    Idle, JogForward, JogBackward, JogLeft, JogRight,
    AttackA, AttackB, AttackC, AttackD,
    RecoveryA, RecoveryB, RecoveryC,
    StunStart, StunLoop, Death,
    StepForward, StepBackward, StepLeft, StepRight,
    Guard, Count
};

struct FRoninAnimationState
{
    ronin::FighterState Combat;
    FVector LocalVelocity = FVector::ZeroVector;
    FVector LocalDodgeDirection = -FVector::ForwardVector;
    uint32 ResetSerial = 0;
    bool bFrozen = false;
};

struct FRoninAnimationPlayback
{
    UAnimSequence* ActionSequence = nullptr;
    float ActionTime = 0.f;
    float IdleTime = 0.f;
    float ActionWeight = 0.f;
    bool bFrozen = false;
};

/** Native pose graph: authored skeletal clips follow the deterministic combat clock. */
UCLASS(Transient)
class RONIN_API URoninAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    URoninAnimInstance();
    UAnimSequence* GetClip(ERoninAnimationClip Clip) const;
    // Game-thread inspection waits for any outstanding parallel pose evaluation.
    FRoninAnimationPlayback GetPlaybackState() const;

    // The source contact interval is mapped onto Active, independently of stance speed.
    static float MapAttackPhase(const ronin::FighterState& State, float ClipLength,
        float ContactStart, float ContactEnd);

protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
    UPROPERTY() TArray<TObjectPtr<UAnimSequence>> Clips;
};
