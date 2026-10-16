#include "CoreMinimal.h"
#include "RoninFighter.h"
#include "RoninGameMode.h"

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST

#include "Containers/Ticker.h"
#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogRoninCapture, Log, All);

namespace
{
struct FRoninCaptureState
{
    FString OutputPath;
    FString TemporaryPath;
    FString CurrentOutputPath;
    FString RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    double StartedAt = FPlatformTime::Seconds();
    double ReadySince = 0.0;
    double NextStatusAt = 0.0;
    double DeadlineSeconds = 300.0;
    double ScenarioStartedAt = 0.0;
    double ParryObservedAt = 0.0;
    FString Scenario = TEXT("idle");
    FIntPoint ViewportSize = FIntPoint::ZeroValue;
    FDelegateHandle ProcessedHandle;
    bool bRequested = false;
    bool bProcessed = false;
    bool bGuardSent = false;
    bool bMotion = false;
    bool bOwnsFixedStep = false;
    bool bPreviousFixedStep = false;
    double PreviousFixedDelta = 0.0;
    int32 CompletedFrames = 0;
    int32 TargetFrames = 90;
    int32 AttackStarts = 0;
    int32 Contacts = 0;
    int32 Defenses = 0;
    int32 Counters = 0;
    uint32 ComboSteps = 0;
    uint64 LastAttackId = 0;
    uint64 QueuedAttackId = 0;
    uint64 LastContactId = 0;
    uint64 IncomingAttackId = 0;
    bool bDefenseSent = false;
    bool bCounterSent = false;
    int64 TotalBytes = 0;
    FVector ExplorationStart = FVector::ZeroVector;
    FVector ExplorationEnd = FVector::ZeroVector;
    bool bExplorationGrounded = false;
    bool bExplorationFinite = true;
    bool bSuspendedControllerInput = false;
    bool bPreviousControllerTick = false;

    void RestoreControllerInput()
    {
        if (!bSuspendedControllerInput) return;
        UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        if (APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr)
            Player->SetActorTickEnabled(bPreviousControllerTick);
        bSuspendedControllerInput = false;
    }

    void RestoreClock()
    {
        if (!bOwnsFixedStep) return;
        FApp::SetFixedDeltaTime(PreviousFixedDelta);
        FApp::SetUseFixedTimeStep(bPreviousFixedStep);
        bOwnsFixedStep = false;
    }

    bool MotionSucceeded() const
    {
        if (Scenario == TEXT("explore-motion"))
            return bExplorationFinite && bExplorationGrounded && ExplorationEnd.X > 2000.;
        return Scenario == TEXT("combo-motion") ?
            AttackStarts == 5 && ComboSteps == 0x1fu && Contacts == 5 :
            Defenses == 1 && Counters == 1 && Contacts >= 1;
    }

    void Unbind()
    {
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ProcessedHandle);
        ProcessedHandle.Reset();
    }

    bool Finish(bool bSuccess, const TCHAR* Reason)
    {
        Unbind();
        RestoreClock();
        RestoreControllerInput();
        if (Scenario == TEXT("explore-motion"))
            UE_LOG(LogRoninCapture, Display,
                TEXT("RONIN_CAPTURE_EXPLORE_RESULT start=%s end=%s grounded=%d finite=%d success=%d"),
                *ExplorationStart.ToString(), *ExplorationEnd.ToString(), bExplorationGrounded, bExplorationFinite, bSuccess);
        if (bMotion)
            UE_LOG(LogRoninCapture, Display,
                TEXT("RONIN_CAPTURE_MOTION_RESULT scenario=%s frames=%d/%d attacks=%d comboMask=%u contacts=%d defenses=%d counters=%d bytes=%lld success=%d"),
                *Scenario, CompletedFrames, TargetFrames, AttackStarts, ComboSteps, Contacts, Defenses, Counters, TotalBytes, bSuccess);
        if (bSuccess)
        {
            UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_COMPLETE %s (%dx%d, %lld bytes; frames=%d)"),
                *OutputPath, ViewportSize.X, ViewportSize.Y,
                bMotion ? TotalBytes : IFileManager::Get().FileSize(*OutputPath), bMotion ? CompletedFrames : 1);
        }
        else
        {
            UE_LOG(LogRoninCapture, Error, TEXT("RONIN_CAPTURE_FAILED %s: %s"), *OutputPath, Reason);
            IFileManager::Get().Delete(*TemporaryPath, false, false, true);
        }
        FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1, TEXT("ronin.Capture"));
        return false;
    }
};

TWeakPtr<FRoninCaptureState> ActiveCapture;

void StartCapture(const TArray<FString>& Args)
{
    if (ActiveCapture.IsValid())
    {
        UE_LOG(LogRoninCapture, Warning, TEXT("ronin.Capture is already waiting for a screenshot."));
        return;
    }
    if (Args.Num() < 1 || Args.Num() > 3 || FPaths::IsRelative(Args[0]) ||
        !FPaths::GetExtension(Args[0]).Equals(TEXT("png"), ESearchCase::IgnoreCase) ||
        FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
    {
        UE_LOG(LogRoninCapture, Error, TEXT("Usage: ronin.Capture <absolute-output.png> [timeout-seconds] [idle|guard|attack|close|parry|parry-motion|dodge-motion|combo-motion|explore-motion]; a GPU RHI is required."));
        FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("ronin.Capture invalid arguments"));
        return;
    }

    const TSharedRef<FRoninCaptureState> State = MakeShared<FRoninCaptureState>();
    if (Args.Num() >= 2)
    {
        if (!Args[1].IsNumeric() || !LexTryParseString(State->DeadlineSeconds, *Args[1]) ||
            !FMath::IsFinite(State->DeadlineSeconds) || State->DeadlineSeconds < 10.0 || State->DeadlineSeconds > 1800.0)
        {
            UE_LOG(LogRoninCapture, Error, TEXT("Capture timeout must be between 10 and 1800 seconds."));
            FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("ronin.Capture invalid timeout"));
            return;
        }
    }
    if (Args.Num() == 3)
    {
        State->Scenario = Args[2].ToLower();
        if (State->Scenario != TEXT("idle") && State->Scenario != TEXT("guard") &&
            State->Scenario != TEXT("attack") && State->Scenario != TEXT("close") && State->Scenario != TEXT("parry") &&
            State->Scenario != TEXT("parry-motion") && State->Scenario != TEXT("dodge-motion") &&
            State->Scenario != TEXT("combo-motion") && State->Scenario != TEXT("explore-motion"))
        {
            UE_LOG(LogRoninCapture, Error, TEXT("Unknown capture scenario: %s."), *State->Scenario);
            FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("ronin.Capture invalid scenario"));
            return;
        }
    }
    // Waiting for shaders does not need an uncapped gameplay render loop.
    if (IConsoleVariable* FrameLimit = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
        FrameLimit->Set(30.f, ECVF_SetByConsole);
    State->OutputPath = Args[0];
    FPaths::NormalizeFilename(State->OutputPath);
    State->bMotion = State->Scenario.EndsWith(TEXT("-motion"));
    State->TargetFrames = State->Scenario == TEXT("explore-motion") ? 240 : State->Scenario == TEXT("combo-motion") ? 180 : 90;
    State->CurrentOutputPath = State->OutputPath;
    State->TemporaryPath = State->OutputPath + TEXT(".ronin-") + State->RunId + TEXT(".png");
    State->NextStatusAt = State->StartedAt + 10.0;
    const FString Directory = FPaths::GetPath(State->OutputPath);
    if (!IFileManager::Get().DirectoryExists(*Directory) && !IFileManager::Get().MakeDirectory(*Directory, true))
    {
        State->Finish(false, TEXT("Cannot create the output directory."));
        return;
    }
    ActiveCapture = State;
    UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_WAIT %s; waiting for viewport/HUD and 5 shader-idle seconds; deadline %.0f seconds."),
        *State->OutputPath, State->DeadlineSeconds);

    // The ticker owns the state; the screenshot callback only keeps a weak reference.
    // No world, player, or viewport UObject is retained across frames.
    FTSTicker::GetCoreTicker().AddTicker(TEXT("RoninCapture"), 0.0f, [State](float)
    {
        if (IsEngineExitRequested())
        {
            State->Unbind();
            State->RestoreClock();
            State->RestoreControllerInput();
            return false;
        }
        const double Now = FPlatformTime::Seconds();
        if (Now - State->StartedAt >= State->DeadlineSeconds)
        {
            return State->Finish(false, TEXT("Timed out waiting for shaders, the viewport, or screenshot processing."));
        }
        if (State->bProcessed)
        {
            // The processed delegate runs after UE's synchronous image-save attempt.
            // A unique temporary name prevents an older capture from counting as success.
            State->Unbind();
            const int64 Bytes = IFileManager::Get().FileSize(*State->TemporaryPath);
            if (Bytes <= 0 ||
                !IFileManager::Get().Move(*State->CurrentOutputPath, *State->TemporaryPath, true, false, false, true))
            {
                return State->Finish(false, TEXT("Screenshot processing did not produce a writable output PNG."));
            }
            if (!State->bMotion) return State->Finish(true, TEXT(""));
            State->TotalBytes += Bytes;
            ++State->CompletedFrames;
            State->bProcessed = State->bRequested = false;
            if (State->CompletedFrames % 30 == 0)
                UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_FRAMES %s %d/%d"),
                    *State->Scenario, State->CompletedFrames, State->TargetFrames);
            if (State->CompletedFrames == State->TargetFrames)
                return State->Finish(State->MotionSucceeded(), TEXT("Frames saved, but the required gameplay contact chain, defense/counter, or grounded traversal did not occur."));
        }
        if (State->bRequested) return true;

        bool bShadersReady = true;
        int32 RemainingJobs = 0;
#if WITH_EDITOR
        if (GShaderCompilingManager)
        {
            bShadersReady = !GShaderCompilingManager->IsCompiling();
            RemainingJobs = GShaderCompilingManager->GetNumRemainingJobs();
        }
#endif
        UGameViewportClient* Client = GEngine ? GEngine->GameViewport.Get() : nullptr;
        UWorld* World = Client ? Client->GetWorld() : nullptr;
        APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
        const bool bViewportReady = Client && Client->Viewport && World && World->HasBegunPlay() && Player && Player->GetHUD() &&
            Client->Viewport->GetSizeXY().X > 0 && Client->Viewport->GetSizeXY().Y > 0;
        if (Now >= State->NextStatusAt)
        {
            UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_WAIT elapsed=%.0fs viewport=%d shadersReady=%d remainingJobs=%d"),
                Now - State->StartedAt, bViewportReady, bShadersReady, RemainingJobs);
            State->NextStatusAt = Now + 10.0;
        }
        const bool bMotionStarted = State->bMotion && State->ScenarioStartedAt != 0.0;
        if (!bViewportReady || FScreenshotRequest::IsScreenshotRequested() || (!bMotionStarted && !bShadersReady))
        {
            State->ReadySince = 0.0;
            return true;
        }
        if (!bMotionStarted)
        {
            if (State->ReadySince == 0.0) State->ReadySince = Now;
            if (Now - State->ReadySince < 5.0) return true;
        }

        if (State->bMotion)
        {
            ARoninGameMode* Game = World->GetAuthGameMode<ARoninGameMode>();
            if (!Game || !Game->PlayerFighter || !Game->EnemyFighter)
                return State->Finish(false, TEXT("Motion capture requires the RONIN duel."));
            const bool bExplore = State->Scenario == TEXT("explore-motion");
            UInputComponent* ExploreInput = bExplore ? Game->PlayerFighter->FindComponentByClass<UInputComponent>() : nullptr;
            auto InvokeExploreAction = [ExploreInput](FName Name)
            {
                if (!ExploreInput) return false;
                for (int32 Index = 0; Index < ExploreInput->GetNumActionBindings(); ++Index)
                {
                    const FInputActionBinding& Binding = ExploreInput->GetActionBinding(Index);
                    if (Binding.GetActionName() == Name && Binding.KeyEvent == IE_Pressed && Binding.ActionDelegate.IsBound())
                    {
                        Binding.ActionDelegate.Execute(FKey());
                        return true;
                    }
                }
                return false;
            };
            if (!bMotionStarted)
            {
                // Shader startup stays on wall time. Only the settled motion QA
                // sequence uses a fixed simulation frame, including PNG I/O stalls.
                State->bPreviousFixedStep = FApp::UseFixedTimeStep();
                State->PreviousFixedDelta = FApp::GetFixedDeltaTime();
                State->bOwnsFixedStep = true;
                FApp::SetFixedDeltaTime(1.0 / 30.0);
                FApp::SetUseFixedTimeStep(true);
                Game->StartDuel();
                State->ScenarioStartedAt = Now;
                if (bExplore)
                {
                    State->ExplorationStart = Game->PlayerFighter->GetActorLocation();
                    // Keep the other fighter inside the courtyard, off the exit
                    // path. Its normal movement and collision remain enabled.
                    Game->EnemyFighter->ResetPose(State->ExplorationStart + FVector(0, 650, 0), FRotator(0, -90, 0));
                    if (Game->bLockOn && !InvokeExploreAction(TEXT("Lock")))
                        return State->Finish(false, TEXT("Exploration requires the bound lock-on action."));
                    // Feed the held axis ourselves during QA. Suspend the
                    // controller's zero-hardware input tick; World still updates
                    // its camera manager after actor movement every frame.
                    State->bPreviousControllerTick = Player->IsActorTickEnabled();
                    State->bSuspendedControllerInput = true;
                    Player->SetActorTickEnabled(false);
                    UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_EXPLORE_BEGIN start=%s"), *State->ExplorationStart.ToString());
                }
                else
                {
                    Game->EnemyFighter->ResetPose(Game->PlayerFighter->GetActorLocation() +
                        Game->PlayerFighter->GetActorForwardVector() * 190.f,
                        Game->PlayerFighter->GetActorRotation() + FRotator(0.f, 180.f, 0.f));
                    if (State->Scenario == TEXT("combo-motion")) Game->LightAttack();
                    else
                    {
                        const auto Kind = State->Scenario == TEXT("dodge-motion") ? ronin::AttackKind::Unblockable : ronin::AttackKind::Light;
                        if (!Game->Combat.TryAttack(ronin::FighterId::Enemy, Kind))
                            return State->Finish(false, TEXT("The incoming gameplay attack was rejected."));
                        State->IncomingAttackId = Game->Combat.Get(ronin::FighterId::Enemy).attackId;
                    }
                }
                UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_MOTION_BEGIN %s frames=%d fixedDelta=1/30"),
                    *State->Scenario, State->TargetFrames);
            }

            // Suppress only autonomous attack selection in this QA fixture.
            // Movement, reactive blocking, contact and all action phases remain live.
            auto& Enemy = const_cast<ronin::FighterState&>(Game->Combat.Get(ronin::FighterId::Enemy));
            Enemy.decisionTime = 100.f;
            const auto& Fighter = Game->Combat.Get(ronin::FighterId::Player);
            if (bExplore)
            {
                FInputAxisBinding* Forward = ExploreInput ? ExploreInput->AxisBindings.FindByPredicate([](const FInputAxisBinding& Axis)
                    { return Axis.AxisName == TEXT("MoveForward") && Axis.AxisDelegate.IsBound(); }) : nullptr;
                if (!Forward) return State->Finish(false, TEXT("Exploration requires the bound forward axis."));
                Forward->AxisDelegate.Execute(1.f);
                if (State->CompletedFrames == 0 && !InvokeExploreAction(TEXT("Sprint")))
                    return State->Finish(false, TEXT("Exploration requires the bound sprint action."));
                State->ExplorationEnd = Game->PlayerFighter->GetActorLocation();
                State->bExplorationFinite &= !State->ExplorationEnd.ContainsNaN();
                State->bExplorationGrounded = Game->PlayerFighter->GetCharacterMovement()->IsMovingOnGround();
            }
            else if (State->Scenario != TEXT("combo-motion"))
            {
                if (!State->bDefenseSent && Enemy.attackId == State->IncomingAttackId &&
                    Enemy.action == ronin::Action::Windup && Enemy.actionDuration - Enemy.actionTime <= .04f)
                {
                    if (State->Scenario == TEXT("parry-motion")) Game->Guard(true);
                    else
                    {
                        // Use the bound movement action to choose a sidestep, then
                        // release it immediately. BeginDodge stores the direction.
                        UInputComponent* Input = Game->PlayerFighter->FindComponentByClass<UInputComponent>();
                        FInputAxisBinding* Strafe = Input ? Input->AxisBindings.FindByPredicate([](const FInputAxisBinding& Axis)
                            { return Axis.AxisName == TEXT("MoveRight") && Axis.AxisDelegate.IsBound(); }) : nullptr;
                        if (!Strafe) return State->Finish(false, TEXT("Dodge motion requires the bound MoveRight axis."));
                        Strafe->AxisDelegate.Execute(1.f);
                        Game->Dodge();
                        Strafe->AxisDelegate.Execute(0.f);
                    }
                    State->bDefenseSent = true;
                }
                const bool bExpectedDefense = Enemy.attackId == State->IncomingAttackId &&
                    (State->Scenario == TEXT("dodge-motion") ? Enemy.lastHit == ronin::HitResult::Dodged :
                        (Enemy.lastHit == ronin::HitResult::Parried ||
                            (Enemy.lastHit == ronin::HitResult::GuardBreak && Enemy.action == ronin::Action::Broken)));
                if (bExpectedDefense) State->Defenses = 1;
                const float CounterWindow = State->Scenario == TEXT("dodge-motion") ?
                    ronin::CombatCore::DodgeCounterWindow : ronin::CombatCore::ParryCounterWindow;
                if (State->Defenses && !State->bCounterSent && Fighter.action == ronin::Action::Idle &&
                    Fighter.counterTime > 0.f && Fighter.counterTime <= CounterWindow - .08f)
                {
                    Game->Guard(false);
                    Game->LightAttack();
                    State->bCounterSent = true;
                }
            }
            if (Fighter.attackId && Fighter.attackId != State->LastAttackId)
            {
                State->LastAttackId = Fighter.attackId;
                ++State->AttackStarts;
                if (Fighter.isCounterAttack) ++State->Counters;
                if (Fighter.currentAttack == ronin::AttackKind::Light && Fighter.comboStep < 5)
                    State->ComboSteps |= 1u << Fighter.comboStep;
                UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_ACTION frame=%d attack=%llu step=%d counter=%d"),
                    State->CompletedFrames, static_cast<unsigned long long>(Fighter.attackId), Fighter.comboStep + 1, Fighter.isCounterAttack);
            }
            if (Fighter.attackId != State->LastContactId &&
                (Fighter.lastHit == ronin::HitResult::Hit || Fighter.lastHit == ronin::HitResult::Blocked || Fighter.lastHit == ronin::HitResult::GuardBreak))
            {
                State->LastContactId = Fighter.attackId;
                ++State->Contacts;
                UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_CONTACT frame=%d attack=%llu result=%d"),
                    State->CompletedFrames, static_cast<unsigned long long>(Fighter.attackId), static_cast<int32>(Fighter.lastHit));
            }
            if (State->Scenario == TEXT("combo-motion") && Fighter.comboStep < 4 &&
                Fighter.attackId != State->QueuedAttackId && !Fighter.comboQueued &&
                (Fighter.action == ronin::Action::Active || Fighter.action == ronin::Action::Recovery))
            {
                State->QueuedAttackId = Fighter.attackId;
                Game->LightAttack();
            }
            const FString Stem = FPaths::Combine(FPaths::GetPath(State->OutputPath), FPaths::GetBaseFilename(State->OutputPath));
            State->CurrentOutputPath = FString::Printf(TEXT("%s-%04d.png"), *Stem, State->CompletedFrames);
            State->TemporaryPath = State->CurrentOutputPath + TEXT(".ronin-") + State->RunId + TEXT(".png");
        }

        // Exercise the game's actual actions after startup has settled. Do not
        // assign animation poses or phase times just to manufacture a screenshot.
        if (!State->bMotion && State->Scenario != TEXT("idle"))
        {
            ARoninGameMode* Game = World->GetAuthGameMode<ARoninGameMode>();
            if (!Game || !Game->PlayerFighter || !Game->EnemyFighter)
                return State->Finish(false, TEXT("Capture scenario requires the RONIN duel."));
            if (State->ScenarioStartedAt == 0.0)
            {
                Game->StartDuel();
                if (State->Scenario == TEXT("guard")) Game->Guard(true);
                if (State->Scenario == TEXT("attack")) Game->LightAttack();
                if (State->Scenario == TEXT("close") || State->Scenario == TEXT("parry"))
                {
                    Game->EnemyFighter->ResetPose(Game->PlayerFighter->GetActorLocation() +
                        Game->PlayerFighter->GetActorForwardVector() * 190.f,
                        Game->PlayerFighter->GetActorRotation() + FRotator(0.f, 180.f, 0.f));
                }
                State->ScenarioStartedAt = Now;
                return true;
            }
            if (Now - State->ScenarioStartedAt > 3.0)
                return State->Finish(false, TEXT("The requested gameplay scenario did not reach its capture state."));
            if (State->Scenario == TEXT("attack"))
            {
                const auto& Fighter = Game->Combat.Get(ronin::FighterId::Player);
                if (Fighter.action != ronin::Action::Active || Fighter.actionTime < Fighter.actionDuration * .4f)
                    return true;
            }
            else if (State->Scenario == TEXT("parry"))
            {
                const auto& Enemy = Game->Combat.Get(ronin::FighterId::Enemy);
                if (!State->bGuardSent && Enemy.action == ronin::Action::Windup &&
                    Enemy.actionDuration-Enemy.actionTime <= .08f)
                {
                    Game->Guard(true);
                    State->bGuardSent = true;
                }
                if (Game->PerfectParries == 0) return true;
                if (State->ParryObservedAt == 0.0) State->ParryObservedAt = Now;
                if (Now-State->ParryObservedAt < .05) return true;
            }
            else if (Now - State->ScenarioStartedAt < .3) return true;
        }

        State->ViewportSize = Client->Viewport->GetSizeXY();
        const TWeakPtr<FRoninCaptureState> WeakState = State;
        State->ProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([WeakState]
        {
            if (const TSharedPtr<FRoninCaptureState> Capture = WeakState.Pin()) Capture->bProcessed = true;
        });
        State->bRequested = true;
        if (!State->bMotion || State->CompletedFrames == 0)
            UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_REQUEST %s (%dx%d, canvas HUD included)"),
                *State->CurrentOutputPath, State->ViewportSize.X, State->ViewportSize.Y);
        // Canvas HUD is already in the viewport. bShowUI=false selects GPU viewport
        // readback instead of Slate's native-window screenshot path in offscreen mode.
        FScreenshotRequest::RequestScreenshot(State->TemporaryPath, false, false, false);
        return true;
    });
}

FAutoConsoleCommand CaptureCommand(TEXT("ronin.Capture"),
    TEXT("Capture actual gameplay after shaders settle, then exit. Args: absolute PNG, timeout (10-1800; default 300), scenario (idle|guard|attack|close|parry|parry-motion|dodge-motion|combo-motion|explore-motion; default idle). Motion writes stem-0000.png onward at fixed 30 fps: 90 defense frames, 180 five-cut frames, or 240 exploration frames."),
    FConsoleCommandWithArgsDelegate::CreateStatic(&StartCapture), ECVF_Cheat);
}

#endif
