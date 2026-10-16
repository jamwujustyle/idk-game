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
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Distributions/DistributionFloat.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Misc/ConfigCacheIni.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/TypeData/ParticleModuleTypeDataAnimTrail.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
#include "Sound/SoundWave.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr const TCHAR* BladeTrailAssetPath =
    TEXT("/Game/ParagonKwang/FX/Particles/Abilities/Primary/FX/P_Kwang_Primary_Trail.P_Kwang_Primary_Trail");
constexpr const TCHAR* ContactSparkAssetPath =
    TEXT("/Game/ParagonKwang/FX/Particles/Abilities/Primary/FX/P_Kwang_Primary_Impact.P_Kwang_Primary_Impact");

UStaticMeshComponent* FindWeapon(const ARoninFighter* Fighter, FName Name)
{
    TArray<UStaticMeshComponent*> Components;
    Fighter->GetComponents(Components);
    for (UStaticMeshComponent* Component : Components)
        if (Component->GetFName() == Name) return Component;
    return nullptr;
}

TArray<FTransform> ReferenceComponentPose(const FReferenceSkeleton& Skeleton)
{
    TArray<FTransform> Result = Skeleton.GetRefBonePose();
    for (int32 Bone = 0; Bone < Result.Num(); ++Bone)
    {
        const int32 Parent = Skeleton.GetParentIndex(Bone);
        if (Parent != INDEX_NONE) Result[Bone] = Result[Bone] * Result[Parent];
    }
    return Result;
}

// Check the observable skinning relationship, rather than duplicating the
// native node's local-space correction. This also catches composition mistakes.
bool CheckEquippedPose(FAutomationTestBase& Test, ARoninFighter* Fighter, const FString& Label)
{
    USkeletalMeshComponent* Body = Fighter->GetMesh();
    if (!Test.TestNotNull(Label + TEXT(" has a skeletal asset"), Body->GetSkeletalMeshAsset())) return false;
    const FReferenceSkeleton& Skeleton = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform> Reference = ReferenceComponentPose(Skeleton);
    bool bOk = true;
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const FName Hand(Side == 0 ? TEXT("mixamorig_RightHand") : TEXT("mixamorig_LeftHand"));
        const FName Branch(Side == 0 ? TEXT("Bone_020") : TEXT("Bone_025"));
        const FName Tip(Side == 0 ? TEXT("Bone_019") : TEXT("Bone_024"));
        const int32 H = Skeleton.FindBoneIndex(Hand), A = Skeleton.FindBoneIndex(Branch), C = Skeleton.FindBoneIndex(Tip);
        if (!Test.TestTrue(Label + TEXT(" has all weighted wrist bones"), H != INDEX_NONE && A != INDEX_NONE && C != INDEX_NONE))
        { bOk = false; continue; }
        const FTransform HandPose = Body->GetSocketTransform(Hand, RTS_Component);
        const FTransform BranchPose = Body->GetSocketTransform(Branch, RTS_Component);
        const FTransform TipPose = Body->GetSocketTransform(Tip, RTS_Component);
        bOk &= Test.TestTrue(Label + TEXT(" keeps weighted wrist branches coherent with the hand"),
            BranchPose.GetRelativeTransform(HandPose).Equals(Reference[A].GetRelativeTransform(Reference[H]), .002));
        bOk &= Test.TestTrue(Label + TEXT(" preserves the coarse finger branch's bind shape"),
            TipPose.GetRelativeTransform(BranchPose).Equals(Skeleton.GetRefBonePose()[C], .002));
    }
    const FName Hips(TEXT("mixamorig_Hips"));
    const int32 HipIndex = Skeleton.FindBoneIndex(Hips);
    if (Test.TestTrue(Label + TEXT(" has a pelvis root"), HipIndex == 0))
    {
        const FVector Pelvis = Body->GetSocketTransform(Hips, RTS_Component).GetTranslation();
        bOk &= Test.TestFalse(Label + TEXT(" has a finite pelvis pose"), Pelvis.ContainsNaN());
        bOk &= Test.TestTrue(Label + TEXT(" keeps an upright pelvis above the feet"), Pelvis.Z > 30. && Pelvis.Z < 135.);
        bOk &= Test.TestTrue(Label + TEXT(" leaves horizontal travel to the capsule"),
            FVector::Dist2D(Pelvis, Reference[HipIndex].GetTranslation()) < .01);
    }
    else bOk = false;
    for (const TCHAR* Name : {TEXT("Katana"), TEXT("Saya")})
    {
        const UStaticMeshComponent* Weapon = FindWeapon(Fighter, Name);
        if (!Test.TestNotNull(Label + TEXT(" retains ") + Name, Weapon)) { bOk = false; continue; }
        bOk &= Test.TestTrue(Label + TEXT(" keeps its weapon attached to the evaluated bone"),
            Weapon->GetComponentTransform().GetRelativeTransform(Body->GetSocketTransform(Weapon->GetAttachSocketName()))
                .Equals(Weapon->GetRelativeTransform(), .002));
        bOk &= Test.TestTrue(Label + TEXT(" preserves physical weapon scale"),
            Weapon->GetComponentTransform().GetScale3D().Equals(FVector::OneVector, .001));
    }
    return bOk;
}

// Snapshot the source graph and the settings altered by runtime filtering.
// Compare observed values instead of duplicating the pack's original tuning.
TArray<FString> SnapshotImpactSource(const UParticleSystem* Source)
{
    TArray<FString> Result;
    Result.Add(FString::Printf(TEXT("warmup:%.9g"), Source->WarmupTime));
    for (const UParticleEmitter* Emitter : Source->Emitters)
    {
        Result.Add(GetPathNameSafe(Emitter));
        if (!Emitter) continue;
        Result.Add(Emitter->EmitterName.ToString());
        for (const UParticleLODLevel* LOD : Emitter->LODLevels)
        {
            Result.Add(GetPathNameSafe(LOD));
            if (!LOD) continue;
            Result.Add(GetPathNameSafe(LOD->RequiredModule));
            Result.Add(GetPathNameSafe(LOD->SpawnModule));
            Result.Add(GetPathNameSafe(LOD->TypeDataModule));
            for (const UParticleModule* Module : LOD->Modules) Result.Add(GetPathNameSafe(Module));
            if (const auto* Required = LOD->RequiredModule.Get())
                Result.Add(FString::Printf(TEXT("duration:%.9g,%.9g,%d delay:%.9g,%.9g,%d loops:%d"),
                    Required->EmitterDuration, Required->EmitterDurationLow, int32(Required->bEmitterDurationUseRange),
                    Required->EmitterDelay, Required->EmitterDelayLow, int32(Required->bEmitterDelayUseRange), Required->EmitterLoops));
            if (auto* Spawn = LOD->SpawnModule.Get())
            {
                Result.Add(GetPathNameSafe(Spawn->Rate.Distribution));
                for (float Time : {0.f, .05f, .5f, 1.f})
                {
                    FRandomStream Random(17);
                    Result.Add(FString::Printf(TEXT("rate:%.9g"), Spawn->Rate.GetValue(Time, nullptr, &Random)));
                }
                for (const FParticleBurst& Burst : Spawn->BurstList)
                    Result.Add(FString::Printf(TEXT("burst:%d,%d,%.9g"), Burst.Count, Burst.CountLow, Burst.Time));
            }
        }
    }
    return Result;
}
}

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
    for (const TCHAR* Name : {TEXT("Katana"), TEXT("Saya")})
    {
        const UStaticMeshComponent* Weapon = FindWeapon(Fighter, Name);
        if (!TestNotNull(FString(Name) + TEXT(" component exists"), Weapon)) { bOk = false; continue; }
        bOk &= TestNotNull(FString(Name) + TEXT(" resolves its original mesh"), Weapon->GetStaticMesh().Get());
        const FName ParentBone(FCString::Strcmp(Name, TEXT("Katana")) == 0 ? TEXT("mixamorig_RightHand") : TEXT("mixamorig_Hips"));
        bOk &= TestTrue(FString(Name) + TEXT(" is attached to the intended body bone"),
            Weapon->GetAttachParent() == Body && Weapon->GetAttachSocketName() == ParentBone);
        bOk &= TestTrue(FString(Name) + TEXT(" leaves collision to deterministic combat"),
            Weapon->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Weapon->GetGenerateOverlapEvents());
        if (!Body || !Body->GetSkeletalMeshAsset()) continue;
        const FReferenceSkeleton& Skeleton = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
        const int32 ParentIndex = Skeleton.FindBoneIndex(ParentBone);
        if (!TestTrue(FString(Name) + TEXT(" attachment bone resolves"), ParentIndex != INDEX_NONE)) { bOk = false; continue; }
        const FTransform BindWeapon = Weapon->GetRelativeTransform() * ReferenceComponentPose(Skeleton)[ParentIndex];
        const FVector TipDirection = BindWeapon.TransformVectorNoScale(FVector::UpVector);
        if (FCString::Strcmp(Name, TEXT("Katana")) == 0)
        {
            bOk &= TestTrue(TEXT("Neutral katana points forward with its edge down in imported mesh space"),
                TipDirection.Equals(FVector::RightVector, .001)
                && BindWeapon.TransformVectorNoScale(-FVector::ForwardVector).Equals(-FVector::UpVector, .001));
            bOk &= TestTrue(TEXT("Katana grip pivot is within a hand length of the wrist"),
                Weapon->GetRelativeLocation().Size() > 2. && Weapon->GetRelativeLocation().Size() < 14.);
        }
        else
        {
            const FVector Mouth = BindWeapon.GetTranslation();
            bOk &= TestTrue(TEXT("Saya mouth sits at the left hip and its tip points back/down"),
                Mouth.X > 10. && Mouth.X < 30. && Mouth.Z > 85. && Mouth.Z < 110.
                && TipDirection.Y < -.8 && TipDirection.Z < 0.);
        }
    }
    const UParticleSystemComponent* Trail = Fighter->FindComponentByClass<UParticleSystemComponent>();
    bOk &= TestNotNull(TEXT("Fighter owns its reusable blade trail component"), Trail);
    if (Trail)
    {
        bOk &= TestTrue(TEXT("Blade trail samples the original katana"), Trail->GetAttachParent() == Fighter->GetKatana());
        bOk &= TestFalse(TEXT("Blade trail does not activate on spawn"), Trail->bAutoActivate);
        bOk &= TestFalse(TEXT("Blade trail starts inactive"), Trail->IsActive());
        bOk &= TestFalse(TEXT("Blade trail survives for later attacks and rematches"), Trail->bAutoDestroy);
        bOk &= TestTrue(TEXT("Blade trail uses the selected authored effect"),
            Trail->Template && Trail->Template->GetPathName() == BladeTrailAssetPath);
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
    for (const TCHAR* Name : {TEXT("SK_Samurai")})
    {
        const FString Path = FString::Printf(TEXT("/Game/Characters/Samurai/%s.%s"), Name, Name);
        USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path);
        bOk &= TestNotNull(FString::Printf(TEXT("Authored fighter mesh loads: %s"), Name), Mesh);
        if (!Mesh) continue;
        bOk &= TestNotNull(FString::Printf(TEXT("Fighter skeleton resolves: %s"), Name), Mesh->GetSkeleton());
        for (const FName Bone : {FName(TEXT("mixamorig_RightHand")), FName(TEXT("mixamorig_Spine"))})
            bOk &= TestTrue(FString::Printf(TEXT("%s has required pose bone %s"), Name, *Bone.ToString()),
                Mesh->GetRefSkeleton().FindBoneIndex(Bone) != INDEX_NONE);
        bOk &= TestTrue(TEXT("Original samurai has adult human dimensions"),
            Mesh->GetBounds().BoxExtent.Z > 80.f && Mesh->GetBounds().BoxExtent.Z < 100.f);
        bOk &= TestTrue(TEXT("Original samurai has its textured material"),
            Mesh->GetMaterials().Num() == 1 && Mesh->GetMaterials()[0].MaterialInterface
            && Mesh->GetMaterials()[0].MaterialInterface->GetPathName().StartsWith(TEXT("/Game/Characters/Samurai/")));
        const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
        bOk &= TestEqual(TEXT("Samurai pelvis is bone zero"), Skeleton.FindBoneIndex(TEXT("mixamorig_Hips")), 0);
        for (int32 Side = 0; Side < 2; ++Side)
        {
            const int32 Hand = Skeleton.FindBoneIndex(Side == 0 ? TEXT("mixamorig_RightHand") : TEXT("mixamorig_LeftHand"));
            const int32 Branch = Skeleton.FindBoneIndex(Side == 0 ? TEXT("Bone_020") : TEXT("Bone_025"));
            const int32 Tip = Skeleton.FindBoneIndex(Side == 0 ? TEXT("Bone_019") : TEXT("Bone_024"));
            if (!TestTrue(TEXT("All weighted wrist joints survive import"), Hand != INDEX_NONE && Branch != INDEX_NONE && Tip != INDEX_NONE))
            { bOk = false; continue; }
            bOk &= TestTrue(TEXT("Hand and weighted helper are siblings, with a helper child"),
                Skeleton.GetParentIndex(Hand) == Skeleton.GetParentIndex(Branch) && Skeleton.GetParentIndex(Tip) == Branch);
        }
        for (int32 Index = 0; Index < static_cast<int32>(ERoninAnimationClip::Count); ++Index)
        {
            UAnimSequence* Sequence = Animation->GetClip(static_cast<ERoninAnimationClip>(Index));
            const FString Label = FString::Printf(TEXT("%s animation slot %d"), Name, Index);
            bOk &= TestNotNull(Label + TEXT(" resolves"), Sequence);
            if (!Sequence) continue;
            bOk &= TestTrue(Label + TEXT(" uses the fighter skeleton"), Sequence->GetSkeleton() == Mesh->GetSkeleton());
            bOk &= TestTrue(Label + TEXT(" has a playable authored duration"), Sequence->GetPlayLength() > 0.f);
            bOk &= TestTrue(Label + TEXT(" is a full pose suitable for sequence evaluation"), Sequence->GetAdditiveAnimType() == AAT_None);
            bOk &= TestFalse(Label + TEXT(" preserves animated pelvis height instead of locking all root axes"), Sequence->bForceRootLock);
        }
    }
    for (const TCHAR* Name : {TEXT("Katana"), TEXT("Saya")})
    {
        const FString Path = FString::Printf(TEXT("/Game/Weapons/Katana/SM_%s.SM_%s"), Name, Name);
        UStaticMesh* Weapon = LoadObject<UStaticMesh>(nullptr, *Path);
        if (!TestNotNull(FString(Name) + TEXT(" original asset loads"), Weapon)) { bOk = false; continue; }
        const FBox Bounds = Weapon->GetBoundingBox();
        bOk &= TestTrue(FString(Name) + TEXT(" has centimetre-scale physical bounds"),
            Bounds.IsValid && Bounds.GetSize().Z > 70. && Bounds.GetSize().Z < 110.
            && Bounds.GetSize().X < 10. && Bounds.GetSize().Y < 10.);
        if (FCString::Strcmp(Name, TEXT("Katana")) != 0) continue;
        const UStaticMeshSocket* Base = Weapon->FindSocket(TEXT("FX_weapon_base"));
        const UStaticMeshSocket* Tip = Weapon->FindSocket(TEXT("FX_weapon_tip"));
        if (!TestNotNull(TEXT("Katana exposes its blade base for trails"), Base)
            || !TestNotNull(TEXT("Katana exposes its actual blade tip for trails"), Tip)) { bOk = false; continue; }
        bOk &= TestTrue(TEXT("Trail endpoints remain inside the original blade bounds"),
            Bounds.ExpandBy(.01).IsInsideOrOn(Base->RelativeLocation) && Bounds.ExpandBy(.01).IsInsideOrOn(Tip->RelativeLocation));
        bOk &= TestTrue(TEXT("Trail follows the exposed blade and its curved tip"),
            Base->RelativeLocation.Equals(FVector(0, 0, 18), .01)
            && Tip->RelativeLocation.Equals(FVector(3.3, 0, 90), .01));
        bOk &= TestTrue(TEXT("Trail spans the blade forward from the grip, ending at the real tip"),
            Base->RelativeLocation.Z > 0. && Tip->RelativeLocation.Z - Base->RelativeLocation.Z > 65.
            && FMath::Abs(Tip->RelativeLocation.Z - Bounds.Max.Z) < 1.);
    }
    UParticleSystem* Trail = LoadObject<UParticleSystem>(nullptr, BladeTrailAssetPath);
    bOk &= TestNotNull(TEXT("Authored blade trail loads as a Cascade particle system"), Trail);
    if (Trail)
    {
        bool bHasSocketTrail = false;
        for (const UParticleEmitter* Emitter : Trail->Emitters)
        {
            if (!Emitter) continue;
            for (const UParticleLODLevel* LOD : Emitter->LODLevels)
                bHasSocketTrail |= LOD && LOD->TypeDataModule && LOD->TypeDataModule->IsA<UParticleModuleTypeDataAnimTrail>();
        }
        bOk &= TestTrue(TEXT("Authored effect supports BeginTrails socket sampling"), bHasSocketTrail);
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
        TEXT("Restart"), TEXT("Help"), TEXT("StartDuel"), TEXT("Sprint"), TEXT("Jump")})
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
    for (const FKey& Key : {FKey(TEXT("Gamepad_LeftThumbstick")), FKey(TEXT("LeftShift"))})
        bOk &= TestTrue(FString::Printf(TEXT("Sprint uses the requested input: %s"), *Key.ToString()),
            Actions.ContainsByPredicate([&Key](const FInputActionKeyMapping& Mapping)
            { return Mapping.ActionName == TEXT("Sprint") && Mapping.Key == Key; }));
    for (const FKey& Key : {FKey(TEXT("Gamepad_FaceButton_Bottom")), FKey(TEXT("SpaceBar"))})
        bOk &= TestTrue(FString::Printf(TEXT("Jump uses the requested input: %s"), *Key.ToString()),
            Actions.ContainsByPredicate([&Key](const FInputActionKeyMapping& Mapping)
            { return Mapping.ActionName == TEXT("Jump") && Mapping.Key == Key; }));
    for (const FKey& Key : {FKey(TEXT("Gamepad_FaceButton_Right")), FKey(TEXT("LeftControl"))})
        bOk &= TestTrue(FString::Printf(TEXT("Dodge remains distinct from jump: %s"), *Key.ToString()),
            Actions.ContainsByPredicate([&Key](const FInputActionKeyMapping& Mapping)
            { return Mapping.ActionName == TEXT("Dodge") && Mapping.Key == Key; }));
    bOk &= TestFalse(TEXT("Space no longer triggers dodge alongside jump"),
        Actions.ContainsByPredicate([](const FInputActionKeyMapping& Mapping)
        { return Mapping.ActionName == TEXT("Dodge") && Mapping.Key == FKey(TEXT("SpaceBar")); }));
    // Paused quality cycling reuses Jump's gamepad button; keeping a second
    // consuming A binding would prevent the active-game jump action.
    bOk &= TestTrue(TEXT("Quality cycling retains its keyboard binding"),
        Actions.ContainsByPredicate([](const FInputActionKeyMapping& Mapping)
        { return Mapping.ActionName == TEXT("Quality") && Mapping.Key.IsValid() && !Mapping.Key.IsGamepadKey(); }));
    bOk &= TestFalse(TEXT("Quality cannot consume the active-game jump button"),
        Actions.ContainsByPredicate([](const FInputActionKeyMapping& Mapping)
        { return Mapping.ActionName == TEXT("Quality") && Mapping.Key == FKey(TEXT("Gamepad_FaceButton_Bottom")); }));
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

    TArray<UObject*> GameModeReferences;
    FReferenceFinder GameModeFinder(GameModeReferences, nullptr, false, true, false, true);
    GameModeFinder.FindReferences(GetMutableDefault<ARoninGameMode>());
    UParticleSystem* SparkSource = LoadObject<UParticleSystem>(nullptr, ContactSparkAssetPath);
    bOk &= TestTrue(TEXT("Game mode retains the authored contact-spark source for cooking"),
        SparkSource && GameModeReferences.Contains(SparkSource));

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
    for (const TCHAR* Name : {TEXT("SK_Samurai")})
    {
        const FString Path = FString::Printf(TEXT("/Game/Characters/Samurai/%s.%s"), Name, Name);
        UObject* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path);
        bOk &= TestTrue(FString::Printf(TEXT("Runtime default retains cookable mesh reference: %s"), Name),
            Mesh && FighterReferences.Contains(Mesh));
    }
    UParticleSystemComponent* Trail = GetMutableDefault<ARoninFighter>()->FindComponentByClass<UParticleSystemComponent>();
    bOk &= TestTrue(TEXT("Fighter default retains the trail component for cooking"), Trail && FighterReferences.Contains(Trail));
    if (Trail)
    {
        TArray<UObject*> TrailReferences;
        FReferenceFinder TrailFinder(TrailReferences, nullptr, false, true, false, true);
        TrailFinder.FindReferences(Trail);
        UParticleSystem* Template = LoadObject<UParticleSystem>(nullptr, BladeTrailAssetPath);
        bOk &= TestTrue(TEXT("Trail component retains the selected cookable effect"), Template && TrailReferences.Contains(Template));
    }
    for (const TCHAR* Name : {TEXT("Katana"), TEXT("Saya")})
    {
        UStaticMeshComponent* Weapon = FindWeapon(GetDefault<ARoninFighter>(), Name);
        bOk &= TestTrue(FString(Name) + TEXT(" component remains reachable from the fighter default"),
            Weapon && FighterReferences.Contains(Weapon));
        if (!Weapon) continue;
        TArray<UObject*> WeaponReferences;
        FReferenceFinder WeaponFinder(WeaponReferences, nullptr, false, true, false, true);
        WeaponFinder.FindReferences(Weapon);
        const FString Path = FString::Printf(TEXT("/Game/Weapons/Katana/SM_%s.SM_%s"), Name, Name);
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
        bOk &= TestTrue(FString(Name) + TEXT(" original mesh is retained by a cookable hard reference"),
            Mesh && Weapon->GetStaticMesh() == Mesh && WeaponReferences.Contains(Mesh));
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
    UParticleSystem* SparkSource = LoadObject<UParticleSystem>(nullptr, ContactSparkAssetPath);
    if (!TestNotNull(TEXT("Authored impact source loads before runtime filtering"), SparkSource)) return false;
    const TArray<FString> OriginalImpact = SnapshotImpactSource(SparkSource);

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
    bOk &= TestEqual(TEXT("Authored impact retains all seven original emitters"), SparkSource->Emitters.Num(), 7);
    const UParticleSystem* ContactSparks = Game->GetContactSparkTemplate();
    bOk &= TestNotNull(TEXT("StartPlay prepares a filtered contact-spark template"), ContactSparks);
    if (ContactSparks)
    {
        bOk &= TestTrue(TEXT("Filtered sparks are a transient game-owned copy"), ContactSparks != SparkSource
            && ContactSparks->HasAnyFlags(RF_Transient) && ContactSparks->IsIn(Game));
        bOk &= TestEqual(TEXT("Only the two physical spark emitters can activate"), ContactSparks->Emitters.Num(), 2);
        TSet<FName> ExpectedNames{FName(TEXT("Sparks")), FName(TEXT("Sparks_Smaller"))};
        for (const UParticleEmitter* Emitter : ContactSparks->Emitters)
        {
            if (!TestNotNull(TEXT("Filtered emitter exists"), Emitter)) { bOk = false; continue; }
            bOk &= TestTrue(TEXT("Filtered emitter is owned by its runtime template"), Emitter->IsIn(ContactSparks));
            bOk &= TestEqual(TEXT("Filtered emitter is an expected unique spark layer"), ExpectedNames.Remove(Emitter->EmitterName), 1);
            bOk &= TestEqual(TEXT("Both authored spark LODs survive filtering"), Emitter->LODLevels.Num(), 2);
            for (const UParticleLODLevel* LOD : Emitter->LODLevels)
            {
                if (!TestNotNull(TEXT("Filtered spark LOD exists"), LOD)) { bOk = false; continue; }
                auto Owned = [&](const UObject* Object)
                {
                    return TestTrue(TEXT("Spark modules are remapped into the runtime copy"), Object && Object->IsIn(ContactSparks));
                };
                bOk &= Owned(LOD);
                bOk &= Owned(LOD->RequiredModule);
                bOk &= Owned(LOD->SpawnModule);
                bOk &= Owned(LOD->TypeDataModule);
                for (const UParticleModule* Module : LOD->Modules) if (Module) bOk &= Owned(Module);
                for (const UParticleModule* Module : LOD->SpawnModules) bOk &= Owned(Module);
                for (const UParticleModule* Module : LOD->UpdateModules) bOk &= Owned(Module);
                bOk &= TestTrue(TEXT("Filtered spark simulation retains GPU type data"),
                    LOD->TypeDataModule && LOD->TypeDataModule->IsA<UParticleModuleTypeDataGpu>());
                bOk &= TestTrue(TEXT("Runtime duplication rebuilds executable module lists"),
                    !LOD->Modules.IsEmpty() && !LOD->SpawnModules.IsEmpty() && !LOD->UpdateModules.IsEmpty());
                if (!LOD->RequiredModule || !LOD->SpawnModule) continue;
                const auto* Required = LOD->RequiredModule.Get();
                bOk &= TestTrue(TEXT("Contact emission starts immediately and cannot loop"),
                    Required->EmitterDelay == 0.f && Required->EmitterDelayLow == 0.f
                    && Required->EmitterLoops == 1 && Required->EmitterDuration > 0.f && Required->EmitterDuration <= .1f);
                auto* Spawn = LOD->SpawnModule.Get();
                for (float Time : {0.f, .025f, .05f, 1.f})
                    bOk &= TestEqual(TEXT("Contact sparks have no continuous emission"), Spawn->Rate.GetValue(Time), 0.f);
                bOk &= TestEqual(TEXT("Each contact layer emits one bounded burst"), Spawn->BurstList.Num(), 1);
                if (Spawn->BurstList.Num() == 1)
                {
                    const FParticleBurst& Burst = Spawn->BurstList[0];
                    const int32 ExpectedCount = Emitter->EmitterName == TEXT("Sparks") ? 6 : 12;
                    const float BurstScale = Spawn->BurstScale.GetValue(0.f);
                    bOk &= TestTrue(TEXT("Contact burst is small, fixed and immediate"),
                        Burst.Count == ExpectedCount && Burst.CountLow == -1 && Burst.Time == 0.f
                        && BurstScale > 0.f && BurstScale <= 1.f);
                }
            }
        }
        bOk &= TestTrue(TEXT("Both required physical spark layers are present"), ExpectedNames.IsEmpty());
    }
    bOk &= TestTrue(TEXT("Runtime filtering preserves authored impact graph and emission settings"),
        SnapshotImpactSource(SparkSource) == OriginalImpact);
    bOk &= TestTrue(TEXT("Both fighters receive BeginPlay"), Player->HasActorBegunPlay() && Enemy->HasActorBegunPlay());
    URoninAnimInstance* PlayerAnimation = Cast<URoninAnimInstance>(Player->GetMesh()->GetAnimInstance());
    URoninAnimInstance* EnemyAnimation = Cast<URoninAnimInstance>(Enemy->GetMesh()->GetAnimInstance());
    if (!TestNotNull(TEXT("Player creates a live native animation instance"), PlayerAnimation)
        || !TestNotNull(TEXT("Enemy creates a live native animation instance"), EnemyAnimation)) return false;
    FLinearColor PlayerTint, EnemyTint;
    bOk &= TestTrue(TEXT("Both fighters use the original samurai with distinct armor tints"),
        Player->GetMesh()->GetSkeletalMeshAsset() == Enemy->GetMesh()->GetSkeletalMeshAsset()
        && Player->GetMesh()->GetMaterial(0)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ColorTint")), PlayerTint)
        && Enemy->GetMesh()->GetMaterial(0)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("ColorTint")), EnemyTint)
        && !PlayerTint.Equals(EnemyTint));
    const FName WeaponBone(TEXT("mixamorig_RightHand"));
    bOk &= TestTrue(TEXT("Live player exposes the animated weapon bone"), Player->GetMesh()->GetBoneIndex(WeaponBone) != INDEX_NONE);
    UStaticMeshComponent* PlayerKatana = Player->GetKatana();
    if (!TestNotNull(TEXT("Live player equips the original katana"), PlayerKatana)) return false;
    TArray<UParticleSystemComponent*> BladeTrails;
    for (ARoninFighter* Fighter : {Player, Enemy})
    {
        TArray<UParticleSystemComponent*> Components;
        Fighter->GetComponents(Components);
        bOk &= TestEqual(TEXT("Each fighter owns one blade trail component"), Components.Num(), 1);
        for (UParticleSystemComponent* Trail : Components)
        {
            BladeTrails.Add(Trail);
            bOk &= TestFalse(TEXT("Live blade trail remains inactive before the duel"), Trail->IsActive());
            const auto& Prerequisites = Trail->PrimaryComponentTick.GetPrerequisites();
            bOk &= TestTrue(TEXT("Trail samples after combat phase updates"),
                Prerequisites.Contains(FTickPrerequisite(Fighter, Fighter->PrimaryActorTick)));
            bOk &= TestTrue(TEXT("Trail samples after the skeletal pose is evaluated"),
                Prerequisites.Contains(FTickPrerequisite(Fighter->GetMesh(), Fighter->GetMesh()->PrimaryComponentTick)));
        }
    }
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
    auto InvokeAxis = [this, Input](FName Name, float Value)
    {
        for (const FInputAxisBinding& Binding : Input->AxisBindings)
        {
            if (Binding.AxisName == Name && Binding.AxisDelegate.IsBound())
            {
                Binding.AxisDelegate.Execute(Value);
                return true;
            }
        }
        AddError(FString::Printf(TEXT("Live input component has no bound axis: %s"), *Name.ToString()));
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
    bOk &= InvokeAction(TEXT("Jump"), IE_Pressed);
    const double StartupHeight = Player->GetActorLocation().Z;
    double HighestStartupHeight = StartupHeight;
    // Simulate a long shader/asset warmup. Both the encounter clock and core
    // decision/phase clocks must remain untouched, not merely the AI attack gate.
    for (int32 Frame = 0; Frame < 180; ++Frame)
    {
        TestWorld.TickTestWorld(1.f/30.f);
        HighestStartupHeight = FMath::Max(HighestStartupHeight, Player->GetActorLocation().Z);
        if (Frame == 12) bOk &= InvokeAction(TEXT("Jump"), IE_Released);
    }
    bOk &= TestTrue(TEXT("Jump input cannot lift the fighter before the duel starts"), HighestStartupHeight <= StartupHeight + 1.);
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
    const FTransform HandBeforeGuard = Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component);
    bOk &= InvokeAction(TEXT("Guard"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound guard press raises guard"), Game->Combat.Get(ronin::FighterId::Player).guarding);
    for (int32 Frame = 0; Frame < 12; ++Frame)
    {
        TestWorld.TickTestWorld(Step);
        if (Frame == 5 || Frame == 11)
            bOk &= CheckEquippedPose(*this, Player, FString::Printf(TEXT("Guard frame %d"), Frame));
    }
    bOk &= TestFalse(TEXT("Guard evaluates a raised-hand pose through the native graph"),
        Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component).Equals(HandBeforeGuard, .01));
    bOk &= CheckEquippedPose(*this, Enemy, TEXT("Enemy idle"));
    bOk &= InvokeAction(TEXT("Guard"), IE_Released);
    bOk &= TestFalse(TEXT("Bound guard release clears guard"), Game->Combat.Get(ronin::FighterId::Player).guarding);
    const FRoninAnimationPlayback BeforePause = PlayerAnimation->GetPlaybackState();
    const FTransform WeaponBeforePause = Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component);
    const FVector TipBeforePause = PlayerKatana->GetSocketLocation(TEXT("FX_weapon_tip"));
    bOk &= TestTrue(TEXT("Native idle graph advances in the live world"), BeforePause.IdleTime > .5f);
    bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound pause action pauses combat"), Game->bCombatPaused);
    const FVector PausedPosition = Player->GetActorLocation();
    const float PausedTime = Game->GetRoundTime();
    bOk &= InvokeAction(TEXT("Light"), IE_Pressed);
    bOk &= InvokeAction(TEXT("Jump"), IE_Pressed);
    for (int32 Frame = 0; Frame < 12; ++Frame) TestWorld.TickTestWorld(Step);
    bOk &= TestTrue(TEXT("Paused input cannot start an attack"), Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Idle);
    bOk &= TestTrue(TEXT("Paused fighter remains in place"), Player->GetActorLocation().Equals(PausedPosition, 0.1));
    bOk &= InvokeAction(TEXT("Jump"), IE_Released);
    bOk &= TestEqual(TEXT("Pause freezes the duel clock"), Game->GetRoundTime(), PausedTime);
    const FRoninAnimationPlayback PausedAnimation = PlayerAnimation->GetPlaybackState();
    bOk &= TestEqual(TEXT("Pause freezes the native idle clock"), PausedAnimation.IdleTime, BeforePause.IdleTime);
    bOk &= TestTrue(TEXT("Pause preserves the evaluated weapon pose"),
        Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component).Equals(WeaponBeforePause, .001));
    bOk &= TestTrue(TEXT("Pause also freezes the attached original blade tip"),
        PlayerKatana->GetSocketLocation(TEXT("FX_weapon_tip")).Equals(TipBeforePause, .001));
    bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
    bOk &= TestFalse(TEXT("Bound pause action resumes combat"), Game->bCombatPaused);
    bOk &= InvokeAction(TEXT("Light"), IE_Pressed);
    bOk &= TestTrue(TEXT("Bound light attack starts the runtime attack"), Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Windup);
    for (int32 Frame = 0; Frame < 12; ++Frame)
    {
        TestWorld.TickTestWorld(Step);
        if (Frame == 3 || Frame == 7 || Frame == 11)
            bOk &= CheckEquippedPose(*this, Player, FString::Printf(TEXT("Attack frame %d"), Frame));
    }
    const FRoninAnimationPlayback AttackAnimation = PlayerAnimation->GetPlaybackState();
    const FTransform AttackingWeapon = Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component);
    bOk &= TestTrue(TEXT("Attack blends in a real authored action clip"), AttackAnimation.ActionSequence
        && AttackAnimation.ActionSequence != PlayerAnimation->GetClip(ERoninAnimationClip::Idle)
        && AttackAnimation.ActionTime > 0.f && AttackAnimation.ActionWeight > .5f);
    bOk &= TestFalse(TEXT("Retargeted attack moves the sword hand in component space"), AttackingWeapon.Equals(WeaponBeforePause, .01));
    bOk &= TestFalse(TEXT("The attached katana tip follows the attack in world space"),
        PlayerKatana->GetSocketLocation(TEXT("FX_weapon_tip")).Equals(TipBeforePause, .01));
    bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
    for (int32 Frame = 0; Frame < 12; ++Frame) TestWorld.TickTestWorld(Step);
    const FRoninAnimationPlayback PausedAttack = PlayerAnimation->GetPlaybackState();
    bOk &= TestEqual(TEXT("Pause freezes the authored attack time"), PausedAttack.ActionTime, AttackAnimation.ActionTime);
    bOk &= TestEqual(TEXT("Pause freezes the action blend"), PausedAttack.ActionWeight, AttackAnimation.ActionWeight);
    bOk &= TestTrue(TEXT("Pause holds the evaluated attack pose"),
        Player->GetMesh()->GetSocketTransform(WeaponBone, RTS_Component).Equals(AttackingWeapon, .001));
    // These controls execute with NullRHI even though Cascade cannot activate
    // emitters there. Visible ribbon emission and tail cleanup need a GPU pass.
    for (const UParticleSystemComponent* Trail : BladeTrails)
    {
        bOk &= TestEqual(TEXT("Pause freezes the blade trail clock"), Trail->CustomTimeDilation, 0.f);
        bOk &= TestFalse(TEXT("Pause prevents new blade trail samples"), Trail->IsComponentTickEnabled());
    }
    bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
    bOk &= TestTrue(TEXT("Rematch returns directly to an active duel"), Game->HasDuelStarted() && Game->CanControl());
    bOk &= TestEqual(TEXT("Rematch resets the duel clock"), Game->GetRoundTime(), 0.f);
    bOk &= TestTrue(TEXT("Rematch clears the previous attack"), Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Idle);
    for (const UParticleSystemComponent* Trail : BladeTrails)
    {
        bOk &= TestEqual(TEXT("Rematch restores the paused blade trail clock"), Trail->CustomTimeDilation, 1.f);
        bOk &= TestFalse(TEXT("Rematch keeps trail sampling disabled across teleport"), Trail->IsComponentTickEnabled());
    }

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
        bOk &= CheckEquippedPose(*this, Player, TEXT("Actual contact pose"));
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
        for (const UParticleSystemComponent* Trail : BladeTrails)
        {
            bOk &= TestEqual(TEXT("Contact hit-stop freezes blade trail clocks"), Trail->CustomTimeDilation, 0.f);
            bOk &= TestFalse(TEXT("Contact hit-stop prevents new trail samples"), Trail->IsComponentTickEnabled());
        }
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

    // Drive the same core-then-movement order as GameMode with two frame sizes.
    // This isolates the swept step from AI decisions and confirms its last fraction
    // is applied even when the core has reached idle on that simulation tick.
    for (const ronin::Stance Stance : {ronin::Stance::Flow, ronin::Stance::Gale})
    {
        for (const float Delta : {.05f, 1.f / 120.f})
        {
            bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
            Player->ResetPose(FVector(0, -300, 100), FRotator::ZeroRotator);
            Enemy->ResetPose(FVector(650, 600, 100), FRotator(0, 180, 0));
            bOk &= TestTrue(TEXT("Dodge trial selects its stance"), Game->Combat.SetStance(ronin::FighterId::Player, Stance));
            bOk &= InvokeAction(TEXT("Heavy"), IE_Pressed);
            bOk &= InvokeAction(TEXT("Guard"), IE_Pressed);
            bOk &= TestTrue(TEXT("A physical guard press cancels attack windup into a fresh parry"),
                Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Idle
                && Game->Combat.Get(ronin::FighterId::Player).parryTime > 0.f);
            bOk &= InvokeAction(TEXT("Guard"), IE_Released);
            bOk &= InvokeAction(TEXT("Heavy"), IE_Pressed);
            bOk &= InvokeAction(TEXT("Dodge"), IE_Pressed);
            bOk &= TestTrue(TEXT("Bound dodge cancels the attack windup"),
                Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Dodging);
            const FVector DodgeStart = Player->GetActorLocation();
            bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
            Player->AdvanceDodge(Delta);
            bOk &= TestTrue(TEXT("A paused dodge cannot move the capsule"), Player->GetActorLocation().Equals(DodgeStart, .001));
            bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
            for (int32 Frame = 0; Frame < 64 && Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Dodging; ++Frame)
            {
                Game->Combat.Tick(Delta);
                Player->AdvanceDodge(Delta);
            }
            const FVector DodgeEnd = Player->GetActorLocation();
            const double ExpectedDistance = Stance == ronin::Stance::Gale ? 210. : 180.;
            bOk &= TestTrue(TEXT("Swept dodge finishes at its bounded distance across frame sizes"),
                FMath::Abs(FVector::Dist2D(DodgeStart, DodgeEnd) - ExpectedDistance) < .1);
            bOk &= TestTrue(TEXT("Directional step does not change capsule height"), FMath::Abs(DodgeEnd.Z - DodgeStart.Z) < .1);
            Player->AdvanceDodge(.5f);
            bOk &= TestTrue(TEXT("Finished step cannot continue drifting"), Player->GetActorLocation().Equals(DodgeEnd, .001));
            bOk &= InvokeAction(TEXT("Dodge"), IE_Pressed);
            bOk &= TestTrue(TEXT("Another dodge is immediately available after recovery"),
                Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Dodging);
            bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
            const FVector RematchPosition = Player->GetActorLocation();
            Player->AdvanceDodge(Delta);
            bOk &= TestTrue(TEXT("Rematch discards any unfinished dodge displacement"),
                Player->GetActorLocation().Equals(RematchPosition, .001));
        }
    }

    // Sample the bound stick delegates immediately before the fighter tick;
    // the headless controller has no hardware and otherwise supplies zero axes.
    bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
    bOk &= InvokeAxis(TEXT("MoveForward"), 1.f);
    Player->Tick(Step);
    bOk &= TestEqual(TEXT("Ordinary movement uses walking speed"), Player->GetCharacterMovement()->MaxWalkSpeed, 260.f);
    bOk &= InvokeAction(TEXT("Sprint"), IE_Pressed);
    Player->Tick(Step);
    bOk &= TestEqual(TEXT("The bound sprint toggle enables running"), Player->GetCharacterMovement()->MaxWalkSpeed, 520.f);
    bOk &= InvokeAction(TEXT("Sprint"), IE_Pressed);
    Player->Tick(Step);
    bOk &= TestEqual(TEXT("A second sprint press returns to walking"), Player->GetCharacterMovement()->MaxWalkSpeed, 260.f);
    bOk &= InvokeAction(TEXT("Sprint"), IE_Pressed);
    bOk &= InvokeAxis(TEXT("MoveForward"), 0.f);
    bOk &= InvokeAxis(TEXT("MoveRight"), 0.f);
    Player->Tick(Step);
    bOk &= InvokeAxis(TEXT("MoveForward"), 1.f);
    Player->Tick(Step);
    bOk &= TestEqual(TEXT("Releasing the stick clears sprint until another click"), Player->GetCharacterMovement()->MaxWalkSpeed, 260.f);
    for (const TCHAR* CancelAction : {TEXT("Guard"), TEXT("Light"), TEXT("Dodge"), TEXT("Restart")})
    {
        bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
        bOk &= InvokeAxis(TEXT("MoveForward"), 1.f);
        bOk &= InvokeAction(TEXT("Sprint"), IE_Pressed);
        Player->Tick(Step);
        bOk &= TestEqual(FString(CancelAction) + TEXT(" trial begins while sprinting"), Player->GetCharacterMovement()->MaxWalkSpeed, 520.f);
        bOk &= InvokeAction(CancelAction, IE_Pressed);
        Player->Tick(Step);
        bOk &= TestTrue(FString(CancelAction) + TEXT(" immediately removes sprint movement"),
            Player->GetCharacterMovement()->MaxWalkSpeed <= 260.f);
        bOk &= InvokeAction(TEXT("Guard"), IE_Released);
        Game->Combat.Tick(2.f);
        bOk &= InvokeAxis(TEXT("MoveForward"), 1.f);
        Player->Tick(Step);
        bOk &= TestEqual(FString(CancelAction) + TEXT(" cannot silently restore sprint after recovery"),
            Player->GetCharacterMovement()->MaxWalkSpeed, 260.f);
    }

    // The jump action must travel through CharacterMovement's real falling and
    // landing states. Keep the opponent distant so this measures movement alone.
    bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
    Player->ResetPose(FVector(-500, -500, 100), FRotator::ZeroRotator);
    Enemy->ResetPose(FVector(650, 600, 100), FRotator(0, 180, 0));
    for (int32 Frame = 0; Frame < 45; ++Frame) TestWorld.TickTestWorld(Step);
    bOk &= TestTrue(TEXT("Jump trial starts on walkable ground"), Player->GetCharacterMovement()->IsMovingOnGround());
    const double JumpStartHeight = Player->GetActorLocation().Z;
    double JumpPeakHeight = JumpStartHeight;
    double MaximumJumpTilt = 0.;
    Game->bLockOn = false;
    const bool bJumpControllerTickWasEnabled = Controller->IsActorTickEnabled();
    Controller->SetActorTickEnabled(false);
    bool bSawFalling = false, bLanded = false;
    bOk &= InvokeAction(TEXT("Jump"), IE_Pressed);
    for (int32 Frame = 0; Frame < 240; ++Frame)
    {
        bOk &= InvokeAxis(TEXT("MoveForward"), 1.f);
        TestWorld.TickTestWorld(Step);
        MaximumJumpTilt = FMath::Max(MaximumJumpTilt, FMath::Abs(FRotator::NormalizeAxis(Player->GetActorRotation().Pitch)));
        MaximumJumpTilt = FMath::Max(MaximumJumpTilt, FMath::Abs(FRotator::NormalizeAxis(Player->GetActorRotation().Roll)));
        if (Frame == 3) bOk &= InvokeAction(TEXT("Jump"), IE_Released);
        if (Frame == 8)
        {
            bOk &= TestTrue(TEXT("Pause trial reaches an actual airborne jump"), Player->GetCharacterMovement()->IsFalling());
            bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
            const FVector AirbornePausePosition = Player->GetActorLocation();
            for (int32 PausedFrame = 0; PausedFrame < 12; ++PausedFrame) TestWorld.TickTestWorld(Step);
            bOk &= TestTrue(TEXT("Pausing midair freezes the capsule including its height"),
                Player->GetActorLocation().Equals(AirbornePausePosition, .001));
            bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
        }
        bSawFalling |= Player->GetCharacterMovement()->IsFalling();
        JumpPeakHeight = FMath::Max(JumpPeakHeight, Player->GetActorLocation().Z);
        if (bSawFalling && Player->GetCharacterMovement()->IsMovingOnGround()) { bLanded = true; break; }
    }
    bOk &= InvokeAxis(TEXT("MoveForward"), 0.f);
    Controller->SetActorTickEnabled(bJumpControllerTickWasEnabled);
    bOk &= TestTrue(TEXT("An unlocked moving jump keeps the fighter upright during ascent and descent"), MaximumJumpTilt < .1);
    bOk &= TestTrue(TEXT("Bound jump actually leaves the ground and gains useful height"),
        bSawFalling && JumpPeakHeight - JumpStartHeight > 20.);
    bOk &= TestTrue(TEXT("Jump lands back on the courtyard without vertical drift"),
        bLanded && FMath::Abs(Player->GetActorLocation().Z - JumpStartHeight) < 2.
        && Player->GetCharacterMovement()->CurrentFloor.HitResult.GetActor() == Arena);

    bOk &= InvokeAction(TEXT("Jump"), IE_Pressed);
    for (int32 Frame = 0; Frame < 8; ++Frame) TestWorld.TickTestWorld(Step);
    bOk &= InvokeAction(TEXT("Jump"), IE_Released);
    bOk &= TestTrue(TEXT("Airborne attack trial begins while falling"), Player->GetCharacterMovement()->IsFalling());
    bOk &= InvokeAction(TEXT("Heavy"), IE_Pressed);
    bOk &= TestTrue(TEXT("A real heavy attack begins during the jump"),
        Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Windup);
    bool bAttackJumpLanded = false;
    for (int32 Frame = 0; Frame < 180; ++Frame)
    {
        TestWorld.TickTestWorld(Step);
        if (Player->GetCharacterMovement()->IsMovingOnGround()) { bAttackJumpLanded = true; break; }
    }
    bOk &= TestTrue(TEXT("Airborne attacking preserves gravity and lands instead of hovering"),
        bAttackJumpLanded && FMath::Abs(Player->GetActorLocation().Z - JumpStartHeight) < 2.);

    // Supply held input through the actual axis/action delegates. Suspend only
    // this headless controller's zero-hardware input tick during the fixture.
    bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
    Player->ResetPose(FVector(0, 0, 100), FRotator::ZeroRotator);
    Enemy->ResetPose(FVector(-700, 650, 100), FRotator(0, 180, 0));
    Game->bLockOn = false;
    for (int32 Frame = 0; Frame < 30; ++Frame) TestWorld.TickTestWorld(1.f / 60.f);
    const bool bControllerTickWasEnabled = Controller->IsActorTickEnabled();
    Controller->SetActorTickEnabled(false);
    bOk &= InvokeAxis(TEXT("MoveForward"), 1.f);
    bOk &= InvokeAction(TEXT("Sprint"), IE_Pressed);
    for (int32 Frame = 0; Frame < 300; ++Frame)
    {
        bOk &= InvokeAxis(TEXT("MoveForward"), 1.f);
        TestWorld.TickTestWorld(1.f / 60.f);
        if (Player->GetActorLocation().X > 1700. && Player->GetCharacterMovement()->IsMovingOnGround()) break;
    }
    bOk &= InvokeAxis(TEXT("MoveForward"), 0.f);
    Controller->SetActorTickEnabled(bControllerTickWasEnabled);
    bOk &= TestTrue(TEXT("Held forward sprint traverses the courtyard exit and reaches the outer trail"),
        Player->GetActorLocation().X > 1600. && FMath::Abs(Player->GetActorLocation().Y) < 50.
        && Player->GetCharacterMovement()->IsMovingOnGround()
        && Player->GetCharacterMovement()->CurrentFloor.HitResult.GetActor() == Arena);

    // Trigger the presentation through actual incoming contact, rather than
    // starting its envelope directly. Keep each trial before the AI's first turn.
    auto TriggerDefense = [&](bool bDodge)
    {
        bool bResult = InvokeAction(TEXT("Restart"), IE_Pressed);
        Player->ResetPose(FVector(-65, 0, 100), FRotator::ZeroRotator);
        Enemy->ResetPose(FVector(65, 0, 100), FRotator(0, 180, 0));
        const uint32 Serial = Game->GetDefenseCueSerial();
        const float Health = Game->Combat.Get(ronin::FighterId::Player).health;
        bResult &= TestTrue(TEXT("Defense trial starts an incoming committed attack"),
            Game->Combat.TryAttack(ronin::FighterId::Enemy, ronin::AttackKind::Light));
        Game->Combat.Tick(Game->Combat.Get(ronin::FighterId::Enemy).spec.windup - Step * .5f);
        bResult &= InvokeAction(bDodge ? TEXT("Dodge") : TEXT("Guard"), IE_Pressed);
        for (int32 Frame = 0; Frame < 12 && Game->GetDefenseCueSerial() == Serial; ++Frame)
            TestWorld.TickTestWorld(Step);
        bResult &= TestEqual(TEXT("One confirmed defense starts one cinematic cue"), Game->GetDefenseCueSerial(), Serial + 1);
        bResult &= TestTrue(TEXT("The incoming attack reports the intended defense result"),
            Game->Combat.Get(ronin::FighterId::Enemy).lastHit == (bDodge ? ronin::HitResult::Dodged : ronin::HitResult::Parried));
        bResult &= TestEqual(TEXT("The defense cue identifies its real contact type"), Game->IsPerfectDodgeCue(), bDodge);
        bResult &= TestEqual(TEXT("Successful defense preserves player health"), Game->Combat.Get(ronin::FighterId::Player).health, Health);
        bResult &= TestTrue(TEXT("Defense supplies a live counter opportunity and slows combat"),
            Game->Combat.Get(ronin::FighterId::Player).counterTime > 0.f
            && Game->GetCombatTimeScale() >= 0.f && Game->GetCombatTimeScale() < 1.f && Game->CanControl());
        return bResult;
    };
    for (const bool bDodge : {false, true})
    {
        if (!TriggerDefense(bDodge)) { bOk = false; continue; }
        const float CounterBefore = Game->Combat.Get(ronin::FighterId::Player).counterTime;
        const float RoundBefore = Game->GetRoundTime();
        const float IdleBefore = PlayerAnimation->GetPlaybackState().IdleTime;
        float ReportedSimulationTime = 0.f;
        // Observe beyond the contact hold plus one slowed 120 Hz simulation
        // quantum; the longer parry beat may legitimately advance zero steps
        // during its first 100 ms.
        constexpr int32 DefenseObservationFrames = 18;
        for (int32 Frame = 0; Frame < DefenseObservationFrames; ++Frame)
        {
            TestWorld.TickTestWorld(Step);
            ReportedSimulationTime += Game->GetCombatDeltaSeconds();
            bOk &= TestTrue(TEXT("Defense time scale remains finite and bounded"),
                FMath::IsFinite(Game->GetCombatTimeScale()) && Game->GetCombatTimeScale() >= 0.f && Game->GetCombatTimeScale() <= 1.f);
        }
        const float Simulated = Game->GetRoundTime() - RoundBefore;
        bOk &= TestTrue(TEXT("The defense beat advances less combat time than real frame time"),
            Simulated > 0.f && Simulated < Step * DefenseObservationFrames * .8f);
        bOk &= TestTrue(TEXT("The reported frame delta matches the actual fixed combat steps"),
            FMath::IsNearlyEqual(ReportedSimulationTime, Simulated, .0001f));
        bOk &= TestTrue(TEXT("Slow motion preserves the counter window in combat time"),
            FMath::IsNearlyEqual(CounterBefore - Game->Combat.Get(ronin::FighterId::Player).counterTime, Simulated, .0001f));
        bOk &= TestTrue(TEXT("Native pose clocks follow the same slowed simulation"),
            FMath::IsNearlyEqual(PlayerAnimation->GetPlaybackState().IdleTime - IdleBefore, Simulated, Step * 1.1f));
        const uint32 ConfirmedSerial = Game->GetDefenseCueSerial();
        for (int32 Frame = 0; Frame < 72 && Game->GetCombatTimeScale() < 1.f; ++Frame)
            TestWorld.TickTestWorld(Step);
        bOk &= TestEqual(TEXT("The real-time defense envelope returns to normal speed"), Game->GetCombatTimeScale(), 1.f);
        bOk &= TestEqual(TEXT("One contact cannot replay its cinematic on later frames"), Game->GetDefenseCueSerial(), ConfirmedSerial);
        // A dodge must complete its real action before an attack can be accepted.
        for (int32 Frame = 0; Frame < 60 && Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Dodging; ++Frame)
            TestWorld.TickTestWorld(Step);
        bOk &= InvokeAction(TEXT("Guard"), IE_Released);
        bOk &= TestTrue(TEXT("The counter opportunity survives the defense presentation"),
            Game->Combat.Get(ronin::FighterId::Player).counterTime > 0.f);
        const float EnemyHealthBeforeCounter = Game->Combat.Get(ronin::FighterId::Enemy).health;
        bOk &= InvokeAction(TEXT("Light"), IE_Pressed);
        bOk &= TestTrue(TEXT("A bound attack can use the post-defense counter opportunity"),
            Game->Combat.Get(ronin::FighterId::Player).action == ronin::Action::Windup
            && Game->Combat.Get(ronin::FighterId::Player).counterTime == 0.f
            && Game->Combat.Get(ronin::FighterId::Player).isCounterAttack);
        for (int32 Frame = 0; Frame < 120 && !Game->Combat.Get(ronin::FighterId::Player).hitResolved; ++Frame)
            TestWorld.TickTestWorld(Step);
        bOk &= TestTrue(TEXT("An earned counter closes real defense spacing and hits the enemy"),
            Game->Combat.Get(ronin::FighterId::Player).lastHit == ronin::HitResult::Hit
            && Game->Combat.Get(ronin::FighterId::Enemy).health < EnemyHealthBeforeCounter);

        if (!TriggerDefense(bDodge)) { bOk = false; continue; }
        bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
        const float PausedCounter = Game->Combat.Get(ronin::FighterId::Player).counterTime;
        const float PausedRound = Game->GetRoundTime();
        const uint32 PausedSerial = Game->GetDefenseCueSerial();
        for (int32 Frame = 0; Frame < 12; ++Frame) TestWorld.TickTestWorld(Step);
        bOk &= TestEqual(TEXT("Pause sets combat scale to zero during a defense beat"), Game->GetCombatTimeScale(), 0.f);
        bOk &= TestEqual(TEXT("Pause preserves a live counter opportunity"), Game->Combat.Get(ronin::FighterId::Player).counterTime, PausedCounter);
        bOk &= TestEqual(TEXT("Pause freezes the defense simulation"), Game->GetRoundTime(), PausedRound);
        bOk &= InvokeAction(TEXT("Pause"), IE_Pressed);
        bOk &= TestEqual(TEXT("Resume clears stale defense slow motion"), Game->GetCombatTimeScale(), 1.f);
        TestWorld.TickTestWorld(Step);
        bOk &= TestEqual(TEXT("Resuming cannot replay the cleared contact cue"), Game->GetDefenseCueSerial(), PausedSerial);
        bOk &= TestEqual(TEXT("Cleared defense speed stays restored on the next frame"), Game->GetCombatTimeScale(), 1.f);

        if (!TriggerDefense(bDodge)) { bOk = false; continue; }
        const uint32 ResetSerial = Game->GetDefenseCueSerial();
        bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
        bOk &= TestEqual(TEXT("Rematch immediately restores normal combat speed"), Game->GetCombatTimeScale(), 1.f);
        bOk &= TestEqual(TEXT("Rematch clears the previous counter window"), Game->Combat.Get(ronin::FighterId::Player).counterTime, 0.f);
        for (int32 Frame = 0; Frame < 12; ++Frame) TestWorld.TickTestWorld(Step);
        bOk &= TestEqual(TEXT("Rematch cannot inherit the previous defense envelope"), Game->GetCombatTimeScale(), 1.f);
        bOk &= TestEqual(TEXT("Rematch does not replay or reuse the defense cue serial"), Game->GetDefenseCueSerial(), ResetSerial);
    }

    // Resolve real attacks through GameMode, then measure the swept victim
    // capsule. Blocking must brace with a small recoil; a committed heavy hit
    // must interrupt the opponent and displace it substantially farther.
    auto MeasureContactRecoil = [&](bool bStrong, double EnemyX)
    {
        bOk &= InvokeAction(TEXT("Restart"), IE_Pressed);
        Player->ResetPose(FVector(EnemyX - 180., 0, 100), FRotator::ZeroRotator);
        Enemy->ResetPose(FVector(EnemyX, 0, 100), FRotator(0, 180, 0));
        FRoninAnimationState Before;
        Enemy->GetAnimationState(Before);
        const float HealthBefore = Game->Combat.Get(ronin::FighterId::Enemy).health;
        if (bStrong)
        {
            // The long incoming commitment is interrupted by the faster heavy
            // strike. It cannot auto-brace while attacking.
            bOk &= TestTrue(TEXT("Recoil trial starts the enemy's committed attack"),
                Game->Combat.TryAttack(ronin::FighterId::Enemy, ronin::AttackKind::Unblockable));
        }
        bOk &= InvokeAction(bStrong ? TEXT("Heavy") : TEXT("Light"), IE_Pressed);
        for (int32 Frame = 0; Frame < 120 && !Game->Combat.Get(ronin::FighterId::Player).hitResolved; ++Frame)
            TestWorld.TickTestWorld(Step);
        const auto& Victim = Game->Combat.Get(ronin::FighterId::Enemy);
        bOk &= TestTrue(TEXT("Recoil is caused by the intended actual contact"),
            Game->Combat.Get(ronin::FighterId::Player).lastHit ==
                (bStrong ? ronin::HitResult::Hit : ronin::HitResult::Blocked));
        FRoninAnimationState Impact;
        Enemy->GetAnimationState(Impact);
        bOk &= TestEqual(TEXT("One resolved contact produces one victim reaction"), Impact.ImpactSerial, Before.ImpactSerial + 1);
        bOk &= TestTrue(TEXT("Victim presentation receives the correct block or strong-hit kind"),
            Impact.bImpactBlocked == !bStrong && Impact.bImpactStrong == bStrong);
        if (bStrong)
            bOk &= TestTrue(TEXT("A heavy contact damages and staggers the committed enemy"),
                Victim.health < HealthBefore && Victim.action == ronin::Action::Staggered);
        else
            bOk &= TestTrue(TEXT("The enemy braces an incoming cut without gaining a parry or losing health"),
                Victim.guarding && Victim.parryTime == 0.f && Victim.health == HealthBefore && Victim.posture > 0.f);

        const FVector ContactLocation = Enemy->GetActorLocation();
        double MaximumTravel = 0.;
        float SimulatedTime = 0.f;
        for (int32 Frame = 0; Frame < 90 && SimulatedTime < .23f; ++Frame)
        {
            TestWorld.TickTestWorld(Step);
            SimulatedTime += Game->GetCombatDeltaSeconds();
            const FVector Position = Enemy->GetActorLocation();
            MaximumTravel = FMath::Max(MaximumTravel, Position.X - ContactLocation.X);
            bOk &= TestTrue(TEXT("Contact recoil remains finite and lateral drift stays bounded"),
                !Position.ContainsNaN() && FMath::Abs(Position.Y - ContactLocation.Y) < 1.);
        }
        Enemy->GetAnimationState(Impact);
        bOk &= TestEqual(TEXT("The same active hit cannot restart recoil on later frames"), Impact.ImpactSerial, Before.ImpactSerial + 1);
        return MaximumTravel;
    };
    const double BlockTravel = MeasureContactRecoil(false, 90.);
    const double StrongTravel = MeasureContactRecoil(true, 90.);
    bOk &= TestTrue(TEXT("A blocked cut produces a small visible physical recoil"), BlockTravel > 2. && BlockTravel <= 12.);
    bOk &= TestTrue(TEXT("A strong hit moves the victim substantially farther than a block"),
        StrongTravel > 30. && StrongTravel <= 50. && StrongTravel > BlockTravel * 3.);

    // Exploration removes the old courtyard enclosure. A local obstacle keeps
    // this sweep regression independent of the larger world's outer terrain.
    AActor* WallFixture = World->SpawnActor<AActor>();
    if (TestNotNull(TEXT("Recoil collision fixture can be created"), WallFixture))
    {
        UBoxComponent* Wall = NewObject<UBoxComponent>(WallFixture, TEXT("RecoilSweepObstacle"));
        WallFixture->SetRootComponent(Wall);
        WallFixture->AddInstanceComponent(Wall);
        Wall->SetBoxExtent(FVector(25, 200, 300));
        Wall->SetCollisionProfileName(TEXT("BlockAll"));
        Wall->SetWorldLocation(FVector(875, 0, 250));
        Wall->RegisterComponent();
        const double InnerEdge = Wall->Bounds.Origin.X - Wall->Bounds.BoxExtent.X;
        const double Radius = Enemy->GetCapsuleComponent()->GetScaledCapsuleRadius();
        const double WallTravel = MeasureContactRecoil(true, InnerEdge - Radius - 5.);
        bOk &= TestTrue(TEXT("A wall limits strong recoil before the capsule crosses it"),
            WallTravel > 0. && WallTravel < StrongTravel - 5.
            && Enemy->GetActorLocation().X + Radius <= InnerEdge + .5);
        bOk &= TestTrue(TEXT("Recoil leaves the victim supported by the courtyard"),
            Enemy->GetCharacterMovement()->IsMovingOnGround());
        WallFixture->Destroy();
    }
    else bOk = false;

    for (const FVector2D Point : {FVector2D(1400, 0), FVector2D(4200, -2700),
        FVector2D(6900, -3600), FVector2D(-3900, 3100)})
    {
        FHitResult GroundHit;
        const bool bGround = World->LineTraceSingleByChannel(GroundHit,
            FVector(Point.X, Point.Y, 5000), FVector(Point.X, Point.Y, -2000), ECC_Visibility);
        bOk &= TestTrue(FString::Printf(TEXT("Explorable terrain provides collision at (%g, %g)"), Point.X, Point.Y),
            bGround && GroundHit.GetActor() == Arena && GroundHit.GetComponent()
            && GroundHit.GetComponent()->GetFName() == TEXT("ForestTerrain") && GroundHit.ImpactNormal.Z > .5f);
    }

    bOk &= TestTrue(TEXT("Combat and rematches leave the authored impact source unchanged"),
        SnapshotImpactSource(SparkSource) == OriginalImpact);
    bOk &= TestWorld.DestroyTestWorld(false);
    TestWorld.ForwardErrorMessages(this);
    return bOk && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoninLockOnCameraTest, "Ronin.Integration.LockOnCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoninLockOnCameraTest::RunTest(const FString& Parameters)
{
    bool bOk = true;
    const FVector AcrossYawSeam = FRotator(0, -179, 0).Vector();
    for (const float Rate : {30.f, 60.f, 120.f})
    {
        const float Delta = 1.f / Rate;
        const FRotator Normal = ARoninFighter::EvaluateLockOnRotation(FRotator(-12, 179, 0), AcrossYawSeam, Delta);
        for (const double EquivalentPitch : {348., -372., 708.})
        {
            const FRotator Wrapped = ARoninFighter::EvaluateLockOnRotation(FRotator(EquivalentPitch, 179, 0), AcrossYawSeam, Delta);
            bOk &= TestTrue(TEXT("Equivalent stored pitches produce the same lock-on view"), Wrapped.Equals(Normal, .0001));
        }
        const double YawTravel = FRotator::NormalizeAxis(Normal.Yaw - 179.);
        bOk &= TestTrue(TEXT("Lock-on crosses the yaw seam along the short direction"),
            YawTravel >= 0. && YawTravel <= 2.001 && YawTravel <= Delta * 160. + .001);

        for (const double StartingPitch : {348., 25., 305.})
        {
            FRotator View(StartingPitch, 179, 0);
            bool bStayedFramed = true;
            for (int32 Frame = 0; Frame < int32(Rate * 2.f); ++Frame)
            {
                // Reproduce normalized controller storage on every retreat frame.
                View.Pitch = FRotator::ClampAxis(View.Pitch);
                const FVector RetreatTarget = AcrossYawSeam * (150. + Frame * 8.) + FVector(0, 0, 40.);
                View = ARoninFighter::EvaluateLockOnRotation(View, RetreatTarget, Delta);
                const double Pitch = FRotator::NormalizeAxis(View.Pitch);
                bStayedFramed &= !View.ContainsNaN() && Pitch >= -25.001 && Pitch <= -5.999
                    && FMath::Abs(View.Roll) < .001;
            }
            bOk &= TestTrue(TEXT("Retreat and repeated angle wrapping never send lock-on overhead"), bStayedFramed);
            bOk &= TestTrue(TEXT("Lock-on settles back to its playable pitch"),
                FMath::Abs(FRotator::NormalizeAxis(View.Pitch) + 12.) < .05);
        }
    }
    const FRotator Coincident = ARoninFighter::EvaluateLockOnRotation(FRotator(348, 73, 0), FVector::ZeroVector, 1.f / 60.f);
    bOk &= TestTrue(TEXT("Coincident targets preserve yaw without invalid camera angles"),
        !Coincident.ContainsNaN() && FMath::IsNearlyEqual(Coincident.Yaw, 73.)
        && FMath::IsNearlyEqual(FRotator::NormalizeAxis(Coincident.Pitch), -12.));
    return bOk && !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoninImpactCameraTest, "Ronin.Integration.ImpactCamera",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoninImpactCameraTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    auto* Modifier=NewObject<URoninImpactCameraModifier>();
    if (!TestNotNull(TEXT("Native impact modifier can be constructed without a rendered world"), Modifier)) return false;
    FMinimalViewInfo Base;
    Base.Location=FVector(80.,120.,250.);
    Base.Rotation=FRotator(-12.,37.,0.);
    Base.FOV=82.f;
    const auto SameView=[&Base](const FMinimalViewInfo& View)
    {
        return View.Location.Equals(Base.Location,.0001) && View.Rotation.Equals(Base.Rotation,.0001) &&
            FMath::IsNearlyEqual(View.FOV,Base.FOV,.0001f);
    };
    bool bOk=true;
    for (auto Event : {ronin::HapticEvent::Swing,ronin::HapticEvent::HeavySwing,ronin::HapticEvent::Dodge})
    {
        Modifier->Pulse(Event);
        auto View=Base;
        bOk &= TestFalse(TEXT("Impact modifier preserves the remaining camera modifier chain"),
            Modifier->ModifyCamera(1.f/60.f,View));
        bOk &= TestTrue(TEXT("Movement actions leave the camera unchanged"),SameView(View));
    }
    for (float Rate : {30.f,60.f,120.f})
    {
        for (auto Event : {ronin::HapticEvent::Hit,ronin::HapticEvent::Block,ronin::HapticEvent::PerfectDodge,
            ronin::HapticEvent::Parry,ronin::HapticEvent::Damage,ronin::HapticEvent::GuardBreak,
            ronin::HapticEvent::Execute,ronin::HapticEvent::Counter})
        {
            Modifier->Clear();
            Modifier->Pulse(Event);
            auto View=Base;
            Modifier->ModifyCamera(1.f/Rate,View);
            bOk &= TestFalse(TEXT("Confirmed contact supplies an immediate view impulse"),SameView(View));
            bool bBounded=true;
            for (int32 Frame=0; Frame<int32(Rate); ++Frame)
            {
                // The engine provides a fresh base POV each frame, not last frame's offset.
                View=Base;
                Modifier->ModifyCamera(1.f/Rate,View);
                bBounded &= FVector::Dist(View.Location,Base.Location)<=2.401 &&
                    FMath::Abs(View.Rotation.Pitch-Base.Rotation.Pitch)<=.676 &&
                    FMath::Abs(View.FOV-Base.FOV)<=.676f &&
                    View.Rotation.Yaw==Base.Rotation.Yaw && View.Rotation.Roll==Base.Rotation.Roll;
            }
            bOk &= TestTrue(TEXT("Repeated camera evaluation stays bounded without accumulating aim or roll"),bBounded);
            bOk &= TestTrue(TEXT("Every contact settles exactly to the base view"),SameView(View));
        }
        for (const bool bDodge : {false, true})
        {
            Modifier->Clear();
            Modifier->Pulse(bDodge ? ronin::HapticEvent::PerfectDodge : ronin::HapticEvent::Parry);
            Modifier->PlayDefenseBeat(bDodge);
            const double ForwardLimit = bDodge ? 24. : 60.;
            const double YawLimit = bDodge ? .8 : 2.;
            const double PitchLimit = bDodge ? .4 : 1.;
            const float FovLimit = bDodge ? 4.f : 8.f;
            bool bBounded = true;
            double MaximumForward = 0.;
            float MaximumFovPush = 0.f;
            auto DefenseView = Base;
            for (int32 Frame = 0; Frame <= FMath::CeilToInt(Rate * .65f); ++Frame)
            {
                DefenseView = Base;
                Modifier->ModifyCamera(1.f / Rate, DefenseView);
                MaximumForward = FMath::Max(MaximumForward,
                    FVector::DotProduct(DefenseView.Location - Base.Location, Base.Rotation.Vector()));
                MaximumFovPush = FMath::Max(MaximumFovPush, Base.FOV - DefenseView.FOV);
                bBounded &= !DefenseView.Location.ContainsNaN() && !DefenseView.Rotation.ContainsNaN()
                    && FVector::Dist(DefenseView.Location, Base.Location) <= ForwardLimit + 2.401
                    && FMath::Abs(DefenseView.Rotation.Yaw - Base.Rotation.Yaw) <= YawLimit + .001
                    && FMath::Abs(DefenseView.Rotation.Pitch - Base.Rotation.Pitch) <= PitchLimit + .676
                    && FMath::Abs(DefenseView.FOV - Base.FOV) <= FovLimit + .676f
                    && DefenseView.Rotation.Roll == Base.Rotation.Roll;
            }
            bOk &= TestTrue(TEXT("Defense lens push remains bounded with the concurrent contact impulse"), bBounded);
            bOk &= TestTrue(TEXT("Confirmed defense supplies a visible forward push and lens change"),
                MaximumForward > ForwardLimit * .75 && MaximumFovPush > FovLimit * .75f);
            bOk &= TestTrue(TEXT("Defense and contact return exactly to the fresh base view after 0.6 seconds"), SameView(DefenseView));
        }
    }
    Modifier->PlayDefenseBeat(false);
    auto ClearedDefenseView = Base;
    Modifier->ModifyCamera(.04f, ClearedDefenseView);
    ClearedDefenseView = Base;
    Modifier->ModifyCamera(.04f, ClearedDefenseView);
    bOk &= TestFalse(TEXT("Defense beat is active before explicit cleanup"), SameView(ClearedDefenseView));
    Modifier->Clear();
    ClearedDefenseView = Base;
    Modifier->ModifyCamera(.04f, ClearedDefenseView);
    bOk &= TestTrue(TEXT("Pause and rematch cleanup also cancel the defense lens push"), SameView(ClearedDefenseView));
    Modifier->Pulse(ronin::HapticEvent::Parry);
    auto View=Base;
    Modifier->ModifyCamera(.05f,View);
    Modifier->Pulse(ronin::HapticEvent::Hit);
    Modifier->Pulse(ronin::HapticEvent::Parry);
    View=Base;
    Modifier->ModifyCamera(0.f,View);
    bOk &= TestTrue(TEXT("Lower-priority and duplicate contacts cannot fill the parry's quiet gap"),SameView(View));
    Modifier->Clear();
    Modifier->Pulse(ronin::HapticEvent::Execute);
    View=Base;
    Modifier->ModifyCamera(.01f,View);
    bOk &= TestFalse(TEXT("Execution supplies feedback before reset"),SameView(View));
    Modifier->Clear();
    View=Base;
    Modifier->ModifyCamera(.1f,View);
    bOk &= TestTrue(TEXT("Pause or rematch cleanup immediately restores the base view"),SameView(View));
    View=Base;
    Modifier->ModifyCamera(.1f,View);
    bOk &= TestTrue(TEXT("Cleared feedback cannot resume on subsequent frames"),SameView(View));
    return bOk && !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
