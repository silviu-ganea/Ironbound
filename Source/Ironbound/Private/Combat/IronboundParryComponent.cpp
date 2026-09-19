#include "Combat/IronboundParryComponent.h"

#include "Animation/Skeleton.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimCompositeBase.h"
#include "Combat/IronboundCombatExecutionComponent.h"
#include "Combat/IronboundCombatFocusComponent.h"
#include "Combat/IronboundEquipmentComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"

UIronboundParryComponent::UIronboundParryComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UIronboundParryComponent::BeginPlay()
{
    Super::BeginPlay();

    FighterMesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
    CombatFocus = GetOwner()->FindComponentByClass<UIronboundCombatFocusComponent>();

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
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Parry Anatomy [%s]: UpperArm=%.1f cm Forearm=%.1f cm TotalArm=%.1f cm"),
            *GetNameSafe(GetOwner()),
            UpperArmLength,
            ForearmLength,
            ArmLength);
    }
}

bool UIronboundParryComponent::CalculateArmDimensions()
{
    if (!FighterMesh || !FighterMesh->GetSkeletalMeshAsset())
    {
        return false;
    }

    const int32 UpperArmIndex = FighterMesh->GetBoneIndex(UpperArmBone);
    const int32 LowerArmIndex = FighterMesh->GetBoneIndex(LowerArmBone);
    const int32 HandIndex = FighterMesh->GetBoneIndex(HandBone);

    if (UpperArmIndex == INDEX_NONE || LowerArmIndex == INDEX_NONE || HandIndex == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("Parry: %s is missing one or more arm bones"), *GetNameSafe(GetOwner()));
        return false;
    }

    const FReferenceSkeleton& RefSkeleton = FighterMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

    auto GetRefPoseComponentTransform =
        [&RefSkeleton, &RefPose](int32 BoneIndex)
        {
            FTransform ComponentTransform = RefPose[BoneIndex];
            int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);

            while (ParentIndex != INDEX_NONE)
            {
                ComponentTransform *= RefPose[ParentIndex];
                ParentIndex = RefSkeleton.GetParentIndex(ParentIndex);
            }

            return ComponentTransform;
        };

    const FVector Shoulder = GetRefPoseComponentTransform(UpperArmIndex).GetTranslation();
    const FVector Elbow = GetRefPoseComponentTransform(LowerArmIndex).GetTranslation();
    const FVector Hand = GetRefPoseComponentTransform(HandIndex).GetTranslation();

    UpperArmLength = FVector::Distance(Shoulder, Elbow);
    ForearmLength = FVector::Distance(Elbow, Hand);
    ArmLength = UpperArmLength + ForearmLength;

    return true;
}

void UIronboundParryComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    UpdateAttackTimingState();

    if (!bDrawParryDebug)
    {
        return;
    }

    DrawAnatomyDebug();
    DrawParrySolutionDebug();
    DrawDiagnosticsDebug();
}

void UIronboundParryComponent::UpdateAttackTimingState()
{
    AActor* Target = CombatFocus ? CombatFocus->GetCombatTarget() : nullptr;

    UIronboundCombatExecutionComponent* TargetExecution =
        Target ? Target->FindComponentByClass<UIronboundCombatExecutionComponent>() : nullptr;

    const bool bCommitted =
        TargetExecution &&
        TargetExecution->HasCommittedBladePath();

    if (!bCommitted)
    {
        bObservedCommittedAttack = false;
        bAttackRecognized = false;
        bReactionReady = false;
        RecognitionWorldTime = 0.f;
        ObservedAttacker.Reset();
        bDiagnosticsLoggedForObservedAttack = false;
        LastParryEarlyExitReason.Reset();
        return;
    }

    if (!bObservedCommittedAttack || ObservedAttacker.Get() != Target)
    {
        bObservedCommittedAttack = true;
        bAttackRecognized = false;
        bReactionReady = false;
        ObservedAttacker = Target;
        RecognitionWorldTime = 0.f;

        bDiagnosticsLoggedForObservedAttack = false;
        LastParryEarlyExitReason.Reset();

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("ParryDiag [%s]: observed committed attack from %s | waiting for recognizable blade motion"),
            *GetNameSafe(GetOwner()),
            *GetNameSafe(Target));

        return;
    }

    /*
     * Recognition is automatic: the trajectory analyzer already derives
     * ActiveStartTime from the contiguous high-speed blade movement around
     * the strike peak. Until the attacker's playback reaches that point, the
     * defender does not get perfect knowledge of the future attack path.
     */
    if (!bAttackRecognized)
    {
        float CurrentSourceTime = 0.f;

        if (!GetIncomingSourcePlaybackTime(CurrentSourceTime))
        {
            return;
        }

        const FBladeTrajectory& IncomingTrajectory =
            TargetExecution->GetCommittedTrajectory();

        if (!IncomingTrajectory.bValid)
        {
            return;
        }

        if (CurrentSourceTime + KINDA_SMALL_NUMBER <
            IncomingTrajectory.ActiveStartTime)
        {
            return;
        }

        bAttackRecognized = true;
        RecognitionWorldTime =
            GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "ParryDiag [%s]: attack recognized | source %.3fs | "
                "recognition threshold %.3fs | reaction delay %.3fs"),
            *GetNameSafe(GetOwner()),
            CurrentSourceTime,
            IncomingTrajectory.ActiveStartTime,
            ReactionDelaySeconds);

        return;
    }

    if (!bReactionReady && GetWorld())
    {
        const float ElapsedSinceRecognition =
            GetWorld()->GetTimeSeconds() - RecognitionWorldTime;

        if (ElapsedSinceRecognition >= ReactionDelaySeconds)
        {
            bReactionReady = true;

            float SourceTime = 0.f;
            const bool bHasSourceTime =
                GetIncomingSourcePlaybackTime(SourceTime);

            UE_LOG(
                LogTemp,
                Warning,
                TEXT(
                    "ParryDiag [%s]: reaction ready %.3fs after recognition | "
                    "attacker source time %s"),
                *GetNameSafe(GetOwner()),
                ElapsedSinceRecognition,
                bHasSourceTime
                    ? *FString::Printf(TEXT("%.3fs"), SourceTime)
                    : TEXT("unavailable"));
        }
    }
}

bool UIronboundParryComponent::GetIncomingSourcePlaybackTime(float& OutSourceTime) const
{
    OutSourceTime = 0.f;

    if (!bObservedCommittedAttack)
    {
        return false;
    }

    AActor* Attacker = ObservedAttacker.Get();
    if (!Attacker)
    {
        return false;
    }

    UIronboundEquipmentComponent* AttackerEquipment =
        Attacker->FindComponentByClass<UIronboundEquipmentComponent>();

    USkeletalMeshComponent* AttackerMesh =
        AttackerEquipment ? AttackerEquipment->GetFighterMesh() : nullptr;

    UAnimInstance* AnimInstance =
        AttackerMesh ? AttackerMesh->GetAnimInstance() : nullptr;

    UAnimMontage* ActiveMontage =
        AnimInstance ? AnimInstance->GetCurrentActiveMontage() : nullptr;

    if (!AnimInstance || !ActiveMontage)
    {
        return false;
    }

    const float MontagePosition =
        AnimInstance->Montage_GetPosition(ActiveMontage);

    // Convert montage-track position back into the source animation time.
    // This respects montage segment offsets and segment play-rate scaling,
    // so it matches FBladeSegment::TimeSeconds.
    for (const FSlotAnimationTrack& SlotTrack : ActiveMontage->SlotAnimTracks)
    {
        for (const FAnimSegment& Segment : SlotTrack.AnimTrack.AnimSegments)
        {
            const float SegmentStart = Segment.StartPos;
            const float SegmentEnd = SegmentStart + Segment.GetLength();

            if (MontagePosition + KINDA_SMALL_NUMBER < SegmentStart ||
                MontagePosition - KINDA_SMALL_NUMBER > SegmentEnd)
            {
                continue;
            }

            OutSourceTime =
                Segment.ConvertTrackPosToAnimPos(MontagePosition);

            return true;
        }
    }

    return false;
}

float UIronboundParryComponent::GetTimeUntilIncomingSample(float IncomingTime) const
{
    float CurrentSourceTime = 0.f;

    if (!GetIncomingSourcePlaybackTime(CurrentSourceTime))
    {
        return -1.f;
    }

    // ReactionDelaySeconds has already elapsed before solving begins.
    // Remaining time is therefore purely trajectory time minus the attacker's
    // current source-animation time.
    return IncomingTime - CurrentSourceTime;
}

void UIronboundParryComponent::DrawAnatomyDebug() const
{
    if (!FighterMesh)
    {
        return;
    }

    if (FighterMesh->GetBoneIndex(UpperArmBone) == INDEX_NONE ||
        FighterMesh->GetBoneIndex(LowerArmBone) == INDEX_NONE ||
        FighterMesh->GetBoneIndex(HandBone) == INDEX_NONE)
    {
        return;
    }

    const FVector Shoulder = FighterMesh->GetSocketLocation(UpperArmBone);
    const FVector Elbow = FighterMesh->GetSocketLocation(LowerArmBone);
    const FVector Hand = FighterMesh->GetSocketLocation(HandBone);

    DrawDebugSphere(GetWorld(), Shoulder, 5.f, 12, FColor::Cyan, false, 0.f, 0, 1.5f);
    DrawDebugSphere(GetWorld(), Elbow, 4.f, 12, FColor::Yellow, false, 0.f, 0, 1.5f);
    DrawDebugSphere(GetWorld(), Hand, 4.f, 12, FColor::Green, false, 0.f, 0, 1.5f);

    DrawDebugLine(GetWorld(), Shoulder, Elbow, FColor::Cyan, false, 0.f, 0, 2.f);
    DrawDebugLine(GetWorld(), Elbow, Hand, FColor::Green, false, 0.f, 0, 2.f);
}

bool UIronboundParryComponent::GetCurrentWeaponGeometry(
    FVector& OutCurrentBaseWorld,
    FVector& OutCurrentTipWorld,
    float& OutBladeLength,
    float& OutMinFraction,
    float& OutMaxFraction) const
{
    OutCurrentBaseWorld = FVector::ZeroVector;
    OutCurrentTipWorld = FVector::ZeroVector;
    OutBladeLength = 0.f;
    OutMinFraction = 0.f;
    OutMaxFraction = 0.f;

    const UIronboundEquipmentComponent* Equipment =
        GetOwner()->FindComponentByClass<UIronboundEquipmentComponent>();

    if (!Equipment || !Equipment->bReady || !Equipment->Definition)
    {
        return false;
    }

    UStaticMeshComponent* Weapon = Equipment->GetWeapon();
    if (!Weapon)
    {
        return false;
    }

    const FVector BladeBaseLocal = Equipment->Definition->BladeBase;
    const FVector BladeTipLocal = Equipment->Definition->BladeTip;

    OutBladeLength = FVector::Distance(BladeBaseLocal, BladeTipLocal);
    if (OutBladeLength <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    OutCurrentBaseWorld =
        Weapon->GetComponentTransform().TransformPosition(BladeBaseLocal);

    OutCurrentTipWorld =
        Weapon->GetComponentTransform().TransformPosition(BladeTipLocal);

    OutMinFraction = FMath::Clamp(MinParryBladeFraction, 0.f, 1.f);
    OutMaxFraction = FMath::Clamp(MaxParryBladeFraction, OutMinFraction, 1.f);

    return true;
}

bool UIronboundParryComponent::FindBestElbowPosition(
    const FVector& ShoulderWorld,
    const FVector& RequiredHandWorld,
    const FVector& CurrentElbowWorld,
    FVector& OutElbowWorld,
    float& OutElbowMovementCost) const
{
    OutElbowWorld = FVector::ZeroVector;
    OutElbowMovementCost = TNumericLimits<float>::Max();

    if (UpperArmLength <= KINDA_SMALL_NUMBER || ForearmLength <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const FVector ShoulderToHand = RequiredHandWorld - ShoulderWorld;
    const float ShoulderToHandDistance = ShoulderToHand.Size();

    if (ShoulderToHandDistance <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const float MaximumReach = UpperArmLength + ForearmLength - ArmReachMargin;
    const float MinimumReach = FMath::Abs(UpperArmLength - ForearmLength) + ArmReachMargin;

    if (ShoulderToHandDistance > MaximumReach || ShoulderToHandDistance < MinimumReach)
    {
        return false;
    }

    const FVector Axis = ShoulderToHand / ShoulderToHandDistance;
    const float DistanceSquared = ShoulderToHandDistance * ShoulderToHandDistance;
    const float UpperArmSquared = UpperArmLength * UpperArmLength;
    const float ForearmSquared = ForearmLength * ForearmLength;

    const float AlongAxis =
        (UpperArmSquared - ForearmSquared + DistanceSquared) /
        (2.f * ShoulderToHandDistance);

    float CircleRadiusSquared = UpperArmSquared - AlongAxis * AlongAxis;

    if (CircleRadiusSquared < -KINDA_SMALL_NUMBER)
    {
        return false;
    }

    CircleRadiusSquared = FMath::Max(0.f, CircleRadiusSquared);

    const float CircleRadius = FMath::Sqrt(CircleRadiusSquared);
    const FVector CircleCenter = ShoulderWorld + Axis * AlongAxis;

    FVector BasisA = FVector::CrossProduct(Axis, FVector::UpVector);
    if (BasisA.IsNearlyZero())
    {
        BasisA = FVector::CrossProduct(Axis, FVector::ForwardVector);
    }
    BasisA.Normalize();

    const FVector BasisB = FVector::CrossProduct(Axis, BasisA).GetSafeNormal();

    if (BasisA.IsNearlyZero() || BasisB.IsNearlyZero())
    {
        return false;
    }

    const FVector CurrentShoulderToElbow = CurrentElbowWorld - ShoulderWorld;

    FVector CurrentElbowSide =
        CurrentShoulderToElbow -
        Axis * FVector::DotProduct(CurrentShoulderToElbow, Axis);

    if (CurrentElbowSide.IsNearlyZero())
    {
        const FVector OwnerRight = GetOwner()->GetActorRightVector();

        CurrentElbowSide =
            OwnerRight -
            Axis * FVector::DotProduct(OwnerRight, Axis);
    }

    if (CurrentElbowSide.IsNearlyZero())
    {
        return false;
    }

    CurrentElbowSide.Normalize();

    const int32 SampleCount = FMath::Max(4, ElbowCircleSamples);
    bool bFoundElbow = false;

    for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
    {
        const float Angle =
            2.f * PI *
            static_cast<float>(SampleIndex) /
            static_cast<float>(SampleCount);

        const FVector RadialDirection =
            (BasisA * FMath::Cos(Angle) + BasisB * FMath::Sin(Angle)).GetSafeNormal();

        if (RadialDirection.IsNearlyZero())
        {
            continue;
        }

        const float ElbowSideDot =
            FVector::DotProduct(RadialDirection, CurrentElbowSide);

        if (ElbowSideDot < MinimumElbowSideDot)
        {
            continue;
        }

        const FVector CandidateElbow =
            CircleCenter + RadialDirection * CircleRadius;

        const float CandidateUpperArmLength =
            FVector::Distance(ShoulderWorld, CandidateElbow);

        const float CandidateForearmLength =
            FVector::Distance(CandidateElbow, RequiredHandWorld);

        if (!FMath::IsNearlyEqual(CandidateUpperArmLength, UpperArmLength, 0.1f) ||
            !FMath::IsNearlyEqual(CandidateForearmLength, ForearmLength, 0.1f))
        {
            continue;
        }

        const float ElbowMovement =
            FVector::Distance(CurrentElbowWorld, CandidateElbow);

        if (!bFoundElbow || ElbowMovement < OutElbowMovementCost)
        {
            bFoundElbow = true;
            OutElbowWorld = CandidateElbow;
            OutElbowMovementCost = ElbowMovement;
        }
    }

    return bFoundElbow;
}

bool UIronboundParryComponent::EvaluateCandidate(
    const FVector& ShoulderWorld,
    const FVector& CurrentElbowWorld,
    const FVector& IncomingBaseWorld,
    const FVector& IncomingTipWorld,
    const FVector& IncomingContactWorld,
    const FVector& IncomingDirection,
    float IncomingTime,
    const FVector& DefenseDirection,
    float DefenderBladeFraction,
    float BladeLength,
    const FVector& CurrentDefenseBase,
    const FVector& CurrentDefenseDirection,
    FIronboundParryCandidate& OutCandidate) const
{
    if (IncomingDirection.IsNearlyZero() || DefenseDirection.IsNearlyZero())
    {
        return false;
    }

    const FVector NormalizedDefenseDirection = DefenseDirection.GetSafeNormal();

    const float AbsDot =
        FMath::Clamp(
            FMath::Abs(FVector::DotProduct(IncomingDirection, NormalizedDefenseDirection)),
            0.f,
            1.f);

    const float IntersectionAngleDegrees =
        FMath::RadiansToDegrees(FMath::Acos(AbsDot));

    if (IntersectionAngleDegrees < MinimumIntersectionAngleDegrees)
    {
        ++LastDiagnostics.AngleRejected;
        return false;
    }

    const float ContactDistanceAlongDefenseBlade =
        BladeLength * DefenderBladeFraction;

    const FVector CandidateBase =
        IncomingContactWorld -
        NormalizedDefenseDirection * ContactDistanceAlongDefenseBlade;

    const FVector CandidateTip =
        CandidateBase + NormalizedDefenseDirection * BladeLength;

    FVector CandidateElbow;
    float ElbowMovementCost = 0.f;

    if (!FindBestElbowPosition(
            ShoulderWorld,
            CandidateBase,
            CurrentElbowWorld,
            CandidateElbow,
            ElbowMovementCost))
    {
        ++LastDiagnostics.ElbowRejected;
        return false;
    }

    const float TranslationCost =
        FVector::Distance(CurrentDefenseBase, CandidateBase);

    const float RotationAbsDot =
        FMath::Clamp(
            FMath::Abs(
                FVector::DotProduct(
                    CurrentDefenseDirection,
                    NormalizedDefenseDirection)),
            0.f,
            1.f);

    const float RotationDegrees =
        FMath::RadiansToDegrees(FMath::Acos(RotationAbsDot));

    // ------------------------------------------------------------
    // Runtime timing feasibility.
    // ------------------------------------------------------------

    const float TimeUntilContact =
        GetTimeUntilIncomingSample(IncomingTime);

    if (TimeUntilContact < MinimumTimeToContact)
    {
        ++LastDiagnostics.TimingRejected;
        return false;
    }

    const float RequiredHandSpeed =
        TranslationCost / TimeUntilContact;

    const float RequiredBladeAngularSpeed =
        RotationDegrees / TimeUntilContact;

    if (RequiredHandSpeed > MaxParryHandSpeed ||
        RequiredBladeAngularSpeed > MaxParryBladeAngularSpeed)
    {
        ++LastDiagnostics.SpeedRejected;
        return false;
    }

    const float MovementCost =
        TranslationCost * TranslationCostWeight +
        RotationDegrees * RotationCostWeight +
        ElbowMovementCost * ElbowMovementCostWeight;

    const float HandSpeedUtilization =
        MaxParryHandSpeed > KINDA_SMALL_NUMBER
            ? RequiredHandSpeed / MaxParryHandSpeed
            : 1.f;

    const float BladeSpeedUtilization =
        MaxParryBladeAngularSpeed > KINDA_SMALL_NUMBER
            ? RequiredBladeAngularSpeed / MaxParryBladeAngularSpeed
            : 1.f;

    /*
     * The harder of hand translation and blade rotation determines timing
     * pressure. Because required speed already equals movement / available
     * time, this directly rewards useful time margin while still allowing a
     * later interception when it is substantially easier geometrically.
     */
    const float TimingPressure =
        FMath::Max(HandSpeedUtilization, BladeSpeedUtilization);

    const float SelectionCost =
        MovementCost +
        TimingPressureCostWeight *
            FMath::Square(TimingPressure);

    OutCandidate.ContactPoint = IncomingContactWorld;
    OutCandidate.IncomingBase = IncomingBaseWorld;
    OutCandidate.IncomingTip = IncomingTipWorld;
    OutCandidate.DefenseBase = CandidateBase;
    OutCandidate.DefenseTip = CandidateTip;
    OutCandidate.RequiredHandPosition = CandidateBase;
    OutCandidate.RequiredElbowPosition = CandidateElbow;
    OutCandidate.IncomingTime = IncomingTime;
    OutCandidate.TimeUntilContact = TimeUntilContact;
    OutCandidate.DefenderBladeFraction = DefenderBladeFraction;
    OutCandidate.IntersectionAngleDegrees = IntersectionAngleDegrees;
    OutCandidate.RequiredHandSpeed = RequiredHandSpeed;
    OutCandidate.RequiredBladeAngularSpeed = RequiredBladeAngularSpeed;
    OutCandidate.MovementCost = MovementCost;
    OutCandidate.SelectionCost = SelectionCost;
    OutCandidate.bValid = true;

    ++LastDiagnostics.ValidCandidates;

    return true;
}

bool UIronboundParryComponent::FindBestParryCandidate(
    FIronboundParryCandidate& OutCandidate) const
{
    OutCandidate = FIronboundParryCandidate();
    LastDiagnostics = FIronboundParryDiagnostics();
    LastParryEarlyExitReason.Reset();

    if (!FighterMesh)
    {
        LastParryEarlyExitReason = TEXT("no FighterMesh");
        LogDiagnosticsOnce();
        return false;
    }

    if (!CombatFocus)
    {
        LastParryEarlyExitReason = TEXT("no CombatFocus");
        LogDiagnosticsOnce();
        return false;
    }

    if (!bObservedCommittedAttack)
    {
        LastParryEarlyExitReason = TEXT("no observed committed attack");
        LogDiagnosticsOnce();
        return false;
    }

    // No trajectory solving before the blade motion is recognizable, and no
    // solving during the defender's real reaction delay.
    if (!bAttackRecognized || !bReactionReady)
    {
        return false;
    }

    if (UpperArmLength <= KINDA_SMALL_NUMBER ||
        ForearmLength <= KINDA_SMALL_NUMBER)
    {
        LastParryEarlyExitReason = TEXT("invalid arm dimensions");
        LogDiagnosticsOnce();
        return false;
    }

    AActor* Target = CombatFocus->GetCombatTarget();
    if (!Target)
    {
        LastParryEarlyExitReason = TEXT("CombatFocus has no target");
        LogDiagnosticsOnce();
        return false;
    }

    if (ObservedAttacker.Get() != Target)
    {
        LastParryEarlyExitReason = TEXT("observed attacker differs from CombatFocus target");
        LogDiagnosticsOnce();
        return false;
    }

    UIronboundCombatExecutionComponent* TargetExecution =
        Target->FindComponentByClass<UIronboundCombatExecutionComponent>();

    if (!TargetExecution)
    {
        LastParryEarlyExitReason = TEXT("target has no CombatExecution");
        LogDiagnosticsOnce();
        return false;
    }

    if (!TargetExecution->HasCommittedBladePath())
    {
        LastParryEarlyExitReason = TEXT("target committed blade path unavailable");
        LogDiagnosticsOnce();
        return false;
    }

    FVector CurrentDefenseBase;
    FVector CurrentDefenseTip;
    float BladeLength = 0.f;
    float MinBladeFraction = 0.f;
    float MaxBladeFraction = 0.f;

    if (!GetCurrentWeaponGeometry(
            CurrentDefenseBase,
            CurrentDefenseTip,
            BladeLength,
            MinBladeFraction,
            MaxBladeFraction))
    {
        LastParryEarlyExitReason = TEXT("defender weapon geometry unavailable");
        LogDiagnosticsOnce();
        return false;
    }

    const FVector CurrentDefenseDirection =
        (CurrentDefenseTip - CurrentDefenseBase).GetSafeNormal();

    if (CurrentDefenseDirection.IsNearlyZero())
    {
        LastParryEarlyExitReason = TEXT("defender blade direction is zero");
        LogDiagnosticsOnce();
        return false;
    }

    if (FighterMesh->GetBoneIndex(UpperArmBone) == INDEX_NONE ||
        FighterMesh->GetBoneIndex(LowerArmBone) == INDEX_NONE)
    {
        LastParryEarlyExitReason = TEXT("required arm bones unavailable");
        LogDiagnosticsOnce();
        return false;
    }

    const FVector ShoulderWorld =
        FighterMesh->GetSocketLocation(UpperArmBone);

    const FVector CurrentElbowWorld =
        FighterMesh->GetSocketLocation(LowerArmBone);

    const FBladeTrajectory& IncomingTrajectory =
        TargetExecution->GetCommittedTrajectory();

    const FTransform& AttackerCommittedTransform =
        TargetExecution->GetCommittedTransform();

    float CurrentIncomingSourceTime = 0.f;
    if (!GetIncomingSourcePlaybackTime(CurrentIncomingSourceTime))
    {
        // Commit may become visible immediately before Blueprint starts the
        // montage. Wait for the actual animation clock instead of treating
        // commit observation as source-animation time zero.
        LastParryEarlyExitReason = TEXT("waiting for attacker montage playback");
        return false;
    }

    if (!IncomingTrajectory.bValid || IncomingTrajectory.Segments.IsEmpty())
    {
        LastParryEarlyExitReason = TEXT("incoming committed trajectory invalid or empty");
        LogDiagnosticsOnce();
        return false;
    }

    const int32 IncomingContactCount = FMath::Max(2, IncomingBladeContactSamples);
    const int32 BladeFractionCount = FMath::Max(2, DefenderBladeFractionSamples);
    const int32 OrientationCount = FMath::Max(4, DefenseOrientationSamples);
    const int32 CrossingAngleCount = FMath::Max(2, DefenseCrossingAngleSamples);

    const float MinimumCrossingAngle =
        FMath::Clamp(MinimumIntersectionAngleDegrees, 0.f, 90.f);

    float BestCost = TNumericLimits<float>::Max();

    for (const FBladeSegment& IncomingSegment : IncomingTrajectory.Segments)
    {
        ++LastDiagnostics.IncomingSegments;

        // Cheap timing broad-phase. The reaction delay has already elapsed;
        // only trajectory samples still ahead of the attacker's actual current
        // source-animation time are eligible.
        const float SegmentTimeUntilContact =
            IncomingSegment.TimeSeconds -
            CurrentIncomingSourceTime;

        if (SegmentTimeUntilContact < MinimumTimeToContact)
        {
            ++LastDiagnostics.ExpiredSegments;
            continue;
        }

        const FVector IncomingBaseWorld =
            AttackerCommittedTransform.TransformPosition(IncomingSegment.Base);

        const FVector IncomingTipWorld =
            AttackerCommittedTransform.TransformPosition(IncomingSegment.Tip);

        const FVector IncomingDirection =
            (IncomingTipWorld - IncomingBaseWorld).GetSafeNormal();

        if (IncomingDirection.IsNearlyZero())
        {
            continue;
        }

        FVector BasisA =
            FVector::CrossProduct(IncomingDirection, FVector::UpVector);

        if (BasisA.IsNearlyZero())
        {
            BasisA =
                FVector::CrossProduct(IncomingDirection, FVector::ForwardVector);
        }

        BasisA.Normalize();

        const FVector BasisB =
            FVector::CrossProduct(IncomingDirection, BasisA).GetSafeNormal();

        for (int32 ContactIndex = 0; ContactIndex < IncomingContactCount; ++ContactIndex)
        {
            const float IncomingAlpha =
                static_cast<float>(ContactIndex) /
                static_cast<float>(IncomingContactCount - 1);

            const FVector IncomingContactWorld =
                FMath::Lerp(IncomingBaseWorld, IncomingTipWorld, IncomingAlpha);

            ++LastDiagnostics.ContactPoints;

            const float MaximumPossibleReach =
                ArmLength + BladeLength * MaxBladeFraction;

            if (FVector::Distance(ShoulderWorld, IncomingContactWorld) > MaximumPossibleReach)
            {
                ++LastDiagnostics.BroadReachRejected;
                continue;
            }

            for (int32 FractionIndex = 0; FractionIndex < BladeFractionCount; ++FractionIndex)
            {
                const float FractionAlpha =
                    static_cast<float>(FractionIndex) /
                    static_cast<float>(BladeFractionCount - 1);

                const float DefenderBladeFraction =
                    FMath::Lerp(
                        MinBladeFraction,
                        MaxBladeFraction,
                        FractionAlpha);

                for (int32 CrossingAngleIndex = 0;
                     CrossingAngleIndex < CrossingAngleCount;
                     ++CrossingAngleIndex)
                {
                    const float CrossingAlpha =
                        static_cast<float>(CrossingAngleIndex) /
                        static_cast<float>(CrossingAngleCount - 1);

                    const float CrossingAngleDegrees =
                        FMath::Lerp(MinimumCrossingAngle, 90.f, CrossingAlpha);

                    const float CrossingAngleRadians =
                        FMath::DegreesToRadians(CrossingAngleDegrees);

                    const float ParallelAmount =
                        FMath::Cos(CrossingAngleRadians);

                    const float PerpendicularAmount =
                        FMath::Sin(CrossingAngleRadians);

                    for (int32 OrientationIndex = 0;
                         OrientationIndex < OrientationCount;
                         ++OrientationIndex)
                    {
                        const float AroundAngle =
                            2.f * PI *
                            static_cast<float>(OrientationIndex) /
                            static_cast<float>(OrientationCount);

                        const FVector RingDirection =
                            BasisA * FMath::Cos(AroundAngle) +
                            BasisB * FMath::Sin(AroundAngle);

                        const FVector DefenseDirection =
                            (IncomingDirection * ParallelAmount +
                             RingDirection * PerpendicularAmount).GetSafeNormal();

                        if (DefenseDirection.IsNearlyZero())
                        {
                            continue;
                        }

                        FIronboundParryCandidate Candidate;
                        ++LastDiagnostics.GeneratedCandidates;

                        if (!EvaluateCandidate(
                                ShoulderWorld,
                                CurrentElbowWorld,
                                IncomingBaseWorld,
                                IncomingTipWorld,
                                IncomingContactWorld,
                                IncomingDirection,
                                IncomingSegment.TimeSeconds,
                                DefenseDirection,
                                DefenderBladeFraction,
                                BladeLength,
                                CurrentDefenseBase,
                                CurrentDefenseDirection,
                                Candidate))
                        {
                            continue;
                        }

                        if (Candidate.SelectionCost < BestCost)
                        {
                            BestCost = Candidate.SelectionCost;
                            OutCandidate = Candidate;

                            LastDiagnostics.BestTimeUntilContact =
                                Candidate.TimeUntilContact;
                            LastDiagnostics.BestRequiredHandSpeed =
                                Candidate.RequiredHandSpeed;
                            LastDiagnostics.BestRequiredBladeAngularSpeed =
                                Candidate.RequiredBladeAngularSpeed;
                        }
                    }
                }
            }
        }
    }

    if (!OutCandidate.bValid)
    {
        LastParryEarlyExitReason = TEXT("solver evaluated trajectory but found no valid candidate");
    }

    LogDiagnosticsOnce();
    return OutCandidate.bValid;
}

void UIronboundParryComponent::LogDiagnosticsOnce() const
{
    // Diagnostics are per observed committed attack. Stay silent while idle.
    if (!bObservedCommittedAttack || bDiagnosticsLoggedForObservedAttack)
    {
        return;
    }

    bDiagnosticsLoggedForObservedAttack = true;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("ParryDiag [%s] attacker=%s | reason=%s | seg=%d expired=%d contact=%d reachFail=%d gen=%d angleFail=%d elbowFail=%d timeFail=%d speedFail=%d valid=%d | best t=%.3f hand=%.0f blade=%.0f"),
        *GetNameSafe(GetOwner()),
        *GetNameSafe(ObservedAttacker.Get()),
        LastParryEarlyExitReason.IsEmpty() ? TEXT("valid candidate found") : *LastParryEarlyExitReason,
        LastDiagnostics.IncomingSegments,
        LastDiagnostics.ExpiredSegments,
        LastDiagnostics.ContactPoints,
        LastDiagnostics.BroadReachRejected,
        LastDiagnostics.GeneratedCandidates,
        LastDiagnostics.AngleRejected,
        LastDiagnostics.ElbowRejected,
        LastDiagnostics.TimingRejected,
        LastDiagnostics.SpeedRejected,
        LastDiagnostics.ValidCandidates,
        LastDiagnostics.BestTimeUntilContact,
        LastDiagnostics.BestRequiredHandSpeed,
        LastDiagnostics.BestRequiredBladeAngularSpeed);
}

void UIronboundParryComponent::DrawDiagnosticsDebug() const
{
    if (!bObservedCommittedAttack || !GetWorld() || !GetOwner())
    {
        return;
    }

    const FString Summary =
        FString::Printf(
            TEXT("ParryDiag seg=%d expired=%d contact=%d reachFail=%d gen=%d angleFail=%d elbowFail=%d timeFail=%d speedFail=%d valid=%d | best t=%.3f hand=%.0f blade=%.0f"),
            LastDiagnostics.IncomingSegments,
            LastDiagnostics.ExpiredSegments,
            LastDiagnostics.ContactPoints,
            LastDiagnostics.BroadReachRejected,
            LastDiagnostics.GeneratedCandidates,
            LastDiagnostics.AngleRejected,
            LastDiagnostics.ElbowRejected,
            LastDiagnostics.TimingRejected,
            LastDiagnostics.SpeedRejected,
            LastDiagnostics.ValidCandidates,
            LastDiagnostics.BestTimeUntilContact,
            LastDiagnostics.BestRequiredHandSpeed,
            LastDiagnostics.BestRequiredBladeAngularSpeed);

    DrawDebugString(
        GetWorld(),
        GetOwner()->GetActorLocation() + FVector(0.f, 0.f, 220.f),
        Summary,
        nullptr,
        LastDiagnostics.ValidCandidates > 0 ? FColor::Green : FColor::Red,
        0.f,
        true);
}

void UIronboundParryComponent::DrawParrySolutionDebug() const
{
    FIronboundParryCandidate BestCandidate;

    if (!FindBestParryCandidate(BestCandidate))
    {
        return;
    }

    DrawDebugLine(
        GetWorld(),
        BestCandidate.IncomingBase,
        BestCandidate.IncomingTip,
        FColor::Red,
        false,
        0.f,
        0,
        7.f);

    DrawDebugSphere(
        GetWorld(),
        BestCandidate.IncomingBase,
        3.f,
        8,
        FColor::Red,
        false,
        0.f,
        0,
        1.f);

    DrawDebugSphere(
        GetWorld(),
        BestCandidate.IncomingTip,
        3.f,
        8,
        FColor::Red,
        false,
        0.f,
        0,
        1.f);

    DrawDebugLine(
        GetWorld(),
        BestCandidate.DefenseBase,
        BestCandidate.DefenseTip,
        FColor::Cyan,
        false,
        0.f,
        0,
        7.f);

    DrawDebugSphere(
        GetWorld(),
        BestCandidate.ContactPoint,
        7.f,
        16,
        FColor::Green,
        false,
        0.f,
        0,
        3.f);

    DrawDebugSphere(
        GetWorld(),
        BestCandidate.RequiredHandPosition,
        5.f,
        12,
        FColor::Magenta,
        false,
        0.f,
        0,
        2.f);

    DrawDebugSphere(
        GetWorld(),
        BestCandidate.RequiredElbowPosition,
        5.f,
        12,
        FColor::Orange,
        false,
        0.f,
        0,
        2.5f);

    const FVector ShoulderWorld =
        FighterMesh->GetSocketLocation(UpperArmBone);

    DrawDebugLine(
        GetWorld(),
        ShoulderWorld,
        BestCandidate.RequiredElbowPosition,
        FColor::Orange,
        false,
        0.f,
        0,
        4.f);

    DrawDebugLine(
        GetWorld(),
        BestCandidate.RequiredElbowPosition,
        BestCandidate.RequiredHandPosition,
        FColor::Orange,
        false,
        0.f,
        0,
        4.f);

    const FString TimingText =
        FString::Printf(
            TEXT("Parry t=%.3fs | hand=%.0f cm/s | blade=%.0f deg/s"),
            BestCandidate.TimeUntilContact,
            BestCandidate.RequiredHandSpeed,
            BestCandidate.RequiredBladeAngularSpeed);

    DrawDebugString(
        GetWorld(),
        BestCandidate.ContactPoint + FVector(0.f, 0.f, 12.f),
        TimingText,
        nullptr,
        FColor::White,
        0.f,
        true);
}
