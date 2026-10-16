#include "RoninAnimInstance.h"
#include "RoninFighter.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
using Clip = ERoninAnimationClip;
constexpr int32 ClipCount = static_cast<int32>(Clip::Count);

bool IsAttack(ronin::Action Action)
{
    return Action == ronin::Action::Windup || Action == ronin::Action::Active ||
        Action == ronin::Action::Recovery;
}

struct FExplicitSequence : FAnimNode_SequenceEvaluator_Standalone
{
    bool bLoop = true;
    virtual bool IsLooping() const override { return bLoop; }
};

// Capsule movement is authoritative even for source clips containing baked root travel.
struct FRootLockedPose : FAnimNode_Base
{
    FPoseLink Source;
    FBoneReference Hands[2], WristBranches[2], WristTips[2];
    FBoneReference Torso;
    float Crouch = 0.f, LeanForward = 0.f, LeanSide = 0.f, DeflectTwist = 0.f;
    FRootLockedPose()
    {
        Torso.BoneName = TEXT("mixamorig_Spine1");
        Hands[0].BoneName = TEXT("mixamorig_RightHand");
        Hands[1].BoneName = TEXT("mixamorig_LeftHand");
        WristBranches[0].BoneName = TEXT("Bone_020");
        WristBranches[1].BoneName = TEXT("Bone_025");
        WristTips[0].BoneName = TEXT("Bone_019");
        WristTips[1].BoneName = TEXT("Bone_024");
    }
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override
    { Source.Initialize(Context); }
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override
    {
        Source.CacheBones(Context);
        const FBoneContainer& Bones = Context.AnimInstanceProxy->GetRequiredBones();
        Torso.Initialize(Bones);
        for (int32 Side = 0; Side < 2; ++Side)
        {
            Hands[Side].Initialize(Bones);
            WristBranches[Side].Initialize(Bones);
            WristTips[Side].Initialize(Bones);
        }
    }
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override
    { Source.Update(Context); }
    virtual void Evaluate_AnyThread(FPoseContext& Output) override
    {
        Source.Evaluate(Output);
        if (Output.Pose.GetNumBones() > 0)
        {
            const FCompactPoseBoneIndex Root(0);
            // This rig starts at the pelvis. Preserve crouch/bob/fall height while
            // preventing animation root travel from moving it outside the capsule.
            FVector Position = Output.Pose[Root].GetTranslation();
            const FVector Bind = Output.Pose.GetRefPose(Root).GetTranslation();
            Position.X = Bind.X;
            Position.Y = Bind.Y;
            Position.Z -= Crouch;
            Output.Pose[Root].SetTranslation(Position);
        }
        const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
        if (Torso.IsValidToEvaluate(Bones))
        {
            FTransform& Chest = Output.Pose[Torso.GetCompactPoseIndex(Bones)];
            const FQuat Offset = FQuat(FVector::UpVector, FMath::DegreesToRadians(DeflectTwist))
                * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(LeanForward))
                * FQuat(FVector::RightVector, FMath::DegreesToRadians(LeanSide));
            Chest.SetRotation((Offset * Chest.GetRotation()).GetNormalized());
        }
        for (int32 Side = 0; Side < 2; ++Side)
        {
            if (!Hands[Side].IsValidToEvaluate(Bones) || !WristBranches[Side].IsValidToEvaluate(Bones)
                || !WristTips[Side].IsValidToEvaluate(Bones)) continue;
            const auto Hand = Hands[Side].GetCompactPoseIndex(Bones);
            const auto Branch = WristBranches[Side].GetCompactPoseIndex(Bones);
            const auto Tip = WristTips[Side].GetCompactPoseIndex(Bones);
            // Meshy's weighted auxiliary wrist is a sibling of Hand. Let it share
            // the hand delta instead of shearing skin between independent rotations.
            const FTransform BindOffset = Output.Pose.GetRefPose(Branch).GetRelativeTransform(Output.Pose.GetRefPose(Hand));
            Output.Pose[Branch] = BindOffset * Output.Pose[Hand];
            Output.Pose[Tip] = Output.Pose.GetRefPose(Tip);
        }
    }
};

struct FRoninAnimProxy : FAnimInstanceProxy
{
    explicit FRoninAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
    {
        ForwardBackward.A.SetLinkNode(&LocomotionPlayers[1]);
        ForwardBackward.B.SetLinkNode(&LocomotionPlayers[2]);
        LeftRight.A.SetLinkNode(&LocomotionPlayers[3]);
        LeftRight.B.SetLinkNode(&LocomotionPlayers[4]);
        DirectionBlend.A.SetLinkNode(&ForwardBackward);
        DirectionBlend.B.SetLinkNode(&LeftRight);
        LocomotionBlend.A.SetLinkNode(&LocomotionPlayers[0]);
        LocomotionBlend.B.SetLinkNode(&DirectionBlend);
        GuardBlend.BasePose.SetLinkNode(&LocomotionBlend);
        GuardBlend.AddPose();
        GuardBlend.BlendPoses[0].SetLinkNode(&GuardPlayer);
        FBranchFilter UpperBody;
        UpperBody.BoneName = TEXT("mixamorig_Spine");
        UpperBody.BlendDepth = 3;
        GuardBlend.LayerSetup[0].BranchFilters.Add(UpperBody);
        GuardBlend.bMeshSpaceRotationBlend = true;
        ActionBlend.A.SetLinkNode(&ActionPlayers[0]);
        ActionBlend.B.SetLinkNode(&ActionPlayers[1]);
        OutputBlend.A.SetLinkNode(&GuardBlend);
        OutputBlend.B.SetLinkNode(&ActionBlend);
        Root.Source.SetLinkNode(&OutputBlend);
    }

    virtual void Initialize(UAnimInstance* Instance) override
    {
        const auto* Ronin = CastChecked<URoninAnimInstance>(Instance);
        for (int32 Index = 0; Index < ClipCount; ++Index)
            Assets[Index] = Ronin->GetClip(static_cast<Clip>(Index));
        for (int32 Index = 0; Index < 5; ++Index)
            LocomotionPlayers[Index].SetSequence(Assets[Index]);
        GuardPlayer.SetSequence(Asset(Clip::Guard));
        GuardPlayer.SetExplicitTime(.8f);
        GuardPlayer.bLoop = false;
        for (auto& Player : ActionPlayers)
        {
            Player.SetSequence(Asset(Clip::Idle));
            Player.bLoop = false;
        }
        FAnimInstanceProxy::Initialize(Instance);
    }

    virtual FAnimNode_Base* GetCustomRootNode() override { return &Root; }

    virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override
    {
        Nodes.Add(&Root);
        for (auto& Player : LocomotionPlayers) Nodes.Add(&Player);
        for (auto& Player : ActionPlayers) Nodes.Add(&Player);
        Nodes.Add(&GuardPlayer);
        Nodes.Add(&ForwardBackward);
        Nodes.Add(&LeftRight);
        Nodes.Add(&DirectionBlend);
        Nodes.Add(&LocomotionBlend);
        Nodes.Add(&GuardBlend);
        Nodes.Add(&ActionBlend);
        Nodes.Add(&OutputBlend);
    }

    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
        if (const auto* Fighter = Cast<ARoninFighter>(Instance->GetOwningActor()))
            Fighter->GetAnimationState(State);
    }

    UAnimSequence* Asset(Clip Id) const { return Assets[static_cast<int32>(Id)]; }
    float Length(Clip Id) const { return Asset(Id) ? Asset(Id)->GetPlayLength() : 0.f; }

    void ResetState()
    {
        IdleTime = GaitPhase = DeathTime = 0.f;
        SmoothedVelocity = FVector::ZeroVector;
        ActionWeight = GuardWeight = 0.f;
        ActionKey = 0;
        CurrentSlot = 0;
        ActionBlend.Alpha = 0.f;
        for (auto& Player : ActionPlayers)
        {
            Player.SetSequence(Asset(Clip::Idle));
            Player.SetExplicitTime(0.f);
        }
        LastAction = ronin::Action::Idle;
        LastPhaseTime = 0.f;
        ReactionSerial = 0;
        bExecutionRecovery = false;
        DefenseTime = 1.f;
        DefenseSerial = State.DefenseSerial;
        ImpactSerial = State.ImpactSerial;
        ImpactTime = 1.f;
        Root.Crouch = Root.LeanForward = Root.LeanSide = Root.DeflectTwist = 0.f;
    }

    virtual void Update(float DeltaSeconds) override
    {
        if (ResetSerial != State.ResetSerial)
        {
            ResetState();
            ResetSerial = State.ResetSerial;
        }
        const float Dt = State.bFrozen ? 0.f : State.SimulationDelta;
        if (DefenseSerial != State.DefenseSerial)
        {
            DefenseSerial = State.DefenseSerial;
            DefenseTime = 0.f;
        }
        else DefenseTime += Dt;
        if (ImpactSerial != State.ImpactSerial)
        {
            ImpactSerial = State.ImpactSerial;
            ImpactTime = 0.f;
            if (State.bImpactBlocked) GuardWeight = 1.f;
        }
        else ImpactTime += Dt;
        const auto& Combat = State.Combat;
        const auto Action = Combat.action;
        IdleTime += Dt;
        SmoothedVelocity = FMath::VInterpTo(SmoothedVelocity, State.LocalVelocity, Dt, 14.f);
        const float Speed = static_cast<float>(SmoothedVelocity.Size2D());
        const float StrideLength = FMath::Max(Length(Clip::JogForward), .01f);
        GaitPhase = FMath::Fmod(GaitPhase + Dt * Speed / (390.f * StrideLength), 1.f);
        LocomotionPlayers[0].SetExplicitTime(FMath::Fmod(IdleTime, FMath::Max(Length(Clip::Idle), .01f)));
        for (int32 Index = 1; Index < 5; ++Index)
            LocomotionPlayers[Index].SetExplicitTime(GaitPhase * Length(static_cast<Clip>(Index)));
        const double Forward = FMath::Abs(SmoothedVelocity.X);
        const double Side = FMath::Abs(SmoothedVelocity.Y);
        ForwardBackward.Alpha = SmoothedVelocity.X < 0.0 ? 1.f : 0.f;
        LeftRight.Alpha = SmoothedVelocity.Y > 0.0 ? 1.f : 0.f;
        DirectionBlend.Alpha = Forward + Side > 1.0 ? static_cast<float>(Side / (Forward + Side)) : 0.f;
        LocomotionBlend.Alpha = FMath::Clamp(Speed / 150.f, 0.f, 1.f);

        GuardWeight = FMath::FInterpConstantTo(GuardWeight,
            Combat.guarding && Action == ronin::Action::Idle ? 1.f : 0.f, Dt, 10.f);
        // A successful deflection has an explicit blade lift and torso recoil,
        // then opens back toward neutral so an immediate counter can take over.
        const float Deflect = State.bDefenseActive && !State.bPerfectDodge && Action == ronin::Action::Idle ? FMath::Clamp(1.f - DefenseTime / .32f, 0.f, 1.f) : 0.f;
        GuardPlayer.SetExplicitTime(FMath::Lerp(.8f, .58f, Deflect));
        GuardBlend.BlendWeights[0] = FMath::Max(GuardWeight, Deflect);
        Root.DeflectTwist = -18.f * Deflect * Deflect;
        Root.Crouch = 2.5f * Deflect;
        Root.LeanForward = 5.f * Deflect;
        Root.LeanSide = 0.f;
        const float Recoil = FMath::Clamp(1.f - ImpactTime / .25f, 0.f, 1.f);
        if (State.bImpactBlocked && Action == ronin::Action::Idle)
        {
            GuardBlend.BlendWeights[0] = FMath::Max(GuardBlend.BlendWeights[0], Recoil);
            Root.LeanForward -= 7.f * Recoil;
            Root.Crouch += 3.f * Recoil;
        }
        else if (Action == ronin::Action::Staggered || Action == ronin::Action::Broken)
        {
            Root.LeanForward -= (State.bImpactStrong ? 16.f : 9.f) * Recoil;
            Root.LeanSide = (ImpactSerial % 2 ? 1.f : -1.f) * 6.f * Recoil;
        }

        if (Action == ronin::Action::Dead)
        {
            if (LastAction != ronin::Action::Dead) DeathTime = 0.f;
            else DeathTime += Dt;
        }
        if (!IsAttack(Action) && (LastAction != Action || Combat.actionTime + .001f < LastPhaseTime))
            ++ReactionSerial;
        if (Action == ronin::Action::Recovery && !IsAttack(LastAction)) bExecutionRecovery = true;
        else if (Action != ronin::Action::Recovery) bExecutionRecovery = false;

        Clip Selected = Clip::Idle;
        float Time = 0.f;
        uint64 Key = 0;
        bool bActionPose = true;
        if (IsAttack(Action))
        {
            Selected = Combat.isCounterAttack || Combat.currentAttack == ronin::AttackKind::Unblockable ? Clip::AttackD :
                Combat.currentAttack == ronin::AttackKind::Heavy ? Clip::AttackC :
                Combat.comboStep == 4 ? Clip::AttackC : Combat.comboStep == 3 ? Clip::AttackD :
                Combat.comboStep == 1 ? Clip::AttackB : Clip::AttackA;
            if (Combat.comboStep == 2 && !Combat.isCounterAttack)
                Root.DeflectTwist = -12.f * FMath::Sin(PI * FMath::Clamp(Combat.attackElapsed / FMath::Max(.01f, Combat.spec.windup + Combat.spec.active + Combat.spec.recovery), 0.f, 1.f));
            // Source sword attacks reach their forward sweep at .2 s, then exit by .3 s.
            // These are visual phase anchors; animation notifies never resolve gameplay hits.
            Time = URoninAnimInstance::MapAttackPhase(Combat, Length(Selected), .2f, .3f);
            if (Action == ronin::Action::Recovery && Selected != Clip::AttackD)
            {
                Selected = Selected == Clip::AttackA ? Clip::RecoveryA :
                    Selected == Clip::AttackB ? Clip::RecoveryB : Clip::RecoveryC;
                const float Progress = Combat.actionDuration > 0.f ?
                    FMath::Clamp(Combat.actionTime / Combat.actionDuration, 0.f, 1.f) : 1.f;
                Time = Progress * Length(Selected);
            }
            if (bExecutionRecovery)
            {
                Selected = Clip::AttackD;
                const float Progress = Combat.actionDuration > 0.f ?
                    FMath::Clamp(Combat.actionTime / Combat.actionDuration, 0.f, 1.f) : 1.f;
                Time = FMath::Lerp(.2f, Length(Selected), Progress);
            }
            Key = 1000 + Combat.attackId * ClipCount + static_cast<int32>(Selected);
        }
        else if (Action == ronin::Action::Dodging)
        {
            const FVector& Direction = State.LocalDodgeDirection;
            Selected = FMath::Abs(Direction.X) >= FMath::Abs(Direction.Y) ?
                (Direction.X >= 0.0 ? Clip::StepForward : Clip::StepBackward) :
                (Direction.Y >= 0.0 ? Clip::StepRight : Clip::StepLeft);
            const float Progress = Combat.actionDuration > 0.f ?
                FMath::Clamp(Combat.actionTime / Combat.actionDuration, 0.f, 1.f) : 1.f;
            // The package has no roll: use the authored directional launch as an evasive step.
            Time = Progress * FMath::Min(.8f, Length(Selected));
            // Lower the centre of mass into the step, then recover it smoothly.
            const float Slip = FMath::Sin(PI * FMath::Clamp(Progress, 0.f, 1.f));
            Root.Crouch = 9.f * Slip;
            Root.LeanForward = -14.f * Direction.X * Slip;
            Root.LeanSide = -18.f * Direction.Y * Slip;
            Key = (1ull << 56) + ReactionSerial * ClipCount + static_cast<int32>(Selected);
        }
        else if (Action == ronin::Action::Staggered || Action == ronin::Action::Broken)
        {
            const float IntroTime = Action == ronin::Action::Broken ? .3f : Combat.actionDuration;
            Selected = Action == ronin::Action::Broken && Combat.actionTime >= IntroTime ? Clip::StunLoop : Clip::StunStart;
            Time = Selected == Clip::StunLoop ?
                FMath::Fmod(Combat.actionTime - IntroTime, FMath::Max(Length(Selected), .01f)) :
                FMath::Clamp(Combat.actionTime / FMath::Max(IntroTime, .01f), 0.f, 1.f) * Length(Selected);
            Key = (2ull << 56) + ReactionSerial * ClipCount + static_cast<int32>(Selected);
        }
        else if (Action == ronin::Action::Dead)
        {
            Selected = Clip::Death;
            Time = FMath::Min(DeathTime, Length(Selected));
            Key = (3ull << 56) + ReactionSerial;
        }
        else if (State.bAirborne)
        {
            Selected = Clip::StepForward;
            Time = State.VerticalSpeed > 0.f ? .22f : .36f;
            Root.Crouch = State.VerticalSpeed > 0.f ? 7.f : 3.f;
            Root.LeanForward = 8.f;
            Key = (4ull << 56);
        }
        else bActionPose = false;

        if (bActionPose && Asset(Selected))
        {
            if (Key != ActionKey)
            {
                CurrentSlot = 1 - CurrentSlot;
                ActionPlayers[CurrentSlot].SetSequence(Asset(Selected));
                ActionKey = Key;
            }
            ActionPlayers[CurrentSlot].SetExplicitTime(Time);
        }
        ActionBlend.Alpha = FMath::FInterpConstantTo(ActionBlend.Alpha,
            CurrentSlot == 1 ? 1.f : 0.f, Dt, 1.f / .06f);
        ActionWeight = FMath::FInterpConstantTo(ActionWeight,
            bActionPose ? 1.f : 0.f, Dt, bActionPose ? 1.f / .07f : 1.f / .12f);
        OutputBlend.Alpha = ActionWeight;
        LastAction = Action;
        LastPhaseTime = Combat.actionTime;
    }

    FRoninAnimationState State;
    UAnimSequence* Assets[ClipCount]{};
    FExplicitSequence LocomotionPlayers[5];
    FExplicitSequence GuardPlayer;
    FExplicitSequence ActionPlayers[2];
    FAnimNode_TwoWayBlend ForwardBackward, LeftRight, DirectionBlend, LocomotionBlend;
    FAnimNode_LayeredBoneBlend GuardBlend;
    FAnimNode_TwoWayBlend ActionBlend, OutputBlend;
    FRootLockedPose Root;
    FVector SmoothedVelocity = FVector::ZeroVector;
    float IdleTime = 0.f, GaitPhase = 0.f, DeathTime = 0.f;
    float ActionWeight = 0.f, GuardWeight = 0.f, LastPhaseTime = 0.f;
    float DefenseTime = 1.f;
    uint32 DefenseSerial = 0;
    uint32 ImpactSerial = 0;
    float ImpactTime = 1.f;
    uint32 ResetSerial = 0;
    uint64 ActionKey = 0, ReactionSerial = 0;
    int32 CurrentSlot = 0;
    ronin::Action LastAction = ronin::Action::Idle;
    bool bExecutionRecovery = false;
};
} // namespace

URoninAnimInstance::URoninAnimInstance()
{
    RootMotionMode = ERootMotionMode::IgnoreRootMotion;
    const TCHAR* Names[] = {
        TEXT("Idle"), TEXT("Jog_Fwd"), TEXT("Jog_Bwd"), TEXT("Jog_Left"), TEXT("Jog_Right"),
        TEXT("PrimaryAttack_A_Slow"), TEXT("PrimaryAttack_B_Slow"),
        TEXT("PrimaryAttack_C_Slow"), TEXT("PrimaryAttack_D_Slow"),
        TEXT("PrimaryAttack_A_Slow_Recovery"), TEXT("PrimaryAttack_B_Slow_Recovery"),
        TEXT("PrimaryAttack_C_Slow_Recovery"), TEXT("Stun_Start"), TEXT("Stun_Loop"), TEXT("Death_Bwd"),
        TEXT("Jog_Fwd_Start"), TEXT("Jog_Bwd_Start"), TEXT("Jog_Left_Start"), TEXT("Jog_Right_Start"),
        TEXT("Ability_RMB")};
    static_assert(UE_ARRAY_COUNT(Names) == ClipCount);
    for (const TCHAR* Name : Names)
    {
        const FString Path = FString::Printf(TEXT("/Game/Retargeted/KwangSamurai/Animations/A_%s.A_%s"), Name, Name);
        ConstructorHelpers::FObjectFinder<UAnimSequence> Found(*Path);
        Clips.Add(Found.Object);
    }
}

UAnimSequence* URoninAnimInstance::GetClip(ERoninAnimationClip Clip) const
{
    const int32 Index = static_cast<int32>(Clip);
    return Clips.IsValidIndex(Index) ? Clips[Index].Get() : nullptr;
}

FRoninAnimationPlayback URoninAnimInstance::GetPlaybackState() const
{
    const auto& Proxy = GetProxyOnGameThread<FRoninAnimProxy>();
    FRoninAnimationPlayback Result;
    Result.ActionSequence = Cast<UAnimSequence>(Proxy.ActionPlayers[Proxy.CurrentSlot].GetSequence());
    Result.ActionTime = Proxy.ActionPlayers[Proxy.CurrentSlot].GetExplicitTime();
    Result.IdleTime = Proxy.IdleTime;
    Result.ActionWeight = Proxy.ActionWeight;
    Result.bFrozen = Proxy.State.bFrozen;
    return Result;
}

float URoninAnimInstance::MapAttackPhase(const ronin::FighterState& State, float ClipLength,
    float ContactStart, float ContactEnd)
{
    ContactStart = FMath::Clamp(ContactStart, 0.f, ClipLength);
    ContactEnd = FMath::Clamp(ContactEnd, ContactStart, ClipLength);
    const float Progress = State.actionDuration > 0.f ?
        FMath::Clamp(State.actionTime / State.actionDuration, 0.f, 1.f) : 1.f;
    if (State.action == ronin::Action::Windup) return FMath::Lerp(0.f, ContactStart, Progress);
    if (State.action == ronin::Action::Active) return FMath::Lerp(ContactStart, ContactEnd, Progress);
    return FMath::Lerp(ContactEnd, ClipLength, Progress);
}

FAnimInstanceProxy* URoninAnimInstance::CreateAnimInstanceProxy()
{
    return new FRoninAnimProxy(this);
}

void URoninAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
    delete static_cast<FRoninAnimProxy*>(InProxy);
}
