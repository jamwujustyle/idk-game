#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RoninArena.h"
#include "RoninFighter.h"
#include "RoninGameMode.h"
#include "RoninHUD.h"
#include "RoninPlayerController.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundWave.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UObjectGlobals.h"

// These tests exercise engine objects, imported assets and merged configuration.
// Combat arithmetic is covered by the separate standalone CombatCore tests.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoninClassDefaultsTest,
    "Ronin.Integration.ClassDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoninClassDefaultsTest::RunTest(const FString& Parameters)
{
    const ARoninFighter* Fighter = GetDefault<ARoninFighter>();
    const ARoninArena* Arena = GetDefault<ARoninArena>();
    const ARoninGameMode* GameMode = GetDefault<ARoninGameMode>();
    if (!TestNotNull(TEXT("Fighter class default constructs"), Fighter)
        || !TestNotNull(TEXT("Arena class default constructs"), Arena)
        || !TestNotNull(TEXT("Game mode class default constructs"), GameMode))
    {
        return false;
    }

    bool bOk = true;
    bOk &= TestTrue(TEXT("Game mode selects the combat HUD"), GameMode->HUDClass == ARoninHUD::StaticClass());
    bOk &= TestTrue(TEXT("Game mode selects the rumble-aware controller"), GameMode->PlayerControllerClass == ARoninPlayerController::StaticClass());
    bOk &= TestNotNull(TEXT("Fighter has a movement component"), Fighter->GetCharacterMovement());
    const UCapsuleComponent* Capsule = Fighter->GetCapsuleComponent();
    bOk &= TestNotNull(TEXT("Fighter has a collision capsule"), Capsule);
    if (Capsule)
    {
        bOk &= TestTrue(TEXT("Fighter capsule has positive dimensions"), Capsule->GetUnscaledCapsuleRadius() > 0.f
            && Capsule->GetUnscaledCapsuleHalfHeight() > Capsule->GetUnscaledCapsuleRadius());
    }

    const UCameraComponent* Camera = Fighter->FindComponentByClass<UCameraComponent>();
    const USpringArmComponent* Boom = Fighter->FindComponentByClass<USpringArmComponent>();
    bOk &= TestNotNull(TEXT("Fighter has a camera"), Camera);
    bOk &= TestNotNull(TEXT("Fighter has a spring arm"), Boom);
    if (Camera && Boom)
    {
        bOk &= TestFalse(TEXT("Camera permits a 16:10 viewport without letterboxing"), Camera->bConstrainAspectRatio);
        bOk &= TestTrue(TEXT("Camera is attached to its spring arm"), Camera->GetAttachParent() == Boom);
        bOk &= TestTrue(TEXT("Camera spring arm has a usable distance"), Boom->TargetArmLength > 0.f);
    }

    TArray<UStaticMeshComponent*> Parts;
    Fighter->GetComponents<UStaticMeshComponent>(Parts);
    bOk &= TestTrue(TEXT("Fighter constructor creates visible mesh parts"), Parts.Num() > 0);
    for (const UStaticMeshComponent* Part : Parts)
    {
        bOk &= TestNotNull<UStaticMesh>(FString::Printf(TEXT("Fighter mesh resolves: %s"), *Part->GetName()), Part->GetStaticMesh());
    }

    const UStaticMeshComponent* Floor = Arena->FindComponentByClass<UStaticMeshComponent>();
    bOk &= TestNotNull(TEXT("Arena constructor creates a collision floor"), Floor);
    if (Floor)
    {
        bOk &= TestNotNull<UStaticMesh>(TEXT("Arena floor mesh resolves"), Floor->GetStaticMesh());
        bOk &= TestTrue(TEXT("Floor supports collision queries"), Floor->IsQueryCollisionEnabled());
        bOk &= TestTrue(TEXT("Floor blocks the fighter capsule"), Floor->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
        if (Floor->GetStaticMesh())
        {
            const double Top = Floor->GetRelativeLocation().Z
                + Floor->GetStaticMesh()->GetBoundingBox().Max.Z * Floor->GetRelativeScale3D().Z;
            bOk &= TestTrue(TEXT("Arena floor surface is at gameplay ground height"), FMath::IsNearlyZero(Top, 0.1));
        }
    }
    return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoninAssetsTest,
    "Ronin.Integration.Assets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoninAssetsTest::RunTest(const FString& Parameters)
{
    bool bOk = TestNotNull(TEXT("Bootstrapped arena map loads"),
        LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/Arena.Arena")));
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/Materials/M_Combat.M_Combat"));
    bOk &= TestNotNull(TEXT("Combat material loads without falling back to an engine material"), Material);
    if (Material)
    {
        FLinearColor Color;
        float Scalar = 0.f;
        bOk &= TestTrue(TEXT("Combat material is opaque"), Material->GetBlendMode() == BLEND_Opaque);
        const UMaterial* BaseMaterial = Material->GetMaterial();
        bOk &= TestNotNull(TEXT("Combat material resolves its base material"), BaseMaterial);
        if (BaseMaterial)
        {
            bOk &= TestTrue(TEXT("Material cooks the courtyard's instanced-mesh shaders"),
                BaseMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
        }
        bOk &= TestTrue(TEXT("Material exposes the runtime Color parameter"),
            Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color));
        bOk &= TestTrue(TEXT("Material exposes the runtime Roughness parameter"),
            Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Roughness")), Scalar));
        bOk &= TestTrue(TEXT("Material exposes the runtime Metallic parameter"),
            Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Metallic")), Scalar));
    }

    for (const TCHAR* Cue : {TEXT("Slash"), TEXT("Clash"), TEXT("Hit"), TEXT("Dodge"), TEXT("Execute")})
    {
        const FString Path = FString::Printf(TEXT("/Game/Audio/%s.%s"), Cue, Cue);
        USoundWave* Sound = LoadObject<USoundWave>(nullptr, *Path);
        bOk &= TestNotNull(FString::Printf(TEXT("Runtime sound cue resolves: %s"), Cue), Sound);
        if (Sound)
        {
            bOk &= TestTrue(FString::Printf(TEXT("Sound cue has playable duration: %s"), Cue), Sound->GetDuration() > 0.f);
        }
    }
    return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoninInputCoverageTest,
    "Ronin.Integration.InputCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoninInputCoverageTest::RunTest(const FString& Parameters)
{
    const UInputSettings* Input = GetDefault<UInputSettings>();
    if (!TestNotNull(TEXT("Merged project input settings load"), Input)) return false;
    bool bOk = true;
    const auto& Actions = Input->GetActionMappings();
    // Test accessibility by device family, not a copy of particular physical keys.
    for (const TCHAR* Action : {TEXT("Light"), TEXT("Heavy"), TEXT("Guard"), TEXT("Dodge"), TEXT("Execute"),
        TEXT("Flow"), TEXT("Stone"), TEXT("Gale"), TEXT("Haptics"), TEXT("Lock"), TEXT("Pause"),
        TEXT("Restart"), TEXT("Help"), TEXT("Quality")})
    {
        bool bGamepad = false;
        bool bKeyboardMouse = false;
        for (const FInputActionKeyMapping& Mapping : Actions)
        {
            if (Mapping.ActionName == FName(Action) && Mapping.Key.IsValid())
            {
                if (Mapping.Key.IsGamepadKey()) bGamepad = true;
                else bKeyboardMouse = true;
            }
        }
        bOk &= TestTrue(FString::Printf(TEXT("Gamepad action is reachable: %s"), Action), bGamepad);
        bOk &= TestTrue(FString::Printf(TEXT("Keyboard/mouse action is reachable: %s"), Action), bKeyboardMouse);
    }
    // Frame-limit cycling reuses Execute's gamepad button while paused.
    bool bFrameCapKey = false;
    for (const FInputActionKeyMapping& Mapping : Actions)
    {
        bFrameCapKey |= Mapping.ActionName == TEXT("FrameCap") && Mapping.Key.IsValid() && !Mapping.Key.IsGamepadKey();
    }
    bOk &= TestTrue(TEXT("Frame-limit cycling has a keyboard binding"), bFrameCapKey);

    const auto& Axes = Input->GetAxisMappings();
    for (const TCHAR* Axis : {TEXT("MoveForward"), TEXT("MoveRight"), TEXT("TurnRate"), TEXT("LookUpRate")})
    {
        bool bBound = false;
        for (const FInputAxisKeyMapping& Mapping : Axes)
        {
            bBound |= Mapping.AxisName == FName(Axis) && Mapping.Key.IsGamepadKey()
                && FMath::IsFinite(Mapping.Scale) && !FMath::IsNearlyZero(Mapping.Scale);
        }
        bOk &= TestTrue(FString::Printf(TEXT("Gamepad axis has an effective mapping: %s"), Axis), bBound);
    }
    return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoninCookConfigurationTest,
    "Ronin.Integration.CookConfiguration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoninCookConfigurationTest::RunTest(const FString& Parameters)
{
    FString DefaultMap;
    GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), DefaultMap, GEngineIni);
    bool bOk = TestTrue(TEXT("Game startup selects the arena"), DefaultMap == TEXT("/Game/Maps/Arena")
        || DefaultMap == TEXT("/Game/Maps/Arena.Arena"));

    FString GameModePath;
    GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GlobalDefaultGameMode"), GameModePath, GEngineIni);
    UClass* ConfiguredGameMode = LoadClass<AGameModeBase>(nullptr, *GameModePath);
    bOk &= TestTrue(TEXT("Configured game mode resolves to the combat runtime"), ConfiguredGameMode == ARoninGameMode::StaticClass());

    TArray<FString> CookDirectories;
    TArray<FString> CookMaps;
    const TCHAR* Section = TEXT("/Script/UnrealEd.ProjectPackagingSettings");
    GConfig->GetArray(Section, TEXT("DirectoriesToAlwaysCook"), CookDirectories, GGameIni);
    GConfig->GetArray(Section, TEXT("MapsToCook"), CookMaps, GGameIni);
    for (const TCHAR* Directory : {TEXT("/Game/Audio"), TEXT("/Game/Materials")})
    {
        bOk &= TestTrue(FString::Printf(TEXT("Cook includes runtime-loaded assets in %s"), Directory),
            CookDirectories.ContainsByPredicate([Directory](const FString& Entry) { return Entry.Contains(Directory); }));
    }
    bOk &= TestTrue(TEXT("Cook includes the startup arena map"), CookMaps.ContainsByPredicate(
        [](const FString& Entry) { return Entry.Contains(TEXT("/Game/Maps/Arena")); }));
    return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoninLiveWorldTest,
    "Ronin.Integration.LiveWorld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoninLiveWorldTest::RunTest(const FString& Parameters)
{
    if (!TestNotNull(TEXT("Engine is available to own the test world"), GEngine)) return false;

    // UE's wrapper owns the game instance, physics world and teardown, including
    // restoring GFrameCounter after manual ticks. No PIE window or viewport is needed.
    FTestWorldWrapper TestWorld;
    if (!TestWorld.CreateTestWorld(EWorldType::Game))
    {
        TestWorld.ForwardErrorMessages(this);
        return false;
    }
    UWorld* World = TestWorld.GetTestWorld();
    World->SetGameMode(FURL());
    ARoninGameMode* Game = Cast<ARoninGameMode>(World->GetAuthGameMode());
    if (!TestNotNull(TEXT("Configured game mode constructs in a live world"), Game)) return false;

    // Use GameMode's real controller factory. SimulatedProxy is the engine's
    // local-player role here; no physical input device is attached to this test.
    ARoninPlayerController* Controller = Cast<ARoninPlayerController>(
        Game->SpawnPlayerController(ROLE_SimulatedProxy, FString()));
    if (!TestNotNull(TEXT("Game mode creates its player controller"), Controller)) return false;
    Controller->InitInputSystem();
    if (!TestWorld.BeginPlayInTestWorld())
    {
        TestWorld.ForwardErrorMessages(this);
        return false;
    }

    ARoninFighter* Player = Game->PlayerFighter.Get();
    ARoninFighter* Enemy = Game->EnemyFighter.Get();
    if (!TestNotNull(TEXT("StartPlay spawns the player"), Player)
        || !TestNotNull(TEXT("StartPlay spawns the enemy"), Enemy)) return false;

    bool bOk = TestTrue(TEXT("StartPlay possesses the player"), Controller->GetPawn() == Player
        && Player->GetController() == Controller);
    bOk &= TestTrue(TEXT("Both fighters receive BeginPlay"), Player->HasActorBegunPlay() && Enemy->HasActorBegunPlay());
    int32 ArenaCount = 0;
    ARoninArena* Arena = nullptr;
    for (TActorIterator<ARoninArena> It(World); It; ++It) { Arena = *It; ++ArenaCount; }
    bOk &= TestEqual(TEXT("StartPlay creates exactly one arena"), ArenaCount, 1);
    int32 FighterCount = 0;
    for (TActorIterator<ARoninFighter> It(World); It; ++It) ++FighterCount;
    bOk &= TestEqual(TEXT("StartPlay creates exactly two fighters"), FighterCount, 2);
    if (!TestNotNull(TEXT("Arena exists for collision checks"), Arena)) return false;
    bOk &= TestTrue(TEXT("Arena receives BeginPlay"), Arena->HasActorBegunPlay());

    constexpr float Step = 1.f / 120.f;
    // Less than one second total keeps this ahead of the AI's opening attack.
    // Check actual character sweeps against the runtime arena, not floor math.
    for (int32 Frame = 0; Frame < 60; ++Frame) TestWorld.TickTestWorld(Step);
    const double PlayerSettledZ = Player->GetActorLocation().Z;
    const double EnemySettledZ = Enemy->GetActorLocation().Z;
    for (int32 Frame = 0; Frame < 30; ++Frame) TestWorld.TickTestWorld(Step);
    for (ARoninFighter* Fighter : {Player, Enemy})
    {
        const FString Label = Fighter == Player ? TEXT("Player") : TEXT("Enemy");
        const FVector Location = Fighter->GetActorLocation();
        bOk &= TestFalse(Label + TEXT(" position stays finite"), Location.ContainsNaN());
        bOk &= TestTrue(Label + TEXT(" remains within the courtyard"), FMath::Abs(Location.X) < 950.
            && FMath::Abs(Location.Y) < 950. && Location.Z > 75. && Location.Z < 125.);
        const UCharacterMovementComponent* Movement = Fighter->GetCharacterMovement();
        bOk &= TestTrue(Label + TEXT(" settles onto walkable ground"), Movement->IsMovingOnGround());
        bOk &= TestTrue(Label + TEXT(" is supported by the runtime arena"), Movement->CurrentFloor.HitResult.GetActor() == Arena);
        const double SettledZ = Fighter == Player ? PlayerSettledZ : EnemySettledZ;
        bOk &= TestTrue(Label + TEXT(" ground height stays stable across ticks"), FMath::Abs(Location.Z - SettledZ) < 1.);
    }

    UCameraComponent* Camera = Player->FindComponentByClass<UCameraComponent>();
    if (TestNotNull(TEXT("Possessed fighter has its live camera"), Camera))
    {
        FMinimalViewInfo View;
        Camera->GetCameraView(Step, View);
        bOk &= TestFalse(TEXT("Live camera position stays finite"), View.Location.ContainsNaN());
        bOk &= TestFalse(TEXT("Live camera rotation stays finite"), View.Rotation.ContainsNaN());
        bOk &= TestTrue(TEXT("Live camera has a usable field of view"), FMath::IsFinite(View.FOV) && View.FOV > 30.f && View.FOV < 120.f);
        const double Distance = FVector::Distance(View.Location, Player->GetActorLocation());
        bOk &= TestTrue(TEXT("Spring arm keeps the camera outside the fighter"), Distance > 100. && Distance < 1000.);
    }
    else bOk = false;

    // Invoke the delegates installed by real possession/PawnClientRestart. This
    // covers action wiring into GameMode; hardware/key dispatch is a live check.
    UInputComponent* Input = Player->FindComponentByClass<UInputComponent>();
    if (!TestNotNull(TEXT("Possession installs the fighter input component"), Input)) return false;
    auto InvokeAction = [this, Input](FName Name, EInputEvent Event)
    {
        for (int32 Index = 0; Index < Input->GetNumActionBindings(); ++Index)
        {
            const FInputActionBinding& Binding = Input->GetActionBinding(Index);
            if (Binding.GetActionName() == Name && Binding.KeyEvent == Event && Binding.ActionDelegate.IsBound())
            {
                Binding.ActionDelegate.Execute(FKey());
                return true;
            }
        }
        AddError(FString::Printf(TEXT("Live pawn has no bound action: %s (%d)"), *Name.ToString(), static_cast<int32>(Event)));
        return false;
    };
    bOk &= InvokeAction(TEXT("Stone"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound stance action reaches the combat runtime"), Game->Combat.Get(ronin::FighterId::Player).stance == ronin::Stance::Stone);
    bOk &= InvokeAction(TEXT("Guard"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound guard press raises guard"), Game->Combat.Get(ronin::FighterId::Player).guarding);
    bOk &= InvokeAction(TEXT("Guard"), IE_Released);
    bOk &= TestFalse(TEXT("Bound guard release clears guard"), Game->Combat.Get(ronin::FighterId::Player).guarding);
    bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound pause action pauses combat"), Game->bCombatPaused);
    const FVector PausedPosition = Player->GetActorLocation();
    bOk &= InvokeAction(TEXT("Light"), IE_Pressed);
    for (int32 Frame = 0; Frame < 12; ++Frame) TestWorld.TickTestWorld(Step);
    bOk &= TestTrue(TEXT("Paused input cannot start an attack"), Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Idle);
    bOk &= TestTrue(TEXT("Paused fighter remains in place"), Player->GetActorLocation().Equals(PausedPosition, 0.1));
    bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
    bOk &= TestFalse(TEXT("Bound pause action resumes combat"), Game->bCombatPaused);
    bOk &= InvokeAction(TEXT("Light"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound light attack starts the runtime attack"), Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Windup);

    bOk &= TestWorld.DestroyTestWorld(false);
    TestWorld.ForwardErrorMessages(this);
    return bOk && !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
