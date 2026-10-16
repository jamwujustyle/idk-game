#include "RoninHUD.h"
#include "RoninGameMode.h"
#include "RoninFighter.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
const FLinearColor Ivory(.9f,.87f,.78f,1.f);
const FLinearColor Muted(.5f,.56f,.58f,1.f);
const FLinearColor Gold(.93f,.68f,.31f,1.f);
const FLinearColor Teal(.32f,.73f,.69f,1.f);
const FLinearColor Red(.8f,.17f,.12f,1.f);
constexpr float FontScale=1.7f;
}

void ARoninHUD::Label(const FString& Text,float X,float Y,float Size,FLinearColor Color,bool bCenter)
{
    if (!GEngine || !Canvas) return;
    FCanvasTextItem Item(FVector2D(X,Y),FText::FromString(Text),GEngine->GetMediumFont(),Color);
    Item.Scale=FVector2D(Size*Scale*FontScale,Size*Scale*FontScale);
    Item.bCentreX=bCenter;
    Item.EnableShadow(FLinearColor(0,0,0,.8f),FVector2D(1,1));
    Canvas->DrawItem(Item);
}

void ARoninHUD::Bar(float X,float Y,float Width,float Height,float Fraction,FLinearColor Color)
{
    DrawRect(FLinearColor(.03f,.04f,.045f,.85f),X-Scale,Y-Scale,Width+2*Scale,Height+2*Scale);
    DrawRect(FLinearColor(.16f,.18f,.18f,.85f),X,Y,Width,Height);
    DrawRect(Color,X,Y,Width*FMath::Clamp(Fraction,0.f,1.f),Height);
}

void ARoninHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* G=Cast<ARoninGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Canvas || !G) return;
    const float W=Canvas->SizeX, H=Canvas->SizeY;
    Scale=FMath::Max(.6f,FMath::Min(W/1600.f,H/1000.f));
    const float S=Scale, Margin=44*S;
    const auto& Player=G->Combat.Get(ronin::FighterId::Player);
    const auto& Enemy=G->Combat.Get(ronin::FighterId::Enemy);
    const bool bPad=G->bUsingGamepad;
    const FString FinishKey=bPad ? TEXT("Y") : TEXT("F");
    Label(TEXT("R O N I N"),Margin,32*S,1.45f,Ivory);
    Label(TEXT("THE LAST COURTYARD"),Margin,65*S,.65f,Muted);
    Label(FString::Printf(TEXT("DUELS WON  %d"),G->Victories),W-Margin-178*S,38*S,.7f,Muted);
    Label(FString::Printf(TEXT("%d FPS"),FMath::RoundToInt(G->DisplayedFPS)),W-Margin-178*S,63*S,.55f,Muted);
    Label(TEXT("COURTYARD SENTINEL"),W*.5f,42*S,.85f,Ivory,true);
    Bar(W*.5f-225*S,74*S,450*S,8*S,Enemy.health/Enemy.maxHealth,Red);
    Bar(W*.5f-160*S,92*S,320*S,5*S,Enemy.posture/Enemy.maxPosture,Gold);
    Label(TEXT("POSTURE"),W*.5f,105*S,.55f,Muted,true);

    if (G->EnemyFighter && Enemy.action!=ronin::Action::Dead && G->bLockOn)
    {
        FVector2D Screen;
        if (PlayerOwner && PlayerOwner->ProjectWorldLocationToScreen(G->EnemyFighter->GetActorLocation()+FVector(0,0,110),Screen))
        {
            const FLinearColor Mark=Enemy.spec.unblockable && Enemy.action==ronin::Action::Windup ? Red : Gold;
            DrawLine(Screen.X-6*S,Screen.Y,Screen.X,Screen.Y-7*S,Mark,1.5f*S);
            DrawLine(Screen.X,Screen.Y-7*S,Screen.X+6*S,Screen.Y,Mark,1.5f*S);
            DrawLine(Screen.X+6*S,Screen.Y,Screen.X,Screen.Y+7*S,Mark,1.5f*S);
            DrawLine(Screen.X,Screen.Y+7*S,Screen.X-6*S,Screen.Y,Mark,1.5f*S);
        }
    }
    if (Enemy.action==ronin::Action::Windup && Enemy.spec.unblockable)
        Label(bPad ? TEXT("DODGE  /  B") : TEXT("DODGE  /  SPACE"),W*.5f,H*.34f,1.3f,Red,true);
    if (Enemy.action==ronin::Action::Broken)
        Label(TEXT("FINISH  /  ")+FinishKey,W*.5f,H*.43f,1.3f,Gold,true);
    else if (Player.counterTime>0.f)
        Label(TEXT("COUNTER WINDOW"),W*.5f,H*.65f,.7f,Teal,true);
    if (G->FeedbackTime>0.f)
        Label(G->Feedback,W*.5f,H-230*S,.85f,G->FeedbackColor,true);

    Label(TEXT("VITALITY"),Margin,H-137*S,.58f,Muted);
    Bar(Margin,H-115*S,265*S,11*S,Player.health/Player.maxHealth,Ivory);
    Bar(Margin,H-92*S,265*S,5*S,Player.stamina/Player.maxStamina,Teal);
    Bar(Margin,H-77*S,265*S,5*S,Player.posture/Player.maxPosture,Gold);
    Label(TEXT("STAMINA / POSTURE"),Margin,H-61*S,.5f,Muted);
    const TCHAR* Stances[]={TEXT("FLOW"),TEXT("STONE"),TEXT("GALE")};
    const TCHAR* Desc[]={TEXT("FAST PRESSURE"),TEXT("GUARD BREAK"),TEXT("MOBILE DEFENSE")};
    const int32 Current=static_cast<int32>(Player.stance);
    for (int32 I=0; I<3; ++I)
    {
        const float X=W*.5f+(I-1)*150*S;
        const bool Selected=I==Current;
        if (Selected) DrawRect(Gold,X-48*S,H-119*S,96*S,2*S);
        const TCHAR* PadKeys[]={TEXT("D-LEFT"),TEXT("D-UP"),TEXT("D-RIGHT")};
        Label(bPad ? PadKeys[I] : FString::FromInt(I+1),X,H-104*S,.55f,Selected ? Gold : Muted,true);
        Label(Stances[I],X,H-83*S,.88f,Selected ? Ivory : Muted,true);
        if (Selected) Label(Desc[I],X,H-56*S,.48f,Muted,true);
    }
    Label(FString::Printf(TEXT("PARRIES  %d"),G->PerfectParries),W-Margin-155*S,H-110*S,.75f,Ivory);
    Label(bPad ? TEXT("MENU  PAUSE    X  HELP") : TEXT("ESC  PAUSE    H  HELP"),W-Margin-260*S,H-57*S,.6f,Muted);

    if (G->bShowHelp && !G->bCombatPaused)
    {
        const float X=Margin, Y=H*.34f;
        DrawRect(FLinearColor(.015f,.025f,.03f,.7f),X-14*S,Y-15*S,274*S,210*S);
        Label(TEXT("CONTROL THE EXCHANGE"),X,Y,.7f,Gold);
        Label(bPad ? TEXT("LS  MOVE    RS  CAMERA") : TEXT("WASD  MOVE    MOUSE  CAMERA"),X,Y+36*S,.6f,Ivory);
        Label(bPad ? TEXT("RB  CUT    RT  HEAVY") : TEXT("LMB  CUT    E  HEAVY"),X,Y+64*S,.6f,Ivory);
        Label(bPad ? TEXT("LB  PARRY / HOLD GUARD") : TEXT("RMB  PARRY / HOLD GUARD"),X,Y+92*S,.6f,Ivory);
        Label(bPad ? TEXT("B  DODGE    Y  FINISH") : TEXT("SPACE  DODGE    F  FINISH"),X,Y+120*S,.6f,Ivory);
        Label(bPad ? TEXT("RS CLICK  LOCK    SELECT  RETRY") : TEXT("TAB  LOCK    R  RETRY"),X,Y+148*S,.6f,Ivory);
        Label(TEXT("Tap guard just before impact."),X,Y+177*S,.55f,Muted);
    }
    const bool bDead=Player.action==ronin::Action::Dead, bWon=Enemy.action==ronin::Action::Dead;
    if (bDead || bWon || G->bCombatPaused)
    {
        DrawRect(FLinearColor(.01f,.018f,.025f,.8f),0,0,W,H);
        Label(G->bCombatPaused ? TEXT("A MOMENT OF STILLNESS") : bWon ? TEXT("THE COURTYARD IS YOURS") : TEXT("RISE AGAIN"),W*.5f,H*.34f,1.7f,bDead ? Red : Ivory,true);
        Label(bPad ? TEXT("VIEW  -  REMATCH") : TEXT("R  -  INSTANT REMATCH"),W*.5f,H*.45f,.9f,Gold,true);
        if (G->bCombatPaused)
        {
            Label(bPad ? TEXT("MENU  -  RETURN TO THE DUEL") : TEXT("ESC  -  RETURN TO THE DUEL"),W*.5f,H*.51f,.75f,Ivory,true);
            Label((bPad ? TEXT("A  QUALITY: ") : TEXT("F2  QUALITY: "))+G->QualityName,W*.5f,H*.58f,.75f,Muted,true);
            Label((bPad ? TEXT("Y  FRAME LIMIT: ") : TEXT("F3  FRAME LIMIT: "))+G->FrameCapName,W*.5f,H*.63f,.75f,Muted,true);
            Label((bPad ? TEXT("D-DOWN  RUMBLE: ") : TEXT("F4  RUMBLE: "))+G->HapticsName,W*.5f,H*.68f,.75f,Muted,true);
        }
    }
    if (G->ImpactFlash>0.f)
    {
        const float Alpha=G->ImpactFlash*1.1f;
        DrawRect(FLinearColor(.6f,.02f,0,Alpha),0,0,W,12*S);
        DrawRect(FLinearColor(.6f,.02f,0,Alpha),0,H-12*S,W,12*S);
    }
}
