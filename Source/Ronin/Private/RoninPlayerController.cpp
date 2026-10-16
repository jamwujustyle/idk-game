#include "RoninPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/Pawn.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "RoninGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

ARoninPlayerController::ARoninPlayerController() { bForceFeedbackEnabled=true; }

void ARoninPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
        InputDeviceConnectionHandle=IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange()
            .AddUObject(this,&ARoninPlayerController::HandleInputDeviceConnectionChange);
}

bool ARoninPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    // Ignore analog stick drift when choosing which button prompts to show.
    if (!Params.Key.IsAnalog() || FMath::Abs(Params.AmountDepressed)>.2f)
    {
        bUsingGamepad=Params.IsGamepad();
        if (Params.IsGamepad() && !Params.IsSimulatedInput() && Params.InputDevice!=INPUTDEVICEID_NONE)
            LastGamepadDevice=Params.InputDevice;
    }
    return Super::InputKey(Params);
}

void ARoninPlayerController::PlayerTick(float Dt)
{
    Super::PlayerTick(Dt);
    if (!IsLocalController()) return;
    const auto* ViewportClient=GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    const bool bHasFocus=ViewportClient && ViewportClient->Viewport && ViewportClient->Viewport->HasFocus();
    if (!bHasFocus)
    {
        Rumble.Stop();
        // Clear held controls once on the transition. Offscreen unattended runs
        // must keep simulating, while feedback is still silenced without focus.
        if (bHadViewportFocus) ClearHeldInputs(!FApp::IsUnattended());
    }
    bHadViewportFocus=bHasFocus;
    Rumble.Tick(Dt);
    ApplyRumble();
}

void ARoninPlayerController::ClearHeldInputs(bool bPauseIfActive)
{
    if (PlayerInput) PlayerInput->FlushPressedKeys();
    if (APawn* ControlledPawn=GetPawn()) ControlledPawn->ConsumeMovementInputVector();
    if (auto* Game=Cast<ARoninGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        // Clearing the core's guard also clears the game's physical hold latch.
        Game->Guard(false);
        if (bPauseIfActive && Game->CanControl()) Game->TogglePause();
    }
}

void ARoninPlayerController::HandleInputDeviceConnectionChange(EInputDeviceConnectionState NewState,
    FPlatformUserId /*PlatformUser*/, FInputDeviceId DeviceId)
{
    if (!IsLocalController() || NewState!=EInputDeviceConnectionState::Disconnected ||
        DeviceId==INPUTDEVICEID_NONE || DeviceId!=LastGamepadDevice)
        return;

    const bool bWasActiveGamepad=bUsingGamepad;
    LastGamepadDevice=INPUTDEVICEID_NONE;
    bUsingGamepad=false;
    StopRumble();
    ClearHeldInputs(bWasActiveGamepad);
}

void ARoninPlayerController::Pulse(ronin::HapticEvent Event)
{
    if (bUsingGamepad) Rumble.Trigger(Event);
}

void ARoninPlayerController::ApplyRumble()
{
    const auto Output=Rumble.GetOutput();
    const float Values[]={Output.leftLarge,Output.leftSmall,Output.rightLarge,Output.rightSmall};
    for (int32 I=0; I<4; ++I)
    {
        const auto Action=Values[I]<=0.f ? EDynamicForceFeedbackAction::Stop : EDynamicForceFeedbackAction::Start;
        // Start with the existing handle renews its short lease; Update changes
        // intensity without resetting elapsed time, which would cause periodic gaps.
        // If delivery stops unexpectedly, the last lease still expires in the engine.
        RumbleHandles[I]=PlayDynamicForceFeedback(Values[I],.08f,I==0,I==1,I==2,I==3,Action,RumbleHandles[I]);
    }
}

void ARoninPlayerController::StopRumble() { Rumble.Stop(); ApplyRumble(); }
void ARoninPlayerController::SetRumbleIntensity(float Intensity) { Rumble.SetIntensity(Intensity); ApplyRumble(); }
void ARoninPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (InputDeviceConnectionHandle.IsValid())
    {
        IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(InputDeviceConnectionHandle);
        InputDeviceConnectionHandle.Reset();
    }
    StopRumble();
    Super::EndPlay(Reason);
}
