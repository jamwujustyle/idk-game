#include "RoninFighter.h"
#include "RoninGameMode.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

ARoninFighter::ARoninFighter()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->MaxWalkSpeed = 390.f;
    GetCharacterMovement()->MaxAcceleration = 2200.f;
    GetCharacterMovement()->BrakingDecelerationWalking = 2200.f;
    GetCharacterMovement()->GroundFriction = 8.f;
    GetCharacterMovement()->bRunPhysicsWithNoController = true;

    Body = CreateDefaultSubobject<USceneComponent>(TEXT("Body"));
    Body->SetupAttachment(RootComponent);
    Body->SetRelativeLocation(FVector(0, 0, -88));
    constexpr auto Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
    constexpr auto Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
    constexpr auto Cone = TEXT("/Engine/BasicShapes/Cone.Cone");
    constexpr auto Cylinder = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
    ArmorParts.Add(MakePart(TEXT("Torso"), Body, Cube, FVector(0,0,111), FVector(.34f,.54f,.55f)));
    ArmorParts.Add(MakePart(TEXT("Waist"), Body, Cube, FVector(0,0,78), FVector(.34f,.54f,.12f)));
    ArmorParts.Add(MakePart(TEXT("Head"), Body, Sphere, FVector(0,0,157), FVector(.29f,.29f,.32f)));
    ArmorParts.Add(MakePart(TEXT("Hat"), Body, Cone, FVector(0,0,174), FVector(.8f,.8f,.2f)));
    MetalParts.Add(MakePart(TEXT("Faceplate"), Body, Cube, FVector(15,0,153), FVector(.045f,.24f,.13f)));
    for (int32 I = 0; I < 4; ++I)
    {
        auto* Plate = MakePart(*FString::Printf(TEXT("ChestPlate%d"), I), Body, Cube,
            FVector(19,0,92+I*12), FVector(.04f,.56f,.065f));
        MetalParts.Add(Plate);
    }
    LeftLeg = MakePart(TEXT("LeftLeg"), Body, Cube, FVector(0,-16,36), FVector(.23f,.25f,.65f));
    RightLeg = MakePart(TEXT("RightLeg"), Body, Cube, FVector(0,16,36), FVector(.23f,.25f,.65f));
    ArmorParts.Add(LeftLeg); ArmorParts.Add(RightLeg);
    ArmorParts.Add(MakePart(TEXT("LeftBoot"), LeftLeg, Cube, FVector(25,0,-40), FVector(1.45f,1.f,.22f)));
    ArmorParts.Add(MakePart(TEXT("RightBoot"), RightLeg, Cube, FVector(25,0,-40), FVector(1.45f,1.f,.22f)));
    LeftArm = MakePart(TEXT("LeftArm"), Body, Cube, FVector(6,-36,109), FVector(.22f,.22f,.5f));
    RightArm = MakePart(TEXT("RightArm"), Body, Cube, FVector(7,36,109), FVector(.22f,.22f,.5f));
    ArmorParts.Add(LeftArm); ArmorParts.Add(RightArm);
    LeftForearm = MakePart(TEXT("LeftForearm"), Body, Cube, FVector::ZeroVector, FVector(.18f,.18f,.32f));
    RightForearm = MakePart(TEXT("RightForearm"), Body, Cube, FVector::ZeroVector, FVector(.18f,.18f,.32f));
    LeftHand = MakePart(TEXT("LeftHand"), Body, Sphere, FVector::ZeroVector, FVector(.11f));
    RightHand = MakePart(TEXT("RightHand"), Body, Sphere, FVector::ZeroVector, FVector(.11f));
    ArmorParts.Add(LeftForearm); ArmorParts.Add(RightForearm);
    ArmorParts.Add(LeftHand); ArmorParts.Add(RightHand);
    ArmorParts.Add(MakePart(TEXT("LeftShoulder"), Body, Cube, FVector(0,-35,132), FVector(.38f,.28f,.16f)));
    ArmorParts.Add(MakePart(TEXT("RightShoulder"), Body, Cube, FVector(0,35,132), FVector(.38f,.28f,.16f)));
    auto* Skirt = MakePart(TEXT("ArmorSkirt"), Body, Cone, FVector(0,0,73), FVector(.67f,.67f,.35f));
    ArmorParts.Add(Skirt);
    SwordPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SwordPivot"));
    SwordPivot->SetupAttachment(Body);
    SwordPivot->SetRelativeLocation(FVector(34,8,116));
    SwordPivot->SetRelativeRotation(FRotator(-18,-20,0));
    Sword = MakePart(TEXT("Blade"), SwordPivot, Cube, FVector(62,0,0), FVector(1.05f,.035f,.065f));
    MetalParts.Add(Sword);
    MetalParts.Add(MakePart(TEXT("SwordTip"), SwordPivot, Cone, FVector(117,0,0), FVector(.06f,.06f,.14f)));
    MetalParts.Last()->SetRelativeRotation(FRotator(-90,0,0));
    MetalParts.Add(MakePart(TEXT("SwordGuard"), SwordPivot, Cylinder, FVector(6,0,0), FVector(.15f,.15f,.025f)));
    MetalParts.Last()->SetRelativeRotation(FRotator(90,0,0));
    ArmorParts.Add(MakePart(TEXT("SwordGrip"), SwordPivot, Cylinder, FVector(-5,0,0), FVector(.055f,.055f,.22f)));
    ArmorParts.Last()->SetRelativeRotation(FRotator(90,0,0));
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->TargetArmLength = 460.f;
    CameraBoom->SocketOffset = FVector(0,55,80);
    CameraBoom->bUsePawnControlRotation = true;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 14.f;
    CameraBoom->ProbeSize = 14.f;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->FieldOfView = 82.f;
    Camera->bConstrainAspectRatio = false;
    UpdateArms();
}

UStaticMeshComponent* ARoninFighter::MakePart(FName Name, USceneComponent* Parent,
    const TCHAR* MeshPath, FVector Location, FVector Scale)
{
    auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
    Part->SetupAttachment(Parent);
    ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(MeshPath);
    Part->SetStaticMesh(Mesh.Object);
    Part->SetRelativeLocation(Location);
    Part->SetRelativeScale3D(Scale);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetGenerateOverlapEvents(false);
    return Part;
}

void ARoninFighter::UpdateArms()
{
    // All limb coordinates are local to Body. Two-segment arms keep both hands attached
    // to the hilt as the sword changes stance, winds up and follows through.
    const FTransform GripTransform = SwordPivot->GetRelativeTransform();
    const FVector RightGrip = GripTransform.TransformPosition(FVector(0,0,0));
    const FVector LeftGrip = GripTransform.TransformPosition(FVector(-12,0,0));

    auto PoseSegment = [](UStaticMeshComponent* Part, const FVector& Start,
        const FVector& End, float Thickness)
    {
        const FVector Direction = End - Start;
        Part->SetRelativeLocation((Start + End) * 0.5);
        Part->SetRelativeRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
        Part->SetRelativeScale3D(FVector(Thickness / 100.0, Thickness / 100.0,
            Direction.Size() / 100.0));
    };
    auto PoseArm = [&PoseSegment](UStaticMeshComponent* Upper, UStaticMeshComponent* Forearm,
        UStaticMeshComponent* Hand, const FVector& Shoulder, const FVector& Grip, float Side)
    {
        const FVector Offset = Grip - Shoulder;
        const double Distance = FMath::Max(Offset.Size(), 0.01);
        const FVector Direction = Offset / Distance;
        const double UpperLength = FMath::Max(32.0, Distance * 0.52);
        const double LowerLength = FMath::Max(34.0, Distance * 0.52);
        const double Along = (UpperLength * UpperLength - LowerLength * LowerLength
            + Distance * Distance) / (2.0 * Distance);
        const double BendHeight = FMath::Sqrt(FMath::Max(0.0, UpperLength * UpperLength - Along * Along));
        const FVector Outward(0.0, Side, -0.35);
        const FVector Bend = (Outward - Direction * FVector::DotProduct(Outward, Direction)).GetSafeNormal();
        const FVector Elbow = Shoulder + Direction * Along + Bend * BendHeight;
        PoseSegment(Upper, Shoulder, Elbow, 20.f);
        PoseSegment(Forearm, Elbow, Grip, 17.f);
        Hand->SetRelativeLocation(Grip);
    };
    PoseArm(RightArm, RightForearm, RightHand, FVector(6,31,132), RightGrip, 1.f);
    PoseArm(LeftArm, LeftForearm, LeftHand, FVector(6,-31,132), LeftGrip, -1.f);
}

void ARoninFighter::BeginPlay()
{
    Super::BeginPlay();
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Combat.M_Combat"));
    if (!Material) Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!Material)
    {
        UE_LOG(LogTemp, Error, TEXT("Ronin fighter could not load its base material."));
        return;
    }
    ArmorMaterial = UMaterialInstanceDynamic::Create(Material, this);
    SteelMaterial = UMaterialInstanceDynamic::Create(Material, this);
    ArmorColor = Identity == ronin::FighterId::Player ? FLinearColor(.035f,.13f,.16f) : FLinearColor(.3f,.035f,.026f);
    ArmorMaterial->SetVectorParameterValue(TEXT("Color"), ArmorColor);
    ArmorMaterial->SetScalarParameterValue(TEXT("Roughness"), .65f);
    ArmorMaterial->SetScalarParameterValue(TEXT("Metallic"), 0.f);
    SteelMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(.5f,.6f,.64f));
    SteelMaterial->SetScalarParameterValue(TEXT("Metallic"), .9f);
    SteelMaterial->SetScalarParameterValue(TEXT("Roughness"), .23f);
    for (UStaticMeshComponent* Part : ArmorParts) Part->SetMaterial(0, ArmorMaterial);
    for (UStaticMeshComponent* Part : MetalParts) Part->SetMaterial(0, SteelMaterial);
}

ARoninGameMode* ARoninFighter::Game() const
{
    return Cast<ARoninGameMode>(UGameplayStatics::GetGameMode(this));
}

void ARoninFighter::SetFrozen(bool bValue)
{
    bFrozen = bValue;
    if (bFrozen)
    {
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->MaxWalkSpeed = 0.f;
        ConsumeMovementInputVector();
    }
}

void ARoninFighter::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* G = Game();
    if (!G || bFrozen || G->bCombatPaused)
    {
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->MaxWalkSpeed = 0.f;
        ConsumeMovementInputVector();
        return;
    }
    const auto& State = G->Combat.Get(Identity);
    auto* Target = Identity == ronin::FighterId::Player ? G->EnemyFighter.Get() : G->PlayerFighter.Get();
    const bool bDead = State.action == ronin::Action::Dead;
    const bool bIdle = State.action == ronin::Action::Idle;
    const bool bCanMove = !bDead && bIdle && G->CanControl();
    FVector Toward = Target ? Target->GetActorLocation() - GetActorLocation() : GetActorForwardVector();
    Toward.Z = 0;
    const float Distance = Toward.Size();
    Toward.Normalize();
    if (Target && !bDead && (Identity == ronin::FighterId::Enemy || G->bLockOn))
    {
        // Committed attacks stop tracking; sidesteps can actually evade them.
        if (bIdle || (State.action == ronin::Action::Windup && State.actionTime < State.actionDuration * .5f))
            SetActorRotation(FMath::RInterpTo(GetActorRotation(), Toward.Rotation(), Dt, 12.f));
    }
    else if (!bDead && GetVelocity().SizeSquared2D() > 20.f)
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), GetVelocity().Rotation(), Dt, 12.f));
    GetCharacterMovement()->MaxWalkSpeed = bCanMove ? (State.guarding ? 130.f : 390.f) : 0.f;
    if (!bCanMove || (Identity == ronin::FighterId::Enemy && Distance <= 180.f))
    {
        GetCharacterMovement()->StopMovementImmediately();
        ConsumeMovementInputVector();
    }
    else if (Identity == ronin::FighterId::Enemy && Target)
    {
        AddMovementInput(Toward, 1.f);
    }
    if (Identity == ronin::FighterId::Player && Controller && Target && G->bLockOn)
    {
        auto Rotation = Controller->GetControlRotation();
        Rotation.Yaw = FMath::FixedTurn(Rotation.Yaw, Toward.Rotation().Yaw, Dt * 160.f);
        Rotation.Pitch = FMath::FInterpTo(Rotation.Pitch, -12.f, Dt, 4.f);
        Controller->SetControlRotation(Rotation);
    }
    Gait += Dt * GetVelocity().Size2D() * .035f;
    const float Walk = bCanMove ? FMath::Min(static_cast<float>(GetVelocity().Size2D())/250.f, 1.f) : 0.f;
    LeftLeg->SetRelativeRotation(FRotator(FMath::Sin(Gait)*24.f*Walk, 0, 0));
    RightLeg->SetRelativeRotation(FRotator(-FMath::Sin(Gait)*24.f*Walk, 0, 0));

    FRotator BladePose(-18.f, -20.f, 0);
    if (State.stance == ronin::Stance::Stone) BladePose = FRotator(58.f, 0, 0);
    if (State.stance == ronin::Stance::Gale) BladePose = FRotator(-30.f, -65.f, 0);
    if (State.guarding) BladePose = FRotator(65.f,-65.f,0);
    const float Progress = State.actionDuration > 0.f ? FMath::Clamp(State.actionTime / State.actionDuration, 0.f, 1.f) : 0.f;
    if (State.action == ronin::Action::Windup)
        BladePose = FRotator(FMath::Lerp(20.f, 65.f, Progress), -80.f, 0);
    if (State.action == ronin::Action::Active)
        BladePose = FRotator(FMath::Lerp(65.f,-30.f,Progress), FMath::Lerp(-80.f,100.f,Progress),0);
    if (State.action == ronin::Action::Recovery)
        BladePose = FRotator(FMath::Lerp(-30.f,-18.f,Progress), FMath::Lerp(100.f,-20.f,Progress),0);
    SwordPivot->SetRelativeRotation(BladePose);
    UpdateArms();
    const float Lean = State.action == ronin::Action::Dodging ? -24.f :
        (State.action == ronin::Action::Staggered || State.action == ronin::Action::Broken ? 22.f : 0.f);
    Body->SetRelativeRotation(FMath::RInterpTo(Body->GetRelativeRotation(), FRotator(bDead ? -85.f : Lean,0,0),Dt,10.f));
    FlashTime = FMath::Max(0.f,FlashTime-Dt);
    if (FlashTime <= 0.f && ArmorMaterial)
        ArmorMaterial->SetVectorParameterValue(TEXT("Color"), State.spec.unblockable && State.action == ronin::Action::Windup
            ? FLinearColor(.8f,.055f,.025f) : ArmorColor);
}

void ARoninFighter::ResetPose(FVector Location, FRotator Rotation)
{
    SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->MaxWalkSpeed = 390.f;
    ConsumeMovementInputVector();
    Body->SetRelativeRotation(FRotator::ZeroRotator);
    LeftLeg->SetRelativeRotation(FRotator::ZeroRotator);
    RightLeg->SetRelativeRotation(FRotator::ZeroRotator);
    SwordPivot->SetRelativeRotation(FRotator(-18,-20,0));
    UpdateArms();
    Gait = 0.f;
    FlashTime = 0; MoveInput = FVector2D::ZeroVector; bFrozen = false;
    if (Controller) Controller->SetControlRotation(FRotator(-12, Rotation.Yaw, 0));
}

void ARoninFighter::BeginDodge()
{
    if (auto* G=Game()) DodgeSpeed=G->Combat.Get(Identity).stance==ronin::Stance::Gale ? 1000.f : 800.f;
    const FRotator Yaw(0,Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw,0);
    DodgeDirection = (FRotationMatrix(Yaw).GetUnitAxis(EAxis::X)*MoveInput.X + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y)*MoveInput.Y).GetSafeNormal();
    if (DodgeDirection.IsNearlyZero()) DodgeDirection = -GetActorForwardVector();
}

void ARoninFighter::AdvanceDodge(float SimulationDelta)
{
    auto* G=Game();
    if (G && G->CanControl() && G->Combat.Get(Identity).action==ronin::Action::Dodging)
    {
        GetCharacterMovement()->StopMovementImmediately();
        AddActorWorldOffset(DodgeDirection*DodgeSpeed*SimulationDelta,true);
    }
}

void ARoninFighter::HitFlash(FLinearColor Color)
{
    FlashTime = .12f;
    if (ArmorMaterial) ArmorMaterial->SetVectorParameterValue(TEXT("Color"),Color);
}

void ARoninFighter::MoveForward(float Value)
{
    MoveInput.X = Value;
    auto* G = Game();
    if (!bFrozen && Controller && G && G->CanControl() && G->Combat.Get(Identity).action == ronin::Action::Idle)
        AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X),Value);
}
void ARoninFighter::MoveRight(float Value)
{
    MoveInput.Y = Value;
    auto* G = Game();
    if (!bFrozen && Controller && G && G->CanControl() && G->Combat.Get(Identity).action == ronin::Action::Idle)
        AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::Y),Value);
}
void ARoninFighter::Turn(float Value) { if (auto* G=Game(); G && !G->bCombatPaused && !G->bLockOn) AddControllerYawInput(Value); }
void ARoninFighter::LookUp(float Value) { if (auto* G=Game(); G && !G->bCombatPaused && !G->bLockOn) AddControllerPitchInput(Value); }
void ARoninFighter::TurnRate(float Value)
{
    if (GetWorld()) Turn(Value * 110.f * GetWorld()->GetDeltaSeconds());
}
void ARoninFighter::LookUpRate(float Value)
{
    if (GetWorld()) LookUp(Value * 110.f * GetWorld()->GetDeltaSeconds());
}
void ARoninFighter::Light() { if (auto* G=Game()) G->LightAttack(); }
void ARoninFighter::Heavy() { if (auto* G=Game()) G->HeavyAttack(); }
void ARoninFighter::GuardDown() { if (auto* G=Game()) G->Guard(true); }
void ARoninFighter::GuardUp() { if (auto* G=Game()) G->Guard(false); }
void ARoninFighter::DodgeInput() { if (auto* G=Game()) G->Dodge(); }
void ARoninFighter::ExecuteInput()
{
    if (auto* G=Game())
    {
        if (G->bCombatPaused) G->CycleFrameCap();
        else G->Execute();
    }
}
void ARoninFighter::Flow() { if (auto* G=Game()) G->ChangeStance(ronin::Stance::Flow); }
void ARoninFighter::Stone() { if (auto* G=Game()) G->ChangeStance(ronin::Stance::Stone); }
void ARoninFighter::Gale() { if (auto* G=Game()) G->ChangeStance(ronin::Stance::Gale); }
void ARoninFighter::Restart() { if (auto* G=Game()) G->RestartDuel(); }
void ARoninFighter::Pause() { if (auto* G=Game()) G->TogglePause(); }
void ARoninFighter::Lock() { if (auto* G=Game()) G->bLockOn = !G->bLockOn; }
void ARoninFighter::Quality() { if (auto* G=Game(); G && G->bCombatPaused) G->CycleQuality(); }
void ARoninFighter::FrameCap() { if (auto* G=Game()) G->CycleFrameCap(); }
void ARoninFighter::Help() { if (auto* G=Game()) G->bShowHelp = !G->bShowHelp; }
void ARoninFighter::Haptics() { if (auto* G=Game()) G->CycleHaptics(); }

void ARoninFighter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    if (APlayerController* Player = Cast<APlayerController>(GetController()))
    {
        if (Player->PlayerCameraManager)
        {
            Player->PlayerCameraManager->ViewPitchMin = -55.f;
            Player->PlayerCameraManager->ViewPitchMax = 25.f;
            Player->PlayerCameraManager->ViewRollMin = 0.f;
            Player->PlayerCameraManager->ViewRollMax = 0.f;
        }
    }
    Input->BindAxis(TEXT("MoveForward"),this,&ARoninFighter::MoveForward);
    Input->BindAxis(TEXT("MoveRight"),this,&ARoninFighter::MoveRight);
    Input->BindAxis(TEXT("Turn"),this,&ARoninFighter::Turn);
    Input->BindAxis(TEXT("LookUp"),this,&ARoninFighter::LookUp);
    Input->BindAxis(TEXT("TurnRate"),this,&ARoninFighter::TurnRate);
    Input->BindAxis(TEXT("LookUpRate"),this,&ARoninFighter::LookUpRate);
    Input->BindAction(TEXT("Light"),IE_Pressed,this,&ARoninFighter::Light);
    Input->BindAction(TEXT("Heavy"),IE_Pressed,this,&ARoninFighter::Heavy);
    Input->BindAction(TEXT("Guard"),IE_Pressed,this,&ARoninFighter::GuardDown);
    Input->BindAction(TEXT("Guard"),IE_Released,this,&ARoninFighter::GuardUp);
    Input->BindAction(TEXT("Dodge"),IE_Pressed,this,&ARoninFighter::DodgeInput);
    Input->BindAction(TEXT("Execute"),IE_Pressed,this,&ARoninFighter::ExecuteInput);
    Input->BindAction(TEXT("Flow"),IE_Pressed,this,&ARoninFighter::Flow);
    Input->BindAction(TEXT("Stone"),IE_Pressed,this,&ARoninFighter::Stone);
    Input->BindAction(TEXT("Gale"),IE_Pressed,this,&ARoninFighter::Gale);
    Input->BindAction(TEXT("Restart"),IE_Pressed,this,&ARoninFighter::Restart);
    Input->BindAction(TEXT("Pause"),IE_Pressed,this,&ARoninFighter::Pause);
    Input->BindAction(TEXT("Lock"),IE_Pressed,this,&ARoninFighter::Lock);
    Input->BindAction(TEXT("Quality"),IE_Pressed,this,&ARoninFighter::Quality);
    Input->BindAction(TEXT("FrameCap"),IE_Pressed,this,&ARoninFighter::FrameCap);
    Input->BindAction(TEXT("Help"),IE_Pressed,this,&ARoninFighter::Help);
    Input->BindAction(TEXT("Haptics"),IE_Pressed,this,&ARoninFighter::Haptics);
}
