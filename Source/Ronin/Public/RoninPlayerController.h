#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HapticCore.h"
#include "RoninPlayerController.generated.h"

UCLASS()
class RONIN_API ARoninPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ARoninPlayerController();
    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaSeconds) override;
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
    void ApplyRumble();
    void ClearHeldInputs(bool bPauseIfActive);
    void HandleInputDeviceConnectionChange(EInputDeviceConnectionState NewState,
        FPlatformUserId PlatformUser, FInputDeviceId DeviceId);
};
