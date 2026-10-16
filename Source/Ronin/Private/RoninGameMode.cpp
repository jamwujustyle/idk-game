#include "RoninGameMode.h"
#include "RoninArena.h"
#include "RoninFighter.h"
#include "RoninHUD.h"
#include "RoninPlayerController.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Misc/ConfigCacheIni.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "UObject/ConstructorHelpers.h"

using ronin::FighterId;
using ronin::Action;
using ronin::AttackKind;
using ronin::HitResult;

namespace
{
constexpr float ContactSparkLifetime = .35f;
constexpr int32 MaxContactSparkBursts = 4;
constexpr float ParryContactBeat = .07f;
constexpr float ParrySlowHold = .3f;
constexpr float ParryReturn = .15f;
constexpr float DodgeSlowHold = .1f;
constexpr float DodgeReturn = .2f;
void PulseFeedback(UObject* Context, ronin::HapticEvent Event)
{
    if (auto* PC=Cast<ARoninPlayerController>(UGameplayStatics::GetPlayerController(Context,0))) PC->Pulse(Event);
}
void StopFeedback(UObject* Context)
{
    if (auto* PC=Cast<ARoninPlayerController>(UGameplayStatics::GetPlayerController(Context,0))) PC->StopRumble();
}
}

ARoninGameMode::ARoninGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = nullptr;
    HUDClass = ARoninHUD::StaticClass();
    PlayerControllerClass = ARoninPlayerController::StaticClass();
    static ConstructorHelpers::FObjectFinder<UParticleSystem> SparkAsset(
        TEXT("/Game/ParagonKwang/FX/Particles/Abilities/Primary/FX/P_Kwang_Primary_Impact.P_Kwang_Primary_Impact"));
    ContactSparkSource = SparkAsset.Object;
}

void ARoninGameMode::StartPlay()
{
    Super::StartPlay();
    InitializeContactSparks();
    GetWorld()->SpawnActor<ARoninArena>();
    // Set identity before BeginPlay builds the fighter's materials.
    PlayerFighter = GetWorld()->SpawnActorDeferred<ARoninFighter>(ARoninFighter::StaticClass(),
        FTransform(FRotator(0,0,0),FVector(-300,0,100)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (PlayerFighter) { PlayerFighter->Identity=FighterId::Player; PlayerFighter->FinishSpawning(FTransform(FRotator(0,0,0),FVector(-300,0,100))); }
    EnemyFighter = GetWorld()->SpawnActorDeferred<ARoninFighter>(ARoninFighter::StaticClass(),
        FTransform(FRotator(0,180,0),FVector(300,0,100)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (EnemyFighter) { EnemyFighter->Identity=FighterId::Enemy; EnemyFighter->FinishSpawning(FTransform(FRotator(0,180,0),FVector(300,0,100))); }
    if (auto* PC = UGameplayStatics::GetPlayerController(this,0))
    {
        PC->Possess(PlayerFighter);
        PC->SetControlRotation(FRotator(-12,0,0));
        PC->SetInputMode(FInputModeGameOnly());
        PC->bShowMouseCursor = false;
        EnableInput(PC);
        // A also changes quality while paused. Starting an already active duel
        // is a no-op, and this binding must leave that existing action reachable.
        InputComponent->BindAction(TEXT("StartDuel"),IE_Pressed,this,&ARoninGameMode::StartDuel).bConsumeInput=false;
        AddTickPrerequisiteActor(PC);
    }
    if (PlayerFighter) PlayerFighter->AddTickPrerequisiteActor(this);
    if (EnemyFighter) EnemyFighter->AddTickPrerequisiteActor(this);
    if (auto* Settings=UGameUserSettings::GetGameUserSettings())
    {
        QualityIndex=Settings->GetOverallScalabilityLevel();
        const TCHAR* Names[]={TEXT("LOW"),TEXT("MEDIUM"),TEXT("HIGH"),TEXT("EPIC")};
        QualityName=QualityIndex>=0 && QualityIndex<4 ? Names[QualityIndex] : TEXT("CUSTOM");
        const float Cap=Settings->GetFrameRateLimit();
        FrameCapIndex=FMath::IsNearlyEqual(Cap,60.f) ? 1 : FMath::IsNearlyEqual(Cap,120.f) ? 2 : FMath::IsNearlyEqual(Cap,240.f) ? 3 : 0;
        FrameCapName=Cap<=0.f ? TEXT("UNCAPPED") : FString::SanitizeFloat(Cap,0)+TEXT(" FPS");
    }
    for (const TCHAR* Name : {TEXT("Slash"),TEXT("Clash"),TEXT("Hit"),TEXT("Dodge"),TEXT("Execute")})
    {
        const FString Path=FString::Printf(TEXT("/Game/Audio/%s.%s"),Name,Name);
        if (auto* Sound=LoadObject<USoundBase>(nullptr,*Path)) Sounds.Add(FName(Name),Sound);
        else UE_LOG(LogTemp, Error, TEXT("Required combat sound is missing: %s"), *Path);
    }
    UE_LOG(LogTemp, Display, TEXT("Combat audio loaded: %d/5 cues; engine sound is %s."),
        Sounds.Num(), GEngine && GEngine->UseSound() ? TEXT("enabled") : TEXT("disabled by this run"));
    float SavedRumble=1.f;
    GConfig->GetFloat(TEXT("Ronin.Input"),TEXT("RumbleIntensity"),SavedRumble,GGameUserSettingsIni);
    const float Strengths[]={0.f,.35f,.7f,1.f};
    const TCHAR* StrengthNames[]={TEXT("OFF"),TEXT("35%"),TEXT("70%"),TEXT("100%")};
    for (int32 I=0; I<4; ++I) if (FMath::IsNearlyEqual(SavedRumble,Strengths[I])) HapticsIndex=I;
    HapticsName=StrengthNames[HapticsIndex];
    if (auto* PC=Cast<ARoninPlayerController>(UGameplayStatics::GetPlayerController(this,0))) PC->SetRumbleIntensity(Strengths[HapticsIndex]);
    RestartDuel();
}

void ARoninGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearDefenseCinematic();
    StopFeedback(this);
    ClearContactSparks();
    Super::EndPlay(EndPlayReason);
}

void ARoninGameMode::InitializeContactSparks()
{
    if (!ContactSparkSource || ContactSparks) return;
    FObjectDuplicationParameters Parameters(ContactSparkSource, this);
    Parameters.FlagMask &= ~(RF_Public | RF_Standalone);
    Parameters.ApplyFlags |= RF_Transient;
    auto* Filtered = Cast<UParticleSystem>(StaticDuplicateObjectEx(Parameters));
    if (!Filtered) return;
    Filtered->Emitters.RemoveAll([](const TObjectPtr<UParticleEmitter>& Emitter)
    {
        return !Emitter || (Emitter->EmitterName != TEXT("Sparks") && Emitter->EmitterName != TEXT("Sparks_Smaller"));
    });
    // Fail closed if the installed pack differs; never fall back to its magical impact.
    if (Filtered->Emitters.Num() != 2) return;
    for (UParticleEmitter* Emitter : Filtered->Emitters)
    {
        if (Emitter->LODLevels.IsEmpty()) return;
        for (UParticleLODLevel* LOD : Emitter->LODLevels)
        {
            if (!LOD || !LOD->RequiredModule || !LOD->SpawnModule || !LOD->TypeDataModule
                || !LOD->IsIn(Filtered) || !LOD->RequiredModule->IsIn(Filtered) || !LOD->SpawnModule->IsIn(Filtered)) return;
            const auto* GPU = Cast<UParticleModuleTypeDataGpu>(LOD->TypeDataModule);
            if (!GPU || !GPU->IsIn(Filtered)) return;
            for (const auto& Module : LOD->Modules)
                if (Module && !Module->IsIn(Filtered)) return;
            auto* Required = LOD->RequiredModule.Get();
            Required->EmitterDelay = Required->EmitterDelayLow = 0.f;
            Required->bEmitterDelayUseRange = false;
            Required->EmitterDuration = Required->EmitterDurationLow = .05f;
            Required->bEmitterDurationUseRange = false;
            Required->EmitterLoops = 1;
            auto* Spawn = LOD->SpawnModule.Get();
            // An empty raw distribution evaluates to zero in editor and cooked builds.
            Spawn->Rate = FRawDistributionFloat();
            Spawn->BurstList.Reset();
            FParticleBurst Burst;
            Burst.Count = Emitter->EmitterName == TEXT("Sparks") ? 6 : 12;
            Spawn->BurstList.Add(Burst);
        }
        // Duplication clears transient module lists; rebuild them and the GPU type-data resources.
        Emitter->UpdateModuleLists();
    }
    Filtered->WarmupTime = 0.f;
    ContactSparks = Filtered;
}

void ARoninGameMode::SpawnContactSparks(FVector Location)
{
    if (!ContactSparks) return;
    if (ContactSparkBursts.Num() >= MaxContactSparkBursts)
    {
        if (auto* Oldest = ContactSparkBursts[0].Component.Get()) Oldest->DestroyComponent();
        ContactSparkBursts.RemoveAt(0);
    }
    auto* Effect = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), ContactSparks,
        FTransform(FRotator::ZeroRotator, Location, FVector(.35f)), false, EPSCPoolMethod::None, false);
    if (!Effect) return;
    Effect->AddTickPrerequisiteActor(this);
    Effect->SecondsBeforeInactive = 0.f;
    Effect->ActivateSystem();
    ContactSparkBursts.Add({Effect, 0.f});
}

void ARoninGameMode::UpdateContactSparks(float Dt)
{
    for (int32 Index = ContactSparkBursts.Num() - 1; Index >= 0; --Index)
    {
        auto& Burst = ContactSparkBursts[Index];
        auto* Effect = Burst.Component.Get();
        if (!Effect) { ContactSparkBursts.RemoveAt(Index); continue; }
        if (!bCombatPaused) Burst.Age += Dt * CombatTimeScale;
        if (Burst.Age >= ContactSparkLifetime)
        {
            Effect->DestroyComponent();
            ContactSparkBursts.RemoveAt(Index);
            continue;
        }
        Effect->CustomTimeDilation = bCombatPaused ? 0.f : CombatTimeScale;
        Effect->SetComponentTickEnabled(!bCombatPaused && CombatTimeScale > 0.f && Effect->IsActive());
    }
}

void ARoninGameMode::ClearContactSparks()
{
    for (const auto& Burst : ContactSparkBursts)
        if (auto* Effect = Burst.Component.Get()) Effect->DestroyComponent();
    ContactSparkBursts.Reset();
}

bool ARoninGameMode::CanTraverse() const
{
    return bDuelStarted && !bCombatPaused && PlayerFighter &&
        Combat.Get(FighterId::Player).action != Action::Dead;
}

bool ARoninGameMode::CanControl() const
{
    return bDuelStarted && !bCombatPaused && PlayerFighter && EnemyFighter &&
        Combat.Get(FighterId::Player).action != Action::Dead && Combat.Get(FighterId::Enemy).action != Action::Dead;
}

void ARoninGameMode::BeginDefenseCinematic(bool bPerfectDodge)
{
    if (!CanControl() || !GetWorld()) return;
    bPerfectDodgeCue = bPerfectDodge;
    DefenseCueStartedAt = GetWorld()->GetRealTimeSeconds();
    ++DefenseCueSerial;
    HitStop = bPerfectDodge ? 0.f : ParryContactBeat;
    CombatTimeScale = bPerfectDodge ? .25f : 0.f;
    PlayerFighter->PlayDefenseReaction(bPerfectDodge);
    if (auto* PC = Cast<ARoninPlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
        PC->PlayDefenseCue(bPerfectDodge);
}

float ARoninGameMode::SampleDefenseTimeScale() const
{
    if (DefenseCueStartedAt < 0.0 || !GetWorld()) return 1.f;
    // This clock is not combat time. A slow frame or interrupted window cannot
    // strand the game in slow motion, and input/UI/feedback are never dilated.
    const float Age = static_cast<float>(FMath::Max(0.0, GetWorld()->GetRealTimeSeconds() - DefenseCueStartedAt));
    const float HoldEnd = bPerfectDodgeCue ? DodgeSlowHold : ParryContactBeat + ParrySlowHold;
    const float ReturnDuration = bPerfectDodgeCue ? DodgeReturn : ParryReturn;
    const float Minimum = bPerfectDodgeCue ? .25f : .18f;
    const float T = FMath::Clamp((Age - HoldEnd) / ReturnDuration, 0.f, 1.f);
    const float Ease = T * T * (3.f - 2.f * T);
    return FMath::Lerp(Minimum, 1.f, Ease);
}

void ARoninGameMode::ClearDefenseCinematic()
{
    DefenseCueStartedAt = -1.0;
    bPerfectDodgeCue = false;
    HitStop = 0.f;
    Accumulator = 0.f;
    CombatFrameDelta = 0.f;
    CombatTimeScale = bCombatPaused ? 0.f : 1.f;
    if (PlayerFighter) PlayerFighter->CancelDefenseReaction();
}

void ARoninGameMode::Tick(float Dt)
{
    Super::Tick(Dt);
    CombatFrameDelta = 0.f;
    SmoothedFrameTime=FMath::FInterpTo(SmoothedFrameTime,FMath::Max(Dt,.0001f),Dt,3.f);
    DisplayedFPS=1.f/SmoothedFrameTime;
    if (auto* PC=Cast<ARoninPlayerController>(UGameplayStatics::GetPlayerController(this,0))) bUsingGamepad=PC->bUsingGamepad;
    // Asset/shader startup may take arbitrarily long. No combat clock, AI
    // decision, held guard, hit resolution or dodge advances before confirmation.
    if (!bDuelStarted || bCombatPaused || !PlayerFighter || !EnemyFighter)
    {
        CombatTimeScale = bCombatPaused ? 0.f : 1.f;
        UpdateContactSparks(Dt);
        return;
    }
    ObservePlayerAttackStart();
    FeedbackTime=FMath::Max(0.f,FeedbackTime-Dt);
    ImpactFlash=FMath::Max(0.f,ImpactFlash-Dt);
    const float RealDelta = FMath::Max(Dt, 0.f);
    const float ContactDelta = FMath::Min(HitStop, RealDelta);
    HitStop = FMath::Max(0.f, HitStop - RealDelta);
    // Consume only the portion of this frame occupied by contact hit-stop.
    // Its final partial frame can already resume the slowed combat clock.
    const float AvailableFraction = RealDelta > 0.f ? (RealDelta - ContactDelta) / RealDelta : (HitStop > 0.f ? 0.f : 1.f);
    CombatTimeScale = AvailableFraction * SampleDefenseTimeScale();
    if (CombatTimeScale <= 0.f)
    {
        PlayerFighter->SetFrozen(true); EnemyFighter->SetFrozen(true);
        UpdateContactSparks(Dt);
        return;
    }
    PlayerFighter->SetFrozen(false); EnemyFighter->SetFrozen(false);
    Accumulator+=FMath::Min(RealDelta,.1f) * CombatTimeScale;
    constexpr float FixedStep=1.f/120.f;
    while (Accumulator>=FixedStep)
    {
        const uint32 PreviousCue = DefenseCueSerial;
        Step(FixedStep);
        CombatFrameDelta += FixedStep;
        Accumulator-=FixedStep;
        if (HitStop>0.f || PreviousCue != DefenseCueSerial)
        {
            Accumulator=0.f;
            CombatTimeScale = HitStop > 0.f ? 0.f : SampleDefenseTimeScale();
            PlayerFighter->SetFrozen(HitStop > 0.f); EnemyFighter->SetFrozen(HitStop > 0.f);
            break;
        }
    }
    UpdateContactSparks(Dt);
}

void ARoninGameMode::Step(float Dt)
{
    Combat.Tick(Dt);
    ObservePlayerAttackStart();
    PlayerFighter->AdvanceDodge(Dt);
    EnemyFighter->AdvanceDodge(Dt);
    PlayerFighter->AdvanceCombatMovement(Dt);
    EnemyFighter->AdvanceCombatMovement(Dt);
    if (bGuardHeld && Combat.Get(FighterId::Player).action==Action::Idle && !Combat.Get(FighterId::Player).guarding)
        Combat.SetGuard(FighterId::Player,true,false);
    RoundTime+=Dt;
    const float Distance=FVector::Dist2D(PlayerFighter->GetActorLocation(),EnemyFighter->GetActorLocation());
    if (CanControl() && RoundTime>1.5f && Combat.EnemyReady() && Distance<240.f)
    {
        // Predictable short guard beats reward switching to Stone to break posture.
        const auto Kind=Combat.NextEnemyAttack();
        Combat.SetGuard(FighterId::Enemy,false);
        if (Combat.TryAttack(FighterId::Enemy,Kind))
        {
            PlayCue(TEXT("Slash"),EnemyFighter->GetActorLocation(),.55f);
            if (Kind==AttackKind::Unblockable) Notify(TEXT("UNBLOCKABLE  /  DODGE"),FLinearColor(1.f,.2f,.1f),.9f);
        }
    }
    else if (CanControl() && Combat.Get(FighterId::Enemy).action==Action::Idle)
    {
        const auto& Incoming = Combat.Get(FighterId::Player);
        const auto& Defender = Combat.Get(FighterId::Enemy);
        const FVector TowardPlayer = (PlayerFighter->GetActorLocation() - EnemyFighter->GetActorLocation()).GetSafeNormal2D();
        const bool bFacingThreat = FVector::DotProduct(EnemyFighter->GetActorForwardVector(), TowardPlayer) > .25f;
        const bool bPressure = Incoming.action == Action::Active ||
            (Incoming.action == Action::Windup && (Incoming.actionTime >= .08f || Defender.guarding)) ||
            (Incoming.action == Action::Recovery && Defender.guarding);
        const bool bBrace = bPressure && bFacingThreat && Distance <= Incoming.spec.range;
        const bool bRecoveryGuard = !Combat.EnemyReady() && Defender.decisionTime > .35f;
        // React to a visible commitment without interrupting an enemy attack or
        // granting a fresh parry. Maintain the brace through incoming contact.
        const bool bEarnedCounter = Incoming.isCounterAttack &&
            Incoming.attackCounterSource != ronin::CounterSource::None &&
            (Incoming.action == Action::Windup || Incoming.action == Action::Active);
        Combat.SetGuard(FighterId::Enemy, !bEarnedCounter && (bBrace || bRecoveryGuard), false);
    }
    // Earlier active contact wins. Ties use commitment order, never a fixed player priority.
    const auto& PlayerState=Combat.Get(FighterId::Player);
    const auto& EnemyState=Combat.Get(FighterId::Enemy);
    const bool EnemyFirst=EnemyState.action==Action::Active &&
        (PlayerState.action!=Action::Active || EnemyState.actionTime>PlayerState.actionTime ||
        (FMath::IsNearlyEqual(EnemyState.actionTime,PlayerState.actionTime) && EnemyState.attackId<PlayerState.attackId));
    ResolveAttack(EnemyFirst ? FighterId::Enemy : FighterId::Player);
    ResolveAttack(EnemyFirst ? FighterId::Player : FighterId::Enemy);
    if (!bRoundScored && Combat.Get(FighterId::Enemy).action==Action::Dead)
    {
        ++Victories; bRoundScored=true;
        Notify(TEXT("DUEL WON"),FLinearColor(.96f,.78f,.38f),20.f);
    }
    if (!bRoundScored && Combat.Get(FighterId::Player).action==Action::Dead)
    {
        bRoundScored=true;
        Notify(TEXT("FALL. LEARN. RETURN."),FLinearColor(.9f,.25f,.18f),20.f);
    }
}

void ARoninGameMode::ResolveAttack(FighterId Id)
{
    const auto& State=Combat.Get(Id);
    if (State.action!=Action::Active) return;
    auto* Source=Id==FighterId::Player ? PlayerFighter.Get() : EnemyFighter.Get();
    auto* Target=Id==FighterId::Player ? EnemyFighter.Get() : PlayerFighter.Get();
    FVector Delta=Target->GetActorLocation()-Source->GetActorLocation();
    if (FMath::Abs(Delta.Z) > 140.f) return;
    const float ContactDistance = Delta.Size();
    Delta.Z=0;
    const bool Facing=FVector::DotProduct(Source->GetActorForwardVector(),Delta.GetSafeNormal())>.25f;
    const bool bGuardContact=Combat.Get(Id==FighterId::Player ? FighterId::Enemy : FighterId::Player).guarding;
    const auto Result=Combat.ResolveHit(Id,ContactDistance,Facing);
    if (Result==HitResult::None || Result==HitResult::Miss) return;
    const FVector Contact=(Source->GetActorLocation()+Target->GetActorLocation())*.5f+FVector(0,0,25);
    if (Result==HitResult::Dodged)
    {
        Notify(Id==FighterId::Enemy ? TEXT("PERFECT DODGE  /  COUNTER READY") : TEXT("EVADED"),FLinearColor(.48f,.85f,.83f));
        if (Id==FighterId::Enemy)
        {
            BeginDefenseCinematic(true);
            PulseFeedback(this,ronin::HapticEvent::PerfectDodge);
        }
        return;
    }
    const FVector ImpactDirection = Source->GetActorForwardVector();
    const bool bAttackerDeflected = Result == HitResult::Parried ||
        (Result == HitResult::GuardBreak && Combat.Get(Id).action == Action::Broken);
    if (bAttackerDeflected)
    {
        Source->ReceiveImpact(-ImpactDirection, false, true);
    }
    else if (Result == HitResult::Hit || Result == HitResult::Blocked || Result == HitResult::GuardBreak)
    {
        const bool bStrong = State.isCounterAttack || State.comboStep == 4 ||
            State.currentAttack == AttackKind::Heavy || Result == HitResult::GuardBreak;
        Target->ReceiveImpact(ImpactDirection, Result == HitResult::Blocked, bStrong);
    }
    FLinearColor Color(.95f,.49f,.16f);
    if (Result==HitResult::Parried)
    {
        Color=FLinearColor(.7f,.9f,1.f);
        if (Id==FighterId::Enemy)
        {
            ++PerfectParries;
            Notify(TEXT("PERFECT PARRY  /  COUNTER READY"),Color);
            BeginDefenseCinematic(false);
        }
        else { Notify(TEXT("PARRIED"),Color); HitStop=.045f; }
        PlayCue(TEXT("Clash"),Contact,1.15f);
        Source->HitFlash(Color);
        PulseFeedback(this,Id==FighterId::Enemy ? ronin::HapticEvent::Parry : ronin::HapticEvent::Block);
    }
    else if (Result==HitResult::GuardBreak)
    {
        Notify(Combat.Get(FighterId::Enemy).action==Action::Broken ? TEXT("GUARD BROKEN  /  FINISH THEM") : TEXT("YOUR GUARD IS BROKEN"),FLinearColor(1.f,.52f,.15f),2.2f);
        PlayCue(TEXT("Clash"),Contact); HitStop=.09f;
        if (Combat.Get(Id).action==Action::Broken)
        {
            Source->HitFlash(Color);
            if (Id==FighterId::Enemy) { ++PerfectParries; BeginDefenseCinematic(false); }
        }
        else Target->HitFlash(Color);
        PulseFeedback(this,Id==FighterId::Player && State.isCounterAttack && !bAttackerDeflected ?
            ronin::HapticEvent::Counter : ronin::HapticEvent::GuardBreak);
    }
    else if (Result==HitResult::Blocked)
    {
        Notify(TEXT("STEEL MEETS STEEL"),Color,.6f);
        PlayCue(TEXT("Clash"),Contact,.55f); HitStop=.03f;
        PulseFeedback(this,ronin::HapticEvent::Block);
    }
    else
    {
        PlayCue(TEXT("Hit"),Contact,State.isCounterAttack ? 1.15f : 1.f); HitStop=.045f;
        Target->HitFlash(FLinearColor(.9f,.12f,.06f));
        if (Id==FighterId::Enemy) ImpactFlash=.22f;
        PulseFeedback(this,Id==FighterId::Enemy ? ronin::HapticEvent::Damage :
            State.isCounterAttack ? ronin::HapticEvent::Counter : ronin::HapticEvent::Hit);
        if (Id==FighterId::Player && State.isCounterAttack)
            Notify(TEXT("CRITICAL COUNTER"),FLinearColor(1.f,.81f,.42f));
    }
    if (Result==HitResult::Parried || Result==HitResult::Blocked ||
        (Result==HitResult::GuardBreak && bGuardContact))
        SpawnContactSparks(Contact);
}

void ARoninGameMode::StartDuel()
{
    if (bDuelStarted || !PlayerFighter || !EnemyFighter) return;
    bDuelStarted=true;
    bShowHelp=false;
    RestartDuel();
}

void ARoninGameMode::RestartDuel()
{
    ClearContactSparks();
    StopFeedback(this);
    Combat.Reset(); Accumulator=0; HitStop=0; RoundTime=0; PerfectParries=0; bRoundScored=false; LastPlayerAttackId=0;
    bCombatPaused=false; ImpactFlash=0; bGuardHeld=false;
    ClearDefenseCinematic();
    if (PlayerFighter) PlayerFighter->ResetPose(FVector(-300,0,100),FRotator(0,0,0));
    if (EnemyFighter) EnemyFighter->ResetPose(FVector(300,0,100),FRotator(0,180,0));
    FeedbackTime=0.f;
    if (bDuelStarted) Notify(TEXT("READ THE BLADE. FIND YOUR OPENING."),FLinearColor(.9f,.84f,.68f),2.5f);
}
void ARoninGameMode::LightAttack()
{
    if (CanControl()) Combat.TryAttack(FighterId::Player,AttackKind::Light);
}
void ARoninGameMode::HeavyAttack()
{
    if (CanControl()) Combat.TryAttack(FighterId::Player,AttackKind::Heavy);
}
void ARoninGameMode::ObservePlayerAttackStart()
{
    const auto& State = Combat.Get(FighterId::Player);
    if (!PlayerFighter || State.attackId == 0 || State.attackId == LastPlayerAttackId) return;
    LastPlayerAttackId = State.attackId;
    // Consume a canceled start as well, so a guard/dodge pressed in the same
    // input frame cannot produce a delayed swing sound or controller pulse.
    if (State.action != Action::Windup && State.action != Action::Active && State.action != Action::Recovery)
        return;
    const bool bHeavy = State.currentAttack == AttackKind::Heavy || State.isCounterAttack || State.comboStep == 4;
    PlayCue(TEXT("Slash"),PlayerFighter->GetActorLocation(),bHeavy ? 1.15f : 1.f);
    PulseFeedback(this,bHeavy ? ronin::HapticEvent::HeavySwing : ronin::HapticEvent::Swing);
}
void ARoninGameMode::Guard(bool bHeld)
{
    if (!bHeld || CanControl()) { bGuardHeld=bHeld; Combat.SetGuard(FighterId::Player,bHeld); }
}
void ARoninGameMode::Dodge()
{
    if (bCombatPaused) { TogglePause(); return; }
    if (CanControl() && Combat.TryDodge(FighterId::Player)) { PlayerFighter->BeginDodge(); PlayCue(TEXT("Dodge"),PlayerFighter->GetActorLocation()); PulseFeedback(this,ronin::HapticEvent::Dodge); }
}
void ARoninGameMode::Execute()
{
    if (!CanControl()) return;
    const FVector Delta=EnemyFighter->GetActorLocation()-PlayerFighter->GetActorLocation();
    if (FMath::Abs(Delta.Z) <= 120.f && Combat.TryExecute(FighterId::Player,Delta.Size(),FVector::DotProduct(PlayerFighter->GetActorForwardVector(),Delta.GetSafeNormal2D())>.25f))
    {
        PlayCue(TEXT("Execute"),EnemyFighter->GetActorLocation()); HitStop=.13f;
        EnemyFighter->HitFlash(FLinearColor(1.f,.5f,.1f));
        PulseFeedback(this,ronin::HapticEvent::Execute);
    }
}
void ARoninGameMode::ChangeStance(ronin::Stance Stance)
{
    if (CanControl() && Combat.SetStance(FighterId::Player,Stance))
    {
        const TCHAR* Name=Stance==ronin::Stance::Flow ? TEXT("FLOW  /  FAST PRESSURE") :
            Stance==ronin::Stance::Stone ? TEXT("STONE  /  BREAK THEIR GUARD") : TEXT("GALE  /  EVADE AND REPOSITION");
        Notify(Name,FLinearColor(.87f,.8f,.6f),.8f);
    }
}
void ARoninGameMode::TogglePause()
{
    if (!bDuelStarted) return;
    bCombatPaused=!bCombatPaused;
    ClearDefenseCinematic();
    UpdateContactSparks(0.f);
    StopFeedback(this);
    Combat.SetGuard(FighterId::Player,false);
    bGuardHeld=false;
    if (PlayerFighter) PlayerFighter->SetFrozen(bCombatPaused);
    if (EnemyFighter) EnemyFighter->SetFrozen(bCombatPaused);
}
void ARoninGameMode::CycleQuality()
{
    QualityIndex=(QualityIndex+1)%4;
    const TCHAR* Names[]={TEXT("LOW"),TEXT("MEDIUM"),TEXT("HIGH"),TEXT("EPIC")};
    QualityName=Names[QualityIndex];
    if (auto* Settings=UGameUserSettings::GetGameUserSettings())
    {
        Settings->SetOverallScalabilityLevel(QualityIndex);
        Settings->ApplyNonResolutionSettings();
        Settings->SaveSettings();
    }
}
void ARoninGameMode::CycleFrameCap()
{
    FrameCapIndex=(FrameCapIndex+1)%4;
    const float Caps[]={0.f,60.f,120.f,240.f};
    FrameCapName=Caps[FrameCapIndex]==0.f ? TEXT("UNCAPPED") : FString::Printf(TEXT("%d FPS"),static_cast<int32>(Caps[FrameCapIndex]));
    if (auto* Settings=UGameUserSettings::GetGameUserSettings())
    {
        Settings->SetFrameRateLimit(Caps[FrameCapIndex]);
        Settings->SetVSyncEnabled(false);
        Settings->ApplyNonResolutionSettings();
        Settings->SaveSettings();
    }
}
void ARoninGameMode::Notify(const FString& Text,FLinearColor Color,float Duration)
{
    Feedback=Text; FeedbackColor=Color; FeedbackTime=Duration;
}
void ARoninGameMode::CycleHaptics()
{
    HapticsIndex=(HapticsIndex+1)%4;
    const float Values[]={0.f,.35f,.7f,1.f};
    const TCHAR* Names[]={TEXT("OFF"),TEXT("35%"),TEXT("70%"),TEXT("100%")};
    HapticsName=Names[HapticsIndex];
    GConfig->SetFloat(TEXT("Ronin.Input"),TEXT("RumbleIntensity"),Values[HapticsIndex],GGameUserSettingsIni);
    GConfig->Flush(false,GGameUserSettingsIni);
    if (auto* PC=Cast<ARoninPlayerController>(UGameplayStatics::GetPlayerController(this,0)))
    {
        PC->StopRumble(); PC->SetRumbleIntensity(Values[HapticsIndex]);
        PC->PreviewRumble();
    }
    Notify(TEXT("RUMBLE: ")+HapticsName,FLinearColor(.87f,.8f,.6f));
}
void ARoninGameMode::PlayCue(FName Cue,FVector Location,float Volume)
{
    if (const auto* Sound=Sounds.Find(Cue)) UGameplayStatics::PlaySoundAtLocation(this,Sound->Get(),Location,Volume);
}
