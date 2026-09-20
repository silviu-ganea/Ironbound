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

namespace
{
	float FindBodyIntersectionTime(
		USkeletalMeshComponent* Mesh,
		const FBladeTrajectory& Trajectory,
		const FTransform& AttackerTransform,
		float CurrentSourceTime)
	{
		if (!Mesh)
		{
			return TNumericLimits<float>::Max();
		}

		struct FBodyProbe
		{
			FName Bone;
			float Radius;
		};

		// Conservative gameplay envelope used only to establish
		// the latest safe parry time.
		static const FBodyProbe Probes[] =
		{
			{ TEXT("pelvis"), 34.f },
			{ TEXT("spine_01"), 30.f },
			{ TEXT("spine_02"), 31.f },
			{ TEXT("spine_03"), 30.f },
			{ TEXT("neck_01"), 24.f },
			{ TEXT("head"), 28.f }
		};

		float FirstIntersectionTime = TNumericLimits<float>::Max();

		for (const FBladeSegment& Segment : Trajectory.Segments)
		{
			if (Segment.TimeSeconds <= CurrentSourceTime)
			{
				continue;
			}

			const FVector BladeBase =
				AttackerTransform.TransformPosition(Segment.Base);

			const FVector BladeTip =
				AttackerTransform.TransformPosition(Segment.Tip);

			for (const FBodyProbe& Probe : Probes)
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
					FirstIntersectionTime =
						FMath::Min(
							FirstIntersectionTime,
							Segment.TimeSeconds);

					break;
				}
			}
		}

		return FirstIntersectionTime;
	}
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
                    TEXT("Parry [%s]: LOCKED quality=%.2f t=%.3f hand=%.0fcm/s blade=%.0fdeg/s cross=%.0f enemyBlade=%.0f%% elbow=%.0f"),
                    *GetNameSafe(GetOwner()), Best.Quality, Best.TimeUntilContact,
                    Best.RequiredHandSpeed, Best.RequiredBladeAngularSpeed,
                    Best.IntersectionAngleDegrees, Best.IncomingBladeFraction * 100.f,
                    Best.ElbowAngleDegrees);
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
    ExecutedElbowPosition = FighterMesh->GetSocketLocation(LowerArmBone);
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

    ExecutedElbowPosition = FMath::VInterpConstantTo(
        ExecutedElbowPosition,
        ActiveParryCandidate.RequiredElbowPosition,
        DeltaTime,
        ParryMovementSpeed * 0.8f);

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
    ExecutedElbowPosition = FVector::ZeroVector;
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

bool UIronboundParryComponent::GetCurrentWeaponGeometry(
    FTransform& OutTransform, FVector& OutBase, FVector& OutTip,
    float& OutLength, float& OutMinFraction, float& OutMaxFraction) const
{
    const UIronboundEquipmentComponent* Equipment =
        GetOwner()->FindComponentByClass<UIronboundEquipmentComponent>();
    if (!Equipment || !Equipment->bReady || !Equipment->Definition || !Equipment->GetWeapon())
        return false;

    OutTransform = Equipment->GetWeapon()->GetComponentTransform();
    OutBase = OutTransform.TransformPosition(Equipment->Definition->BladeBase);
    OutTip = OutTransform.TransformPosition(Equipment->Definition->BladeTip);
    OutLength = FVector::Distance(Equipment->Definition->BladeBase, Equipment->Definition->BladeTip);

    OutMinFraction = FMath::Clamp(MinParryBladeFraction, 0.f, 1.f);
    OutMaxFraction = FMath::Clamp(MaxParryBladeFraction, OutMinFraction, 1.f);
    return OutLength > KINDA_SMALL_NUMBER;
}

bool UIronboundParryComponent::FindBestElbowPosition(
    const FVector& Shoulder, const FVector& Hand, const FVector& CurrentElbow,
    FVector& OutElbow, float& OutMovement, float& OutElbowAngleDegrees) const
{
    OutMovement = TNumericLimits<float>::Max();
    OutElbowAngleDegrees = 0.f;

    const FVector ShoulderToHand = Hand - Shoulder;
    const float Distance = ShoulderToHand.Size();
    const float MinReach = FMath::Abs(UpperArmLength - ForearmLength) + ArmReachMargin;
    const float MaxReach = UpperArmLength + ForearmLength - ArmReachMargin;

    if (Distance <= KINDA_SMALL_NUMBER || Distance < MinReach || Distance > MaxReach)
        return false;

    const FVector Axis = ShoulderToHand / Distance;
    const float Along =
        (UpperArmLength * UpperArmLength - ForearmLength * ForearmLength + Distance * Distance) /
        (2.f * Distance);
    const float RadiusSq = UpperArmLength * UpperArmLength - Along * Along;
    if (RadiusSq < -KINDA_SMALL_NUMBER) return false;

    const float Radius = FMath::Sqrt(FMath::Max(0.f, RadiusSq));
    const FVector Center = Shoulder + Axis * Along;

    FVector BasisA = FVector::CrossProduct(Axis, FVector::UpVector);
    if (BasisA.IsNearlyZero())
        BasisA = FVector::CrossProduct(Axis, FVector::ForwardVector);
    BasisA.Normalize();
    const FVector BasisB = FVector::CrossProduct(Axis, BasisA).GetSafeNormal();

    FVector CurrentSide = CurrentElbow - Shoulder;
    CurrentSide -= Axis * FVector::DotProduct(CurrentSide, Axis);
    if (CurrentSide.IsNearlyZero())
        CurrentSide = GetOwner()->GetActorRightVector();
    CurrentSide.Normalize();

    bool bFound = false;
    const int32 Count = FMath::Max(8, ElbowCircleSamples);

    for (int32 I = 0; I < Count; ++I)
    {
        const float A = 2.f * PI * float(I) / float(Count);
        const FVector Radial =
            (BasisA * FMath::Cos(A) + BasisB * FMath::Sin(A)).GetSafeNormal();

        if (FVector::DotProduct(Radial, CurrentSide) < MinimumElbowSideDot)
            continue;

        const FVector Elbow = Center + Radial * Radius;

        const FVector ToShoulder = (Shoulder - Elbow).GetSafeNormal();
        const FVector ToHand = (Hand - Elbow).GetSafeNormal();
        const float Dot = FMath::Clamp(FVector::DotProduct(ToShoulder, ToHand), -1.f, 1.f);
        const float ElbowAngle = FMath::RadiansToDegrees(FMath::Acos(Dot));

        if (ElbowAngle < MinimumElbowAngleDegrees || ElbowAngle > MaximumElbowAngleDegrees)
            continue;

        // Prefer the current elbow side, but also strongly prefer a useful bend.
        const float Movement = FVector::Distance(CurrentElbow, Elbow);
        const float BendPenalty = FMath::Abs(ElbowAngle - PreferredElbowAngleDegrees) * 0.20f;
        const float Cost = Movement + BendPenalty;

        if (!bFound || Cost < OutMovement)
        {
            bFound = true;
            OutElbow = Elbow;
            OutMovement = Cost;
            OutElbowAngleDegrees = ElbowAngle;
        }
    }

    return bFound;
}

bool UIronboundParryComponent::IsDefensivePoseAnatomicallyUseful(
    const FVector& Shoulder, const FVector& Hand, const FVector& CandidateElbow) const
{
    if (!FighterMesh) return false;

    const FVector OwnerLocation = GetOwner()->GetActorLocation();
    const FVector Forward = GetOwner()->GetActorForwardVector();
    const FVector Right = GetOwner()->GetActorRightVector();

    if (FighterMesh->GetBoneIndex(PelvisBone) != INDEX_NONE)
    {
        const float PelvisZ = FighterMesh->GetSocketLocation(PelvisBone).Z;
        if (Hand.Z < PelvisZ + MinimumHandHeightAbovePelvis)
            return false;
    }

    if (Hand.Z > Shoulder.Z + MaximumHandHeightAboveShoulder)
        return false;

    const FVector LocalOffset = Hand - OwnerLocation;
    if (FVector::DotProduct(LocalOffset, Forward) < MinimumHandForwardFromTorso)
        return false;

    const float LateralFromShoulder =
        FMath::Abs(FVector::DotProduct(Hand - Shoulder, Right));
    if (LateralFromShoulder > MaximumHandLateralFromShoulder)
        return false;

    // Keep the elbow out of the torso centreline.  This is intentionally a
    // broad geometric guard, not a fake animation rule.
    const float ElbowLateral =
        FMath::Abs(FVector::DotProduct(CandidateElbow - OwnerLocation, Right));
    const float ElbowForward =
        FVector::DotProduct(CandidateElbow - OwnerLocation, Forward);
    if (ElbowLateral < 5.f && ElbowForward < 12.f)
        return false;

    return true;
}

bool UIronboundParryComponent::EvaluateCandidate(
    const FVector& Shoulder, const FVector& CurrentElbow,
    const FVector& IncomingBase, const FVector& IncomingTip,
    const FVector& Contact, const FVector& IncomingDirection,
    float IncomingTime, float IncomingBladeFraction,
    const FVector& DefenseDirection,
    float DefenderBladeFraction, float BladeLength,
    const FTransform& CurrentWeaponTransform,
    const FVector& CurrentDefenseBase,
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
        Contact - DefenseDir * (BladeLength * DefenderBladeFraction);
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

    const FTransform HandTransform =
        Equipment->Definition->WeaponToHand * WeaponTransform;
    const FVector Hand = HandTransform.GetLocation();

    FVector Elbow;
    float ElbowCost = 0.f;
    float ElbowAngle = 0.f;
    if (!FindBestElbowPosition(
        Shoulder, Hand, CurrentElbow, Elbow, ElbowCost, ElbowAngle))
    {
        ++LastDiagnostics.AnatomyRejected;
        return false;
    }

    if (!IsDefensivePoseAnatomicallyUseful(Shoulder, Hand, Elbow))
    {
        ++LastDiagnostics.BodyRejected;
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

    // QUALITY: make the defense visually obvious and mechanically strong.
    // We deliberately do NOT reward cheap/minimal movement here.
    const float CrossingQuality =
        1.f - FMath::Clamp(
            FMath::Abs(Crossing - PreferredIntersectionAngleDegrees) /
            FMath::Max(90.f - MinimumIntersectionAngleDegrees, 1.f), 0.f, 1.f);

    const float ElbowQuality =
        1.f - FMath::Clamp(
            FMath::Abs(ElbowAngle - PreferredElbowAngleDegrees) / 60.f, 0.f, 1.f);

    const float BladeCenter = 0.55f;
    const float BladeCenterQuality =
        1.f - FMath::Clamp(
            FMath::Abs(DefenderBladeFraction - BladeCenter) / 0.45f, 0.f, 1.f);

    const float HandUtilization = HandSpeed / FMath::Max(MaxParryHandSpeed, 1.f);
    const float BladeUtilization = BladeSpeed / FMath::Max(MaxParryBladeAngularSpeed, 1.f);
    const float TimingQuality =
        1.f - FMath::Clamp(FMath::Max(HandUtilization, BladeUtilization), 0.f, 1.f);

    // Reward a clearly visible arm action up to the preferred travel amount.
    // Beyond that point there is no additional reward, so this cannot drive
    // the hand arbitrarily far away.
    const float DramaticPoseQuality =
        FMath::Clamp(HandDistance / FMath::Max(PreferredVisibleHandTravel, 1.f), 0.f, 1.f);

    // Contact should happen on the dangerous outer portion of the enemy sword,
    // with a soft preference near PreferredIncomingBladeFraction.
    const float IncomingRange =
        FMath::Max(MaximumIncomingBladeFraction - MinimumIncomingBladeFraction, 0.01f);
    const float IncomingTipContactQuality =
        1.f - FMath::Clamp(
            FMath::Abs(IncomingBladeFraction - PreferredIncomingBladeFraction) /
            IncomingRange, 0.f, 1.f);

    const float Quality =
        CrossingQualityWeight * CrossingQuality +
        ElbowQualityWeight * ElbowQuality +
        BladeCenterQualityWeight * BladeCenterQuality +
        TimingMarginQualityWeight * TimingQuality +
        DramaticPoseQualityWeight * DramaticPoseQuality +
        IncomingTipContactQualityWeight * IncomingTipContactQuality;

    Out.ContactPoint = Contact;
    Out.IncomingBase = IncomingBase;
    Out.IncomingTip = IncomingTip;
    Out.DefenseBase = CandidateBase;
    Out.DefenseTip = CandidateTip;
    Out.RequiredWeaponTransform = WeaponTransform;
    Out.RequiredHandTransform = HandTransform;
    Out.RequiredHandPosition = Hand;
    Out.RequiredElbowPosition = Elbow;
    Out.IncomingTime = IncomingTime;
    Out.TimeUntilContact = TimeUntilContact;
    Out.DefenderBladeFraction = DefenderBladeFraction;
    Out.IncomingBladeFraction = IncomingBladeFraction;
    Out.IntersectionAngleDegrees = Crossing;
    Out.ElbowAngleDegrees = ElbowAngle;
    Out.RequiredHandSpeed = HandSpeed;
    Out.RequiredBladeAngularSpeed = BladeSpeed;
    Out.Quality = Quality;
    Out.bValid = true;

    ++LastDiagnostics.ValidCandidates;
    return true;
}

bool UIronboundParryComponent::FindBestParryCandidate(
    FIronboundParryCandidate& OutCandidate) const
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

    FTransform CurrentWeaponTransform;
    FVector CurrentDefenseBase, CurrentDefenseTip;
    float BladeLength = 0.f, MinFraction = 0.f, MaxFraction = 0.f;

    if (!GetCurrentWeaponGeometry(
        CurrentWeaponTransform, CurrentDefenseBase, CurrentDefenseTip,
        BladeLength, MinFraction, MaxFraction))
        return false;

    const FVector CurrentDefenseDirection =
        (CurrentDefenseTip - CurrentDefenseBase).GetSafeNormal();
    if (CurrentDefenseDirection.IsNearlyZero()) return false;

    const FVector Shoulder = FighterMesh->GetSocketLocation(UpperArmBone);
    const FVector CurrentElbow = FighterMesh->GetSocketLocation(LowerArmBone);

    const FBladeTrajectory& Trajectory = TargetExecution->GetCommittedTrajectory();
    const FTransform& AttackerTransform = TargetExecution->GetCommittedTransform();

    float CurrentSourceTime = 0.f;
    if (!GetIncomingSourcePlaybackTime(CurrentSourceTime))
        return false;

    const float BodyIntersectionTime =
        FindBodyIntersectionTime(
            FighterMesh,
            Trajectory,
            AttackerTransform,
            CurrentSourceTime);

    constexpr float BodySafetyMargin = 0.08f;

    const int32 IncomingContactCount = FMath::Max(2, IncomingBladeContactSamples);
    const int32 BladeFractionCount = FMath::Max(2, DefenderBladeFractionSamples);
    const int32 OrientationCount = FMath::Max(8, DefenseOrientationSamples);
    const int32 CrossingCount = FMath::Max(2, DefenseCrossingAngleSamples);

    float BestQuality = -TNumericLimits<float>::Max();

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
            BodyIntersectionTime - BodySafetyMargin)
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
                            Shoulder, CurrentElbow,
                            IncomingBase, IncomingTip, Contact, IncomingDir,
                            Segment.TimeSeconds, IncomingAlpha,
                            DefenseDirection, DefenderFraction,
                            BladeLength, CurrentWeaponTransform, CurrentDefenseBase,
                            CurrentDefenseDirection, Candidate))
                            continue;

                        const float BodyTimingMargin =
                            FMath::Clamp(
                                (BodyIntersectionTime - Segment.TimeSeconds) / 0.75f,
                                0.f,
                                1.f);

                        // Prefer candidates with more time remaining before body impact.
                        Candidate.Quality += 2.f * BodyTimingMargin;

                        if (Candidate.Quality > BestQuality)
                        {
                            BestQuality = Candidate.Quality;
                            OutCandidate = Candidate;

                            LastDiagnostics.BestTimeUntilContact =
                                Candidate.TimeUntilContact;
                            LastDiagnostics.BestRequiredHandSpeed =
                                Candidate.RequiredHandSpeed;
                            LastDiagnostics.BestRequiredBladeAngularSpeed =
                                Candidate.RequiredBladeAngularSpeed;
                            LastDiagnostics.BestQuality =
                                Candidate.Quality;
                        }
                    }
                }
            }
        }
    }

    if (!OutCandidate.bValid)
        LastParryEarlyExitReason =
            TEXT("no anatomically useful intercept survived hard constraints");

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
    DrawDebugLine(GetWorld(), Shoulder, C.RequiredElbowPosition,
        FColor::Orange, false, 0.f, 0, 3.f);
    DrawDebugLine(GetWorld(), C.RequiredElbowPosition, C.RequiredHandPosition,
        FColor::Orange, false, 0.f, 0, 3.f);

    DrawDebugSphere(GetWorld(), ExecutedElbowPosition, 4.f, 10,
        FColor::Purple, false, 0.f, 0, 2.f);
    DrawDebugSphere(GetWorld(), ExecutedHandTransform.GetLocation(), 4.f, 10,
        FColor::Blue, false, 0.f, 0, 2.f);

    const FString Text = FString::Printf(
        TEXT("Q %.2f | cross %.0f | enemyBlade %.0f%% | elbow %.0f | t %.3f | %s"),
        C.Quality, C.IntersectionAngleDegrees, C.IncomingBladeFraction * 100.f,
        C.ElbowAngleDegrees, C.TimeUntilContact,
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
