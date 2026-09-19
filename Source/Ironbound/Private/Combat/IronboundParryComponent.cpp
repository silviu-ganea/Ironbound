#include "Combat/IronboundParryComponent.h"

#include "Animation/Skeleton.h"
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

    FighterMesh =
        GetOwner()->FindComponentByClass<USkeletalMeshComponent>();

    CombatFocus =
        GetOwner()->FindComponentByClass<UIronboundCombatFocusComponent>();

    if (!FighterMesh)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Parry: %s has no skeletal mesh"),
            *GetNameSafe(GetOwner()));

        return;
    }

    if (!CombatFocus)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Parry: %s has no CombatFocus component"),
            *GetNameSafe(GetOwner()));
    }

    if (CalculateArmDimensions())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT(
                "Parry Anatomy [%s]: "
                "UpperArm=%.1f cm Forearm=%.1f cm TotalArm=%.1f cm"),
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

    const int32 UpperArmIndex =
        FighterMesh->GetBoneIndex(UpperArmBone);

    const int32 LowerArmIndex =
        FighterMesh->GetBoneIndex(LowerArmBone);

    const int32 HandIndex =
        FighterMesh->GetBoneIndex(HandBone);

    if (UpperArmIndex == INDEX_NONE ||
        LowerArmIndex == INDEX_NONE ||
        HandIndex == INDEX_NONE)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Parry: %s is missing one or more arm bones"),
            *GetNameSafe(GetOwner()));

        return false;
    }

    const FReferenceSkeleton& RefSkeleton =
        FighterMesh->GetSkeletalMeshAsset()->GetRefSkeleton();

    const TArray<FTransform>& RefPose =
        RefSkeleton.GetRefBonePose();

    auto GetRefPoseComponentTransform =
        [&RefSkeleton, &RefPose](int32 BoneIndex)
        {
            FTransform ComponentTransform =
                RefPose[BoneIndex];

            int32 ParentIndex =
                RefSkeleton.GetParentIndex(BoneIndex);

            while (ParentIndex != INDEX_NONE)
            {
                ComponentTransform *= RefPose[ParentIndex];

                ParentIndex =
                    RefSkeleton.GetParentIndex(ParentIndex);
            }

            return ComponentTransform;
        };

    const FVector Shoulder =
        GetRefPoseComponentTransform(UpperArmIndex)
            .GetTranslation();

    const FVector Elbow =
        GetRefPoseComponentTransform(LowerArmIndex)
            .GetTranslation();

    const FVector Hand =
        GetRefPoseComponentTransform(HandIndex)
            .GetTranslation();

    UpperArmLength =
        FVector::Distance(Shoulder, Elbow);

    ForearmLength =
        FVector::Distance(Elbow, Hand);

    ArmLength =
        UpperArmLength + ForearmLength;

    return true;
}

void UIronboundParryComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(
        DeltaTime,
        TickType,
        ThisTickFunction);

    if (!bDrawParryDebug)
    {
        return;
    }

    DrawAnatomyDebug();
    DrawParrySolutionDebug();
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

    const FVector Shoulder =
        FighterMesh->GetSocketLocation(UpperArmBone);

    const FVector Elbow =
        FighterMesh->GetSocketLocation(LowerArmBone);

    const FVector Hand =
        FighterMesh->GetSocketLocation(HandBone);

    DrawDebugSphere(
        GetWorld(),
        Shoulder,
        5.f,
        12,
        FColor::Cyan,
        false,
        0.f,
        0,
        1.5f);

    DrawDebugSphere(
        GetWorld(),
        Elbow,
        4.f,
        12,
        FColor::Yellow,
        false,
        0.f,
        0,
        1.5f);

    DrawDebugSphere(
        GetWorld(),
        Hand,
        4.f,
        12,
        FColor::Green,
        false,
        0.f,
        0,
        1.5f);

    DrawDebugLine(
        GetWorld(),
        Shoulder,
        Elbow,
        FColor::Cyan,
        false,
        0.f,
        0,
        2.f);

    DrawDebugLine(
        GetWorld(),
        Elbow,
        Hand,
        FColor::Green,
        false,
        0.f,
        0,
        2.f);
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
        GetOwner()->FindComponentByClass<
            UIronboundEquipmentComponent>();

    if (!Equipment ||
        !Equipment->bReady ||
        !Equipment->Definition)
    {
        return false;
    }

    UStaticMeshComponent* Weapon =
        Equipment->GetWeapon();

    if (!Weapon)
    {
        return false;
    }

    const FVector BladeBaseLocal =
        Equipment->Definition->BladeBase;

    const FVector BladeTipLocal =
        Equipment->Definition->BladeTip;

    OutBladeLength =
        FVector::Distance(
            BladeBaseLocal,
            BladeTipLocal);

    if (OutBladeLength <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    OutCurrentBaseWorld =
        Weapon->GetComponentTransform().TransformPosition(
            BladeBaseLocal);

    OutCurrentTipWorld =
        Weapon->GetComponentTransform().TransformPosition(
            BladeTipLocal);

    OutMinFraction =
        FMath::Clamp(
            MinParryBladeFraction,
            0.f,
            1.f);

    OutMaxFraction =
        FMath::Clamp(
            MaxParryBladeFraction,
            OutMinFraction,
            1.f);

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
    OutElbowMovementCost =
        TNumericLimits<float>::Max();

    if (UpperArmLength <= KINDA_SMALL_NUMBER ||
        ForearmLength <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const FVector ShoulderToHand =
        RequiredHandWorld - ShoulderWorld;

    const float ShoulderToHandDistance =
        ShoulderToHand.Size();

    if (ShoulderToHandDistance <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    // ------------------------------------------------------------
    // Two-bone reach limits.
    // ------------------------------------------------------------

    const float MaximumReach =
        UpperArmLength +
        ForearmLength -
        ArmReachMargin;

    const float MinimumReach =
        FMath::Abs(
            UpperArmLength -
            ForearmLength) +
        ArmReachMargin;

    if (ShoulderToHandDistance > MaximumReach ||
        ShoulderToHandDistance < MinimumReach)
    {
        return false;
    }

    const FVector Axis =
        ShoulderToHand /
        ShoulderToHandDistance;

    // ------------------------------------------------------------
    // Intersection circle of:
    //
    // sphere around shoulder, radius UpperArmLength
    // sphere around hand,     radius ForearmLength
    // ------------------------------------------------------------

    const float DistanceSquared =
        ShoulderToHandDistance *
        ShoulderToHandDistance;

    const float UpperArmSquared =
        UpperArmLength *
        UpperArmLength;

    const float ForearmSquared =
        ForearmLength *
        ForearmLength;

    const float AlongAxis =
        (
            UpperArmSquared -
            ForearmSquared +
            DistanceSquared
        ) /
        (
            2.f *
            ShoulderToHandDistance
        );

    float CircleRadiusSquared =
        UpperArmSquared -
        AlongAxis * AlongAxis;

    if (CircleRadiusSquared < -KINDA_SMALL_NUMBER)
    {
        return false;
    }

    CircleRadiusSquared =
        FMath::Max(
            0.f,
            CircleRadiusSquared);

    const float CircleRadius =
        FMath::Sqrt(
            CircleRadiusSquared);

    const FVector CircleCenter =
        ShoulderWorld +
        Axis * AlongAxis;

    // ------------------------------------------------------------
    // Build basis for the elbow circle.
    // ------------------------------------------------------------

    FVector BasisA =
        FVector::CrossProduct(
            Axis,
            FVector::UpVector);

    if (BasisA.IsNearlyZero())
    {
        BasisA =
            FVector::CrossProduct(
                Axis,
                FVector::ForwardVector);
    }

    BasisA.Normalize();

    const FVector BasisB =
        FVector::CrossProduct(
            Axis,
            BasisA)
            .GetSafeNormal();

    if (BasisA.IsNearlyZero() ||
        BasisB.IsNearlyZero())
    {
        return false;
    }

    // ------------------------------------------------------------
    // ANATOMICAL ELBOW-SIDE REFERENCE
    //
    // The mathematical elbow circle allows 360 degrees of elbow
    // positions.
    //
    // We use the fighter's CURRENT elbow to determine which side
    // of this circle corresponds to the natural bend direction.
    //
    // First project the current shoulder->elbow direction onto the
    // plane perpendicular to Shoulder->RequiredHand.
    //
    // This gives us a pole-vector-like reference without hardcoding
    // world left/right.
    // ------------------------------------------------------------

    const FVector CurrentShoulderToElbow =
        CurrentElbowWorld -
        ShoulderWorld;

    FVector CurrentElbowSide =
        CurrentShoulderToElbow -
        Axis *
        FVector::DotProduct(
            CurrentShoulderToElbow,
            Axis);

    // If the current elbow happens to lie almost exactly on the
    // shoulder->required-hand axis, there is no reliable side
    // reference. Fall back to the owner's right vector projected
    // onto the same plane.
    if (CurrentElbowSide.IsNearlyZero())
    {
        FVector OwnerRight =
            GetOwner()->GetActorRightVector();

        CurrentElbowSide =
            OwnerRight -
            Axis *
            FVector::DotProduct(
                OwnerRight,
                Axis);
    }

    if (CurrentElbowSide.IsNearlyZero())
    {
        return false;
    }

    CurrentElbowSide.Normalize();

    const int32 SampleCount =
        FMath::Max(
            4,
            ElbowCircleSamples);

    bool bFoundElbow = false;

    // ------------------------------------------------------------
    // Sample elbow circle.
    // ------------------------------------------------------------

    for (int32 SampleIndex = 0;
         SampleIndex < SampleCount;
         ++SampleIndex)
    {
        const float Angle =
            2.f * PI *
            static_cast<float>(SampleIndex) /
            static_cast<float>(SampleCount);

        const FVector RadialDirection =
            (
                BasisA * FMath::Cos(Angle) +
                BasisB * FMath::Sin(Angle)
            ).GetSafeNormal();

        if (RadialDirection.IsNearlyZero())
        {
            continue;
        }

        // --------------------------------------------------------
        // NEW ANATOMICAL TEST
        //
        // Positive dot = same elbow side as the current arm.
        // Negative dot = elbow crossed through to the opposite side.
        //
        // At the default MinimumElbowSideDot = 0:
        // exactly half of the mathematical elbow circle is rejected.
        // --------------------------------------------------------

        const float ElbowSideDot =
            FVector::DotProduct(
                RadialDirection,
                CurrentElbowSide);

        if (ElbowSideDot < MinimumElbowSideDot)
        {
            continue;
        }

        const FVector CandidateElbow =
            CircleCenter +
            RadialDirection *
            CircleRadius;

        // --------------------------------------------------------
        // Verify bone lengths.
        // --------------------------------------------------------

        const float CandidateUpperArmLength =
            FVector::Distance(
                ShoulderWorld,
                CandidateElbow);

        const float CandidateForearmLength =
            FVector::Distance(
                CandidateElbow,
                RequiredHandWorld);

        if (!FMath::IsNearlyEqual(
                CandidateUpperArmLength,
                UpperArmLength,
                0.1f))
        {
            continue;
        }

        if (!FMath::IsNearlyEqual(
                CandidateForearmLength,
                ForearmLength,
                0.1f))
        {
            continue;
        }

        // --------------------------------------------------------
        // Among anatomically allowed elbow positions, prefer the
        // one closest to the current animated elbow.
        // --------------------------------------------------------

        const float ElbowMovement =
            FVector::Distance(
                CurrentElbowWorld,
                CandidateElbow);

        if (!bFoundElbow ||
            ElbowMovement < OutElbowMovementCost)
        {
            bFoundElbow = true;

            OutElbowWorld =
                CandidateElbow;

            OutElbowMovementCost =
                ElbowMovement;
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
    if (IncomingDirection.IsNearlyZero() ||
        DefenseDirection.IsNearlyZero())
    {
        return false;
    }

    const FVector NormalizedDefenseDirection =
        DefenseDirection.GetSafeNormal();

    // ------------------------------------------------------------
    // Blade crossing angle.
    // ------------------------------------------------------------

    const float AbsDot =
        FMath::Clamp(
            FMath::Abs(
                FVector::DotProduct(
                    IncomingDirection,
                    NormalizedDefenseDirection)),
            0.f,
            1.f);

    const float IntersectionAngleDegrees =
        FMath::RadiansToDegrees(
            FMath::Acos(
                AbsDot));

    if (IntersectionAngleDegrees <
        MinimumIntersectionAngleDegrees)
    {
        return false;
    }

    // ------------------------------------------------------------
    // Put selected point of defender blade on incoming contact.
    // ------------------------------------------------------------

    const float ContactDistanceAlongDefenseBlade =
        BladeLength *
        DefenderBladeFraction;

    const FVector CandidateBase =
        IncomingContactWorld -
        NormalizedDefenseDirection *
        ContactDistanceAlongDefenseBlade;

    const FVector CandidateTip =
        CandidateBase +
        NormalizedDefenseDirection *
        BladeLength;

    // ------------------------------------------------------------
    // Two-bone + elbow-side feasibility.
    // ------------------------------------------------------------

    FVector CandidateElbow;
    float ElbowMovementCost = 0.f;

    if (!FindBestElbowPosition(
            ShoulderWorld,
            CandidateBase,
            CurrentElbowWorld,
            CandidateElbow,
            ElbowMovementCost))
    {
        return false;
    }

    // ------------------------------------------------------------
    // Movement cost.
    // ------------------------------------------------------------

    const float TranslationCost =
        FVector::Distance(
            CurrentDefenseBase,
            CandidateBase);

    const float RotationAbsDot =
        FMath::Clamp(
            FMath::Abs(
                FVector::DotProduct(
                    CurrentDefenseDirection,
                    NormalizedDefenseDirection)),
            0.f,
            1.f);

    const float RotationDegrees =
        FMath::RadiansToDegrees(
            FMath::Acos(
                RotationAbsDot));

    const float MovementCost =
        TranslationCost *
            TranslationCostWeight
        +
        RotationDegrees *
            RotationCostWeight
        +
        ElbowMovementCost *
            ElbowMovementCostWeight;

    // ------------------------------------------------------------
    // Store candidate.
    // ------------------------------------------------------------

    OutCandidate.ContactPoint =
        IncomingContactWorld;

    OutCandidate.IncomingBase =
        IncomingBaseWorld;

    OutCandidate.IncomingTip =
        IncomingTipWorld;

    OutCandidate.DefenseBase =
        CandidateBase;

    OutCandidate.DefenseTip =
        CandidateTip;

    OutCandidate.RequiredHandPosition =
        CandidateBase;

    OutCandidate.RequiredElbowPosition =
        CandidateElbow;

    OutCandidate.IncomingTime =
        IncomingTime;

    OutCandidate.DefenderBladeFraction =
        DefenderBladeFraction;

    OutCandidate.IntersectionAngleDegrees =
        IntersectionAngleDegrees;

    OutCandidate.MovementCost =
        MovementCost;

    OutCandidate.bValid = true;

    return true;
}

bool UIronboundParryComponent::FindBestParryCandidate(
    FIronboundParryCandidate& OutCandidate) const
{
    OutCandidate =
        FIronboundParryCandidate();

    if (!FighterMesh ||
        !CombatFocus ||
        UpperArmLength <= KINDA_SMALL_NUMBER ||
        ForearmLength <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    AActor* Target =
        CombatFocus->GetCombatTarget();

    if (!Target)
    {
        return false;
    }

    UIronboundCombatExecutionComponent* TargetExecution =
        Target->FindComponentByClass<
            UIronboundCombatExecutionComponent>();

    if (!TargetExecution ||
        !TargetExecution->HasCommittedBladePath())
    {
        return false;
    }

    // ------------------------------------------------------------
    // Current defender weapon.
    // ------------------------------------------------------------

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
        return false;
    }

    const FVector CurrentDefenseDirection =
        (CurrentDefenseTip - CurrentDefenseBase)
            .GetSafeNormal();

    if (CurrentDefenseDirection.IsNearlyZero())
    {
        return false;
    }

    if (FighterMesh->GetBoneIndex(UpperArmBone) == INDEX_NONE ||
        FighterMesh->GetBoneIndex(LowerArmBone) == INDEX_NONE)
    {
        return false;
    }

    const FVector ShoulderWorld =
        FighterMesh->GetSocketLocation(
            UpperArmBone);

    const FVector CurrentElbowWorld =
        FighterMesh->GetSocketLocation(
            LowerArmBone);

    // ------------------------------------------------------------
    // Incoming committed attack.
    // ------------------------------------------------------------

    const FBladeTrajectory& IncomingTrajectory =
        TargetExecution->GetCommittedTrajectory();

    const FTransform& AttackerCommittedTransform =
        TargetExecution->GetCommittedTransform();

    if (!IncomingTrajectory.bValid ||
        IncomingTrajectory.Segments.IsEmpty())
    {
        return false;
    }

    const int32 IncomingContactCount =
        FMath::Max(
            2,
            IncomingBladeContactSamples);

    const int32 BladeFractionCount =
        FMath::Max(
            2,
            DefenderBladeFractionSamples);

    const int32 OrientationCount =
        FMath::Max(
            4,
            DefenseOrientationSamples);

    const int32 CrossingAngleCount =
        FMath::Max(
            2,
            DefenseCrossingAngleSamples);

    const float MinimumCrossingAngle =
        FMath::Clamp(
            MinimumIntersectionAngleDegrees,
            0.f,
            90.f);

    float BestCost =
        TNumericLimits<float>::Max();

    // ------------------------------------------------------------
    // Candidate search.
    // ------------------------------------------------------------

    for (const FBladeSegment& IncomingSegment :
         IncomingTrajectory.Segments)
    {
        const FVector IncomingBaseWorld =
            AttackerCommittedTransform.TransformPosition(
                IncomingSegment.Base);

        const FVector IncomingTipWorld =
            AttackerCommittedTransform.TransformPosition(
                IncomingSegment.Tip);

        const FVector IncomingDirection =
            (IncomingTipWorld - IncomingBaseWorld)
                .GetSafeNormal();

        if (IncomingDirection.IsNearlyZero())
        {
            continue;
        }

        // --------------------------------------------------------
        // Basis around incoming blade.
        // --------------------------------------------------------

        FVector BasisA =
            FVector::CrossProduct(
                IncomingDirection,
                FVector::UpVector);

        if (BasisA.IsNearlyZero())
        {
            BasisA =
                FVector::CrossProduct(
                    IncomingDirection,
                    FVector::ForwardVector);
        }

        BasisA.Normalize();

        const FVector BasisB =
            FVector::CrossProduct(
                IncomingDirection,
                BasisA)
                .GetSafeNormal();

        // --------------------------------------------------------
        // Sample contact along incoming blade.
        // --------------------------------------------------------

        for (int32 ContactIndex = 0;
             ContactIndex < IncomingContactCount;
             ++ContactIndex)
        {
            const float IncomingAlpha =
                static_cast<float>(ContactIndex) /
                static_cast<float>(
                    IncomingContactCount - 1);

            const FVector IncomingContactWorld =
                FMath::Lerp(
                    IncomingBaseWorld,
                    IncomingTipWorld,
                    IncomingAlpha);

            const float MaximumPossibleReach =
                ArmLength +
                BladeLength *
                MaxBladeFraction;

            if (FVector::Distance(
                    ShoulderWorld,
                    IncomingContactWorld) >
                MaximumPossibleReach)
            {
                continue;
            }

            // ----------------------------------------------------
            // Sample usable portion of defending blade.
            // ----------------------------------------------------

            for (int32 FractionIndex = 0;
                 FractionIndex < BladeFractionCount;
                 ++FractionIndex)
            {
                const float FractionAlpha =
                    static_cast<float>(FractionIndex) /
                    static_cast<float>(
                        BladeFractionCount - 1);

                const float DefenderBladeFraction =
                    FMath::Lerp(
                        MinBladeFraction,
                        MaxBladeFraction,
                        FractionAlpha);

                // ------------------------------------------------
                // Sample blade crossing angle.
                // ------------------------------------------------

                for (int32 CrossingAngleIndex = 0;
                     CrossingAngleIndex < CrossingAngleCount;
                     ++CrossingAngleIndex)
                {
                    const float CrossingAlpha =
                        static_cast<float>(
                            CrossingAngleIndex) /
                        static_cast<float>(
                            CrossingAngleCount - 1);

                    const float CrossingAngleDegrees =
                        FMath::Lerp(
                            MinimumCrossingAngle,
                            90.f,
                            CrossingAlpha);

                    const float CrossingAngleRadians =
                        FMath::DegreesToRadians(
                            CrossingAngleDegrees);

                    const float ParallelAmount =
                        FMath::Cos(
                            CrossingAngleRadians);

                    const float PerpendicularAmount =
                        FMath::Sin(
                            CrossingAngleRadians);

                    // --------------------------------------------
                    // Rotate candidate defense direction around
                    // incoming blade.
                    // --------------------------------------------

                    for (int32 OrientationIndex = 0;
                         OrientationIndex < OrientationCount;
                         ++OrientationIndex)
                    {
                        const float AroundAngle =
                            2.f * PI *
                            static_cast<float>(
                                OrientationIndex) /
                            static_cast<float>(
                                OrientationCount);

                        const FVector RingDirection =
                            BasisA *
                                FMath::Cos(AroundAngle)
                            +
                            BasisB *
                                FMath::Sin(AroundAngle);

                        const FVector DefenseDirection =
                            (
                                IncomingDirection *
                                    ParallelAmount
                                +
                                RingDirection *
                                    PerpendicularAmount
                            ).GetSafeNormal();

                        if (DefenseDirection.IsNearlyZero())
                        {
                            continue;
                        }

                        FIronboundParryCandidate Candidate;

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

                        if (Candidate.MovementCost < BestCost)
                        {
                            BestCost =
                                Candidate.MovementCost;

                            OutCandidate =
                                Candidate;
                        }
                    }
                }
            }
        }
    }

    return OutCandidate.bValid;
}

void UIronboundParryComponent::DrawParrySolutionDebug() const
{
    FIronboundParryCandidate BestCandidate;

    if (!FindBestParryCandidate(BestCandidate))
    {
        return;
    }

    // RED = incoming blade.
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

    // CYAN = proposed defending blade.
    DrawDebugLine(
        GetWorld(),
        BestCandidate.DefenseBase,
        BestCandidate.DefenseTip,
        FColor::Cyan,
        false,
        0.f,
        0,
        7.f);

    // GREEN = blade/blade contact.
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

    // MAGENTA = required hand / weapon base.
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

    // ORANGE = proposed elbow.
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
        FighterMesh->GetSocketLocation(
            UpperArmBone);

    // Proposed upper arm.
    DrawDebugLine(
        GetWorld(),
        ShoulderWorld,
        BestCandidate.RequiredElbowPosition,
        FColor::Orange,
        false,
        0.f,
        0,
        4.f);

    // Proposed forearm.
    DrawDebugLine(
        GetWorld(),
        BestCandidate.RequiredElbowPosition,
        BestCandidate.RequiredHandPosition,
        FColor::Orange,
        false,
        0.f,
        0,
        4.f);
}