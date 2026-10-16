#include "RoninArena.h"

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
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

namespace
{
// Keep the approach level, then lift the forest into two flanking ridges.
// All scenery shares this height function so trunks stay planted on the terrain.
double NaturalHeight(double X, double Y)
{
    const double Blend = FMath::SmoothStep(2400.0, 6600.0, FMath::Max(FMath::Abs(X), FMath::Abs(Y)));
    const auto Hill = [X, Y](double CenterX, double CenterY, double Radius)
    {
        return FMath::Exp(-(FMath::Square(X-CenterX)+FMath::Square(Y-CenterY))/FMath::Square(Radius));
    };
    const double Height = 180.0 + 1100.0*Hill(6900.0, -4200.0, 3200.0)
        + 850.0*Hill(7500.0, 4800.0, 3000.0)
        + 80.0*FMath::Sin(X*.0011)*FMath::Cos(Y*.0013);
    return -55.0 + Blend*Height;
}

const FVector2D RestingStones(4200.0, -2700.0);
const FVector2D RidgeOverlook(6900.0, -3600.0);
const FVector2D LanternGrove(-3900.0, 3100.0);

double TerrainHeight(double X, double Y)
{
    double Height = NaturalHeight(X, Y);
    for (const FVector2D& Site : {RestingStones, RidgeOverlook, LanternGrove})
    {
        const double Distance = FVector2D::Distance(FVector2D(X, Y), Site);
        Height = FMath::Lerp(NaturalHeight(Site.X, Site.Y), Height,
            FMath::SmoothStep(380.0, 750.0, Distance));
    }
    // Continuous descents at both exits avoid a ledge that only jumping can cross.
    const double ExitX = FMath::Abs(X);
    if (ExitX >= 800.0 && ExitX <= 1600.0 && FMath::Abs(Y) <= 500.0)
    {
        const double Ramp = FMath::Lerp(0.0, -55.0, FMath::Clamp((ExitX-900.0)/550.0, 0.0, 1.0));
        Height = FMath::Lerp(Height, Ramp, 1.0-FMath::SmoothStep(380.0, 500.0, FMath::Abs(Y)));
    }
    // A visible steep escarpment bounds this finite valley. There are no
    // invisible courtyard walls, and the ground never ends beside a trail.
    Height += 2600.0 * FMath::SmoothStep(7600.0, 8900.0, FMath::Max(FMath::Abs(X), FMath::Abs(Y)));
    return Height;
}
}

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

    UDirectionalLightComponent* Key = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("CloudBreakLight"));
    Key->SetupAttachment(SceneRoot);
    Key->SetMobility(EComponentMobility::Movable);
    Key->SetRelativeRotation(FRotator(-28.0, -145.0, 0.0));
    Key->SetLightColor(FLinearColor(1.0f, 0.91f, 0.8f));
    Key->SetIntensity(2.8f);
    Key->SetCastShadows(true);
    Key->LightSourceAngle = 2.5f;

    SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("OvercastSkyLight"));
    SkyLight->SetupAttachment(SceneRoot);
    SkyLight->SetMobility(EComponentMobility::Movable);
    SkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
    SkyLight->SetIntensity(0.7f);
    SkyLight->SetLightColor(FLinearColor(0.82f, 0.9f, 1.0f));
    SkyLight->bLowerHemisphereIsBlack = true;

    UExponentialHeightFogComponent* Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("GardenMist"));
    Fog->SetupAttachment(SceneRoot);
    Fog->SetRelativeLocation(FVector(0.0, 0.0, -120.0));
    Fog->SetFogDensity(0.065f);
    Fog->SetFogHeightFalloff(0.12f);
    Fog->SetFogInscatteringColor(FLinearColor(0.22f, 0.28f, 0.31f));
    Fog->SetFogMaxOpacity(0.9f);
    Fog->SetStartDistance(1800.0f);
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
    UMaterialInterface* GardenGround = LoadSurface(TEXT("/Game/Environment/Materials/M_ForestGround.M_ForestGround"),
        FLinearColor(0.1f, 0.13f, 0.08f));
    Floor = TintSurface(Floor, FLinearColor(0.52f, 0.65f, 0.78f));
    Edge = TintSurface(Edge, FLinearColor(0.55f, 0.64f, 0.7f));
    GardenGround = TintSurface(GardenGround, FLinearColor(0.28f, 0.32f, 0.24f));
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
    // The courtyard and the surrounding terrain both support capsule movement.
    BuildTerrain(GardenGround);
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        // Low garden walls retain the platform without closing the view of the opponent.
        for (int32 Block = -2; Block <= 2; ++Block)
        {
            const double X = Block * 400.0;
            AddShape(CubeMesh, Edge, FVector(X, Side * 1000.0, 38.0), FVector(395.0, 105.0, 140.0));
            AddShape(CubeMesh, Floor, FVector(X, Side * 1000.0, 112.0), FVector(402.0, 120.0, 12.0));
        }
        // Open a broad east gate and a smaller west exit instead of fencing in the player.
        for (int32 Flank : {-1, 1})
        {
            AddShape(CubeMesh, Edge, FVector(Side * 1000.0, Flank * 700.0, -10.0), FVector(110.0, 500.0, 82.0));
            AddShape(CubeMesh, Floor, FVector(Side * 1000.0, Flank * 700.0, 35.0), FVector(120.0, 500.0, 12.0));
        }
    }

    // The landmark is on the same X axis as the opening duel and its initial camera.
    BuildGate(1560.0f, Wood, DarkWood, Edge);
    BuildTrail({FVector2D(900,0), FVector2D(1560,0), FVector2D(2350,0)}, 700.f, Floor);
    // A readable loop links three distinct stops and returns to the duel court.
    BuildTrail({FVector2D(2350,0), FVector2D(3100,-650), FVector2D(3400,-1900),
        RestingStones, FVector2D(5400,-2800), RidgeOverlook}, 300.f, Floor);
    BuildTrail({RidgeOverlook, FVector2D(6300,-1100), FVector2D(5300,2100),
        FVector2D(3400,3600), FVector2D(900,4100), LanternGrove,
        FVector2D(-4400,1000), FVector2D(-2600,-400), FVector2D(-1000,0)}, 280.f, Floor);
    BuildTrail({FVector2D(-1000,0), FVector2D(-1500,-900), FVector2D(-700,-2300),
        FVector2D(1800,-2700), RestingStones}, 240.f, Floor);
    BuildRestSite(RestingStones, Floor, Edge, DarkWood, Paper);
    BuildRestSite(RidgeOverlook, Floor, Edge, DarkWood, Paper);
    BuildRestSite(LanternGrove, Floor, Edge, DarkWood, Paper);
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        BuildLantern(FVector(1310.0, Side * 590.0, -40.0), Edge, DarkWood, Paper);
        BuildLantern(FVector(-1020.0, Side * 800.0, 20.0), Edge, DarkWood, Paper);
        // The flank screens leave the gate open to the garden, rather than filling its silhouette.
        AddShape(CubeMesh, Wood, FVector(2260.0, Side * 1050.0, 72.0), FVector(22.0, 880.0, 230.0));
        AddShape(CubeMesh, DarkWood, FVector(2260.0, Side * 1050.0, 192.0), FVector(46.0, 900.0, 20.0));
        for (int32 Post = 0; Post < 4; ++Post)
        {
            AddShape(CubeMesh, DarkWood, FVector(2245.0, Side * (620.0 + Post * 285.0), 76.0),
                FVector(38.0, 38.0, 240.0));
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
        // Close scans provide readable chipped edges, with space for vegetation between clusters.
        for (int32 Index = 0; Index < 28; ++Index)
        {
            const int32 Side = Index % 2 == 0 ? -1 : 1;
            const double X = Random.FRandRange(-1650.0f, 2550.0f);
            const double Y = Side * Random.FRandRange(1280.0f, 2100.0f);
            AddNaturalMesh(Rocks[Index % Rocks.Num()], FVector(X, Y, TerrainHeight(X,Y)-3.0),
                Random.FRandRange(120.0f, 280.0f), false, Random.FRandRange(0.0f, 360.0f));
        }
        for (int32 Index = 0; Index < 9; ++Index)
        {
            const double X = -6600.0 + Index * 1600.0;
            const double Y = Index % 2 == 0 ? -6500.0 : 6500.0;
            AddNaturalMesh(Rocks[Index % Rocks.Num()], FVector(X,Y,TerrainHeight(X,Y)-60.0),
                Random.FRandRange(650.0f, 1050.0f), false, Random.FRandRange(0.0f, 360.0f));
        }
    }
    if (!Firs.IsEmpty())
    {
        // Preserve the scan's sapling proportions and anchor it to the terrain.
        auto PlantSapling = [this, &Firs, &Random](int32 Index, const FVector& GroundPosition)
        {
            UStaticMesh* Fir = Firs[Index % Firs.Num()];
            const double SourceHeight = Fir->GetBounds().BoxExtent.Z * 2.0;
            const float Height = static_cast<float>(SourceHeight * Random.FRandRange(0.95f, 1.5f));
            const FVector Planted(GroundPosition.X,GroundPosition.Y,TerrainHeight(GroundPosition.X,GroundPosition.Y));
            AddNaturalMesh(Fir, Planted, Height, true, Random.FRandRange(0.0f, 360.0f));
        };
        for (int32 Index = 0; Index < 24; ++Index)
        {
            const int32 Side = Index % 2 == 0 ? -1 : 1;
            const double X = -1700.0 + (Index / 2) * 450.0;
            const double Y = Side * Random.FRandRange(1150.0f, 1750.0f);
            PlantSapling(Index, FVector(X, Y, 0.0));
        }
        for (int32 Index = 0; Index < 9; ++Index)
        {
            PlantSapling(Index, FVector(2900.0, -3000.0 + Index * 750.0, 0.0));
        }
        // Each destination has a small grove; clearings and path centers stay open.
        for (const FVector2D& Site : {RestingStones, RidgeOverlook, LanternGrove})
        {
            for (int32 Index = 0; Index < 12; ++Index)
            {
                const double Angle = Index * UE_TWO_PI / 12.0;
                const double Radius = Random.FRandRange(650.f, 1050.f);
                PlantSapling(Index, FVector(Site.X + FMath::Cos(Angle)*Radius,
                    Site.Y + FMath::Sin(Angle)*Radius, 0));
            }
        }
    }

    // A small number of full-size scans frame the gate. Keep their trunks and
    // foliage outside the playable floor and the initial line between fighters.
    const TCHAR* MediumFirPaths[] = {
        TEXT("/Game/Environment/FirsMedium/SM_FirMedium01.SM_FirMedium01"),
        TEXT("/Game/Environment/FirsMedium/SM_FirMedium02.SM_FirMedium02"),
        TEXT("/Game/Environment/FirsMedium/SM_FirMedium03.SM_FirMedium03")};
    TArray<UStaticMesh*> MediumFirs;
    for (const TCHAR* Path : MediumFirPaths)
        if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path)) MediumFirs.Add(Mesh);
    if (!MediumFirs.IsEmpty())
    {
        struct FTreePlacement { FVector Ground; float Height; float Yaw; };
        const FTreePlacement Trees[] = {
            {FVector(1650.0, -1650.0, -55.0), 900.f, 37.f},
            {FVector(2250.0, 1750.0, -55.0), 930.f, 142.f},
            {FVector(3500.0, -1200.0, -55.0), 830.f, 252.f},
            {FVector(4100.0, 1250.0, -55.0), 780.f, 73.f},
            {FVector(5050.0, 100.0, -55.0), 680.f, 188.f},
            {FVector(5900.0, -2400.0, -55.0), 890.f, 317.f},
            {FVector(6250.0, 2300.0, -55.0), 900.f, 118.f},
        };
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Trees); ++Index)
        {
            FVector Ground = Trees[Index].Ground;
            Ground.Z = TerrainHeight(Ground.X,Ground.Y);
            AddNaturalMesh(MediumFirs[Index % MediumFirs.Num()], Ground,
                Trees[Index].Height, true, Trees[Index].Yaw);
        }
    }
}

void ARoninArena::BuildTerrain(UMaterialInterface* Material)
{
    constexpr int32 Cells = 128;
    constexpr double HalfSize = 9000.0;
    constexpr double Step = 2.0*HalfSize/Cells;
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents;
    TArray<int32> Triangles;
    for (int32 Y=0; Y<=Cells; ++Y)
    {
        for (int32 X=0; X<=Cells; ++X)
        {
            const double WX = X*Step-HalfSize, WY = Y*Step-HalfSize;
            const double DX = (TerrainHeight(WX+10,WY)-TerrainHeight(WX-10,WY))/20.0;
            const double DY = (TerrainHeight(WX,WY+10)-TerrainHeight(WX,WY-10))/20.0;
            Vertices.Add(FVector(WX,WY,TerrainHeight(WX,WY)));
            Normals.Add(FVector(-DX,-DY,1).GetSafeNormal());
            Tangents.Add(FProcMeshTangent(FVector(1,0,DX).GetSafeNormal(),false));
            UVs.Add(FVector2D(WX/200.0,WY/200.0));
            if (X<Cells && Y<Cells)
            {
                const int32 A=Y*(Cells+1)+X, B=A+1, C=A+Cells+1, D=C+1;
                Triangles.Append({A,C,B,B,C,D});
            }
        }
    }
    UProceduralMeshComponent* Terrain = NewObject<UProceduralMeshComponent>(this,TEXT("ForestTerrain"));
    AddInstanceComponent(Terrain);
    Terrain->SetupAttachment(GetRootComponent());
    Terrain->SetMobility(EComponentMobility::Static);
    // Cook synchronously so the ground is ready before play can start. Procedural
    // mesh collision is double sided and uses the same triangles as the surface.
    Terrain->bUseAsyncCooking = false;
    Terrain->bUseComplexAsSimpleCollision = true;
    Terrain->SetCollisionProfileName(TEXT("BlockAll"));
    Terrain->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    Terrain->SetGenerateOverlapEvents(false);
    Terrain->SetCanEverAffectNavigation(true);
    Terrain->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UVs,{},Tangents,true);
    Terrain->SetMaterial(0,Material);
    Terrain->RegisterComponent();
}

void ARoninArena::BuildTrail(const TArray<FVector2D>& Points, float Width, UMaterialInterface* Material)
{
    TArray<FVector> Vertices, Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents;
    TArray<int32> Triangles;
    double DistanceAlong = 0.0;
    for (int32 Segment = 1; Segment < Points.Num(); ++Segment)
    {
        const FVector2D Delta = Points[Segment] - Points[Segment-1];
        const double Length = Delta.Size();
        const FVector2D Direction = Delta.GetSafeNormal();
        const FVector2D Across(-Direction.Y, Direction.X);
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(Length / 120.0));
        const int32 Start = Vertices.Num();
        for (int32 Step = 0; Step <= Steps; ++Step)
        {
            const double Fraction = static_cast<double>(Step) / Steps;
            const FVector2D Center = Points[Segment-1] + Delta * Fraction;
            for (int32 Side : {-1, 1})
            {
                const FVector2D XY = Center + Across * (Width * .5 * Side);
                const double DX = (TerrainHeight(XY.X+10,XY.Y)-TerrainHeight(XY.X-10,XY.Y))/20.0;
                const double DY = (TerrainHeight(XY.X,XY.Y+10)-TerrainHeight(XY.X,XY.Y-10))/20.0;
                Vertices.Add(FVector(XY.X,XY.Y,TerrainHeight(XY.X,XY.Y)+2.5));
                Normals.Add(FVector(-DX,-DY,1).GetSafeNormal());
                Tangents.Add(FProcMeshTangent(FVector(Direction.X,Direction.Y,DX*Direction.X+DY*Direction.Y).GetSafeNormal(),false));
                UVs.Add(FVector2D((DistanceAlong+Length*Fraction)/200.0, Side < 0 ? 0.0 : Width/200.0));
            }
            if (Step < Steps)
            {
                const int32 A = Start + Step*2;
                Triangles.Append({A,A+1,A+2,A+1,A+3,A+2});
            }
        }
        DistanceAlong += Length;
    }
    UProceduralMeshComponent* Trail = NewObject<UProceduralMeshComponent>(this);
    AddInstanceComponent(Trail);
    Trail->SetupAttachment(GetRootComponent());
    Trail->SetMobility(EComponentMobility::Static);
    Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision); // Terrain directly below supports the route.
    Trail->SetCanEverAffectNavigation(false);
    Trail->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UVs,{},Tangents,false);
    Trail->SetMaterial(0,Material);
    Trail->RegisterComponent();
}

void ARoninArena::BuildRestSite(const FVector2D& Center, UMaterialInterface* Floor,
    UMaterialInterface* Stone, UMaterialInterface* Wood, UMaterialInterface* Paper)
{
    const double Z = TerrainHeight(Center.X, Center.Y);
    BuildTrail({Center-FVector2D(370,0), Center+FVector2D(370,0)}, 700.f, Floor);
    for (int32 Side : {-1, 1})
    {
        BuildLantern(FVector(Center.X-280, Center.Y+Side*330, Z), Stone, Wood, Paper);
        // Low cedar resting benches leave the central route clear.
        AddShape(CubeMesh, Wood, FVector(Center.X+130,Center.Y+Side*310,Z+44), FVector(260,48,14));
        for (int32 Leg : {-1,1})
            AddShape(CubeMesh, Stone, FVector(Center.X+130+Leg*95,Center.Y+Side*310,Z+20), FVector(28,38,40));
    }
    // Three upright weathered stones mark each clearing without a screen or menu.
    for (int32 Index = 0; Index < 3; ++Index)
    {
        const double Height = 100.0 + Index*34.0;
        AddShape(CubeMesh, Stone, FVector(Center.X+420,Center.Y+(Index-1)*130,Z+Height*.5),
            FVector(55,95,Height), FRotator(0,Index*11,0));
    }
}

void ARoninArena::BuildGate(float X, UMaterialInterface* Wood, UMaterialInterface* DarkWood,
    UMaterialInterface* Stone)
{
    if (UStaticMesh* Gate = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/Environment/Torii/SM_ToriiGate.SM_ToriiGate")))
    {
        AddGeometry(Gate,nullptr,FTransform(FRotator(0,90,0),FVector(X,0,-55.0)));
        return;
    }
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
