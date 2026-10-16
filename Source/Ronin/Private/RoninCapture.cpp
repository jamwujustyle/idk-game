#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
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
    double StartedAt = FPlatformTime::Seconds();
    double ReadySince = 0.0;
    double NextStatusAt = 0.0;
    double DeadlineSeconds = 300.0;
    FIntPoint ViewportSize = FIntPoint::ZeroValue;
    FDelegateHandle ProcessedHandle;
    bool bRequested = false;
    bool bProcessed = false;

    void Unbind()
    {
        FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ProcessedHandle);
        ProcessedHandle.Reset();
    }

    bool Finish(bool bSuccess, const TCHAR* Reason)
    {
        Unbind();
        if (bSuccess)
        {
            UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_COMPLETE %s (%dx%d, %lld bytes)"),
                *OutputPath, ViewportSize.X, ViewportSize.Y, IFileManager::Get().FileSize(*OutputPath));
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
    if ((Args.Num() != 1 && Args.Num() != 2) || FPaths::IsRelative(Args[0]) ||
        !FPaths::GetExtension(Args[0]).Equals(TEXT("png"), ESearchCase::IgnoreCase) ||
        FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
    {
        UE_LOG(LogRoninCapture, Error, TEXT("Usage: ronin.Capture <absolute-output.png> [timeout-seconds]; a GPU RHI is required."));
        FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("ronin.Capture invalid arguments"));
        return;
    }

    const TSharedRef<FRoninCaptureState> State = MakeShared<FRoninCaptureState>();
    if (Args.Num() == 2)
    {
        if (!Args[1].IsNumeric() || !LexTryParseString(State->DeadlineSeconds, *Args[1]) ||
            !FMath::IsFinite(State->DeadlineSeconds) || State->DeadlineSeconds < 10.0 || State->DeadlineSeconds > 1800.0)
        {
            UE_LOG(LogRoninCapture, Error, TEXT("Capture timeout must be between 10 and 1800 seconds."));
            FPlatformMisc::RequestExitWithStatus(false, 1, TEXT("ronin.Capture invalid timeout"));
            return;
        }
    }
    // Waiting for shaders does not need an uncapped gameplay render loop.
    if (IConsoleVariable* FrameLimit = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
        FrameLimit->Set(30.f, ECVF_SetByConsole);
    State->OutputPath = Args[0];
    FPaths::NormalizeFilename(State->OutputPath);
    State->TemporaryPath = State->OutputPath + TEXT(".ronin-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".png");
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
            if (IFileManager::Get().FileSize(*State->TemporaryPath) <= 0 ||
                !IFileManager::Get().Move(*State->OutputPath, *State->TemporaryPath, true, false, false, true))
            {
                return State->Finish(false, TEXT("Screenshot processing did not produce a writable output PNG."));
            }
            return State->Finish(true, TEXT(""));
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
        if (!bShadersReady || !bViewportReady || FScreenshotRequest::IsScreenshotRequested())
        {
            State->ReadySince = 0.0;
            return true;
        }
        if (State->ReadySince == 0.0) State->ReadySince = Now;
        if (Now - State->ReadySince < 5.0) return true;

        State->ViewportSize = Client->Viewport->GetSizeXY();
        const TWeakPtr<FRoninCaptureState> WeakState = State;
        State->ProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([WeakState]
        {
            if (const TSharedPtr<FRoninCaptureState> Capture = WeakState.Pin()) Capture->bProcessed = true;
        });
        State->bRequested = true;
        UE_LOG(LogRoninCapture, Display, TEXT("RONIN_CAPTURE_REQUEST %s (%dx%d, canvas HUD included)"),
            *State->OutputPath, State->ViewportSize.X, State->ViewportSize.Y);
        // Canvas HUD is already in the viewport. bShowUI=false selects GPU viewport
        // readback instead of Slate's native-window screenshot path in offscreen mode.
        FScreenshotRequest::RequestScreenshot(State->TemporaryPath, false, false, false);
        return true;
    });
}

FAutoConsoleCommand CaptureCommand(TEXT("ronin.Capture"),
    TEXT("Capture the game viewport and HUD to an absolute PNG path after shaders settle, then exit. Optional timeout: 10-1800 seconds; default 300."),
    FConsoleCommandWithArgsDelegate::CreateStatic(&StartCapture), ECVF_Cheat);
}

#endif
