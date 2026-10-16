#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoninArena.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkyLightComponent;
class UStaticMesh;

/** Walkable forest valley surrounding a flat 1900 cm duel courtyard. */
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
    UMaterialInterface* LoadSurface(const TCHAR* Path, const FLinearColor& FallbackColor);
    UMaterialInterface* TintSurface(UMaterialInterface* Parent, const FLinearColor& Tint);
    void AddGeometry(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform);
    void AddShape(UStaticMesh* Mesh, UMaterialInterface* Material, const FVector& Position,
        const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator);
    void AddNaturalMesh(UStaticMesh* Mesh, const FVector& GroundPosition, float Size,
        bool bSizeByHeight, float Yaw);
    void BuildCourtyard();
    void BuildTerrain(UMaterialInterface* Material);
    void BuildTrail(const TArray<FVector2D>& Points, float Width, UMaterialInterface* Material);
    void BuildRestSite(const FVector2D& Center, UMaterialInterface* Floor,
        UMaterialInterface* Stone, UMaterialInterface* Wood, UMaterialInterface* Paper);
    void BuildGate(float X, UMaterialInterface* Wood, UMaterialInterface* DarkWood, UMaterialInterface* Stone);
    void BuildLantern(const FVector& Position, UMaterialInterface* Stone,
        UMaterialInterface* Wood, UMaterialInterface* Paper);

    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;

    UPROPERTY()
    TObjectPtr<UStaticMesh> CylinderMesh;

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

    // A null entry preserves the imported mesh's complete material-slot assignments.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> BatchOverrides;
};
