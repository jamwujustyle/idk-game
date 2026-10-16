#include "RoninGameMode.h"
#include "RoninArena.h"
#include "RoninFighter.h"
#include "RoninHUD.h"
#include "RoninPlayerController.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "DrawDebugHelpers.h"
#include "Misc/ConfigCacheIni.h"

using ronin::FighterId;
using ronin::Action;
using ronin::AttackKind;
using ronin::HitResult;

namespace
{
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
}

void ARoninGameMode::StartPlay()
{
    Super::StartPlay();
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
    }
    float SavedRumble=.7f;
    GConfig->GetFloat(TEXT("Ronin.Input"),TEXT("RumbleIntensity"),SavedRumble,GGameUserSettingsIni);
    const float Strengths[]={0.f,.35f,.7f,1.f};
    const TCHAR* StrengthNames[]={TEXT("OFF"),TEXT("35%"),TEXT("70%"),TEXT("100%")};
    for (int32 I=0; I<4; ++I) if (FMath::IsNearlyEqual(SavedRumble,Strengths[I])) HapticsIndex=I;
    HapticsName=StrengthNames[HapticsIndex];
    if (auto* PC=Cast<ARoninPlayerController>(UGameplayStatics::GetPlayerController(this,0))) PC->SetRumbleIntensity(Strengths[HapticsIndex]);
    RestartDuel();
}

bool ARoninGameMode::CanControl() const
{
    return bDuelStarted && !bCombatPaused && PlayerFighter && EnemyFighter &&
        Combat.Get(FighterId::Player).action != Action::Dead && Combat.Get(FighterId::Enemy).action != Action::Dead;
}

void ARoninGameMode::Tick(float Dt)
{
    Super::Tick(Dt);
    SmoothedFrameTime=FMath::FInterpTo(SmoothedFrameTime,FMath::Max(Dt,.0001f),Dt,3.f);
    DisplayedFPS=1.f/SmoothedFrameTime;
    if (auto* PC=Cast<ARoninPlayerController>(UGameplayStatics::GetPlayerController(this,0))) bUsingGamepad=PC->bUsingGamepad;
    // Asset/shader startup may take arbitrarily long. No combat clock, AI
    // decision, held guard, hit resolution or dodge advances before confirmation.
    if (!bDuelStarted || bCombatPaused || !PlayerFighter || !EnemyFighter) return;
    FeedbackTime=FMath::Max(0.f,FeedbackTime-Dt);
    ImpactFlash=FMath::Max(0.f,ImpactFlash-Dt);
    if (HitStop>0.f)
    {
        HitStop-=Dt;
        PlayerFighter->SetFrozen(true); EnemyFighter->SetFrozen(true);
        return;
    }
    PlayerFighter->SetFrozen(false); EnemyFighter->SetFrozen(false);
    Accumulator+=FMath::Min(Dt,.1f);
    constexpr float FixedStep=1.f/120.f;
    while (Accumulator>=FixedStep)
    {
        Step(FixedStep);
        Accumulator-=FixedStep;
        if (HitStop>0.f)
        {
            Accumulator=0.f;
            PlayerFighter->SetFrozen(true); EnemyFighter->SetFrozen(true);
            break;
        }
    }
}

void ARoninGameMode::Step(float Dt)
{
    Combat.Tick(Dt);
    PlayerFighter->AdvanceDodge(Dt);
    EnemyFighter->AdvanceDodge(Dt);
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
    else if (CanControl() && Combat.Get(FighterId::Enemy).action==Action::Idle && !Combat.EnemyReady())
    {
        // Only the opening of the recovery interval raises guard; it cannot parry every frame.
        Combat.SetGuard(FighterId::Enemy,Combat.Get(FighterId::Enemy).decisionTime>.35f);
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
    Delta.Z=0;
    const bool Facing=FVector::DotProduct(Source->GetActorForwardVector(),Delta.GetSafeNormal())>.25f;
    const auto Result=Combat.ResolveHit(Id,Delta.Size(),Facing);
    if (Result==HitResult::None || Result==HitResult::Miss) return;
    const FVector Contact=(Source->GetActorLocation()+Target->GetActorLocation())*.5f+FVector(0,0,25);
    if (Result==HitResult::Dodged) { Notify(TEXT("CLEAN EVADE"),FLinearColor(.48f,.85f,.83f)); return; }
    FLinearColor Color(.95f,.49f,.16f);
    if (Result==HitResult::Parried)
    {
        Color=FLinearColor(.7f,.9f,1.f);
        if (Id==FighterId::Enemy) { ++PerfectParries; Notify(TEXT("PERFECT PARRY  /  COUNTER"),Color); }
        else Notify(TEXT("PARRIED"),Color);
        PlayCue(TEXT("Clash"),Contact); HitStop=.065f;
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
            if (Id==FighterId::Enemy) ++PerfectParries;
        }
        else Target->HitFlash(Color);
        PulseFeedback(this,ronin::HapticEvent::GuardBreak);
    }
    else if (Result==HitResult::Blocked)
    {
        Notify(TEXT("STEEL MEETS STEEL"),Color,.6f);
        PlayCue(TEXT("Clash"),Contact,.55f); HitStop=.03f;
        PulseFeedback(this,ronin::HapticEvent::Block);
    }
    else
    {
        PlayCue(TEXT("Hit"),Contact); HitStop=.045f;
        Target->HitFlash(FLinearColor(.9f,.12f,.06f));
        if (Id==FighterId::Enemy) ImpactFlash=.22f;
        PulseFeedback(this,Id==FighterId::Enemy ? ronin::HapticEvent::Damage : ronin::HapticEvent::Block);
    }
    // Brief geometric sparks are deliberately temporary prototype VFX.
    for (int32 I=0; I<9; ++I)
    {
        const float Angle=I*UE_TWO_PI/9.f;
        const FVector Ray(FMath::Cos(Angle)*65.f,FMath::Sin(Angle)*65.f,(I%3)*22.f);
        DrawDebugLine(GetWorld(),Contact,Contact+Ray,Color.ToFColor(true),false,.1f,0,2.f);
    }
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
    StopFeedback(this);
    Combat.Reset(); Accumulator=0; HitStop=0; RoundTime=0; PerfectParries=0; bRoundScored=false;
    bCombatPaused=false; ImpactFlash=0; bGuardHeld=false;
    if (PlayerFighter) PlayerFighter->ResetPose(FVector(-300,0,100),FRotator(0,0,0));
    if (EnemyFighter) EnemyFighter->ResetPose(FVector(300,0,100),FRotator(0,180,0));
    FeedbackTime=0.f;
    if (bDuelStarted) Notify(TEXT("READ THE BLADE. FIND YOUR OPENING."),FLinearColor(.9f,.84f,.68f),2.5f);
}
void ARoninGameMode::LightAttack()
{
    if (CanControl() && Combat.TryAttack(FighterId::Player,AttackKind::Light)) { PlayCue(TEXT("Slash"),PlayerFighter->GetActorLocation()); PulseFeedback(this,ronin::HapticEvent::Swing); }
}
void ARoninGameMode::HeavyAttack()
{
    if (CanControl() && Combat.TryAttack(FighterId::Player,AttackKind::Heavy)) { PlayCue(TEXT("Slash"),PlayerFighter->GetActorLocation(),1.15f); PulseFeedback(this,ronin::HapticEvent::Swing); }
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
    if (Combat.TryExecute(FighterId::Player,Delta.Size2D(),FVector::DotProduct(PlayerFighter->GetActorForwardVector(),Delta.GetSafeNormal2D())>.25f))
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
    }
    Notify(TEXT("RUMBLE: ")+HapticsName,FLinearColor(.87f,.8f,.6f));
}
void ARoninGameMode::PlayCue(FName Cue,FVector Location,float Volume)
{
    if (const auto* Sound=Sounds.Find(Cue)) UGameplayStatics::PlaySoundAtLocation(this,Sound->Get(),Location,Volume);
}
