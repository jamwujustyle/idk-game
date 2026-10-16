#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HapticCore.h"
#include "RoninPlayerController.generated.h"

// A bounded offset to the rendered view, never to the player's aim/control
// rotation. Sharing event arbitration keeps successive contacts from stacking.
UCLASS()
class RONIN_API URoninImpactCameraModifier : public UCameraModifier
{
    GENERATED_BODY()
public:
    URoninImpactCameraModifier();
    virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
    void Pulse(ronin::HapticEvent Event);
    void PlayDefenseBeat(bool bPerfectDodge);
    void Clear();
private:
    ronin::HapticCore Impulse;
    float ImpactScale = 0.f;
    float DefenseAge = -1.f;
    bool bDodgeBeat = false;
};

UCLASS()
class RONIN_API ARoninPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ARoninPlayerController();
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaSeconds) override;
    virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void Pulse(ronin::HapticEvent Event);
    void PlayDefenseCue(bool bPerfectDodge);
    void StopRumble();
    void SetRumbleIntensity(float Intensity);
    void PreviewRumble();
    bool bUsingGamepad = false;
    // Records accepted UE feedback submissions, not physical motor confirmation.
    uint32 GetSubmittedFeedbackSerial() const { return SubmittedFeedbackSerial; }
    ronin::HapticEvent GetLastSubmittedFeedback() const { return LastSubmittedFeedback; }
private:
    ronin::HapticCore Rumble;
    FDynamicForceFeedbackHandle RumbleHandles[4] = {};
    FDelegateHandle InputDeviceConnectionHandle;
    FInputDeviceId LastGamepadDevice = INPUTDEVICEID_NONE;
    bool bHadViewportFocus = false;
    bool bReceivedGamepadInput = false;
    bool bLoggedFeedbackRoute = false;
    uint32 SubmittedFeedbackSerial = 0;
    ronin::HapticEvent LastSubmittedFeedback = ronin::HapticEvent::Swing;
    UPROPERTY(Transient) TObjectPtr<URoninImpactCameraModifier> ImpactCamera;
    void ApplyRumble(float LeaseSeconds = .08f);
    void SubmitRumble(ronin::HapticEvent Event);
    void SilenceControllerRumble();
    bool CanSendGamepadFeedback() const;
    void ClearHeldInputs(bool bPauseIfActive);
    void HandleInputDeviceConnectionChange(EInputDeviceConnectionState NewState,
        FPlatformUserId PlatformUser, FInputDeviceId DeviceId);
};
