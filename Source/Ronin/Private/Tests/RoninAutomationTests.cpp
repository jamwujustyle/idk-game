#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RoninAnimInstance.h"
#include "RoninArena.h"
#include "RoninFighter.h"
#include "RoninGameMode.h"
#include "RoninHUD.h"
#include "RoninPlayerController.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
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
        bOk &= TestEqual(TEXT("Authored visual preserves gameplay capsule radius"), Capsule->GetUnscaledCapsuleRadius(), 34.f);
        bOk &= TestEqual(TEXT("Authored visual preserves gameplay capsule half height"), Capsule->GetUnscaledCapsuleHalfHeight(), 88.f);
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

    USkeletalMeshComponent* Body = Fighter->GetMesh();
    bOk &= TestNotNull(TEXT("Fighter owns an authored skeletal body"), Body);
    if (Body)
    {
        bOk &= TestNotNull(TEXT("Fighter skeletal mesh resolves"), Body->GetSkeletalMeshAsset());
        bOk &= TestTrue(TEXT("Fighter uses the native combat animation graph"), Body->GetAnimClass() == URoninAnimInstance::StaticClass());
        bOk &= TestTrue(TEXT("Visual mesh leaves collision to the gameplay capsule"), Body->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        bOk &= TestTrue(TEXT("Skeletal poses update without a rendered viewport"),
            Body->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
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
    const URoninAnimInstance* Animation = GetDefault<URoninAnimInstance>();
    for (const TCHAR* Name : {TEXT("Kwang_GDC"), TEXT("KwangRosewood")})
    {
        const FString Path = FString::Printf(TEXT("/Game/ParagonKwang/Characters/Heroes/Kwang/Meshes/%s.%s"), Name, Name);
        USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path);
        bOk &= TestNotNull(FString::Printf(TEXT("Authored fighter mesh loads: %s"), Name), Mesh);
        if (!Mesh) continue;
        bOk &= TestNotNull(FString::Printf(TEXT("Fighter skeleton resolves: %s"), Name), Mesh->GetSkeleton());
        for (const FName Bone : {FName(TEXT("weapon_r")), FName(TEXT("spine_01"))})
            bOk &= TestTrue(FString::Printf(TEXT("%s has required pose bone %s"), Name, *Bone.ToString()),
                Mesh->GetRefSkeleton().FindBoneIndex(Bone) != INDEX_NONE);
        for (int32 Index = 0; Index < static_cast<int32>(ERoninAnimationClip::Count); ++Index)
        {
            UAnimSequence* Sequence = Animation->GetClip(static_cast<ERoninAnimationClip>(Index));
            const FString Label = FString::Printf(TEXT("%s animation slot %d"), Name, Index);
            bOk &= TestNotNull(Label + TEXT(" resolves"), Sequence);
            if (!Sequence) continue;
            bOk &= TestTrue(Label + TEXT(" uses the fighter skeleton"), Sequence->GetSkeleton() == Mesh->GetSkeleton());
            bOk &= TestTrue(Label + TEXT(" has a playable authored duration"), Sequence->GetPlayLength() > 0.f);
            bOk &= TestTrue(Label + TEXT(" is a full pose suitable for sequence evaluation"), Sequence->GetAdditiveAnimType() == AAT_None);
        }
    }
    return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoninAnimationTimingTest,
    "Ronin.Integration.AnimationTiming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoninAnimationTimingTest::RunTest(const FString& Parameters)
{
    bool bOk = true;
    // The same authored contact window must survive every gameplay speed. These
    // boundary contracts catch early contact, discontinuities and clip overrun.
    constexpr float ClipLength = 1.2f, ContactStart = .2f, ContactEnd = .3f;
    for (const auto Stance : {ronin::Stance::Flow, ronin::Stance::Stone, ronin::Stance::Gale})
    for (const auto Kind : {ronin::AttackKind::Light, ronin::AttackKind::Heavy, ronin::AttackKind::Unblockable})
    {
        const auto Spec = ronin::CombatCore::GetAttackSpec(Stance, Kind);
        const ronin::Action Phases[] = {ronin::Action::Windup, ronin::Action::Active, ronin::Action::Recovery};
        const float Durations[] = {Spec.windup, Spec.active, Spec.recovery};
        const float Boundaries[] = {0.f, ContactStart, ContactEnd, ClipLength};
        for (int32 Phase = 0; Phase < UE_ARRAY_COUNT(Phases); ++Phase)
        {
            ronin::FighterState State;
            State.action = Phases[Phase];
            State.actionDuration = Durations[Phase];
            const FString Label = FString::Printf(TEXT("Stance %d attack %d phase %d"), int32(Stance), int32(Kind), Phase);
            auto Sample = [&](float Time)
            {
                State.actionTime = Time;
                return URoninAnimInstance::MapAttackPhase(State, ClipLength, ContactStart, ContactEnd);
            };
            bOk &= TestEqual(Label + TEXT(" begins at the shared clip boundary"), Sample(0.f), Boundaries[Phase]);
            bOk &= TestTrue(Label + TEXT(" ends at the shared clip boundary"),
                FMath::IsNearlyEqual(Sample(State.actionDuration), Boundaries[Phase + 1]));
            bOk &= TestEqual(Label + TEXT(" clamps negative elapsed time"), Sample(-.1f), Boundaries[Phase]);
            bOk &= TestTrue(Label + TEXT(" clamps elapsed time beyond the phase"),
                FMath::IsNearlyEqual(Sample(State.actionDuration + .1f), Boundaries[Phase + 1]));
            const float Early = Sample(State.actionDuration * .25f);
            const float Late = Sample(State.actionDuration * .75f);
            bOk &= TestTrue(Label + TEXT(" advances within its contact interval"),
                Early > Boundaries[Phase] && Late > Early && Late < Boundaries[Phase + 1]);
            State.actionDuration = 0.f;
            bOk &= TestTrue(Label + TEXT(" handles an instantaneous phase without NaN"),
                FMath::IsNearlyEqual(Sample(0.f), Boundaries[Phase + 1]));
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
        TEXT("Restart"), TEXT("Help"), TEXT("Quality"), TEXT("StartDuel")})
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

    // Native constructor assets are retained by reflected hard references, so
    // cooking does not need the unrelated sample heroes, maps or ability content.
    TArray<UObject*> FighterReferences;
    FReferenceFinder FighterFinder(FighterReferences, nullptr, false, true, false, true);
    FighterFinder.FindReferences(GetMutableDefault<ARoninFighter>());
    for (const TCHAR* Name : {TEXT("Kwang_GDC"), TEXT("KwangRosewood")})
    {
        const FString Path = FString::Printf(TEXT("/Game/ParagonKwang/Characters/Heroes/Kwang/Meshes/%s.%s"), Name, Name);
        UObject* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path);
        bOk &= TestTrue(FString::Printf(TEXT("Runtime default retains cookable mesh reference: %s"), Name),
            Mesh && FighterReferences.Contains(Mesh));
    }
    TArray<UObject*> AnimationReferences;
    FReferenceFinder AnimationFinder(AnimationReferences, nullptr, false, true, false, true);
    URoninAnimInstance* Animation = GetMutableDefault<URoninAnimInstance>();
    AnimationFinder.FindReferences(Animation);
    for (int32 Index = 0; Index < static_cast<int32>(ERoninAnimationClip::Count); ++Index)
    {
        UAnimSequence* Clip = Animation->GetClip(static_cast<ERoninAnimationClip>(Index));
        bOk &= TestTrue(FString::Printf(TEXT("Native graph retains cookable clip reference: %d"), Index),
            Clip && AnimationReferences.Contains(Clip));
    }
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
    URoninAnimInstance* PlayerAnimation = Cast<URoninAnimInstance>(Player->GetMesh()->GetAnimInstance());
    URoninAnimInstance* EnemyAnimation = Cast<URoninAnimInstance>(Enemy->GetMesh()->GetAnimInstance());
    if (!TestNotNull(TEXT("Player creates a live native animation instance"), PlayerAnimation)
        || !TestNotNull(TEXT("Enemy creates a live native animation instance"), EnemyAnimation)) return false;
    bOk &= TestTrue(TEXT("Enemy uses its distinct authored appearance"),
        Player->GetMesh()->GetSkeletalMeshAsset() != Enemy->GetMesh()->GetSkeletalMeshAsset());
    const FName WeaponBone(TEXT("weapon_r"));
    bOk &= TestTrue(TEXT("Live player exposes the animated weapon bone"), Player->GetMesh()->GetBoneIndex(WeaponBone) != INDEX_NONE);
    int32 ArenaCount = 0;
    ARoninArena* Arena = nullptr;
    for (TActorIterator<ARoninArena> It(World); It; ++It) { Arena = *It; ++ArenaCount; }
    bOk &= TestEqual(TEXT("StartPlay creates exactly one arena"), ArenaCount, 1);
    int32 FighterCount = 0;
    for (TActorIterator<ARoninFighter> It(World); It; ++It) ++FighterCount;
    bOk &= TestEqual(TEXT("StartPlay creates exactly two fighters"), FighterCount, 2);
    if (!TestNotNull(TEXT("Arena exists for collision checks"), Arena)) return false;
    bOk &= TestTrue(TEXT("Arena receives BeginPlay"), Arena->HasActorBegunPlay());

    // Invoke the delegates installed by real possession/PawnClientRestart. This
    // covers action wiring into GameMode; hardware/key dispatch is a live check.
    UInputComponent* Input = Player->FindComponentByClass<UInputComponent>();
    if (!TestNotNull(TEXT("Possession installs the fighter input component"), Input)) return false;
    auto InvokeAction = [this, Input](FName Name, EInputEvent Event, UInputComponent* OverrideInput = nullptr)
    {
        UInputComponent* BoundInput = OverrideInput ? OverrideInput : Input;
        for (int32 Index = 0; Index < BoundInput->GetNumActionBindings(); ++Index)
        {
            const FInputActionBinding& Binding = BoundInput->GetActionBinding(Index);
            if (Binding.GetActionName() == Name && Binding.KeyEvent == Event && Binding.ActionDelegate.IsBound())
            {
                Binding.ActionDelegate.Execute(FKey());
                return true;
            }
        }
        AddError(FString::Printf(TEXT("Live input component has no bound action: %s (%d)"), *Name.ToString(), static_cast<int32>(Event)));
        return false;
    };
    UInputComponent* StartInput = Game->FindComponentByClass<UInputComponent>();
    if (!TestNotNull(TEXT("Game mode installs the start action"), StartInput)) return false;
    bOk &= TestFalse(TEXT("A new world waits for explicit start"), Game->HasDuelStarted());
    bOk &= TestFalse(TEXT("Controls help starts hidden"), Game->bShowHelp);
    bOk &= TestFalse(TEXT("Fighters cannot act before start"), Game->CanControl());
    const auto InitialPlayer = Game->Combat.Get(ronin::FighterId::Player);
    const auto InitialEnemy = Game->Combat.Get(ronin::FighterId::Enemy);
    const FVector WaitingEnemyLocation = Enemy->GetActorLocation();
    bOk &= InvokeAction(TEXT("Light"), IE_Pressed);
    bOk &= InvokeAction(TEXT("Guard"), IE_Pressed);
    // Simulate a long shader/asset warmup. Both the encounter clock and core
    // decision/phase clocks must remain untouched, not merely the AI attack gate.
    for (int32 Frame = 0; Frame < 180; ++Frame) TestWorld.TickTestWorld(1.f/30.f);
    const auto& WaitingPlayer = Game->Combat.Get(ronin::FighterId::Player);
    const auto& WaitingEnemy = Game->Combat.Get(ronin::FighterId::Enemy);
    bOk &= TestEqual(TEXT("Startup does not advance duel time"), Game->GetRoundTime(), 0.f);
    bOk &= TestEqual(TEXT("Startup does not advance enemy decisions"), WaitingEnemy.decisionTime, InitialEnemy.decisionTime);
    bOk &= TestEqual(TEXT("Startup does not advance player phase time"), WaitingPlayer.actionTime, InitialPlayer.actionTime);
    bOk &= TestEqual(TEXT("Startup does not advance enemy phase time"), WaitingEnemy.actionTime, InitialEnemy.actionTime);
    bOk &= TestEqual(TEXT("Player takes no damage during startup"), WaitingPlayer.health, InitialPlayer.health);
    bOk &= TestEqual(TEXT("Enemy takes no damage during startup"), WaitingEnemy.health, InitialEnemy.health);
    bOk &= TestTrue(TEXT("Combat input is ignored before start"), WaitingPlayer.action == ronin::Action::Idle && !WaitingPlayer.guarding);
    bOk &= TestTrue(TEXT("Enemy does not approach during startup"), FVector::Dist2D(WaitingEnemyLocation, Enemy->GetActorLocation()) < .1f);
    bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
    bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
    bOk &= TestFalse(TEXT("Pause and rematch do not bypass explicit first start"), Game->HasDuelStarted());
    bOk &= TestFalse(TEXT("Startup does not enter the pause menu"), Game->bCombatPaused);
    bOk &= InvokeAction(TEXT("StartDuel"), IE_Pressed, StartInput);
    bOk &= TestTrue(TEXT("Bound start action opens the duel"), Game->HasDuelStarted() && Game->CanControl());
    bOk &= TestEqual(TEXT("A started duel begins at time zero"), Game->GetRoundTime(), 0.f);

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

    bOk &= TestTrue(TEXT("Simulation advances only after start"), Game->GetRoundTime() > .5f);
    const float StartedTime = Game->GetRoundTime();
    bOk &= InvokeAction(TEXT("StartDuel"), IE_Pressed, StartInput);
    bOk &= TestEqual(TEXT("Repeated start does not reset an active duel"), Game->GetRoundTime(), StartedTime);
    bOk &= InvokeAction(TEXT("Stone"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound stance action reaches the combat runtime"), Game->Combat.Get(ronin::FighterId::Player).stance == ronin::Stance::Stone);
    bOk &= InvokeAction(TEXT("Guard"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound guard press raises guard"), Game->Combat.Get(ronin::FighterId::Player).guarding);
    bOk &= InvokeAction(TEXT("Guard"), IE_Released);
    bOk &= TestFalse(TEXT("Bound guard release clears guard"), Game->Combat.Get(ronin::FighterId::Player).guarding);
    const FRoninAnimationPlayback BeforePause = PlayerAnimation->GetPlaybackState();
    const FTransform WeaponBeforePause = Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component);
    bOk &= TestTrue(TEXT("Native idle graph advances in the live world"), BeforePause.IdleTime > .5f);
    bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound pause action pauses combat"), Game->bCombatPaused);
    const FVector PausedPosition = Player->GetActorLocation();
    const float PausedTime = Game->GetRoundTime();
    bOk &= InvokeAction(TEXT("Light"), IE_Pressed);
    for (int32 Frame = 0; Frame < 12; ++Frame) TestWorld.TickTestWorld(Step);
    bOk &= TestTrue(TEXT("Paused input cannot start an attack"), Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Idle);
    bOk &= TestTrue(TEXT("Paused fighter remains in place"), Player->GetActorLocation().Equals(PausedPosition, 0.1));
    bOk &= TestEqual(TEXT("Pause freezes the duel clock"), Game->GetRoundTime(), PausedTime);
    const FRoninAnimationPlayback PausedAnimation = PlayerAnimation->GetPlaybackState();
    bOk &= TestEqual(TEXT("Pause freezes the native idle clock"), PausedAnimation.IdleTime, BeforePause.IdleTime);
    bOk &= TestTrue(TEXT("Pause preserves the evaluated weapon pose"),
        Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component).Equals(WeaponBeforePause, .001));
    bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
    bOk &= TestFalse(TEXT("Bound pause action resumes combat"), Game->bCombatPaused);
    bOk &= InvokeAction(TEXT("Light"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound light attack starts the runtime attack"), Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Windup);
    for (int32 Frame = 0; Frame < 12; ++Frame) TestWorld.TickTestWorld(Step);
    const FRoninAnimationPlayback AttackAnimation = PlayerAnimation->GetPlaybackState();
    const FTransform AttackingWeapon = Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component);
    bOk &= TestTrue(TEXT("Attack blends in a real authored action clip"), AttackAnimation.ActionSequence
        && AttackAnimation.ActionSequence != PlayerAnimation->GetClip(ERoninAnimationClip::Idle)
        && AttackAnimation.ActionTime > 0.f && AttackAnimation.ActionWeight > .5f);
    bOk &= TestFalse(TEXT("Authored attack moves weapon_r in component space"), AttackingWeapon.Equals(WeaponBeforePause, .01));
    bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
    for (int32 Frame = 0; Frame < 12; ++Frame) TestWorld.TickTestWorld(Step);
    const FRoninAnimationPlayback PausedAttack = PlayerAnimation->GetPlaybackState();
    bOk &= TestEqual(TEXT("Pause freezes the authored attack time"), PausedAttack.ActionTime, AttackAnimation.ActionTime);
    bOk &= TestEqual(TEXT("Pause freezes the action blend"), PausedAttack.ActionWeight, AttackAnimation.ActionWeight);
    bOk &= TestTrue(TEXT("Pause holds the evaluated attack pose"),
        Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component).Equals(AttackingWeapon, .001));
    bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
    bOk &= TestTrue(TEXT("Rematch returns directly to an active duel"), Game->HasDuelStarted() && Game->CanControl());
    bOk &= TestEqual(TEXT("Rematch resets the duel clock"), Game->GetRoundTime(), 0.f);
    bOk &= TestTrue(TEXT("Rematch clears the previous attack"), Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Idle);

    // Resolve a real close-range contact through GameMode, so this checks the
    // hit-stop integration rather than manually freezing the animation instance.
    Player->ResetPose(FVector(-85, 0, 100), FRotator::ZeroRotator);
    Enemy->ResetPose(FVector(85, 0, 100), FRotator(0, 180, 0));
    for (int32 Frame = 0; Frame < 12; ++Frame) TestWorld.TickTestWorld(Step);
    bOk &= InvokeAction(TEXT("Light"), IE_Pressed);
    bool bObservedHitStop = false;
    for (int32 Frame = 0; Frame < 120; ++Frame)
    {
        TestWorld.TickTestWorld(Step);
        if (PlayerAnimation->GetPlaybackState().bFrozen) { bObservedHitStop = true; break; }
    }
    bOk &= TestTrue(TEXT("Actual combat contact enters animation hit-stop"), bObservedHitStop);
    if (bObservedHitStop)
    {
        const FRoninAnimationPlayback Contact = PlayerAnimation->GetPlaybackState();
        const FTransform ContactWeapon = Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component);
        const float ContactRoundTime = Game->GetRoundTime();
        TestWorld.TickTestWorld(Step);
        const FRoninAnimationPlayback HeldContact = PlayerAnimation->GetPlaybackState();
        bOk &= TestTrue(TEXT("Hit-stop remains active on the next simulation frame"), HeldContact.bFrozen);
        bOk &= TestEqual(TEXT("Hit-stop freezes the combat clock"), Game->GetRoundTime(), ContactRoundTime);
        bOk &= TestEqual(TEXT("Hit-stop freezes the action clip time"), HeldContact.ActionTime, Contact.ActionTime);
        bOk &= TestEqual(TEXT("Hit-stop freezes the idle clock"), HeldContact.IdleTime, Contact.IdleTime);
        bOk &= TestTrue(TEXT("Hit-stop preserves the evaluated contact pose"),
            Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component).Equals(ContactWeapon, .001));
        for (int32 Frame = 0; Frame < 24; ++Frame) TestWorld.TickTestWorld(Step);
        bOk &= TestFalse(TEXT("Animation resumes after hit-stop expires"), PlayerAnimation->GetPlaybackState().bFrozen);
        bOk &= TestTrue(TEXT("Combat clock resumes after hit-stop"), Game->GetRoundTime() > ContactRoundTime);
    }

    // Prepare death through the public combat API without AI or world timing
    // races, then exercise the actual animation graph and bound rematch action.
    bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
    for (int32 Hit = 0; Hit < 8 && Game->Combat.Get(ronin::FighterId::Enemy).action != ronin::Action::Dead; ++Hit)
    {
        if (!Game->Combat.TryAttack(ronin::FighterId::Player, ronin::AttackKind::Heavy)) break;
        const float Windup = Game->Combat.Get(ronin::FighterId::Player).spec.windup;
        Game->Combat.Tick(Windup);
        Game->Combat.ResolveHit(ronin::FighterId::Player, 100.f);
        Game->Combat.Tick(4.f);
    }
    bOk &= TestTrue(TEXT("Death scenario reaches a legitimate dead combat state"),
        Game->Combat.Get(ronin::FighterId::Enemy).action == ronin::Action::Dead);
    for (int32 Frame = 0; Frame < 30; ++Frame) TestWorld.TickTestWorld(Step);
    const FRoninAnimationPlayback Death = EnemyAnimation->GetPlaybackState();
    bOk &= TestTrue(TEXT("Dead fighter evaluates the authored death clip"),
        Death.ActionSequence == EnemyAnimation->GetClip(ERoninAnimationClip::Death)
        && Death.ActionTime > .1f && Death.ActionWeight > .9f);
    bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
    TestWorld.TickTestWorld(Step);
    for (URoninAnimInstance* Animation : {PlayerAnimation, EnemyAnimation})
    {
        const FRoninAnimationPlayback Reset = Animation->GetPlaybackState();
        bOk &= TestTrue(TEXT("Rematch discards previous attack/death evaluation"),
            Reset.ActionSequence == Animation->GetClip(ERoninAnimationClip::Idle)
            && FMath::IsNearlyZero(Reset.ActionTime) && FMath::IsNearlyZero(Reset.ActionWeight));
        bOk &= TestTrue(TEXT("Rematch restarts the idle clock"), Reset.IdleTime <= Step * 2.f);
    }

    bOk &= TestWorld.DestroyTestWorld(false);
    TestWorld.ForwardErrorMessages(this);
    return bOk && !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
