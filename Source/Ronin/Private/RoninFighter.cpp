#include "RoninFighter.h"
#include "RoninAnimInstance.h"
#include "RoninGameMode.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
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

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> PlayerAsset(
        TEXT("/Game/ParagonKwang/Characters/Heroes/Kwang/Meshes/Kwang_GDC.Kwang_GDC"));
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> EnemyAsset(
        TEXT("/Game/ParagonKwang/Characters/Heroes/Kwang/Meshes/KwangRosewood.KwangRosewood"));
    PlayerMesh = PlayerAsset.Object;
    EnemyMesh = EnemyAsset.Object;
    GetMesh()->SetSkeletalMesh(PlayerMesh);
    GetMesh()->SetRelativeLocation(FVector(0, 0, -88));
    GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetGenerateOverlapEvents(false);
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetMesh()->SetAnimInstanceClass(URoninAnimInstance::StaticClass());

    static ConstructorHelpers::FObjectFinder<UParticleSystem> TrailAsset(
        TEXT("/Game/ParagonKwang/FX/Particles/Abilities/Primary/FX/P_Kwang_Primary_Trail.P_Kwang_Primary_Trail"));
    BladeTrail = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("BladeTrail"));
    BladeTrail->SetupAttachment(GetMesh());
    BladeTrail->bAutoActivate = false;
    BladeTrail->bAutoDestroy = false;
    BladeTrail->SecondsBeforeInactive = 0.f;
    BladeTrail->SetTemplate(TrailAsset.Object);

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
}

void ARoninFighter::BeginPlay()
{
    Super::BeginPlay();
    GetMesh()->SetSkeletalMesh(Identity == ronin::FighterId::Enemy ? EnemyMesh : PlayerMesh);
    // GameMode precedes Fighter; the skeletal pose must see this frame's combat and movement.
    GetMesh()->AddTickPrerequisiteActor(this);
    GetMesh()->AddTickPrerequisiteComponent(GetCharacterMovement());
    // Two explicit dependencies also keep Cascade from batching this socket-sampled effect.
    BladeTrail->AddTickPrerequisiteActor(this);
    BladeTrail->AddTickPrerequisiteComponent(GetMesh());
    if (!GetMesh()->GetSkeletalMeshAsset())
        UE_LOG(LogTemp, Error, TEXT("Kwang is missing. Install the licensed Paragon: Kwang content before playing."));

}

ARoninGameMode* ARoninFighter::Game() const
{
    return Cast<ARoninGameMode>(UGameplayStatics::GetGameMode(this));
}

void ARoninFighter::GetAnimationState(FRoninAnimationState& OutState) const
{
    OutState.LocalVelocity = GetActorTransform().InverseTransformVectorNoScale(GetVelocity());
    OutState.LocalDodgeDirection = GetActorTransform().InverseTransformVectorNoScale(DodgeDirection);
    OutState.ResetSerial = AnimationResetSerial;
    OutState.bFrozen = bFrozen;
    if (const auto* G = Game())
    {
        OutState.Combat = G->Combat.Get(Identity);
        OutState.bFrozen |= G->bCombatPaused;
    }
}

void ARoninFighter::SetFrozen(bool bValue)
{
    bFrozen = bValue;
    // Still evaluate the explicit contact pose on the first hit-stop frame, with no clock advance.
    GetMesh()->GlobalAnimRateScale = bFrozen ? 0.f : 1.f;
    BladeTrail->CustomTimeDilation = bFrozen ? 0.f : 1.f;
    BladeTrail->SetComponentTickEnabled(!bFrozen && BladeTrail->IsActive());
    if (bFrozen)
    {
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->MaxWalkSpeed = 0.f;
        ConsumeMovementInputVector();
    }
}

void ARoninFighter::UpdateBladeTrail(bool bActivePhase, bool bFreeze)
{
    if (!BladeTrail->Template) return;
    if (bActivePhase != bBladeTrailEmitting)
    {
        if (bActivePhase)
            BladeTrail->BeginTrails(TEXT("FX_weapon_base"), TEXT("FX_weapon_tip"), ETrailWidthMode_FromCentre, 1.f);
        else
            BladeTrail->EndTrails();
        bBladeTrailEmitting = bActivePhase;
    }
    // Preserve the current ribbon and its age through pause/hit-stop, without new samples.
    BladeTrail->CustomTimeDilation = bFreeze ? 0.f : 1.f;
    BladeTrail->SetComponentTickEnabled(!bFreeze && BladeTrail->IsActive());
}

void ARoninFighter::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* G = Game();
    const bool bFreeze = !G || bFrozen || G->bCombatPaused;
    GetMesh()->GlobalAnimRateScale = bFreeze ? 0.f : 1.f;
    UpdateBladeTrail(G && G->Combat.Get(Identity).action == ronin::Action::Active, bFreeze);
    if (bFreeze)
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
}

void ARoninFighter::ResetPose(FVector Location, FRotator Rotation)
{
    // Discard both live samples and a fading tail before a rematch teleport.
    BladeTrail->ResetParticles(true);
    BladeTrail->SetComponentTickEnabled(false);
    BladeTrail->CustomTimeDilation = 1.f;
    bBladeTrailEmitting = false;
    SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->MaxWalkSpeed = 390.f;
    ConsumeMovementInputVector();
    ++AnimationResetSerial;
    MoveInput = FVector2D::ZeroVector;
    bFrozen = false;
    GetMesh()->GlobalAnimRateScale = 1.f;
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

void ARoninFighter::HitFlash(FLinearColor)
{
    // HUD and contact VFX supply feedback while authored character materials stay intact.
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
