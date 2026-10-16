#include "RoninArena.h"

#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Scene.h"
#include "Engine/StaticMesh.h"
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
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CubeMesh = Cube.Object;
    CylinderMesh = Cylinder.Object;
    ConeMesh = Cone.Object;
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

    UDirectionalLightComponent* Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moonlight"));
    Moon->SetupAttachment(SceneRoot);
    Moon->SetMobility(EComponentMobility::Movable);
    Moon->SetRelativeRotation(FRotator(-36.0, -38.0, 0.0));
    Moon->SetLightColor(FLinearColor(0.57f, 0.72f, 1.0f));
    Moon->SetIntensity(5.0f);
    Moon->SetCastShadows(true);
    Moon->bAtmosphereSunLight = true;
    Moon->LightSourceAngle = 1.4f;

    // A soft opposing light keeps swords and enemy silhouettes readable during orbiting.
    UDirectionalLightComponent* Fill = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("CourtyardFill"));
    Fill->SetupAttachment(SceneRoot);
    Fill->SetMobility(EComponentMobility::Movable);
    Fill->SetRelativeRotation(FRotator(-48.0, 142.0, 0.0));
    Fill->SetLightColor(FLinearColor(0.38f, 0.52f, 0.76f));
    Fill->SetIntensity(1.75f);
    Fill->SetCastShadows(false);

    USkyAtmosphereComponent* Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("NightAtmosphere"));
    Atmosphere->SetupAttachment(SceneRoot);

    SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("NightSkyLight"));
    SkyLight->SetupAttachment(SceneRoot);
    SkyLight->SetMobility(EComponentMobility::Movable);
    SkyLight->SetIntensity(0.8f);
    SkyLight->SetLightColor(FLinearColor(0.45f, 0.60f, 0.85f));
    SkyLight->bLowerHemisphereIsBlack = false;

    UExponentialHeightFogComponent* Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("ValleyMist"));
    Fog->SetupAttachment(SceneRoot);
    Fog->SetRelativeLocation(FVector(0.0, 0.0, -180.0));
    Fog->SetFogDensity(0.012f);
    Fog->SetFogHeightFalloff(0.22f);
    Fog->SetFogInscatteringColor(FLinearColor(0.018f, 0.036f, 0.065f));
    Fog->SetFogMaxOpacity(0.7f);
    Fog->SetStartDistance(1400.0f);
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
    Grade->Settings.BloomIntensity = 0.25f;
    Grade->Settings.bOverride_VignetteIntensity = true;
    Grade->Settings.VignetteIntensity = 0.24f;
    Grade->Settings.bOverride_MotionBlurAmount = true;
    Grade->Settings.MotionBlurAmount = 0.0f;
    Grade->Settings.bOverride_SceneFringeIntensity = true;
    Grade->Settings.SceneFringeIntensity = 0.0f;
}

void ARoninArena::BeginPlay()
{
    Super::BeginPlay();

    // The bootstrap material guarantees the named color/roughness parameters in packaged
    // builds. Engine basic-shape material remains a useful fallback before bootstrap runs.
    if (UMaterialInterface* CombatMaterial = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Materials/M_Combat.M_Combat")))
    {
        ShapeMaterial = CombatMaterial;
    }
    BuildCourtyard();

    // Registration happens after the one-time construction so no frame rebuilds the scenery.
    for (UInstancedStaticMeshComponent* Batch : GeometryBatches)
    {
        Batch->RegisterComponent();
    }
    SkyLight->RecaptureSky();
}

UMaterialInstanceDynamic* ARoninArena::MakeMaterial(const FLinearColor& Color, float Roughness)
{
    if (!ShapeMaterial)
    {
        return nullptr;
    }
    UMaterialInstanceDynamic* Result = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
    Result->SetVectorParameterValue(TEXT("Color"), Color);
    Result->SetScalarParameterValue(TEXT("Roughness"), Roughness);
    Result->SetScalarParameterValue(TEXT("Metallic"), 0.0f);
    Materials.Add(Result);
    return Result;
}

void ARoninArena::AddShape(UStaticMesh* Mesh, UMaterialInterface* Material,
    const FVector& Position, const FVector& Size, const FRotator& Rotation)
{
    if (!Mesh)
    {
        return;
    }

    UInstancedStaticMeshComponent* Batch = nullptr;
    for (UInstancedStaticMeshComponent* Candidate : GeometryBatches)
    {
        if (Candidate->GetStaticMesh() == Mesh && Candidate->GetMaterial(0) == Material)
        {
            Batch = Candidate;
            break;
        }
    }
    if (!Batch)
    {
        Batch = NewObject<UInstancedStaticMeshComponent>(this);
        AddInstanceComponent(Batch);
        Batch->SetupAttachment(GetRootComponent());
        Batch->SetStaticMesh(Mesh);
        Batch->SetMaterial(0, Material);
        Batch->SetMobility(EComponentMobility::Static);
        Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Batch->SetGenerateOverlapEvents(false);
        Batch->SetCanEverAffectNavigation(false);
        GeometryBatches.Add(Batch);
    }

    // Engine basic meshes have a 100 cm bounding box; Size is expressed in centimeters.
    Batch->AddInstance(FTransform(Rotation, Position, Size / 100.0), false);
}

void ARoninArena::AddBeam(UMaterialInterface* Material, const FVector& Start,
    const FVector& End, float Width)
{
    const FVector Direction = End - Start;
    AddShape(CubeMesh, Material, (Start + End) * 0.5,
        FVector(Direction.Size(), Width, Width), Direction.Rotation());
}

void ARoninArena::BuildCourtyard()
{
    UMaterialInstanceDynamic* Grout = MakeMaterial(FLinearColor(0.018f, 0.026f, 0.034f), 0.85f);
    UMaterialInstanceDynamic* Stone = MakeMaterial(FLinearColor(0.16f, 0.20f, 0.23f), 0.63f);
    UMaterialInstanceDynamic* Edge = MakeMaterial(FLinearColor(0.085f, 0.105f, 0.13f), 0.7f);
    UMaterialInstanceDynamic* Wood = MakeMaterial(FLinearColor(0.26f, 0.032f, 0.024f), 0.47f);
    UMaterialInstanceDynamic* Metal = MakeMaterial(FLinearColor(0.028f, 0.035f, 0.047f), 0.27f);
    UMaterialInstanceDynamic* Brass = MakeMaterial(FLinearColor(0.42f, 0.24f, 0.085f), 0.38f);
    UMaterialInstanceDynamic* Bark = MakeMaterial(FLinearColor(0.075f, 0.044f, 0.036f), 0.92f);
    UMaterialInstanceDynamic* Leaves = MakeMaterial(FLinearColor(0.37f, 0.025f, 0.035f), 0.87f);
    UMaterialInstanceDynamic* LeafLight = MakeMaterial(FLinearColor(0.68f, 0.085f, 0.036f), 0.82f);
    UMaterialInstanceDynamic* Paper = MakeMaterial(FLinearColor(1.0f, 0.63f, 0.25f), 0.45f);
    UMaterialInstanceDynamic* Mountains = MakeMaterial(FLinearColor(0.021f, 0.034f, 0.055f), 1.0f);
    if (Metal)
    {
        Metal->SetScalarParameterValue(TEXT("Metallic"), 0.6f);
    }
    if (Brass)
    {
        Brass->SetScalarParameterValue(TEXT("Metallic"), 0.72f);
    }

    UMaterialInstanceDynamic* Tiles[4];
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const float Variation = 0.008f * Index;
        Tiles[Index] = MakeMaterial(FLinearColor(0.11f + Variation, 0.145f + Variation,
            0.17f + Variation), 0.34f + 0.045f * Index);
    }

    TArray<UStaticMeshComponent*> MeshComponents;
    GetComponents<UStaticMeshComponent>(MeshComponents);
    for (UStaticMeshComponent* Component : MeshComponents)
    {
        if (Component->GetFName() == TEXT("CollisionFloor"))
        {
            Component->SetMaterial(0, Grout);
        }
    }

    // Thin noncolliding pavers leave a flat, uninterrupted movement plane below them.
    FRandomStream Random(1729);
    for (int32 X = -6; X <= 6; ++X)
    {
        for (int32 Y = -6; Y <= 6; ++Y)
        {
            AddShape(CubeMesh, Tiles[Random.RandRange(0, 3)], FVector(X * 142.0, Y * 142.0, -1.0),
                FVector(137.0, 137.0, 3.0));
        }
    }

    // A restrained broken brass ring marks the combat space without hiding feet or telegraphs.
    for (int32 Segment = 0; Segment < 48; ++Segment)
    {
        if (Segment % 12 < 2)
        {
            continue;
        }
        const double Angle = Segment * 2.0 * PI / 48.0;
        AddShape(CubeMesh, Brass, FVector(FMath::Cos(Angle) * 625.0,
            FMath::Sin(Angle) * 625.0, 0.9), FVector(77.0, 4.0, 0.6),
            FRotator(0.0, FMath::RadiansToDegrees(Angle) + 90.0, 0.0));
    }

    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        AddShape(CubeMesh, Edge, FVector(Side * 1000.0, 0.0, -13.0), FVector(100.0, 2100.0, 90.0));
        AddShape(CubeMesh, Edge, FVector(0.0, Side * 1000.0, -13.0), FVector(1900.0, 100.0, 90.0));
        AddShape(CubeMesh, Stone, FVector(Side * 1000.0, 0.0, 36.0), FVector(108.0, 2100.0, 12.0));
        AddShape(CubeMesh, Stone, FVector(0.0, Side * 1000.0, 36.0), FVector(1900.0, 108.0, 12.0));

        // Waist-high side rails frame the courtyard; the gates retain an open central axis.
        for (int32 Post = -3; Post <= 3; ++Post)
        {
            AddShape(CubeMesh, Stone, FVector(Side * 1035.0, Post * 300.0, 107.0), FVector(47.0, 47.0, 150.0));
            AddShape(CubeMesh, Metal, FVector(Side * 1035.0, Post * 300.0, 189.0), FVector(62.0, 62.0, 14.0));
        }
        AddShape(CubeMesh, Wood, FVector(Side * 1035.0, 0.0, 153.0), FVector(24.0, 1860.0, 24.0));
        AddShape(CubeMesh, Wood, FVector(Side * 1035.0, 0.0, 94.0), FVector(18.0, 1860.0, 18.0));

        BuildGate(Side * 1350.0f, Wood, Metal, Stone);
        for (int32 Step = 0; Step < 4; ++Step)
        {
            AddShape(CubeMesh, Stone, FVector(0.0, Side * (1070.0 + Step * 95.0), -5.0 - Step * 15.0),
                FVector(700.0 + Step * 35.0, 100.0, 26.0));
        }
        for (int32 Lantern = -1; Lantern <= 1; ++Lantern)
        {
            BuildLantern(FVector(Side * 1100.0, Lantern * 800.0, 35.0), Stone, Metal, Paper);
        }
    }

    BuildTree(FVector(-1510.0, 700.0, -40.0), 1.05f, 23, Bark, Leaves, LeafLight);
    BuildTree(FVector(1480.0, -700.0, -40.0), 1.15f, 39, Bark, Leaves, LeafLight);
    BuildTree(FVector(-1380.0, -1430.0, -40.0), 0.85f, 71, Bark, Leaves, LeafLight);
    BuildTree(FVector(1460.0, 1450.0, -40.0), 0.95f, 91, Bark, Leaves, LeafLight);

    // Leaves at the margins and tiny chips add scale while the middle stays visually quiet.
    for (int32 Leaf = 0; Leaf < 90; ++Leaf)
    {
        const float X = Random.FRandRange(-910.0f, 910.0f);
        const float Y = Random.FRandRange(-910.0f, 910.0f);
        if (FMath::Abs(X) < 620.0f && FMath::Abs(Y) < 620.0f)
        {
            continue;
        }
        AddShape(CubeMesh, Leaf % 3 == 0 ? LeafLight : Leaves, FVector(X, Y, 1.3),
            FVector(Random.FRandRange(6.0f, 12.0f), Random.FRandRange(3.0f, 6.0f), 0.6),
            FRotator(0.0, Random.FRandRange(0.0f, 180.0f), 0.0));
    }

    AddShape(CubeMesh, Mountains, FVector(0.0, 0.0, -240.0), FVector(16000.0, 16000.0, 100.0));
    for (int32 Peak = 0; Peak < 16; ++Peak)
    {
        const double Angle = Peak * 2.0 * PI / 16.0;
        const float Height = Random.FRandRange(1700.0f, 3500.0f);
        AddShape(ConeMesh, Mountains, FVector(FMath::Cos(Angle) * 6000.0,
            FMath::Sin(Angle) * 6000.0, Height * 0.5 - 300.0),
            FVector(Random.FRandRange(2700.0f, 4000.0f), 2800.0, Height),
            FRotator(0.0, Peak * 31.0, 0.0));
    }

    // A distant shrine silhouette gives the northern gate a focal point.
    AddShape(CubeMesh, Edge, FVector(0.0, 2520.0, -40.0), FVector(970.0, 760.0, 180.0));
    AddShape(CubeMesh, Bark, FVector(0.0, 2520.0, 230.0), FVector(640.0, 500.0, 420.0));
    for (int32 Roof = 0; Roof < 3; ++Roof)
    {
        AddShape(CubeMesh, Metal, FVector(0.0, 2520.0, 470.0 + Roof * 70.0),
            FVector(900.0 - Roof * 150.0, 720.0 - Roof * 115.0, 75.0));
    }
    AddShape(CubeMesh, Brass, FVector(0.0, 2260.0, 260.0), FVector(18.0, 8.0, 300.0));
}

void ARoninArena::BuildGate(float Y, UMaterialInterface* Wood, UMaterialInterface* Metal,
    UMaterialInterface* Stone)
{
    for (int32 Side = -1; Side <= 1; Side += 2)
    {
        AddShape(CubeMesh, Stone, FVector(Side * 395.0, Y, -1.0), FVector(160.0, 150.0, 90.0));
        AddShape(CylinderMesh, Metal, FVector(Side * 395.0, Y, 82.0), FVector(96.0, 96.0, 100.0));
        AddShape(CylinderMesh, Wood, FVector(Side * 395.0, Y, 350.0), FVector(74.0, 74.0, 580.0));
        AddShape(CylinderMesh, Metal, FVector(Side * 395.0, Y, 616.0), FVector(81.0, 81.0, 28.0));
        AddShape(CubeMesh, Metal, FVector(Side * 575.0, Y, 708.0), FVector(220.0, 115.0, 46.0),
            FRotator(Side * -10.0, 0.0, 0.0));
    }
    AddShape(CubeMesh, Wood, FVector(0.0, Y, 565.0), FVector(1000.0, 48.0, 40.0));
    AddShape(CubeMesh, Wood, FVector(0.0, Y, 655.0), FVector(1120.0, 78.0, 48.0));
    AddShape(CubeMesh, Metal, FVector(0.0, Y, 692.0), FVector(1060.0, 115.0, 40.0));
    AddShape(CubeMesh, Wood, FVector(0.0, Y, 602.0), FVector(48.0, 45.0, 94.0));
    AddShape(CubeMesh, Metal, FVector(0.0, Y - 48.0, 603.0), FVector(88.0, 14.0, 108.0));
}

void ARoninArena::BuildTree(const FVector& Position, float Scale, int32 Seed,
    UMaterialInterface* Bark, UMaterialInterface* Leaf, UMaterialInterface* LeafLight)
{
    FRandomStream Random(Seed);
    const FVector Fork = Position + FVector(38.0, -12.0, 420.0) * Scale;
    AddBeam(Bark, Position, Fork, 48.0f * Scale);
    AddBeam(Bark, Position + FVector(-30.0, 10.0, 0.0) * Scale,
        Position + FVector(12.0, -7.0, 220.0) * Scale, 58.0f * Scale);
    for (int32 Branch = 0; Branch < 7; ++Branch)
    {
        const double Angle = Branch * 2.0 * PI / 7.0 + Random.FRandRange(-0.25f, 0.25f);
        const float Reach = Random.FRandRange(180.0f, 350.0f) * Scale;
        const FVector Tip = Fork + FVector(FMath::Cos(Angle) * Reach,
            FMath::Sin(Angle) * Reach, Random.FRandRange(65.0f, 180.0f) * Scale);
        AddBeam(Bark, Fork - FVector(0.0, 0.0, Branch * 17.0 * Scale), Tip, 20.0f * Scale);
        AddShape(SphereMesh, Branch % 3 == 0 ? LeafLight : Leaf, Tip + FVector(0.0, 0.0, 50.0 * Scale),
            FVector(310.0, 260.0, 160.0) * Scale,
            FRotator(Random.FRandRange(-12.0f, 12.0f), Branch * 51.0, 0.0));
        AddShape(SphereMesh, Leaf, Tip + FVector(50.0, -20.0, 105.0) * Scale,
            FVector(200.0, 220.0, 140.0) * Scale);
    }
}

void ARoninArena::BuildLantern(const FVector& Position, UMaterialInterface* Stone,
    UMaterialInterface* Metal, UMaterialInterface* Paper)
{
    AddShape(CubeMesh, Stone, Position + FVector(0.0, 0.0, 15.0), FVector(110.0, 110.0, 30.0));
    AddShape(CubeMesh, Stone, Position + FVector(0.0, 0.0, 95.0), FVector(45.0, 45.0, 140.0));
    AddShape(CubeMesh, Metal, Position + FVector(0.0, 0.0, 174.0), FVector(105.0, 105.0, 18.0));
    AddShape(CubeMesh, Paper, Position + FVector(0.0, 0.0, 224.0), FVector(70.0, 70.0, 84.0));
    for (int32 X = -1; X <= 1; X += 2)
    {
        for (int32 Y = -1; Y <= 1; Y += 2)
        {
            AddShape(CubeMesh, Metal, Position + FVector(X * 38.0, Y * 38.0, 224.0), FVector(8.0, 8.0, 92.0));
        }
    }
    AddShape(CubeMesh, Metal, Position + FVector(0.0, 0.0, 278.0), FVector(128.0, 128.0, 25.0));
    AddShape(ConeMesh, Metal, Position + FVector(0.0, 0.0, 310.0), FVector(125.0, 125.0, 52.0));

    UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
    AddInstanceComponent(Light);
    Light->SetupAttachment(GetRootComponent());
    Light->SetMobility(EComponentMobility::Movable);
    // The light sits just outside the inward paper face, so that opaque engine material
    // receives warm light as well as the paving. It does not require an emissive asset.
    Light->SetRelativeLocation(Position + FVector(Position.X > 0.0 ? -70.0 : 70.0, 0.0, 235.0));
    Light->SetLightColor(FLinearColor(1.0f, 0.42f, 0.13f));
    Light->SetIntensityUnits(ELightUnits::Lumens);
    Light->SetIntensity(1250.0f);
    Light->SetAttenuationRadius(620.0f);
    Light->SetSourceRadius(18.0f);
    Light->SetCastShadows(false);
    Light->RegisterComponent();
}
