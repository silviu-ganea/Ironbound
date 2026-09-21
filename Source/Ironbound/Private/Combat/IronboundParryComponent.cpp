#include "Combat/IronboundParryComponent.h"



#include "Animation/Skeleton.h"

#include "Animation/AnimInstance.h"

#include "Animation/AnimMontage.h"

#include "Animation/AnimCompositeBase.h"

#include "Combat/IronboundCombatExecutionComponent.h"

#include "Combat/IronboundCombatFocusComponent.h"

#include "Combat/IronboundCombatBodyComponent.h"

#include "Combat/IronboundEquipmentComponent.h"

#include "Components/SkeletalMeshComponent.h"

#include "Components/StaticMeshComponent.h"

#include "DrawDebugHelpers.h"

#include "Math/RandomStream.h"

#include "Math/UnrealMathUtility.h"



namespace

{

    struct FBodyProbe

    {

        FName Bone;

        float Radius;

    };



    // Conservative gameplay envelope used for both the hard timing gate and

    // candidate clearance ranking. This is intentionally the same geometry in

    // both paths so the solver cannot rank against a different body definition

    // than the one used to invalidate a parry.

    static const FBodyProbe GBodyProbes[] =

    {

        { TEXT("pelvis"), 34.f },

        { TEXT("spine_01"), 30.f },

        { TEXT("spine_02"), 31.f },

        { TEXT("spine_03"), 30.f },

        { TEXT("neck_01"), 24.f },

        { TEXT("head"), 28.f }

    };



    float FindBodyIntersectionTime(

        USkeletalMeshComponent* Mesh,

        const FBladeTrajectory& Trajectory,

        const FTransform& AttackerTransform,

        float CurrentSourceTime,

        bool& bOutAlreadyIntersected)

    {

        bOutAlreadyIntersected = false;



        if (!Mesh)

        {

            return TNumericLimits<float>::Max();

        }



        for (const FBladeSegment& Segment : Trajectory.Segments)

        {

            const FVector BladeBase =

                AttackerTransform.TransformPosition(Segment.Base);



            const FVector BladeTip =

                AttackerTransform.TransformPosition(Segment.Tip);



            for (const FBodyProbe& Probe : GBodyProbes)

            {

                if (Mesh->GetBoneIndex(Probe.Bone) == INDEX_NONE)

                {

                    continue;

                }



                const FVector BodyPoint =

                    Mesh->GetSocketLocation(Probe.Bone);



                const FVector ClosestPoint =

                    FMath::ClosestPointOnSegment(

                        BodyPoint,

                        BladeBase,

                        BladeTip);



                if (FVector::DistSquared(BodyPoint, ClosestPoint) <=

                    FMath::Square(Probe.Radius))

                {

                    if (Segment.TimeSeconds <=

                        CurrentSourceTime + KINDA_SMALL_NUMBER)

                    {

                        bOutAlreadyIntersected = true;

                    }



                    // Trajectory samples are ordered by source time. Returning

                    // the first hit preserves the first body intersection,

                    // including one that happened before reaction became ready.

                    return Segment.TimeSeconds;

                }

            }

        }



        return TNumericLimits<float>::Max();

    }



    float FindMinimumBodyClearance(

        USkeletalMeshComponent* Mesh,

        const FVector& BladeBase,

        const FVector& BladeTip)

    {

        if (!Mesh)

        {

            return -TNumericLimits<float>::Max();

        }



        float MinimumClearance = TNumericLimits<float>::Max();

        bool bFoundProbe = false;



        for (const FBodyProbe& Probe : GBodyProbes)

        {

            if (Mesh->GetBoneIndex(Probe.Bone) == INDEX_NONE)

            {

                continue;

            }



            bFoundProbe = true;

            const FVector BodyPoint = Mesh->GetSocketLocation(Probe.Bone);

            const FVector ClosestPoint =

                FMath::ClosestPointOnSegment(

                    BodyPoint,

                    BladeBase,

                    BladeTip);



            const float Clearance =

                FMath::Sqrt(

                    FVector::DistSquared(BodyPoint, ClosestPoint)) -

                Probe.Radius;



            MinimumClearance = FMath::Min(MinimumClearance, Clearance);

        }



    return bFoundProbe

        ? MinimumClearance

        : -TNumericLimits<float>::Max();

}

}



FArmExtensionMetrics UIronboundParryComponent::CalculateArmExtensionMetrics(
    const FVector& Shoulder,
    const FVector& Hand,
    float UpperArmLengthParam,
    float ForearmLengthParam) const
{
    const float D = FVector::Distance(Shoulder, Hand);
    const float A = UpperArmLengthParam;
    const float B = ForearmLengthParam;
    const float MaxReach = A + B;
    const float MinReach = FMath::Abs(A - B);

    FArmExtensionMetrics Metrics;
    Metrics.Distance = D;
    Metrics.MaxReach = MaxReach;
    Metrics.MinReach = MinReach;
    Metrics.UpperArmLength = A;
    Metrics.ForearmLength = B;

    if (A <= KINDA_SMALL_NUMBER || B <= KINDA_SMALL_NUMBER || MaxReach <= KINDA_SMALL_NUMBER)
    {
        return Metrics;
    }

    Metrics.ExtensionRatio = D / MaxReach;
    if (D < MinReach - ArmReachMargin || D > MaxReach + ArmReachMargin || D <= KINDA_SMALL_NUMBER)
    {
        return Metrics;
    }

    // Kinematic non-collinearity of the two-link arm. This is zero at both
    // the fully folded and fully extended singularities, and one at 90 degrees.
    const float CosJoint = FMath::Clamp(
        (D * D - A * A - B * B) / (2.f * A * B), -1.f, 1.f);
    Metrics.GeometricFreedom = FMath::Sqrt(FMath::Max(0.f, 1.f - CosJoint * CosJoint));
    return Metrics;
}


float UIronboundParryComponent::CalculateMinimumWristDeviationAnalytical(
    const FVector& Shoulder,
    const FVector& Hand,
    const FVector& NeutralForearmDir,
    float UpperArmLengthParam,
    float ForearmLengthParam) const
{
    const FVector ShoulderToHand = Hand - Shoulder;
    const float D = ShoulderToHand.Size();
    const float A = UpperArmLengthParam;
    const float B = ForearmLengthParam;
    const float MaxReach = A + B;
    const float MinReach = FMath::Abs(A - B);

    if (A <= KINDA_SMALL_NUMBER || B <= KINDA_SMALL_NUMBER ||
        D <= KINDA_SMALL_NUMBER || D < MinReach - ArmReachMargin || D > MaxReach + ArmReachMargin)
    {
        return PI;
    }

    const FVector Axis = ShoulderToHand / D;
    const FVector NeutralDir = NeutralForearmDir.GetSafeNormal();
    if (NeutralDir.IsNearlyZero())
    {
        return PI;
    }

    const float Along = (A * A - B * B + D * D) / (2.f * D);
    const float RadiusSq = FMath::Max(0.f, A * A - Along * Along);
    const float Radius = FMath::Sqrt(RadiusSq);
    const FVector Center = Shoulder + Axis * Along;

    // The elbow that would make the wrist perfectly neutral. Find the closest
    // geometrically valid elbow on the true shoulder-hand elbow circle.
    const FVector NeutralElbow = Hand - NeutralDir * B;

    FVector ValidElbow = Center;
    if (Radius > KINDA_SMALL_NUMBER)
    {
        const FVector ToNeutral = NeutralElbow - Center;
        const FVector PlaneVector = ToNeutral - FVector::DotProduct(ToNeutral, Axis) * Axis;

        if (!PlaneVector.IsNearlyZero())
        {
            ValidElbow = Center + PlaneVector.GetSafeNormal() * Radius;
        }
        else
        {
            FVector Orthogonal1, Orthogonal2;
            Axis.FindBestAxisVectors(Orthogonal1, Orthogonal2);
            ValidElbow = Center + Orthogonal1 * Radius;
        }
    }

    const FVector ValidForearmDir = (Hand - ValidElbow).GetSafeNormal();
    if (ValidForearmDir.IsNearlyZero())
    {
        return PI;
    }

    return FMath::Acos(FMath::Clamp(
        FVector::DotProduct(NeutralDir, ValidForearmDir), -1.f, 1.f));
}


FVector UIronboundParryComponent::CalculateReferenceForearmDirection() const
{
    if (!FighterMesh || !FighterMesh->GetSkeletalMeshAsset()) return FVector::ForwardVector;

    const int32 LowerArmIndex = FighterMesh->GetBoneIndex(LowerArmBone);
    const int32 HandIndex = FighterMesh->GetBoneIndex(HandBone);
    if (LowerArmIndex == INDEX_NONE || HandIndex == INDEX_NONE) return FVector::ForwardVector;

    const FReferenceSkeleton& RefSkeleton = FighterMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

    auto GetCS = [&RefSkeleton, &RefPose](int32 BoneIndex)
    {
        FTransform Result = RefPose[BoneIndex];
        for (int32 Parent = RefSkeleton.GetParentIndex(BoneIndex);
             Parent != INDEX_NONE;
             Parent = RefSkeleton.GetParentIndex(Parent))
        {
            Result *= RefPose[Parent];
        }
        return Result;
    };

    const FTransform LowerArmCS = GetCS(LowerArmIndex);
    const FTransform HandCS = GetCS(HandIndex);
    const FVector ForearmCS = (HandCS.GetLocation() - LowerArmCS.GetLocation()).GetSafeNormal();
    return LowerArmCS.InverseTransformVectorNoScale(ForearmCS).GetSafeNormal();
}


FTransform UIronboundParryComponent::CalculateNeutralWristRelationship() const
{
    if (!FighterMesh || !FighterMesh->GetSkeletalMeshAsset()) return FTransform::Identity;

    const int32 LowerArmIndex = FighterMesh->GetBoneIndex(LowerArmBone);
    const int32 HandIndex = FighterMesh->GetBoneIndex(HandBone);
    if (LowerArmIndex == INDEX_NONE || HandIndex == INDEX_NONE) return FTransform::Identity;

    const FReferenceSkeleton& RefSkeleton = FighterMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

    auto GetCS = [&RefSkeleton, &RefPose](int32 BoneIndex)
    {
        FTransform Result = RefPose[BoneIndex];
        for (int32 Parent = RefSkeleton.GetParentIndex(BoneIndex);
             Parent != INDEX_NONE;
             Parent = RefSkeleton.GetParentIndex(Parent))
        {
            Result *= RefPose[Parent];
        }
        return Result;
    };

    const FTransform LowerArmCS = GetCS(LowerArmIndex);
    const FTransform HandCS = GetCS(HandIndex);
    return HandCS.GetRelativeTransform(LowerArmCS);
}


FVector UIronboundParryComponent::CalculateNeutralForearmDirection(const FTransform& RequiredHandTransform) const
{
    const FTransform NeutralWristRelationship = CalculateNeutralWristRelationship();
    const FVector ReferenceForearmDirLocal = CalculateReferenceForearmDirection();

    // UE composition: HandWorld = HandRelativeToForearm * ForearmWorld.
    const FTransform RequiredForearmTransform =
        NeutralWristRelationship.Inverse() * RequiredHandTransform;

    return RequiredForearmTransform
        .TransformVectorNoScale(ReferenceForearmDirLocal)
        .GetSafeNormal();
}




float UIronboundParryComponent::ApplyPolicyToRawMetrics(const FCandidateRawMetrics& Raw, const FParryPolicy& Policy) const

{

    // Convert wrist deviation to comfort (0-1, where 1 = comfortable)

    float wristComfort = 1.0f - (Raw.WristDeviationRadians / PI);

    wristComfort = FMath::Pow(wristComfort, Policy.WristSharpness);



    // Convert geometric freedom to comfort (0-1, where 1 = comfortable)

    float freedomComfort = FMath::Pow(Raw.GeometricFreedom, Policy.FreedomSharpness);



    // Tactical quality is already 0-1 where 1 = better

    float tacticalQuality = Raw.TacticalQuality;



    // Combined quality with tunable weighting

    return tacticalQuality * Policy.TacticalWeight + 

           wristComfort * (1.0f - Policy.TacticalWeight) * 0.5f + 

           freedomComfort * (1.0f - Policy.TacticalWeight) * 0.5f;

}



void UIronboundParryComponent::SelectComfortableCandidate(
    const TArray<FIronboundParryCandidate>& Candidates,
    FIronboundParryCandidate& OutSelected,
    uint32 SelectionSeed,
    const FParryPolicy& Policy) const
{
    if (Candidates.IsEmpty())
    {
        OutSelected = FIronboundParryCandidate();
        return;
    }

    float BestQuality = -TNumericLimits<float>::Max();
    int32 BestIndex = INDEX_NONE;
    for (int32 Index = 0; Index < Candidates.Num(); ++Index)
    {
        if (Candidates[Index].Quality > BestQuality)
        {
            BestQuality = Candidates[Index].Quality;
            BestIndex = Index;
        }
    }

    if (BestIndex == INDEX_NONE || BestQuality <= KINDA_SMALL_NUMBER)
    {
        OutSelected = Candidates[FMath::Max(BestIndex, 0)];
        return;
    }

    const float Skill = FMath::Clamp(Policy.ParrySkill, 0.f, 1.f);
    const float HighSkillThreshold = FMath::Clamp(Policy.QualityBandWidth, 0.6f, 1.f);
    const float ThresholdRatio = FMath::Lerp(0.60f, HighSkillThreshold, Skill);
    const float QualityThreshold = BestQuality * ThresholdRatio;

    TArray<int32> AcceptableIndices;
    for (int32 Index = 0; Index < Candidates.Num(); ++Index)
    {
        if (Candidates[Index].Quality >= QualityThreshold)
        {
            AcceptableIndices.Add(Index);
        }
    }

    if (AcceptableIndices.IsEmpty())
    {
        OutSelected = Candidates[BestIndex];
        return;
    }

    FRandomStream RandomStream(SelectionSeed);
    const float Exponent = FMath::Lerp(1.f, 4.f, Skill);
    float TotalWeight = 0.f;
    for (const int32 Index : AcceptableIndices)
    {
        const float Ratio = FMath::Clamp(Candidates[Index].Quality / BestQuality, 0.f, 1.f);
        TotalWeight += FMath::Pow(Ratio, Exponent);
    }

    if (TotalWeight <= KINDA_SMALL_NUMBER)
    {
        OutSelected = Candidates[BestIndex];
        return;
    }

    float Pick = RandomStream.FRand() * TotalWeight;
    for (const int32 Index : AcceptableIndices)
    {
        const float Ratio = FMath::Clamp(Candidates[Index].Quality / BestQuality, 0.f, 1.f);
        Pick -= FMath::Pow(Ratio, Exponent);
        if (Pick <= 0.f)
        {
            OutSelected = Candidates[Index];
            return;
        }
    }

    OutSelected = Candidates[BestIndex];
}


void UIronboundParryComponent::SelectComfortableCandidate(

    const TArray<FIronboundParryCandidate>& Candidates,

    FIronboundParryCandidate& OutSelected,

    uint32 SelectionSeed) const

{

    // Default policy for simple selection

    FParryPolicy DefaultPolicy;

    DefaultPolicy.ParrySkill = 0.5f; // Medium skill

    DefaultPolicy.QualityBandWidth = 0.8f; // Medium band width



    SelectComfortableCandidate(Candidates, OutSelected, SelectionSeed, DefaultPolicy);

}



UIronboundParryComponent::UIronboundParryComponent()

{

    PrimaryComponentTick.bCanEverTick = true;

    PrimaryComponentTick.TickGroup = TG_PrePhysics;

}



void UIronboundParryComponent::BeginPlay()

{

    Super::BeginPlay();



    FighterMesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();

    CombatFocus = GetOwner()->FindComponentByClass<UIronboundCombatFocusComponent>();

    CombatBody = GetOwner()->FindComponentByClass<UIronboundCombatBodyComponent>();



    if (!FighterMesh)

    {

        UE_LOG(LogTemp, Warning, TEXT("Parry: %s has no skeletal mesh"), *GetNameSafe(GetOwner()));

        return;

    }



    if (!CombatFocus)

    {

        UE_LOG(LogTemp, Warning, TEXT("Parry: %s has no CombatFocus component"), *GetNameSafe(GetOwner()));

    }



    if (CalculateArmDimensions())

    {

        UE_LOG(LogTemp, Warning,

            TEXT("Parry Anatomy [%s]: UpperArm=%.1f Forearm=%.1f Total=%.1f cm"),

            *GetNameSafe(GetOwner()), UpperArmLength, ForearmLength, ArmLength);

    }

}



bool UIronboundParryComponent::CalculateArmDimensions()

{

    if (!FighterMesh || !FighterMesh->GetSkeletalMeshAsset()) return false;



    const int32 UpperArmIndex = FighterMesh->GetBoneIndex(UpperArmBone);

    const int32 LowerArmIndex = FighterMesh->GetBoneIndex(LowerArmBone);

    const int32 HandIndex = FighterMesh->GetBoneIndex(HandBone);



    if (UpperArmIndex == INDEX_NONE || LowerArmIndex == INDEX_NONE || HandIndex == INDEX_NONE)

        return false;



    const FReferenceSkeleton& RefSkeleton = FighterMesh->GetSkeletalMeshAsset()->GetRefSkeleton();

    const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();



    auto GetCS = [&RefSkeleton, &RefPose](int32 BoneIndex)

    {

        FTransform Result = RefPose[BoneIndex];

        for (int32 Parent = RefSkeleton.GetParentIndex(BoneIndex);

             Parent != INDEX_NONE;

             Parent = RefSkeleton.GetParentIndex(Parent))

        {

            Result *= RefPose[Parent];

        }

        return Result;

    };



    const FVector Shoulder = GetCS(UpperArmIndex).GetTranslation();

    const FVector Elbow = GetCS(LowerArmIndex).GetTranslation();

    const FVector Hand = GetCS(HandIndex).GetTranslation();



    UpperArmLength = FVector::Distance(Shoulder, Elbow);

    ForearmLength = FVector::Distance(Elbow, Hand);

    ArmLength = UpperArmLength + ForearmLength;

    return true;

}



void UIronboundParryComponent::TickComponent(

    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)

{

    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);



    UpdateAttackTimingState();



    if (!bHasActiveParryCandidate && bObservedCommittedAttack)

    {

        ParryState = bReactionReady

            ? EIronboundParryState::Moving

            : (bAttackRecognized ? EIronboundParryState::Reacting : EIronboundParryState::Observing);



        if (bReactionReady)

        {

            FIronboundParryCandidate Best;

            if (FindBestParryCandidate(Best))

            {

                ActiveParryCandidate = Best;

                bHasActiveParryCandidate = true;

                InitializeExecutionPose();

                ParryState = EIronboundParryState::Moving;



                if (CombatBody)

                    CombatBody->BeginParryBrace();



                UE_LOG(LogTemp, Warning,

                    TEXT("Parry [%s]: LOCKED quality=%.2f t=%.3f hand=%.0fcm/s blade=%.0fdeg/s cross=%.0f enemyBlade=%.0f%%"),

                    *GetNameSafe(GetOwner()), Best.Quality, Best.TimeUntilContact,

                    Best.RequiredHandSpeed, Best.RequiredBladeAngularSpeed,

                    Best.IntersectionAngleDegrees, Best.IncomingBladeFraction * 100.f);

            }

        }

    }



    if (bHasActiveParryCandidate)

        UpdateExecutionPose(DeltaTime);



    if (bDrawParryDebug)

    {

        DrawAnatomyDebug();

        DrawParrySolutionDebug();

        DrawDiagnosticsDebug();

    }

}



void UIronboundParryComponent::InitializeExecutionPose()

{

    if (!FighterMesh) return;



    ExecutedHandTransform = FighterMesh->GetSocketTransform(HandBone, RTS_World);

}



void UIronboundParryComponent::UpdateExecutionPose(float DeltaTime)

{

    if (!bHasActiveParryCandidate) return;



    const FVector CurrentLocation = ExecutedHandTransform.GetLocation();

    const FVector TargetLocation = ActiveParryCandidate.RequiredHandTransform.GetLocation();

    const FVector NewLocation = FMath::VInterpConstantTo(

        CurrentLocation, TargetLocation, DeltaTime, ParryMovementSpeed);



    const FQuat CurrentRotation = ExecutedHandTransform.GetRotation();

    const FQuat TargetRotation = ActiveParryCandidate.RequiredHandTransform.GetRotation();

    const float AngleRadians = CurrentRotation.AngularDistance(TargetRotation);

    FQuat NewRotation = TargetRotation;



    if (AngleRadians > KINDA_SMALL_NUMBER)

    {

        const float MaxStepRadians = FMath::DegreesToRadians(ParryRotationSpeedDegrees) * DeltaTime;

        const float Alpha = FMath::Clamp(MaxStepRadians / AngleRadians, 0.f, 1.f);

        NewRotation = FQuat::Slerp(CurrentRotation, TargetRotation, Alpha).GetNormalized();

    }



    ExecutedHandTransform.SetLocation(NewLocation);

    ExecutedHandTransform.SetRotation(NewRotation);

    ExecutedHandTransform.SetScale3D(FVector::OneVector);



    const float PositionError = FVector::Distance(NewLocation, TargetLocation);

    const float RotationErrorDeg =

        FMath::RadiansToDegrees(NewRotation.AngularDistance(TargetRotation));



    if (PositionError <= PositionArrivalTolerance && RotationErrorDeg <= 3.f)

        ParryState = EIronboundParryState::Holding;

    else

        ParryState = EIronboundParryState::Moving;

}



void UIronboundParryComponent::ResetParryAction()

{

    if (CombatBody && CombatBody->IsParryBraced())

        CombatBody->EndParryBrace();



    ActiveParryCandidate = FIronboundParryCandidate();

    bHasActiveParryCandidate = false;

    ExecutedHandTransform = FTransform::Identity;

    ParryState = EIronboundParryState::Observing;

}



void UIronboundParryComponent::UpdateAttackTimingState()

{

    AActor* Target = CombatFocus ? CombatFocus->GetCombatTarget() : nullptr;

    UIronboundCombatExecutionComponent* TargetExecution =

        Target ? Target->FindComponentByClass<UIronboundCombatExecutionComponent>() : nullptr;

    const bool bCommitted = TargetExecution && TargetExecution->HasCommittedBladePath();



    if (!bCommitted)

    {

        ResetParryAction();

        bObservedCommittedAttack = false;

        bAttackRecognized = false;

        bReactionReady = false;

        ObservationWorldTime = 0.f;

        RecognitionWorldTime = 0.f;

        ObservedAttacker.Reset();

        bDiagnosticsLoggedForObservedAttack = false;

        LastParryEarlyExitReason.Reset();

        return;

    }



    if (!bObservedCommittedAttack || ObservedAttacker.Get() != Target)

    {

        ResetParryAction();

        bObservedCommittedAttack = true;

        bAttackRecognized = false;

        bReactionReady = false;

        ObservedAttacker = Target;

        ObservationWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

        RecognitionWorldTime = 0.f;

        bDiagnosticsLoggedForObservedAttack = false;

        LastParryEarlyExitReason.Reset();

        return;

    }



    if (!bAttackRecognized)

    {

        if (!GetWorld()) return;

        const float Elapsed = GetWorld()->GetTimeSeconds() - ObservationWorldTime;

        if (Elapsed + KINDA_SMALL_NUMBER < PerceptionDelaySeconds) return;



        if (!TargetExecution->GetCommittedTrajectory().bValid) return;



        bAttackRecognized = true;

        ParryState = EIronboundParryState::Reacting;

        RecognitionWorldTime = GetWorld()->GetTimeSeconds();

        return;

    }



    if (!bReactionReady && GetWorld())

    {

        const float Elapsed = GetWorld()->GetTimeSeconds() - RecognitionWorldTime;

        if (Elapsed >= ReactionDelaySeconds)

        {

            bReactionReady = true;

            ParryState = EIronboundParryState::Moving;

        }

    }

}



bool UIronboundParryComponent::GetIncomingSourcePlaybackTime(float& OutSourceTime) const

{

    OutSourceTime = 0.f;

    AActor* Attacker = ObservedAttacker.Get();

    if (!Attacker) return false;



    UIronboundEquipmentComponent* Equipment =

        Attacker->FindComponentByClass<UIronboundEquipmentComponent>();

    USkeletalMeshComponent* Mesh = Equipment ? Equipment->GetFighterMesh() : nullptr;

    UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;

    UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;

    if (!Anim || !Montage) return false;



    const float MontagePosition = Anim->Montage_GetPosition(Montage);



    for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)

    {

        for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)

        {

            const float Start = Segment.StartPos;

            const float End = Start + Segment.GetLength();

            if (MontagePosition + KINDA_SMALL_NUMBER < Start ||

                MontagePosition - KINDA_SMALL_NUMBER > End)

                continue;



            OutSourceTime = Segment.ConvertTrackPosToAnimPos(MontagePosition);

            return true;

        }

    }

    return false;

}



float UIronboundParryComponent::GetTimeUntilIncomingSample(float IncomingTime) const

{

    float CurrentSourceTime = 0.f;

    return GetIncomingSourcePlaybackTime(CurrentSourceTime)

        ? IncomingTime - CurrentSourceTime

        : -1.f;

}







bool UIronboundParryComponent::EvaluateCandidate(

    const FVector& ShoulderWorld,

    const FVector& IncomingBaseWorld,

    const FVector& IncomingTipWorld,

    const FVector& IncomingContactWorld,

    const FVector& IncomingDirection,

    float IncomingTime,

    float IncomingBladeFraction,

    const FVector& DefenseDirection,

    float DefenderBladeFraction,

    float BladeLength,

    const FTransform& CurrentWeaponTransform,

    const FVector& CurrentDefenseDirection,

    FIronboundParryCandidate& Out) const

{

    const FVector DefenseDir = DefenseDirection.GetSafeNormal();

    if (IncomingDirection.IsNearlyZero() || DefenseDir.IsNearlyZero()) return false;



    const float AbsDot = FMath::Clamp(

        FMath::Abs(FVector::DotProduct(IncomingDirection, DefenseDir)), 0.f, 1.f);

    const float Crossing = FMath::RadiansToDegrees(FMath::Acos(AbsDot));



    if (Crossing < MinimumIntersectionAngleDegrees)

    {

        ++LastDiagnostics.AngleRejected;

        return false;

    }



    const FVector CandidateBase =

        IncomingContactWorld - DefenseDir * (BladeLength * DefenderBladeFraction);

    const FVector CandidateTip = CandidateBase + DefenseDir * BladeLength;



    const UIronboundEquipmentComponent* Equipment =

        GetOwner()->FindComponentByClass<UIronboundEquipmentComponent>();

    if (!Equipment || !Equipment->Definition) return false;



    const FVector BladeBaseLocal = Equipment->Definition->BladeBase;



    const FQuat Alignment =

        FQuat::FindBetweenNormals(CurrentDefenseDirection, DefenseDir);

    const FQuat WeaponRotation =

        (Alignment * CurrentWeaponTransform.GetRotation()).GetNormalized();



    const FVector WeaponTranslation =

        CandidateBase -

        WeaponRotation.RotateVector(CurrentWeaponTransform.GetScale3D() * BladeBaseLocal);



    const FTransform WeaponTransform(

        WeaponRotation, WeaponTranslation, CurrentWeaponTransform.GetScale3D());



    // Equipment defines WeaponWorld = WeaponToHand * HandWorld, so invert that
    // authored grip relation to recover the hand pose required by this weapon pose.
    const FTransform HandTransform =
        Equipment->Definition->WeaponToHand.Inverse() * WeaponTransform;

    const FVector Hand = HandTransform.GetLocation();



    // Calculate biomechanical metrics first to check geometric validity

    const FArmExtensionMetrics ExtensionMetrics = CalculateArmExtensionMetrics(

        ShoulderWorld, Hand, UpperArmLength, ForearmLength);



    if (ExtensionMetrics.Distance < ExtensionMetrics.MinReach || 

        ExtensionMetrics.Distance > ExtensionMetrics.MaxReach)

    {

        ++LastDiagnostics.AnatomyRejected;

        return false;

    }



    const float TimeUntilContact = GetTimeUntilIncomingSample(IncomingTime);

    if (TimeUntilContact < MinimumTimeToContact)

    {

        ++LastDiagnostics.TimingRejected;

        return false;

    }



    const FVector CurrentHand = FighterMesh->GetSocketLocation(HandBone);

    const float HandDistance = FVector::Distance(CurrentHand, Hand);



    const float RotationDot = FMath::Clamp(

        FMath::Abs(FVector::DotProduct(CurrentDefenseDirection, DefenseDir)), 0.f, 1.f);

    const float RotationDegrees =

        FMath::RadiansToDegrees(FMath::Acos(RotationDot));



    const float HandSpeed = HandDistance / TimeUntilContact;

    const float BladeSpeed = RotationDegrees / TimeUntilContact;



    if (HandSpeed > MaxParryHandSpeed || BladeSpeed > MaxParryBladeAngularSpeed)

    {

        ++LastDiagnostics.SpeedRejected;

        return false;

    }



    // Calculate tactical quality metrics

    const float CrossingQuality =

        1.f - FMath::Clamp(

            FMath::Abs(Crossing - PreferredIntersectionAngleDegrees) /

            FMath::Max(90.f - MinimumIntersectionAngleDegrees, 1.f), 0.f, 1.f);



    const float BladeCenter = 0.55f;

    const float BladeCenterQuality =

        1.f - FMath::Clamp(

            FMath::Abs(DefenderBladeFraction - BladeCenter) / 0.45f, 0.f, 1.f);





    // Reward a clearly visible arm action up to the preferred travel amount

    const float DramaticPoseQuality =

        FMath::Clamp(HandDistance / FMath::Max(PreferredVisibleHandTravel, 1.f), 0.f, 1.f);



    // Contact should happen on the dangerous outer portion of the enemy sword

    const float IncomingRange =

        FMath::Max(MaximumIncomingBladeFraction - MinimumIncomingBladeFraction, 0.01f);

    const float IncomingTipContactQuality =

        1.f - FMath::Clamp(

            FMath::Abs(IncomingBladeFraction - PreferredIncomingBladeFraction) /

            IncomingRange, 0.f, 1.f);



    // Raw tactical quality (before biomechanical comfort)

    const float TacticalQuality =

        CrossingQualityWeight * CrossingQuality +

        BladeCenterQualityWeight * BladeCenterQuality +

        DramaticPoseQualityWeight * DramaticPoseQuality +

        IncomingTipContactQualityWeight * IncomingTipContactQuality;



    // Calculate biomechanical comfort metrics

    const FVector NeutralForearmDir = CalculateNeutralForearmDirection(HandTransform);

    const float WristDeviation = CalculateMinimumWristDeviationAnalytical(

        ShoulderWorld, Hand, NeutralForearmDir, UpperArmLength, ForearmLength);







    Out.ContactPoint = IncomingContactWorld;

    Out.IncomingBase = IncomingBaseWorld;

    Out.IncomingTip = IncomingTipWorld;

    Out.DefenseBase = CandidateBase;

    Out.DefenseTip = CandidateTip;

    Out.RequiredWeaponTransform = WeaponTransform;

    Out.RequiredHandTransform = HandTransform;

    Out.RequiredHandPosition = Hand;

    Out.IncomingTime = IncomingTime;

    Out.TimeUntilContact = TimeUntilContact;

    Out.DefenderBladeFraction = DefenderBladeFraction;

    Out.IncomingBladeFraction = IncomingBladeFraction;

    Out.IntersectionAngleDegrees = Crossing;

    Out.RequiredHandSpeed = HandSpeed;

    Out.RequiredBladeAngularSpeed = BladeSpeed;

    Out.Quality = TacticalQuality; // Temporary - will be updated with policy

    Out.bValid = true;



    return true;

}



bool UIronboundParryComponent::FindBestParryCandidate(FIronboundParryCandidate& OutCandidate) const

{

    OutCandidate = FIronboundParryCandidate();

    LastDiagnostics = FIronboundParryDiagnostics();

    LastParryEarlyExitReason.Reset();



    if (!FighterMesh || !CombatFocus || !bObservedCommittedAttack ||

        !bAttackRecognized || !bReactionReady)

        return false;



    AActor* Target = CombatFocus->GetCombatTarget();

    if (!Target || ObservedAttacker.Get() != Target) return false;



    UIronboundCombatExecutionComponent* TargetExecution =

        Target->FindComponentByClass<UIronboundCombatExecutionComponent>();

    if (!TargetExecution || !TargetExecution->HasCommittedBladePath())

        return false;



    const UIronboundEquipmentComponent* Equipment =
        GetOwner()->FindComponentByClass<UIronboundEquipmentComponent>();
    const UStaticMeshComponent* Weapon = Equipment ? Equipment->GetWeapon() : nullptr;
    if (!Equipment || !Equipment->bReady || !Equipment->Definition || !Weapon)
    {
        LastParryEarlyExitReason = TEXT("defender weapon geometry unavailable");
        return false;
    }

    const FTransform CurrentWeaponTransform = Weapon->GetComponentTransform();
    const FVector CurrentDefenseBase =
        CurrentWeaponTransform.TransformPosition(Equipment->Definition->BladeBase);
    const FVector CurrentDefenseTip =
        CurrentWeaponTransform.TransformPosition(Equipment->Definition->BladeTip);
    const FVector CurrentDefenseDirection =
        (CurrentDefenseTip - CurrentDefenseBase).GetSafeNormal();
    const float BladeLength = FVector::Distance(CurrentDefenseBase, CurrentDefenseTip);
    if (CurrentDefenseDirection.IsNearlyZero() || BladeLength <= KINDA_SMALL_NUMBER)
    {
        LastParryEarlyExitReason = TEXT("defender blade geometry invalid");
        return false;
    }

    const float MinFraction = FMath::Clamp(MinParryBladeFraction, 0.f, 1.f);
    const float MaxFraction = FMath::Clamp(MaxParryBladeFraction, MinFraction, 1.f);

    const FVector Shoulder = FighterMesh->GetSocketLocation(UpperArmBone);



    const FBladeTrajectory& Trajectory = TargetExecution->GetCommittedTrajectory();

    const FTransform& AttackerTransform = TargetExecution->GetCommittedTransform();



    float CurrentSourceTime = 0.f;

    if (!GetIncomingSourcePlaybackTime(CurrentSourceTime))

        return false;



    bool bBodyAlreadyIntersected = false;

    const float BodyIntersectionTime =

        FindBodyIntersectionTime(

            FighterMesh,

            Trajectory,

            AttackerTransform,

            CurrentSourceTime,

            bBodyAlreadyIntersected);



    LastDiagnostics.FirstBodyIntersectionTime =

        BodyIntersectionTime < TNumericLimits<float>::Max()

            ? BodyIntersectionTime

            : -1.f;



    if (bBodyAlreadyIntersected)

    {

        LastParryEarlyExitReason =

            TEXT("incoming blade already intersected defender body");

        ++LastDiagnostics.TimingRejected;

        LogDiagnosticsOnce();

        return false;

    }



    const int32 IncomingContactCount = FMath::Max(2, IncomingBladeContactSamples);

    const int32 BladeFractionCount = FMath::Max(2, DefenderBladeFractionSamples);

    const int32 OrientationCount = FMath::Max(8, DefenseOrientationSamples);

    const int32 CrossingCount = FMath::Max(2, DefenseCrossingAngleSamples);



    TArray<FIronboundParryCandidate> Candidates;

    float BestTacticalQuality = -TNumericLimits<float>::Max();



    for (const FBladeSegment& Segment : Trajectory.Segments)

    {

        ++LastDiagnostics.IncomingSegments;



        const float TimeUntil = Segment.TimeSeconds - CurrentSourceTime;

        if (TimeUntil < MinimumTimeToContact)

        {

            ++LastDiagnostics.ExpiredSegments;

            continue;

        }



        // Never accept a parry after the incoming blade has already

        // entered the defender's torso/head envelope.

        if (Segment.TimeSeconds >=

            BodyIntersectionTime - BodySafetyMarginSeconds)

        {

            ++LastDiagnostics.TimingRejected;

            continue;

        }



        const FVector IncomingBase =

            AttackerTransform.TransformPosition(Segment.Base);

        const FVector IncomingTip =

            AttackerTransform.TransformPosition(Segment.Tip);

        const FVector IncomingDir =

            (IncomingTip - IncomingBase).GetSafeNormal();

        if (IncomingDir.IsNearlyZero()) continue;





        FVector BasisA = FVector::CrossProduct(IncomingDir, FVector::UpVector);

        if (BasisA.IsNearlyZero())

            BasisA = FVector::CrossProduct(IncomingDir, FVector::ForwardVector);

        BasisA.Normalize();

        const FVector BasisB =

            FVector::CrossProduct(IncomingDir, BasisA).GetSafeNormal();



        for (int32 ContactIndex = 0; ContactIndex < IncomingContactCount; ++ContactIndex)

        {

            const float ContactSampleAlpha =

                float(ContactIndex) / float(IncomingContactCount - 1);

            const float IncomingAlpha =

                FMath::Lerp(

                    FMath::Clamp(MinimumIncomingBladeFraction, 0.f, 1.f),

                    FMath::Clamp(MaximumIncomingBladeFraction,

                        MinimumIncomingBladeFraction, 1.f),

                    ContactSampleAlpha);

            const FVector Contact =

                FMath::Lerp(IncomingBase, IncomingTip, IncomingAlpha);

            ++LastDiagnostics.ContactPoints;



            const float MaxPossibleReach =

                ArmLength + BladeLength * MaxFraction;

            if (FVector::Distance(Shoulder, Contact) > MaxPossibleReach)

            {

                ++LastDiagnostics.BroadReachRejected;

                continue;

            }



            for (int32 FractionIndex = 0; FractionIndex < BladeFractionCount; ++FractionIndex)

            {

                const float FractionAlpha =

                    float(FractionIndex) / float(BladeFractionCount - 1);

                const float DefenderFraction =

                    FMath::Lerp(MinFraction, MaxFraction, FractionAlpha);



                for (int32 CrossingIndex = 0; CrossingIndex < CrossingCount; ++CrossingIndex)

                {

                    const float CrossingAlpha =

                        float(CrossingIndex) / float(CrossingCount - 1);

                    const float CrossingDeg =

                        FMath::Lerp(MinimumIntersectionAngleDegrees, 90.f, CrossingAlpha);

                    const float CrossingRad = FMath::DegreesToRadians(CrossingDeg);



                    for (int32 OrientationIndex = 0; OrientationIndex < OrientationCount; ++OrientationIndex)

                    {

                        const float Around =

                            2.f * PI * float(OrientationIndex) / float(OrientationCount);

                        const FVector Ring =

                            BasisA * FMath::Cos(Around) + BasisB * FMath::Sin(Around);



                        const FVector DefenseDirection =

                            (IncomingDir * FMath::Cos(CrossingRad) +

                             Ring * FMath::Sin(CrossingRad)).GetSafeNormal();



                        ++LastDiagnostics.GeneratedCandidates;



                        FIronboundParryCandidate Candidate;

                        if (!EvaluateCandidate(

                            Shoulder,

                            IncomingBase, IncomingTip, Contact, IncomingDir,

                            Segment.TimeSeconds, IncomingAlpha,

                            DefenseDirection, DefenderFraction,

                            BladeLength, CurrentWeaponTransform,

                            CurrentDefenseDirection, Candidate))

                            continue;



                        const float BodyTimingMargin =

                            FMath::Clamp(

                                (BodyIntersectionTime - Segment.TimeSeconds) / 0.75f,

                                0.f,

                                1.f);





                        const float CandidateBodyClearance =
                            FindMinimumBodyClearance(
                                FighterMesh, Candidate.DefenseBase, Candidate.DefenseTip);
                        const float CandidateBodyClearanceQuality =
                            FMath::Clamp(
                                CandidateBodyClearance /
                                    FMath::Max(BodyClearancePreferenceDistance, 1.f),
                                0.f, 1.f);

                        Candidate.BodyClearance = CandidateBodyClearance;
                        Candidate.Quality +=
                            2.f * BodyTimingMargin +
                            BodyClearanceQualityWeight * CandidateBodyClearanceQuality;



                        if (Candidate.Quality > BestTacticalQuality)

                        {

                            BestTacticalQuality = Candidate.Quality;

                        }



                        Candidates.Add(Candidate);
                        ++LastDiagnostics.ValidCandidates;

                    }

                }

            }

        }

    }



    if (Candidates.Num() == 0)

    {

        LastParryEarlyExitReason =

            TEXT("no anatomically useful intercept survived hard constraints");

        LogDiagnosticsOnce();

        return false;

    }



    // Apply policy-based selection

    FParryPolicy Policy;

    Policy.ParrySkill = ParrySkill;

    Policy.QualityBandWidth = QualityBandWidth;

    Policy.WristSharpness = WristSharpness;

    Policy.FreedomSharpness = FreedomSharpness;

    Policy.TacticalWeight = TacticalWeight;



    // Calculate final quality with policy

    for (auto& Candidate : Candidates)

    {

        const float TacticalMaxScore = FMath::Max(
            CrossingQualityWeight + BladeCenterQualityWeight + DramaticPoseQualityWeight +
            IncomingTipContactQualityWeight + BodyClearanceQualityWeight + 2.f,
            KINDA_SMALL_NUMBER);

        FCandidateRawMetrics RawMetrics = {

            FMath::Clamp(Candidate.Quality / TacticalMaxScore, 0.f, 1.f),

            0.f,

            0.f,

            true

        };



        // Calculate biomechanical metrics for this candidate

        const FVector NeutralForearmDir = CalculateNeutralForearmDirection(Candidate.RequiredHandTransform);

        const float WristDeviation = CalculateMinimumWristDeviationAnalytical(

            Shoulder, Candidate.RequiredHandPosition, NeutralForearmDir, UpperArmLength, ForearmLength);



        const FArmExtensionMetrics ExtensionMetrics = CalculateArmExtensionMetrics(

            Shoulder, Candidate.RequiredHandPosition, UpperArmLength, ForearmLength);



        RawMetrics.WristDeviationRadians = WristDeviation;

        RawMetrics.GeometricFreedom = ExtensionMetrics.GeometricFreedom;



        Candidate.Quality = ApplyPolicyToRawMetrics(RawMetrics, Policy);

    }



    // Select best candidate with skill-based variation

    const uint32 SelectionSeed = HashCombine(
        GetTypeHash(GetOwner()),
        GetTypeHash(Target));
    SelectComfortableCandidate(Candidates, OutCandidate, SelectionSeed, Policy);



    // Update diagnostics with best candidate info

    if (OutCandidate.bValid)

    {

        LastDiagnostics.BestTimeUntilContact = OutCandidate.TimeUntilContact;

        LastDiagnostics.BestRequiredHandSpeed = OutCandidate.RequiredHandSpeed;

        LastDiagnostics.BestRequiredBladeAngularSpeed = OutCandidate.RequiredBladeAngularSpeed;

        LastDiagnostics.BestBodyClearance = OutCandidate.BodyClearance;

        LastDiagnostics.BestQuality = OutCandidate.Quality;

    }



    LogDiagnosticsOnce();

    return OutCandidate.bValid;

}



void UIronboundParryComponent::LogDiagnosticsOnce() const

{

    if (!bObservedCommittedAttack || bDiagnosticsLoggedForObservedAttack) return;

    bDiagnosticsLoggedForObservedAttack = true;



    UE_LOG(LogTemp, Warning,

        TEXT("ParryDiag [%s] gen=%d valid=%d | reach=%d angle=%d anatomy=%d body=%d time=%d speed=%d | bestQ=%.2f t=%.3f hand=%.0f blade=%.0f | %s"),

        *GetNameSafe(GetOwner()),

        LastDiagnostics.GeneratedCandidates, LastDiagnostics.ValidCandidates,

        LastDiagnostics.BroadReachRejected, LastDiagnostics.AngleRejected,

        LastDiagnostics.AnatomyRejected, LastDiagnostics.BodyRejected,

        LastDiagnostics.TimingRejected, LastDiagnostics.SpeedRejected,

        LastDiagnostics.BestQuality, LastDiagnostics.BestTimeUntilContact,

        LastDiagnostics.BestRequiredHandSpeed,

        LastDiagnostics.BestRequiredBladeAngularSpeed,

        LastParryEarlyExitReason.IsEmpty() ? TEXT("candidate locked") : *LastParryEarlyExitReason);

}



void UIronboundParryComponent::DrawAnatomyDebug() const

{

    if (!FighterMesh) return;



    const FVector Shoulder = FighterMesh->GetSocketLocation(UpperArmBone);

    const FVector Elbow = FighterMesh->GetSocketLocation(LowerArmBone);

    const FVector Hand = FighterMesh->GetSocketLocation(HandBone);



    DrawDebugSphere(GetWorld(), Shoulder, 4.f, 10, FColor::Cyan, false, 0.f, 0, 1.f);

    DrawDebugSphere(GetWorld(), Elbow, 4.f, 10, FColor::Yellow, false, 0.f, 0, 1.f);

    DrawDebugSphere(GetWorld(), Hand, 4.f, 10, FColor::Green, false, 0.f, 0, 1.f);

}



void UIronboundParryComponent::DrawParrySolutionDebug() const

{

    if (!bHasActiveParryCandidate || !FighterMesh) return;



    const FIronboundParryCandidate& C = ActiveParryCandidate;



    DrawDebugLine(GetWorld(), C.IncomingBase, C.IncomingTip,

        FColor::Red, false, 0.f, 0, 6.f);

    DrawDebugLine(GetWorld(), C.DefenseBase, C.DefenseTip,

        FColor::Cyan, false, 0.f, 0, 6.f);

    DrawDebugSphere(GetWorld(), C.ContactPoint, 6.f, 12,

        FColor::Green, false, 0.f, 0, 2.f);



    // Locked final arm = orange. Executed/moving targets = purple/blue.

    const FVector Shoulder = FighterMesh->GetSocketLocation(UpperArmBone);

    DrawDebugLine(GetWorld(), Shoulder, C.RequiredHandPosition,

        FColor::Orange, false, 0.f, 0, 3.f);



    DrawDebugSphere(GetWorld(), ExecutedHandTransform.GetLocation(), 4.f, 10,

        FColor::Blue, false, 0.f, 0, 2.f);



    const FString Text = FString::Printf(

        TEXT("Q %.2f | cross %.0f | enemyBlade %.0f%% | t %.3f | %s"),

        C.Quality, C.IntersectionAngleDegrees, C.IncomingBladeFraction * 100.f,

        C.TimeUntilContact,

        ParryState == EIronboundParryState::Holding ? TEXT("HOLD") : TEXT("MOVE"));



    DrawDebugString(GetWorld(), C.ContactPoint + FVector(0.f, 0.f, 14.f),

        Text, nullptr, FColor::White, 0.f, true);

}



void UIronboundParryComponent::DrawDiagnosticsDebug() const

{

    if (!bObservedCommittedAttack || !GetWorld() || !GetOwner()) return;



    const FString Text = FString::Printf(

        TEXT("Parry gen=%d valid=%d | anatomy=%d body=%d speed=%d | Q=%.2f"),

        LastDiagnostics.GeneratedCandidates,

        LastDiagnostics.ValidCandidates,

        LastDiagnostics.AnatomyRejected,

        LastDiagnostics.BodyRejected,

        LastDiagnostics.SpeedRejected,

        LastDiagnostics.BestQuality);



    DrawDebugString(

        GetWorld(),

        GetOwner()->GetActorLocation() + FVector(0.f, 0.f, 220.f),

        Text, nullptr,

        LastDiagnostics.ValidCandidates > 0 ? FColor::Green : FColor::Red,

        0.f, true);

    }
