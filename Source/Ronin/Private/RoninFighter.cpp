#include "RoninFighter.h"
#include "RoninAnimInstance.h"
#include "RoninGameMode.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RotationMatrix.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
void StopPlanarMovement(UCharacterMovementComponent* Movement)
{
    if (Movement->IsFalling())
    {
        Movement->Velocity.X = 0.f;
        Movement->Velocity.Y = 0.f;
    }
    else Movement->StopMovementImmediately();
}
}

ARoninFighter::ARoninFighter()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->MaxWalkSpeed = 260.f;
    GetCharacterMovement()->MaxAcceleration = 2200.f;
    GetCharacterMovement()->BrakingDecelerationWalking = 2200.f;
    GetCharacterMovement()->GroundFriction = 8.f;
    GetCharacterMovement()->bRunPhysicsWithNoController = true;
    GetCharacterMovement()->JumpZVelocity = 520.f;
    GetCharacterMovement()->GravityScale = 1.4f;
    GetCharacterMovement()->AirControl = .25f;
    JumpMaxCount = 1;
    JumpMaxHoldTime = .12f;

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> PlayerAsset(
        TEXT("/Game/Characters/Samurai/SK_Samurai.SK_Samurai"));
    PlayerMesh = PlayerAsset.Object;
    EnemyMesh = PlayerAsset.Object;
    GetMesh()->SetSkeletalMesh(PlayerMesh);
    GetMesh()->SetRelativeLocation(FVector(0, 0, -88));
    GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetGenerateOverlapEvents(false);
    GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    GetMesh()->SetAnimInstanceClass(URoninAnimInstance::StaticClass());

    static ConstructorHelpers::FObjectFinder<UStaticMesh> KatanaAsset(
        TEXT("/Game/Weapons/Katana/SM_Katana.SM_Katana"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SayaAsset(
        TEXT("/Game/Weapons/Katana/SM_Saya.SM_Saya"));
    Katana = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Katana"));
    Katana->SetupAttachment(GetMesh(), TEXT("mixamorig_RightHand"));
    Katana->SetStaticMesh(KatanaAsset.Object);
    // Original blade: grip-centre pivot, local +Z tip and -X cutting edge.
    Katana->SetRelativeTransform(FTransform(
        FQuat(-.597783623, .197935307, .528157378, .569671957),
        FVector(-.246960336, -.247584995, 9.169885795)));
    Saya = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Saya"));
    Saya->SetupAttachment(GetMesh(), TEXT("mixamorig_Hips"));
    Saya->SetStaticMesh(SayaAsset.Object);
    Saya->SetRelativeTransform(FTransform(
        FQuat(.558554594, .552298262, -.437216237, .437978718),
        FVector(18.798893836, 11.483722529, 2.198725012)));
    for (UStaticMeshComponent* Weapon : {Katana.Get(), Saya.Get()})
    {
        Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Weapon->SetGenerateOverlapEvents(false);
    }

    static ConstructorHelpers::FObjectFinder<UParticleSystem> TrailAsset(
        TEXT("/Game/ParagonKwang/FX/Particles/Abilities/Primary/FX/P_Kwang_Primary_Trail.P_Kwang_Primary_Trail"));
    BladeTrail = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("BladeTrail"));
    BladeTrail->SetupAttachment(Katana);
    BladeTrail->bAutoActivate = false;
    BladeTrail->bAutoDestroy = false;
    BladeTrail->SecondsBeforeInactive = 0.f;
    BladeTrail->SetTemplate(TrailAsset.Object);

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->TargetArmLength = 380.f;
    CameraBoom->SocketOffset = FVector(0,55,65);
    CameraBoom->bUsePawnControlRotation = true;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 14.f;
    CameraBoom->ProbeSize = 14.f;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->FieldOfView = 74.f;
    Camera->bConstrainAspectRatio = false;
}

void ARoninFighter::BeginPlay()
{
    Super::BeginPlay();
    GetMesh()->SetSkeletalMesh(Identity == ronin::FighterId::Enemy ? EnemyMesh : PlayerMesh);
    if (UMaterialInstanceDynamic* Armor = GetMesh()->CreateAndSetMaterialInstanceDynamic(0))
        Armor->SetVectorParameterValue(TEXT("ColorTint"), Identity == ronin::FighterId::Enemy
            ? FLinearColor(.72f, .43f, .34f) : FLinearColor::White);
    // GameMode precedes Fighter; the skeletal pose must see this frame's combat and movement.
    GetMesh()->AddTickPrerequisiteActor(this);
    GetMesh()->AddTickPrerequisiteComponent(GetCharacterMovement());
    // Two explicit dependencies also keep Cascade from batching this socket-sampled effect.
    BladeTrail->AddTickPrerequisiteActor(this);
    BladeTrail->AddTickPrerequisiteComponent(GetMesh());
    if (!GetMesh()->GetSkeletalMeshAsset())
        UE_LOG(LogTemp, Error, TEXT("Original samurai mesh is missing. Run scripts/import_samurai.py before playing."));

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
    OutState.DefenseSerial = DefenseReactionSerial;
    OutState.bDefenseActive = bDefenseReactionActive;
    OutState.ImpactSerial = ImpactSerial;
    OutState.bImpactBlocked = bImpactBlocked;
    OutState.bImpactStrong = bImpactStrong;
    OutState.bPerfectDodge = bPerfectDodgeReaction;
    OutState.bFrozen = bFrozen;
    OutState.bAirborne = GetCharacterMovement()->IsFalling();
    OutState.VerticalSpeed = GetVelocity().Z;
    if (const auto* G = Game())
    {
        OutState.Combat = G->Combat.Get(Identity);
        OutState.bFrozen |= G->bCombatPaused;
        OutState.TimeScale = G->GetCombatTimeScale();
        OutState.SimulationDelta = G->GetCombatDeltaSeconds();
        if (OutState.TimeScale > .001f) OutState.LocalVelocity /= OutState.TimeScale;
    }
}

void ARoninFighter::PlayDefenseReaction(bool bPerfectDodge)
{
    ++DefenseReactionSerial;
    bPerfectDodgeReaction = bPerfectDodge;
    bDefenseReactionActive = true;
}

void ARoninFighter::ReceiveImpact(FVector Direction, bool bBlocked, bool bStrong)
{
    ++ImpactSerial;
    bImpactBlocked = bBlocked;
    bImpactStrong = bStrong;
    ImpactDirection = Direction.GetSafeNormal2D();
    ImpactDistance = bBlocked ? 8.f : bStrong ? 45.f : 18.f;
    ImpactElapsed = 0.f;
    StopPlanarMovement(GetCharacterMovement());
}

FRotator ARoninFighter::EvaluateLockOnRotation(FRotator Current, FVector Toward, float DeltaSeconds)
{
    // Control rotation stores -12 degrees as 348. Interpolating that raw number
    // toward -12 takes the long route through the overhead pitch clamp.
    Current.Pitch = FMath::FInterpTo(FRotator::NormalizeAxis(Current.Pitch), -12.f, DeltaSeconds, 4.f);
    Current.Pitch = FMath::Clamp(Current.Pitch, -25.f, -6.f);
    Toward.Z = 0.f;
    if (!Toward.IsNearlyZero())
        Current.Yaw = FMath::FixedTurn(Current.Yaw, Toward.Rotation().Yaw, DeltaSeconds * 160.f);
    Current.Roll = 0.f;
    return Current;
}

void ARoninFighter::SetFrozen(bool bValue)
{
    if (bValue != bFrozen)
    {
        if (bValue)
        {
            FrozenVelocity = GetCharacterMovement()->Velocity;
            GetCharacterMovement()->SetComponentTickEnabled(false);
        }
        else
        {
            GetCharacterMovement()->SetComponentTickEnabled(true);
            GetCharacterMovement()->Velocity = FrozenVelocity;
        }
    }
    bFrozen = bValue;
    // Still evaluate the explicit contact pose on the first hit-stop frame, with no clock advance.
    GetMesh()->GlobalAnimRateScale = bFrozen ? 0.f : 1.f;
    BladeTrail->CustomTimeDilation = bFrozen ? 0.f : 1.f;
    BladeTrail->SetComponentTickEnabled(!bFrozen && BladeTrail->IsActive());
    if (bFrozen)
    {
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
    BladeTrail->CustomTimeDilation = bFreeze ? 0.f : (Game() ? Game()->GetCombatTimeScale() : 1.f);
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
        GetCharacterMovement()->MaxWalkSpeed = 0.f;
        ConsumeMovementInputVector();
        return;
    }
    const auto& State = G->Combat.Get(Identity);
    auto* Target = Identity == ronin::FighterId::Player ? G->EnemyFighter.Get() : G->PlayerFighter.Get();
    const bool bDead = State.action == ronin::Action::Dead;
    const bool bIdle = State.action == ronin::Action::Idle;
    const float CombatScale = G->GetCombatTimeScale();
    FVector Toward = Target ? Target->GetActorLocation() - GetActorLocation() : GetActorForwardVector();
    Toward.Z = 0;
    const float Distance = Toward.Size();
    const bool bCanMove = !bDead && bIdle && ImpactElapsed >= .2f &&
        (Identity == ronin::FighterId::Player ? G->CanTraverse() : G->CanControl() && Distance <= 850.f);
    if (!bCanMove || State.guarding || MoveInput.IsNearlyZero(.1f)) bSprintRequested = false;
    if (Identity == ronin::FighterId::Player && Target &&
        (Distance > 900.f || G->Combat.Get(ronin::FighterId::Enemy).action == ronin::Action::Dead)) G->bLockOn = false;
    const bool bSprinting = bSprintRequested && Identity == ronin::FighterId::Player;
    Toward.Normalize();
    if (Target && !bDead && !bSprinting && (Identity == ronin::FighterId::Enemy || G->bLockOn))
    {
        // Committed attacks stop tracking; sidesteps can actually evade them.
        if (bIdle || (State.action == ronin::Action::Windup && State.actionTime < State.actionDuration * .5f))
            SetActorRotation(FMath::RInterpTo(GetActorRotation(), Toward.Rotation(), Dt * CombatScale, 12.f));
    }
    else if (!bDead && GetVelocity().SizeSquared2D() > 20.f)
    {
        const FVector PlanarVelocity(GetVelocity().X, GetVelocity().Y, 0.f);
        const FRotator Facing = FMath::RInterpTo(GetActorRotation(), PlanarVelocity.Rotation(), Dt * CombatScale, 12.f);
        SetActorRotation(FRotator(0.f, Facing.Yaw, 0.f));
    }
    const float MoveSpeed = State.guarding ? 130.f : bSprinting ? 520.f : 260.f;
    GetCharacterMovement()->MaxWalkSpeed = bCanMove ? MoveSpeed * CombatScale : 0.f;
    if (CombatScale < 1.f)
        GetCharacterMovement()->Velocity = GetCharacterMovement()->Velocity.GetClampedToMaxSize2D(GetCharacterMovement()->MaxWalkSpeed);
    if (!bCanMove || (Identity == ronin::FighterId::Enemy && Distance <= 180.f))
    {
        StopPlanarMovement(GetCharacterMovement());
        ConsumeMovementInputVector();
    }
    else if (Identity == ronin::FighterId::Enemy && Target)
    {
        AddMovementInput(Toward, 1.f);
    }
    if (Identity == ronin::FighterId::Player && Controller && Target && G->bLockOn)
    {
        Controller->SetControlRotation(EvaluateLockOnRotation(Controller->GetControlRotation(), Toward, Dt));
    }
}

void ARoninFighter::AdvanceCombatMovement(float SimulationDelta)
{
    auto* G = Game();
    if (!G || SimulationDelta <= 0.f) return;
    const auto& State = G->Combat.Get(Identity);
    auto* Target = Identity == ronin::FighterId::Player ? G->EnemyFighter.Get() : G->PlayerFighter.Get();
    if (ImpactElapsed < .2f && SimulationDelta > 0.f)
    {
        const float Before = ImpactElapsed / .2f;
        ImpactElapsed = FMath::Min(.2f, ImpactElapsed + SimulationDelta);
        const float After = ImpactElapsed / .2f;
        const auto EaseOut = [](float T) { return T * (2.f - T); };
        // Sweep the capsule: a wall stops recoil instead of clipping the victim.
        AddActorWorldOffset(ImpactDirection * ImpactDistance * (EaseOut(After) - EaseOut(Before)), true);
    }
    const bool bAttack = State.action == ronin::Action::Windup || State.action == ronin::Action::Active || State.action == ronin::Action::Recovery;
    if (bAttack && State.attackId != AdvancingAttackId)
    {
        AdvancingAttackId = State.attackId;
        AttackAdvanceProgress = 0.f;
        AttackAdvanceDistance = 0.f;
        if (Target)
        {
            const FVector Delta = Target->GetActorLocation() - GetActorLocation();
            const float Reach = State.isCounterAttack ? State.spec.range + 158.f : 260.f;
            AttackAdvanceDirection = Delta.GetSafeNormal2D();
            if (Delta.Size2D() <= Reach)
                AttackAdvanceDistance = FMath::Clamp(static_cast<float>(Delta.Size2D()) - 165.f, 0.f, State.isCounterAttack ? 170.f : 32.f);
        }
    }
    if ((State.action == ronin::Action::Windup || State.action == ronin::Action::Active) && SimulationDelta > 0.f)
    {
        const float Progress = State.action == ronin::Action::Active ? 1.f :
            FMath::Clamp(State.actionTime / FMath::Max(State.actionDuration, .001f), 0.f, 1.f);
        // A short, committed step keeps consecutive cuts connected after recoil.
        AddActorWorldOffset(AttackAdvanceDirection * AttackAdvanceDistance * FMath::Max(0.f, Progress - AttackAdvanceProgress), true);
        AttackAdvanceProgress = Progress;
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
    GetCharacterMovement()->SetComponentTickEnabled(true);
    GetCharacterMovement()->StopMovementImmediately();
    FrozenVelocity = FVector::ZeroVector;
    GetCharacterMovement()->MaxWalkSpeed = 260.f;
    ConsumeMovementInputVector();
    ++AnimationResetSerial;
    bDefenseReactionActive = false;
    ImpactElapsed = .2f;
    ImpactDistance = AttackAdvanceDistance = AttackAdvanceProgress = 0.f;
    AdvancingAttackId = 0;
    MoveInput = FVector2D::ZeroVector;
    bSprintRequested = false;
    StopJumping();
    JumpCurrentCount = 0;
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    DodgeDuration = DodgeElapsed = 0.f;
    bFrozen = false;
    GetMesh()->GlobalAnimRateScale = 1.f;
    if (Controller) Controller->SetControlRotation(FRotator(-12, Rotation.Yaw, 0));
}

void ARoninFighter::BeginDodge()
{
    if (auto* G=Game())
    {
        const auto& State = G->Combat.Get(Identity);
        DodgeDistance = State.stance == ronin::Stance::Gale ? 210.f : 180.f;
        DodgeDuration = State.actionDuration;
        DodgeElapsed = 0.f;
    }
    const FRotator Yaw(0,Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw,0);
    DodgeDirection = (FRotationMatrix(Yaw).GetUnitAxis(EAxis::X)*MoveInput.X + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y)*MoveInput.Y).GetSafeNormal();
    if (DodgeDirection.IsNearlyZero()) DodgeDirection = -GetActorForwardVector();
}

void ARoninFighter::AdvanceDodge(float SimulationDelta)
{
    auto* G=Game();
    if (!G || !G->CanControl() || SimulationDelta <= 0.f || DodgeDuration <= 0.f || DodgeElapsed >= DodgeDuration)
        return;
    const auto& State = G->Combat.Get(Identity);
    if (State.action == ronin::Action::Dodging || State.action == ronin::Action::Idle)
    {
        // GameMode advances the core first. Integrate the exact phase interval,
        // including its final fraction when that step has already reached idle.
        const float NextTime = State.action == ronin::Action::Dodging ?
            FMath::Clamp(State.actionTime, DodgeElapsed, DodgeDuration) : DodgeDuration;
        const auto Travel = [](float Phase) { return 1.6f * Phase - .6f * Phase * Phase; };
        const float DeltaDistance = DodgeDistance *
            (Travel(NextTime / DodgeDuration) - Travel(DodgeElapsed / DodgeDuration));
        StopPlanarMovement(GetCharacterMovement());
        AddActorWorldOffset(DodgeDirection * DeltaDistance, true);
        DodgeElapsed = NextTime;
    }
    else DodgeElapsed = DodgeDuration; // A hit or posture break interrupts the step.
}

void ARoninFighter::HitFlash(FLinearColor)
{
    // HUD and contact VFX supply feedback while authored character materials stay intact.
}

void ARoninFighter::MoveForward(float Value)
{
    MoveInput.X = Value;
    auto* G = Game();
    if (!bFrozen && Controller && G && G->CanTraverse() && G->Combat.Get(Identity).action == ronin::Action::Idle)
        AddMovementInput(FRotationMatrix(FRotator(0,Controller->GetControlRotation().Yaw,0)).GetUnitAxis(EAxis::X),Value);
}
void ARoninFighter::MoveRight(float Value)
{
    MoveInput.Y = Value;
    auto* G = Game();
    if (!bFrozen && Controller && G && G->CanTraverse() && G->Combat.Get(Identity).action == ronin::Action::Idle)
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
void ARoninFighter::Sprint()
{
    if (auto* G=Game(); G && G->CanTraverse() && !G->Combat.Get(Identity).guarding)
        bSprintRequested = !bSprintRequested;
}

void ARoninFighter::JumpInput()
{
    auto* G = Game();
    if (!G) return;
    if (G->bCombatPaused) { G->CycleQuality(); return; }
    const auto& State = G->Combat.Get(Identity);
    // A also confirms the title screen; that same press must not launch a jump.
    if (G->CanTraverse() && G->GetRoundTime() > .1f && State.action == ronin::Action::Idle &&
        !State.guarding && GetCharacterMovement()->IsMovingOnGround()) Jump();
}
void ARoninFighter::JumpRelease() { StopJumping(); }

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
    Input->BindAction(TEXT("Sprint"),IE_Pressed,this,&ARoninFighter::Sprint);
    Input->BindAction(TEXT("Jump"),IE_Pressed,this,&ARoninFighter::JumpInput);
    Input->BindAction(TEXT("Jump"),IE_Released,this,&ARoninFighter::JumpRelease);
}
