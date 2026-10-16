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
    void Clear();
private:
    ronin::HapticCore Impulse;
    float ImpactScale = 0.f;
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
    void StopRumble();
    void SetRumbleIntensity(float Intensity);
    bool bUsingGamepad = false;
private:
    ronin::HapticCore Rumble;
    FDynamicForceFeedbackHandle RumbleHandles[4] = {};
    FDelegateHandle InputDeviceConnectionHandle;
    FInputDeviceId LastGamepadDevice = INPUTDEVICEID_NONE;
    bool bHadViewportFocus = false;
    UPROPERTY(Transient) TObjectPtr<URoninImpactCameraModifier> ImpactCamera;
    void ApplyRumble(float LeaseSeconds = .08f);
    void ClearHeldInputs(bool bPauseIfActive);
    void HandleInputDeviceConnectionChange(EInputDeviceConnectionState NewState,
        FPlatformUserId PlatformUser, FInputDeviceId DeviceId);
};
