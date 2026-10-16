#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CombatCore.h"
#include "RoninFighter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class USceneComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ARoninGameMode;

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

private:
    ARoninGameMode* Game() const;
    UStaticMeshComponent* MakePart(FName Name, USceneComponent* Parent, const TCHAR* MeshPath,
        FVector Location, FVector Scale);
    void UpdateArms();
    void MoveForward(float Value);
    void MoveRight(float Value);
    void Turn(float Value);
    void LookUp(float Value);
    void TurnRate(float Value);
    void LookUpRate(float Value);
    void Light(); void Heavy(); void GuardDown(); void GuardUp(); void DodgeInput();
    void ExecuteInput(); void Flow(); void Stone(); void Gale(); void Restart();
    void Pause(); void Lock(); void Quality(); void FrameCap(); void Help(); void Haptics();

    UPROPERTY() TObjectPtr<USceneComponent> Body;
    UPROPERTY() TObjectPtr<USceneComponent> SwordPivot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Sword;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> LeftLeg;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RightLeg;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> LeftArm;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RightArm;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> LeftForearm;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RightForearm;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> LeftHand;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RightHand;
    UPROPERTY() TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> ArmorParts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> MetalParts;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ArmorMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SteelMaterial;
    FLinearColor ArmorColor;
    FVector DodgeDirection = FVector::ForwardVector;
    float DodgeSpeed = 800.f;
    FVector2D MoveInput = FVector2D::ZeroVector;
    float Gait = 0.f;
    float FlashTime = 0.f;
    bool bFrozen = false;
};
