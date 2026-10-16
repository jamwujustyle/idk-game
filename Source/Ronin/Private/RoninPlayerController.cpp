#include "RoninPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/Pawn.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "RoninGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoninFeedback, Log, All);

URoninImpactCameraModifier::URoninImpactCameraModifier()
{
    Impulse.SetIntensity(1.f);
    Priority=10;
}

void URoninImpactCameraModifier::Pulse(ronin::HapticEvent Event)
{
    float Scale=0.f;
    switch (Event)
    {
    case ronin::HapticEvent::Hit: Scale=.5f; break;
    case ronin::HapticEvent::Counter: Scale=1.1f; break;
    case ronin::HapticEvent::Block: Scale=.4f; break;
    case ronin::HapticEvent::PerfectDodge: Scale=.18f; break;
    case ronin::HapticEvent::Parry: Scale=.8f; break;
    case ronin::HapticEvent::Damage: Scale=1.f; break;
    case ronin::HapticEvent::GuardBreak: Scale=1.2f; break;
    case ronin::HapticEvent::Execute: Scale=1.5f; break;
    default: return; // Movement alone never shakes the camera.
    }
    if (Impulse.Trigger(Event)) ImpactScale=Scale;
}

bool URoninImpactCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
    const auto Output=Impulse.GetOutput();
    const float Weight=ImpactScale*FMath::Clamp(Output.leftLarge+.35f*Output.leftSmall,0.f,1.f);
    // The camera manager supplies a fresh base view each frame; these small
    // offsets decay to zero without accumulating rotation or affecting lock-on.
    InOutPOV.Location-=InOutPOV.Rotation.Vector()*(1.6f*Weight);
    InOutPOV.Rotation.Pitch+=.45f*Weight;
    InOutPOV.FOV+=.45f*Weight;
    if (DefenseAge >= 0.f)
    {
        const float Duration = bDodgeBeat ? .3f : .52f;
        const float ReturnDuration = bDodgeBeat ? .2f : .15f;
        const float Phase = FMath::Clamp((DefenseAge - (Duration - ReturnDuration)) / ReturnDuration, 0.f, 1.f);
        const float Release = 1.f - Phase * Phase * (3.f - 2.f * Phase);
        const float Attack = FMath::Clamp(DefenseAge / .035f, 0.f, 1.f);
        const float Emphasis = Attack * Attack * (3.f - 2.f * Attack) * Release;
        // Push along the existing view and hold the clash briefly in frame.
        // These offsets never accumulate into control rotation or orbit either fighter.
        InOutPOV.FOV -= (bDodgeBeat ? 4.f : 8.f) * Emphasis;
        InOutPOV.Location += InOutPOV.Rotation.Vector() * ((bDodgeBeat ? 24.f : 60.f) * Emphasis);
        InOutPOV.Rotation.Yaw += (bDodgeBeat ? .8f : 2.f) * Emphasis;
        InOutPOV.Rotation.Pitch -= (bDodgeBeat ? .4f : 1.f) * Emphasis;
        DefenseAge += FMath::Max(DeltaTime, 0.f);
        if (DefenseAge >= Duration) DefenseAge = -1.f;
    }
    Impulse.Tick(DeltaTime);
    return false;
}

void URoninImpactCameraModifier::PlayDefenseBeat(bool bPerfectDodge)
{
    DefenseAge = 0.f;
    bDodgeBeat = bPerfectDodge;
}

void URoninImpactCameraModifier::Clear() { Impulse.Stop(); ImpactScale=0.f; DefenseAge=-1.f; }

ARoninPlayerController::ARoninPlayerController() { bForceFeedbackEnabled=true; }

void ARoninPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        InputDeviceConnectionHandle=IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange()
            .AddUObject(this,&ARoninPlayerController::HandleInputDeviceConnectionChange);
        if (PlayerCameraManager)
            ImpactCamera=Cast<URoninImpactCameraModifier>(PlayerCameraManager->AddNewCameraModifier(
                URoninImpactCameraModifier::StaticClass()));
    }
}

bool ARoninPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    // Ignore analog stick drift when choosing which button prompts to show.
    if (!Params.Key.IsAnalog() || FMath::Abs(Params.AmountDepressed)>.2f)
    {
        if (bUsingGamepad && !Params.IsGamepad())
        {
            Rumble.Stop();
            ApplyRumble();
        }
        bUsingGamepad=Params.IsGamepad();
        if (Params.IsGamepad() && !Params.IsSimulatedInput())
        {
            if (!bReceivedGamepadInput || LastGamepadDevice != Params.InputDevice)
            {
                UE_LOG(LogRoninFeedback, Display, TEXT("Physical gamepad input received: device=%d, player input index=%d, mapper state=%d"),
                    Params.InputDevice.GetId(), GetInputIndex(),
                    static_cast<int32>(IPlatformInputDeviceMapper::Get().GetInputDeviceConnectionState(Params.InputDevice)));
                bLoggedFeedbackRoute = false;
            }
            bReceivedGamepadInput = true;
            LastGamepadDevice=Params.InputDevice;
        }
    }
    return Super::InputKey(Params);
}

void ARoninPlayerController::PlayerTick(float Dt)
{
    if (!IsLocalController()) { Super::PlayerTick(Dt); return; }
    const auto* ViewportClient=GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    const bool bHasFocus=ViewportClient && ViewportClient->Viewport && ViewportClient->Viewport->HasFocus();
    if (!bHasFocus)
    {
        // Offscreen captures still need visual reactions, but must never send
        // physical controller feedback without a focused game viewport.
        if (FApp::IsUnattended()) SilenceControllerRumble();
        else StopRumble();
        // Clear held controls once on the transition. Offscreen unattended runs
        // must keep simulating, while feedback is still silenced without focus.
        if (bHadViewportFocus) ClearHeldInputs(!FApp::IsUnattended());
    }
    bHadViewportFocus=bHasFocus;
    Super::PlayerTick(Dt);
}

void ARoninPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
    Super::PostProcessInput(DeltaTime,bGamePaused);
    if (!IsLocalController()) return;
    if (bGamePaused) StopRumble();
    else if (!bHadViewportFocus)
    {
        if (FApp::IsUnattended()) SilenceControllerRumble();
        else StopRumble();
    }
    // This runs after action bindings but before UE processes force feedback.
    // Submit the attack/contact onset this frame, then advance its next sample.
    // A lease longer than this frame also survives a slow frame's engine update.
    ApplyRumble(FMath::Max(.08f,DeltaTime+.01f));
    Rumble.Tick(DeltaTime);
}

void ARoninPlayerController::ClearHeldInputs(bool bPauseIfActive)
{
    StopRumble();
    if (PlayerInput) PlayerInput->FlushPressedKeys();
    if (APawn* ControlledPawn=GetPawn()) ControlledPawn->ConsumeMovementInputVector();
    if (auto* Game=Cast<ARoninGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        // Clearing the core's guard also clears the game's physical hold latch.
        Game->Guard(false);
        Game->ClearDefenseCinematic();
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
    bReceivedGamepadInput=false;
    bLoggedFeedbackRoute=false;
    bUsingGamepad=false;
    StopRumble();
    ClearHeldInputs(bWasActiveGamepad);
}

void ARoninPlayerController::Pulse(ronin::HapticEvent Event)
{
    if (!IsLocalController()) return;
    SubmitRumble(Event);
    if (ImpactCamera && (bHadViewportFocus || FApp::IsUnattended())) ImpactCamera->Pulse(Event);
}

void ARoninPlayerController::SubmitRumble(ronin::HapticEvent Event)
{
    if (CanSendGamepadFeedback() && Rumble.Trigger(Event))
    {
        LastSubmittedFeedback = Event;
        ++SubmittedFeedbackSerial;
        ApplyRumble();
        if (!bLoggedFeedbackRoute)
        {
            UE_LOG(LogRoninFeedback, Display, TEXT("Submitted rumble to UE player input index=%d at intensity=%g; physical motor response is not measured."),
                GetInputIndex(), Rumble.GetIntensity());
            bLoggedFeedbackRoute = true;
        }
    }
}

void ARoninPlayerController::PreviewRumble() { SubmitRumble(ronin::HapticEvent::Parry); }

void ARoninPlayerController::PlayDefenseCue(bool bPerfectDodge)
{
    if (IsLocalController() && ImpactCamera && (bHadViewportFocus || FApp::IsUnattended()))
        ImpactCamera->PlayDefenseBeat(bPerfectDodge);
}

bool ARoninPlayerController::CanSendGamepadFeedback() const
{
    if (!IsLocalController() || FApp::IsUnattended() || !bHadViewportFocus || !bUsingGamepad ||
        !bReceivedGamepadInput || GetInputIndex() == INDEX_NONE)
        return false;
    // This controller already received the physical gamepad event through UE's
    // input routing. Linux sends feedback via GetInputIndex itself; requiring a
    // second exact mapper match can reject legacy events with incomplete metadata.
    // Unknown metadata is allowed, while an explicit disconnect always silences it.
    return LastGamepadDevice == INPUTDEVICEID_NONE ||
        IPlatformInputDeviceMapper::Get().GetInputDeviceConnectionState(LastGamepadDevice) != EInputDeviceConnectionState::Disconnected;
}

void ARoninPlayerController::ApplyRumble(float LeaseSeconds)
{
    const auto Output=CanSendGamepadFeedback() ? Rumble.GetOutput() : ronin::HapticOutput{};
    const float Values[]={Output.leftLarge,Output.leftSmall,Output.rightLarge,Output.rightSmall};
    for (int32 I=0; I<4; ++I)
    {
        const auto Action=Values[I]<=0.f ? EDynamicForceFeedbackAction::Stop : EDynamicForceFeedbackAction::Start;
        // Start with the existing handle renews its short lease; Update changes
        // intensity without resetting elapsed time, which would cause periodic gaps.
        // If delivery stops unexpectedly, the last lease still expires in the engine.
        RumbleHandles[I]=PlayDynamicForceFeedback(Values[I],LeaseSeconds,I==0,I==1,I==2,I==3,Action,RumbleHandles[I]);
    }
}

void ARoninPlayerController::SilenceControllerRumble()
{
    Rumble.Stop();
    ApplyRumble();
}
void ARoninPlayerController::StopRumble()
{
    SilenceControllerRumble();
    if (ImpactCamera) ImpactCamera->Clear();
}
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
