#include "RoninArena.h"

#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

ARoninArena::ARoninArena()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CourtyardRoot"));
    SceneRoot->SetMobility(EComponentMobility::Static);
    SetRootComponent(SceneRoot);

    // Hard references make these engine assets available to the cooker as well as the editor.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CubeMesh = Cube.Object;
    CylinderMesh = Cylinder.Object;
    SphereMesh = Sphere.Object;
    ShapeMaterial = Material.Object;

    UStaticMeshComponent* Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CollisionFloor"));
    Floor->SetupAttachment(SceneRoot);
    Floor->SetMobility(EComponentMobility::Static);
    Floor->SetStaticMesh(CubeMesh);
    Floor->SetRelativeLocation(FVector(0.0, 0.0, -35.0));
    Floor->SetRelativeScale3D(FVector(19.0, 19.0, 0.7));
    Floor->SetCollisionProfileName(TEXT("BlockAll"));
    Floor->SetGenerateOverlapEvents(false);

    // Boundary collision is independent of ornamental geometry and never obstructs the camera.
    for (int32 Side = 0; Side < 4; ++Side)
    {
        UBoxComponent* Boundary = CreateDefaultSubobject<UBoxComponent>(
            *FString::Printf(TEXT("ArenaBoundary%d"), Side));
        Boundary->SetupAttachment(SceneRoot);
        Boundary->SetMobility(EComponentMobility::Static);
        const bool bAlongX = Side < 2;
        const double Sign = Side % 2 == 0 ? -1.0 : 1.0;
        Boundary->SetRelativeLocation(bAlongX ? FVector(Sign * 975.0, 0.0, 250.0)
                                             : FVector(0.0, Sign * 975.0, 250.0));
        Boundary->SetBoxExtent(bAlongX ? FVector(25.0, 1000.0, 300.0)
                                      : FVector(1000.0, 25.0, 300.0));
        Boundary->SetCollisionProfileName(TEXT("BlockAll"));
        Boundary->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
        Boundary->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
        Boundary->SetGenerateOverlapEvents(false);
        Boundary->SetHiddenInGame(true);
    }

    UDirectionalLightComponent* Key = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("CloudBreakLight"));
    Key->SetupAttachment(SceneRoot);
    Key->SetMobility(EComponentMobility::Movable);
    Key->SetRelativeRotation(FRotator(-38.0, -42.0, 0.0));
    Key->SetLightColor(FLinearColor(0.91f, 0.95f, 1.0f));
    Key->SetIntensity(2.4f);
    Key->SetCastShadows(true);
    Key->LightSourceAngle = 2.5f;

    SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("OvercastSkyLight"));
    SkyLight->SetupAttachment(SceneRoot);
    SkyLight->SetMobility(EComponentMobility::Movable);
    SkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
    SkyLight->SetIntensity(0.65f);
    SkyLight->SetLightColor(FLinearColor::White);
    SkyLight->bLowerHemisphereIsBlack = true;

    UExponentialHeightFogComponent* Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("GardenMist"));
    Fog->SetupAttachment(SceneRoot);
    Fog->SetRelativeLocation(FVector(0.0, 0.0, -120.0));
    Fog->SetFogDensity(0.028f);
    Fog->SetFogHeightFalloff(0.2f);
    Fog->SetFogInscatteringColor(FLinearColor(0.15f, 0.19f, 0.21f));
    Fog->SetFogMaxOpacity(0.86f);
    Fog->SetStartDistance(1100.0f);
    Fog->SetVolumetricFog(false);

    UPostProcessComponent* Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("CourtyardGrade"));
    Grade->SetupAttachment(SceneRoot);
    Grade->bUnbound = true;
    Grade->Priority = 1.0f;
    Grade->Settings.bOverride_AutoExposureMethod = true;
    Grade->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Grade->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Grade->Settings.AutoExposureApplyPhysicalCameraExposure = false;
    Grade->Settings.bOverride_AutoExposureBias = true;
    Grade->Settings.AutoExposureBias = 0.0f;
    Grade->Settings.bOverride_BloomIntensity = true;
    Grade->Settings.BloomIntensity = 0.15f;
    Grade->Settings.bOverride_VignetteIntensity = true;
    Grade->Settings.VignetteIntensity = 0.16f;
    Grade->Settings.bOverride_MotionBlurAmount = true;
    Grade->Settings.MotionBlurAmount = 0.0f;
    Grade->Settings.bOverride_SceneFringeIntensity = true;
    Grade->Settings.SceneFringeIntensity = 0.0f;
}

void ARoninArena::BeginPlay()
{
    Super::BeginPlay();
    if (UMaterialInterface* CombatMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Materials/M_Combat.M_Combat")))
    {
        ShapeMaterial = CombatMaterial;
    }
    if (UTextureCube* Sky = LoadObject<UTextureCube>(nullptr,
        TEXT("/Game/Environment/Textures/T_OvercastSky.T_OvercastSky")))
    {
        SkyLight->SetCubemap(Sky);
    }
    BuildCourtyard();
    for (UInstancedStaticMeshComponent* Batch : GeometryBatches)
    {
        Batch->RegisterComponent();
    }
    SkyLight->RecaptureSky();
}

UMaterialInstanceDynamic* ARoninArena::MakeMaterial(const FLinearColor& Color, float Roughness)
{
    if (!ShapeMaterial) return nullptr;
    UMaterialInstanceDynamic* Result = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
    Result->SetVectorParameterValue(TEXT("Color"), Color);
    Result->SetScalarParameterValue(TEXT("Roughness"), Roughness);
    Result->SetScalarParameterValue(TEXT("Metallic"), 0.0f);
    Materials.Add(Result);
    return Result;
}

UMaterialInterface* ARoninArena::LoadSurface(const TCHAR* Path, const FLinearColor& FallbackColor)
{
    if (UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr, Path)) return Surface;
    UE_LOG(LogTemp, Warning, TEXT("Missing courtyard surface %s; run scripts/import_environment.py."), Path);
    return MakeMaterial(FallbackColor, 0.7f);
}

UMaterialInterface* ARoninArena::TintSurface(UMaterialInterface* Parent, const FLinearColor& Tint)
{
    if (!Parent) return nullptr;
    UMaterialInstanceDynamic* Result = UMaterialInstanceDynamic::Create(Parent, this);
    Result->SetVectorParameterValue(TEXT("ColorTint"), Tint);
    Materials.Add(Result);
    return Result;
}

void ARoninArena::AddGeometry(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform)
{
    if (!Mesh) return;
    UInstancedStaticMeshComponent* Batch = nullptr;
    for (int32 Index = 0; Index < GeometryBatches.Num(); ++Index)
    {
        if (GeometryBatches[Index]->GetStaticMesh() == Mesh && BatchOverrides[Index] == Material)
        {
            Batch = GeometryBatches[Index];
            break;
        }
    }
    if (!Batch)
    {
        Batch = NewObject<UInstancedStaticMeshComponent>(this);
        AddInstanceComponent(Batch);
        Batch->SetupAttachment(GetRootComponent());
        Batch->SetStaticMesh(Mesh);
        if (Material) Batch->SetMaterial(0, Material);
        Batch->SetMobility(EComponentMobility::Static);
        Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Batch->SetGenerateOverlapEvents(false);
        Batch->SetCanEverAffectNavigation(false);
        GeometryBatches.Add(Batch);
        BatchOverrides.Add(Material);
    }
    Batch->AddInstance(Transform, false);
}

void ARoninArena::AddShape(UStaticMesh* Mesh, UMaterialInterface* Material,
    const FVector& Position, const FVector& Size, const FRotator& Rotation)
{
    // Engine construction meshes are 100 cm across; imported meshes use their actual bounds.
    AddGeometry(Mesh, Material, FTransform(Rotation, Position, Size / 100.0));
}

void ARoninArena::AddNaturalMesh(UStaticMesh* Mesh, const FVector& GroundPosition,
    float Size, bool bSizeByHeight, float Yaw)
{
    if (!Mesh) return;
    const FBoxSphereBounds Bounds = Mesh->GetBounds();
    const double Dimension = bSizeByHeight ? Bounds.BoxExtent.Z * 2.0
        : FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) * 2.0;
    if (Dimension < UE_SMALL_NUMBER) return;
    const double MeshScale = Size / Dimension;
    const FRotator Rotation(0.0, Yaw, 0.0);
    // Imported scan pivots can be offset. Center the footprint and plant its lowest point.
    const FVector Pivot = GroundPosition - Rotation.RotateVector(Bounds.Origin * MeshScale)
        + FVector(0.0, 0.0, Bounds.BoxExtent.Z * MeshScale);
    AddGeometry(Mesh, nullptr, FTransform(Rotation, Pivot, FVector(MeshScale)));
}

void ARoninArena::BuildCourtyard()
{
    UMaterialInterface* Floor = LoadSurface(TEXT("/Game/Environment/Materials/M_StoneFloor.M_StoneFloor"),
        FLinearColor(0.18f, 0.2f, 0.21f));
    UMaterialInterface* Edge = LoadSurface(TEXT("/Game/Environment/Materials/M_StoneEdge.M_StoneEdge"),
        FLinearColor(0.1f, 0.12f, 0.13f));
    UMaterialInterface* Cedar = LoadSurface(TEXT("/Game/Environment/Materials/M_Cedar.M_Cedar"),
        FLinearColor(0.2f, 0.09f, 0.045f));
    UMaterialInterface* Wood = TintSurface(Cedar, FLinearColor(0.7f, 0.42f, 0.32f));
    UMaterialInterface* DarkWood = TintSurface(Cedar, FLinearColor(0.24f, 0.27f, 0.28f));
    UMaterialInterface* Paper = MakeMaterial(FLinearColor(0.82f, 0.62f, 0.34f), 0.88f);

    TArray<UStaticMeshComponent*> Components;
    GetComponents<UStaticMeshComponent>(Components);
    for (UStaticMeshComponent* Component : Components)
    {
        if (Component->GetFName() == TEXT("CollisionFloor")) Component->SetMaterial(0, Floor);
    }

    if (UMaterialInterface* SkyMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Environment/Materials/M_Sky.M_Sky")))
    {
        UStaticMeshComponent* SkyDome = NewObject<UStaticMeshComponent>(this, TEXT("OvercastSkyDome"));
        AddInstanceComponent(SkyDome);
        SkyDome->SetupAttachment(GetRootComponent());
        SkyDome->SetStaticMesh(SphereMesh);
        SkyDome->SetMaterial(0, SkyMaterial);
        SkyDome->SetRelativeScale3D(FVector(4000.0));
        SkyDome->SetMobility(EComponentMobility::Static);
        SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SkyDome->SetCastShadow(false);
        SkyDome->SetCanEverAffectNavigation(false);
        SkyDome->RegisterComponent();
    }

    // A continuous stone surface avoids repeated cube seams across the dueling ground.
    // Only the original invisible boundary boxes and the floor affect movement.
    AddShape(CubeMesh, Edge, FVector(0.0, 0.0, -125.0), FVector(18000.0, 18000.0, 140.0));
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        // Low garden walls retain the platform without closing the view of the opponent.
        for (int32 Block = -2; Block <= 2; ++Block)
        {
            const double X = Block * 400.0;
            AddShape(CubeMesh, Edge, FVector(X, Side * 1000.0, 38.0), FVector(395.0, 105.0, 140.0));
            AddShape(CubeMesh, Floor, FVector(X, Side * 1000.0, 112.0), FVector(402.0, 120.0, 12.0));
        }
        AddShape(CubeMesh, Edge, FVector(Side * 1000.0, 0.0, -10.0), FVector(110.0, 1900.0, 82.0));
        AddShape(CubeMesh, Floor, FVector(Side * 1000.0, 0.0, 35.0), FVector(120.0, 1900.0, 12.0));
    }

    // The landmark is on the same X axis as the opening duel and its initial camera.
    BuildGate(1560.0f, Wood, DarkWood, Edge);
    for (int32 Step = 0; Step < 4; ++Step)
    {
        AddShape(CubeMesh, Floor, FVector(1080.0 + Step * 105.0, 0.0, -12.0 - Step * 12.0),
            FVector(110.0, 720.0 + Step * 25.0, 26.0));
    }
    AddShape(CubeMesh, Floor, FVector(1830.0, 0.0, -52.0), FVector(820.0, 780.0, 24.0));
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        BuildLantern(FVector(1310.0, Side * 590.0, -40.0), Edge, DarkWood, Paper);
        BuildLantern(FVector(-1020.0, Side * 800.0, 20.0), Edge, DarkWood, Paper);
        // A weathered timber screen behind the gate terminates the path at human scale.
        AddShape(CubeMesh, Cedar, FVector(2260.0, Side * 500.0, 120.0), FVector(22.0, 1000.0, 330.0));
        AddShape(CubeMesh, DarkWood, FVector(2260.0, Side * 500.0, 292.0), FVector(46.0, 1020.0, 24.0));
        for (int32 Post = 0; Post < 4; ++Post)
        {
            AddShape(CubeMesh, DarkWood, FVector(2245.0, Side * (80.0 + Post * 300.0), 120.0),
                FVector(45.0, 45.0, 340.0));
        }
    }

    // Populated after import_environment.py identifies the FBX object names.
    const TCHAR* RockPaths[] = {
        TEXT("/Game/Environment/Rocks/SM_Rock01.SM_Rock01"),
        TEXT("/Game/Environment/Rocks/SM_Rock02.SM_Rock02"),
        TEXT("/Game/Environment/Rocks/SM_Rock03.SM_Rock03"),
        TEXT("/Game/Environment/Rocks/SM_Rock04.SM_Rock04"),
        TEXT("/Game/Environment/Rocks/SM_Rock05.SM_Rock05"),
        TEXT("/Game/Environment/Rocks/SM_Rock06.SM_Rock06")};
    TArray<UStaticMesh*> Rocks;
    for (const TCHAR* Path : RockPaths)
    {
        if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path)) Rocks.Add(Mesh);
    }
    const TCHAR* FirPaths[] = {
        TEXT("/Game/Environment/Firs/SM_Fir01.SM_Fir01"),
        TEXT("/Game/Environment/Firs/SM_Fir02.SM_Fir02"),
        TEXT("/Game/Environment/Firs/SM_Fir03.SM_Fir03")};
    TArray<UStaticMesh*> Firs;
    for (const TCHAR* Path : FirPaths)
    {
        if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path)) Firs.Add(Mesh);
    }
    FRandomStream Random(1729);
    if (!Rocks.IsEmpty())
    {
        // Close scans provide readable chipped edges; the larger rear clusters overlap into a ridge.
        for (int32 Index = 0; Index < 28; ++Index)
        {
            const int32 Side = Index % 2 == 0 ? -1 : 1;
            const double X = Random.FRandRange(-1650.0f, 2550.0f);
            const double Y = Side * Random.FRandRange(1280.0f, 2100.0f);
            AddNaturalMesh(Rocks[Index % Rocks.Num()], FVector(X, Y, -58.0),
                Random.FRandRange(180.0f, 430.0f), false, Random.FRandRange(0.0f, 360.0f));
        }
        for (int32 Index = 0; Index < 11; ++Index)
        {
            AddNaturalMesh(Rocks[Index % Rocks.Num()],
                FVector(3100.0 + Random.FRandRange(-150.0f, 350.0f), -3600.0 + Index * 680.0, -110.0),
                Random.FRandRange(950.0f, 1600.0f), false, Random.FRandRange(0.0f, 360.0f));
        }
    }
    if (!Firs.IsEmpty())
    {
        for (int32 Index = 0; Index < 24; ++Index)
        {
            const int32 Side = Index % 2 == 0 ? -1 : 1;
            const double X = -1700.0 + (Index / 2) * 450.0;
            const double Y = Side * Random.FRandRange(1680.0f, 2900.0f);
            AddNaturalMesh(Firs[Index % Firs.Num()], FVector(X, Y, -65.0),
                Random.FRandRange(310.0f, 570.0f), true, Random.FRandRange(0.0f, 360.0f));
        }
        for (int32 Index = 0; Index < 9; ++Index)
        {
            AddNaturalMesh(Firs[Index % Firs.Num()], FVector(2900.0, -3000.0 + Index * 750.0, 180.0),
                Random.FRandRange(520.0f, 760.0f), true, Random.FRandRange(0.0f, 360.0f));
        }
    }
}

void ARoninArena::BuildGate(float X, UMaterialInterface* Wood, UMaterialInterface* DarkWood,
    UMaterialInterface* Stone)
{
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        AddShape(CylinderMesh, Stone, FVector(X, Side * 330.0, -5.0), FVector(115.0, 115.0, 100.0));
        AddShape(CylinderMesh, DarkWood, FVector(X, Side * 330.0, 66.0), FVector(68.0, 68.0, 76.0));
        AddShape(CylinderMesh, Wood, FVector(X, Side * 330.0, 295.0), FVector(58.0, 58.0, 470.0),
            FRotator(0.0, 0.0, Side * 2.0));
        AddShape(CubeMesh, DarkWood, FVector(X, Side * 450.0, 573.0), FVector(100.0, 190.0, 32.0),
            FRotator(0.0, 0.0, Side * 8.0));
    }
    AddShape(CubeMesh, Wood, FVector(X, 0.0, 445.0), FVector(44.0, 870.0, 32.0));
    AddShape(CubeMesh, Wood, FVector(X, 0.0, 535.0), FVector(70.0, 910.0, 38.0));
    AddShape(CubeMesh, DarkWood, FVector(X, 0.0, 559.0), FVector(100.0, 790.0, 26.0));
    AddShape(CubeMesh, Wood, FVector(X, 0.0, 488.0), FVector(42.0, 38.0, 90.0));
    AddShape(CubeMesh, DarkWood, FVector(X - 42.0, 0.0, 488.0), FVector(12.0, 74.0, 96.0));
}

void ARoninArena::BuildLantern(const FVector& Position, UMaterialInterface* Stone,
    UMaterialInterface* Wood, UMaterialInterface* Paper)
{
    // Small lanterns light the approach, leaving the opponent's silhouette uncluttered.
    AddShape(CubeMesh, Stone, Position + FVector(0.0, 0.0, 12.0), FVector(76.0, 76.0, 24.0));
    AddShape(CubeMesh, Stone, Position + FVector(0.0, 0.0, 62.0), FVector(28.0, 28.0, 85.0));
    AddShape(CubeMesh, Wood, Position + FVector(0.0, 0.0, 112.0), FVector(66.0, 66.0, 12.0));
    AddShape(CubeMesh, Paper, Position + FVector(0.0, 0.0, 147.0), FVector(39.0, 39.0, 57.0));
    for (int32 X = -1; X <= 1; X += 2)
    {
        for (int32 Y = -1; Y <= 1; Y += 2)
        {
            AddShape(CubeMesh, Wood, Position + FVector(X * 24.0, Y * 24.0, 147.0), FVector(5.0, 5.0, 65.0));
        }
    }
    AddShape(CubeMesh, Wood, Position + FVector(0.0, 0.0, 182.0), FVector(80.0, 80.0, 14.0));
    AddShape(CubeMesh, Wood, Position + FVector(0.0, 0.0, 193.0), FVector(66.0, 66.0, 10.0));

    UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
    AddInstanceComponent(Light);
    Light->SetupAttachment(GetRootComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetRelativeLocation(Position + FVector(-36.0, 0.0, 147.0));
    Light->SetLightColor(FLinearColor(1.0f, 0.55f, 0.27f));
    Light->SetIntensityUnits(ELightUnits::Lumens);
    Light->SetIntensity(260.0f);
    Light->SetAttenuationRadius(420.0f);
    Light->SetSourceRadius(12.0f);
    Light->SetCastShadows(false);
    Light->RegisterComponent();
}
