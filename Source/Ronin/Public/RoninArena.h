#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoninArena.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkyLightComponent;
class UStaticMesh;

/** Self-contained courtyard. The collision floor is 1900 cm square, with its top at Z = 0. */
UCLASS()
class RONIN_API ARoninArena : public AActor
{
    GENERATED_BODY()

public:
    ARoninArena();

protected:
    virtual void BeginPlay() override;

private:
    UMaterialInstanceDynamic* MakeMaterial(const FLinearColor& Color, float Roughness);
    void AddShape(UStaticMesh* Mesh, UMaterialInterface* Material, const FVector& Position,
        const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator);
    void AddBeam(UMaterialInterface* Material, const FVector& Start, const FVector& End, float Width);
    void BuildCourtyard();
    void BuildGate(float Y, UMaterialInterface* Wood, UMaterialInterface* Metal, UMaterialInterface* Stone);
    void BuildTree(const FVector& Position, float Scale, int32 Seed, UMaterialInterface* Bark,
        UMaterialInterface* Leaf, UMaterialInterface* LeafLight);
    void BuildLantern(const FVector& Position, UMaterialInterface* Stone,
        UMaterialInterface* Metal, UMaterialInterface* Paper);

    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;

    UPROPERTY()
    TObjectPtr<UStaticMesh> CylinderMesh;

    UPROPERTY()
    TObjectPtr<UStaticMesh> ConeMesh;

    UPROPERTY()
    TObjectPtr<UStaticMesh> SphereMesh;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> ShapeMaterial;

    UPROPERTY()
    TObjectPtr<USkyLightComponent> SkyLight;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> GeometryBatches;
};
