#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RoninHUD.generated.h"

class ARoninGameMode;

UCLASS()
class RONIN_API ARoninHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    void Label(const FString& Text, float X, float Y, float Size, FLinearColor Color, bool bCenter = false);
    void Bar(float X, float Y, float Width, float Height, float Fraction, FLinearColor Color);
    void Controls(const ARoninGameMode& Game, float X, float Y);
    void Statistics(const ARoninGameMode& Game, float X, float Y);
    float Scale = 1.f;
};
