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

void ARoninHUD::Statistics(const ARoninGameMode& Game,float X,float Y)
{
    Label(FString::Printf(TEXT("DUELS WON  %d     PARRIES  %d     %d FPS"),
        Game.Victories,Game.PerfectParries,FMath::RoundToInt(Game.DisplayedFPS)),X,Y,.58f,Muted,true);
}

void ARoninHUD::Controls(const ARoninGameMode& Game,float X,float Y)
{
    const float S=Scale;
    const bool bPad=Game.bUsingGamepad;
    DrawRect(FLinearColor(.015f,.025f,.03f,.9f),X-22*S,Y-20*S,548*S,330*S);
    Label(TEXT("CONTROLS"),X,Y,.8f,Gold);
    Label(bPad ? TEXT("LS  MOVE     RS  CAMERA") : TEXT("WASD  MOVE     MOUSE  CAMERA"),X,Y+42*S,.65f,Ivory);
    Label(bPad ? TEXT("RB  CUT     RT  HEAVY") : TEXT("LMB  CUT     E  HEAVY"),X,Y+72*S,.65f,Ivory);
    Label(bPad ? TEXT("LB  PARRY / HOLD GUARD") : TEXT("RMB  PARRY / HOLD GUARD"),X,Y+102*S,.65f,Ivory);
    Label(bPad ? TEXT("B  DODGE     Y  FINISH") : TEXT("SPACE  DODGE     F  FINISH"),X,Y+132*S,.65f,Ivory);
    Label(bPad ? TEXT("D-PAD LEFT / UP / RIGHT  STANCE") : TEXT("1 / 2 / 3  FLOW / STONE / GALE"),X,Y+162*S,.65f,Ivory);
    Label(bPad ? TEXT("RS CLICK  LOCK     VIEW  REMATCH") : TEXT("TAB  LOCK     R  REMATCH"),X,Y+192*S,.65f,Ivory);
    Label(TEXT("Tap guard just before impact to parry."),X,Y+235*S,.6f,Muted);
    Label(TEXT("Red attack: dodge. Full gold posture: guard break."),X,Y+259*S,.55f,Muted);
    if (Game.HasDuelStarted()) Statistics(Game,X+252*S,Y+287*S);
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
    const bool bDead=Player.action==ronin::Action::Dead, bWon=Enemy.action==ronin::Action::Dead;
    const FString HelpHint=bPad ? TEXT("X  CONTROLS") : TEXT("H  CONTROLS");

    if (!G->HasDuelStarted())
    {
        DrawRect(FLinearColor(.01f,.018f,.025f,.42f),0,0,W,H);
        Label(TEXT("R O N I N"),W*.5f,H*.17f,2.5f,Ivory,true);
        Label(TEXT("THE LAST COURTYARD"),W*.5f,H*.26f,.75f,Gold,true);
        if (G->bShowHelp) Controls(*G,W*.5f-252*S,H*.34f);
        else Label(TEXT("Read the blade. Find your opening."),W*.5f,H*.43f,.8f,Ivory,true);
        Label(bPad ? TEXT("A  START DUEL") : TEXT("ENTER  START DUEL"),W*.5f,H*.73f,1.f,Gold,true);
        Label(G->bShowHelp ? (bPad ? TEXT("X  HIDE CONTROLS") : TEXT("H  HIDE CONTROLS")) : HelpHint,
            W*.5f,H*.84f,.65f,Muted,true);
        return;
    }

    if (G->bCombatPaused || bDead || bWon)
    {
        DrawRect(FLinearColor(.01f,.018f,.025f,.85f),0,0,W,H);
        Label(G->bCombatPaused ? TEXT("PAUSED") : bWon ? TEXT("DUEL WON") : TEXT("RISE AGAIN"),
            W*.5f,H*.2f,1.9f,bDead && !G->bCombatPaused ? Red : Ivory,true);
        if (G->bShowHelp)
        {
            Controls(*G,W*.5f-252*S,H*.33f);
        }
        else
        {
            Label(bPad ? TEXT("VIEW  REMATCH") : TEXT("R  REMATCH"),W*.5f,H*.36f,.9f,Gold,true);
            if (G->bCombatPaused)
            {
                Label(bPad ? TEXT("MENU / B  RESUME") : TEXT("ESC / SPACE  RESUME"),W*.5f,H*.43f,.8f,Ivory,true);
                Label((bPad ? TEXT("A  QUALITY  ") : TEXT("F2  QUALITY  "))+G->QualityName,W*.5f,H*.53f,.7f,Muted,true);
                Label((bPad ? TEXT("Y  FRAME LIMIT  ") : TEXT("F3  FRAME LIMIT  "))+G->FrameCapName,W*.5f,H*.59f,.7f,Muted,true);
                Label((bPad ? TEXT("D-DOWN  RUMBLE  ") : TEXT("F4  RUMBLE  "))+G->HapticsName,W*.5f,H*.65f,.7f,Muted,true);
            }
            Statistics(*G,W*.5f,H*.76f);
        }
        Label(G->bShowHelp ? (bPad ? TEXT("X  BACK") : TEXT("H  BACK")) : HelpHint,W*.5f,H*.87f,.65f,Muted,true);
        return;
    }

    Label(TEXT("COURTYARD SENTINEL"),W*.5f,32*S,.7f,Ivory,true);
    Bar(W*.5f-180*S,60*S,360*S,7*S,Enemy.health/Enemy.maxHealth,Red);
    Bar(W*.5f-135*S,76*S,270*S,4*S,Enemy.posture/Enemy.maxPosture,Gold);
    if (G->EnemyFighter && G->bLockOn)
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
        Label(bPad ? TEXT("B  DODGE") : TEXT("SPACE  DODGE"),W*.5f,H*.34f,1.15f,Red,true);
    if (Enemy.action==ronin::Action::Broken)
        Label(bPad ? TEXT("Y  FINISH") : TEXT("F  FINISH"),W*.5f,H*.43f,1.15f,Gold,true);
    else if (Player.counterTime>0.f)
        Label(TEXT("COUNTER WINDOW"),W*.5f,H*.65f,.7f,Teal,true);
    if (G->FeedbackTime>0.f)
        Label(G->Feedback,W*.5f,H-185*S,.75f,G->FeedbackColor,true);

    Label(TEXT("VITALITY"),Margin,H-112*S,.55f,Muted);
    Bar(Margin,H-90*S,220*S,9*S,Player.health/Player.maxHealth,Ivory);
    Label(TEXT("POSTURE"),Margin,H-70*S,.45f,Player.posture>=Player.maxPosture*.75f ? Red : Muted);
    Bar(Margin,H-48*S,220*S,5*S,Player.posture/Player.maxPosture,
        Player.posture>=Player.maxPosture*.75f ? Red : Gold);
    const TCHAR* Stances[]={TEXT("FLOW"),TEXT("STONE"),TEXT("GALE")};
    const TCHAR* Desc[]={TEXT("FAST PRESSURE"),TEXT("GUARD BREAK"),TEXT("MOBILE DEFENSE")};
    const int32 Current=static_cast<int32>(Player.stance);
    DrawRect(Gold,W*.5f-34*S,H-109*S,68*S,2*S);
    Label(Stances[Current],W*.5f,H-94*S,.9f,Ivory,true);
    Label(Desc[Current],W*.5f,H-63*S,.5f,Muted,true);
    Label(bPad ? TEXT("MENU  PAUSE    X  CONTROLS") : TEXT("ESC  PAUSE    H  CONTROLS"),
        W-210*S,H-65*S,.55f,Muted,true);
    if (G->bShowHelp) Controls(*G,Margin+22*S,H*.31f);
    if (G->ImpactFlash>0.f)
    {
        const float Alpha=G->ImpactFlash*1.1f;
        DrawRect(FLinearColor(.6f,.02f,0,Alpha),0,0,W,12*S);
        DrawRect(FLinearColor(.6f,.02f,0,Alpha),0,H-12*S,W,12*S);
    }
}
