#include "Combat/CombatExecutor_ProceduralParry.h"

#include "Combat/CombatBodyComponent.h"
#include "Combat/CombatBodyProbes.h"
#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatExecutionComponent.h"
#include "Combat/CombatExecutionTypes.h"
#include "Combat/WeaponDefinition.h"
#include "Combat/CombatTechniqueExecutionConfigs.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimCompositeBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "Math/UnrealMathUtility.h"
#include "Ironbound.h"

namespace
{
	float GetActualBladeCrossing(AActor* A, AActor* B, float& OutDistance)
	{
		OutDistance = TNumericLimits<float>::Max();
		if (!A || !B) return -1.f;
		auto GetBlade = [](AActor* Actor, FVector& Base, FVector& Tip) -> bool
		{
			UCombatEquipmentComponent* E = Actor->FindComponentByClass<UCombatEquipmentComponent>();
			UStaticMeshComponent* W = E ? E->GetWeapon() : nullptr;
			if (!E || !E->Definition || !W) return false;
			const FTransform T = W->GetComponentTransform();
			Base = T.TransformPosition(E->Definition->BladeBase);
			Tip = T.TransformPosition(E->Definition->BladeTip);
			return true;
		};
		FVector A0, A1, B0, B1;
		if (!GetBlade(A, A0, A1) || !GetBlade(B, B0, B1)) return -1.f;
		const FVector AD = (A1 - A0).GetSafeNormal();
		const FVector BD = (B1 - B0).GetSafeNormal();
		if (AD.IsNearlyZero() || BD.IsNearlyZero()) return -1.f;
		const float AbsDot = FMath::Clamp(FMath::Abs(FVector::DotProduct(AD, BD)), 0.f, 1.f);
		FVector CA, CB;
		FMath::SegmentDistToSegmentSafe(A0, A1, B0, B1, CA, CB);
		OutDistance = FVector::Distance(CA, CB);
		return FMath::RadiansToDegrees(FMath::Acos(AbsDot));
	}
}

const UExecConfig_ProceduralParry* UCombatExecutor_ProceduralParry::ParryConfig() const
{
	return Cast<UExecConfig_ProceduralParry>(Config);
}

FName UCombatExecutor_ProceduralParry::ResolveUpperArmBone() const
{
	if (const UCombatEquipmentComponent* Equipment = GetEquipment())
	{
		const FName GripBone = Equipment->GetUpperArmBone();
		if (!GripBone.IsNone()) return GripBone;
	}
	const UExecConfig_ProceduralParry* Cfg = ParryConfig();
	return Cfg ? Cfg->UpperArmBone : NAME_None;
}

FName UCombatExecutor_ProceduralParry::ResolveLowerArmBone() const
{
	if (const UCombatEquipmentComponent* Equipment = GetEquipment())
	{
		const FName GripBone = Equipment->GetLowerArmBone();
		if (!GripBone.IsNone()) return GripBone;
	}
	const UExecConfig_ProceduralParry* Cfg = ParryConfig();
	return Cfg ? Cfg->LowerArmBone : NAME_None;
}

FName UCombatExecutor_ProceduralParry::ResolveHandBone() const
{
	if (const UCombatEquipmentComponent* Equipment = GetEquipment())
	{
		const FName GripBone = Equipment->GetHandBone();
		if (!GripBone.IsNone()) return GripBone;
	}
	const UExecConfig_ProceduralParry* Cfg = ParryConfig();
	return Cfg ? Cfg->HandBone : NAME_None;
}

// ============================================================================
// Arm anatomy
// ============================================================================

bool UCombatExecutor_ProceduralParry::CalculateArmDimensions()
{
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset()) return false;

	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	if (!LocalConfig) return false;

	const int32 UpperArmIndex = Mesh->GetBoneIndex(ResolveUpperArmBone());
	const int32 LowerArmIndex = Mesh->GetBoneIndex(ResolveLowerArmBone());
	const int32 HandIndex = Mesh->GetBoneIndex(ResolveHandBone());
	if (UpperArmIndex == INDEX_NONE || LowerArmIndex == INDEX_NONE || HandIndex == INDEX_NONE)
		return false;
	const FReferenceSkeleton& RefSkeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
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

FArmExtensionMetrics UCombatExecutor_ProceduralParry::CalculateArmExtensionMetrics(
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
	if (D < MinReach - ParryConfig()->ArmReachMargin || D > MaxReach + ParryConfig()->ArmReachMargin || D <= KINDA_SMALL_NUMBER)
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

float UCombatExecutor_ProceduralParry::CalculateMinimumWristDeviationAnalytical(
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
		D <= KINDA_SMALL_NUMBER || D < MinReach - ParryConfig()->ArmReachMargin || D > MaxReach + ParryConfig()->ArmReachMargin)
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

FVector UCombatExecutor_ProceduralParry::CalculateReferenceForearmDirection() const
{
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !LocalConfig) return FVector::ForwardVector;
	const int32 LowerArmIndex = Mesh->GetBoneIndex(ResolveLowerArmBone());
	const int32 HandIndex = Mesh->GetBoneIndex(ResolveHandBone());
	if (LowerArmIndex == INDEX_NONE || HandIndex == INDEX_NONE) return FVector::ForwardVector;
	const FReferenceSkeleton& RefSkeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
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

FTransform UCombatExecutor_ProceduralParry::CalculateNeutralWristRelationship() const
{
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	if (!Mesh || !Mesh->GetSkeletalMeshAsset() || !LocalConfig) return FTransform::Identity;
	const int32 LowerArmIndex = Mesh->GetBoneIndex(ResolveLowerArmBone());
	const int32 HandIndex = Mesh->GetBoneIndex(ResolveHandBone());
	if (LowerArmIndex == INDEX_NONE || HandIndex == INDEX_NONE) return FTransform::Identity;
	const FReferenceSkeleton& RefSkeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
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

FVector UCombatExecutor_ProceduralParry::CalculateNeutralForearmDirection(const FTransform& RequiredHandTransform) const
{
	const FTransform NeutralWristRelationship = CalculateNeutralWristRelationship();
	const FVector ReferenceForearmDirLocal = CalculateReferenceForearmDirection();
	// UE composition: HandWorld = HandRelativeToForearm * ForearmWorld.
	const FTransform RequiredForearmTransform = NeutralWristRelationship.Inverse() * RequiredHandTransform;
	return RequiredForearmTransform
		.TransformVectorNoScale(ReferenceForearmDirLocal)
		.GetSafeNormal();
}

// ============================================================================
// Policy + selection
// ============================================================================

float UCombatExecutor_ProceduralParry::ApplyPolicyToRawMetrics(const FCandidateRawMetrics& Raw, const FParryPolicy& LocalPolicy) const
{
	// Convert wrist deviation to comfort (0-1, where 1 = comfortable)
	float wristComfort = 1.0f - (Raw.WristDeviationRadians / PI);
	wristComfort = FMath::Pow(wristComfort, LocalPolicy.WristSharpness);
	// Convert geometric freedom to comfort (0-1, where 1 = comfortable)
	float freedomComfort = FMath::Pow(Raw.GeometricFreedom, LocalPolicy.FreedomSharpness);
	// Tactical quality is already 0-1 where 1 = better
	float tacticalQuality = Raw.TacticalQuality;
	// Combined quality with tunable weighting
	return tacticalQuality * LocalPolicy.TacticalWeight +
		   wristComfort * (1.0f - LocalPolicy.TacticalWeight) * 0.5f +
		   freedomComfort * (1.0f - LocalPolicy.TacticalWeight) * 0.5f;
}

void UCombatExecutor_ProceduralParry::SelectComfortableCandidate(
	const TArray<FParryCandidate>& Candidates,
	FParryCandidate& OutSelected,
	uint32 SelectionSeed,
	const FParryPolicy& LocalPolicy) const
{
	if (Candidates.IsEmpty())
	{
		OutSelected = FParryCandidate();
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
	const float Skill = FMath::Clamp(LocalPolicy.ParrySkill, 0.f, 1.f);
	const float HighSkillThreshold = FMath::Clamp(LocalPolicy.QualityBandWidth, 0.6f, 1.f);
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

// ============================================================================
// Threat timing (objective observation of the attacker's playback)
// ============================================================================

bool UCombatExecutor_ProceduralParry::GetAttackerSourcePlaybackTime(float& OutSourceTime) const
{
	OutSourceTime = 0.f;
	const FCombatThreat& Threat = Request.ThreatContext.Threat;
	AActor* Attacker = Threat.Attacker;
	if (!Attacker) return false;

	UCombatEquipmentComponent* AttackerEquipment = Attacker->FindComponentByClass<UCombatEquipmentComponent>();
	USkeletalMeshComponent* Mesh = AttackerEquipment ? AttackerEquipment->GetFighterMesh() : nullptr;
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

// ============================================================================
// Candidate solving (migrated verbatim)
// ============================================================================

bool UCombatExecutor_ProceduralParry::EvaluateCandidate(
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
	FParryCandidate& Out) const
{
	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	const FCombatThreat& Threat = Request.ThreatContext.Threat;
	float CurrentSourceTime = Threat.CurrentSourceTime;
	GetAttackerSourcePlaybackTime(CurrentSourceTime);

	const FVector DefenseDir = DefenseDirection.GetSafeNormal();
	const FVector PlannedIncomingDir = (IncomingTipWorld - IncomingBaseWorld).GetSafeNormal();
	if (PlannedIncomingDir.IsNearlyZero() || DefenseDir.IsNearlyZero()) return false;
	// Free DOF #1: slide the defender sword along its own blade axis.
	// DefenderBladeFraction is already sampled by FindBestParryCandidate.
	const FVector CandidateBase = IncomingContactWorld - DefenseDir * (BladeLength * DefenderBladeFraction);
	const FVector CandidateTip = CandidateBase + DefenseDir * BladeLength;
	const UCombatEquipmentComponent* Equipment = GetFighter()->FindComponentByClass<UCombatEquipmentComponent>();
	if (!Equipment || !Equipment->Definition) return false;
	const FVector BladeBaseLocal = Equipment->Definition->BladeBase;
	// Align the current physical blade axis with the selected parry axis.
	const FQuat Alignment = FQuat::FindBetweenNormals(CurrentDefenseDirection, DefenseDir);
	const FQuat AxisAlignedRotation = (Alignment * CurrentWeaponTransform.GetRotation()).GetNormalized();
	// Free DOF #2: roll the whole weapon around the selected blade axis.
	// This leaves the cyan blade line and crossing angle unchanged, while
	// changing the authored grip/hand transform. Because WeaponToHand has
	// positional offsets, this can improve BOTH wrist rotation and CCDIK's
	// required hand position.
	constexpr int32 AxialRollSamples = 12; // 30-degree increments.
	bool bFoundRoll = false;
	float BestRollComfort = -TNumericLimits<float>::Max();
	FTransform BestWeaponTransform = FTransform::Identity;
	FTransform BestHandTransform = FTransform::Identity;
	FVector BestHand = FVector::ZeroVector;
	for (int32 RollIndex = 0; RollIndex < AxialRollSamples; ++RollIndex)
	{
		const float RollRadians = 2.f * PI * float(RollIndex) / float(AxialRollSamples);
		const FQuat AxialRoll(DefenseDir, RollRadians);
		const FQuat WeaponRotation = (AxialRoll * AxisAlignedRotation).GetNormalized();
		// Re-anchor BladeBase at exactly CandidateBase after rolling.
		const FVector WeaponTranslation = CandidateBase -
			WeaponRotation.RotateVector(
				CurrentWeaponTransform.GetScale3D() * BladeBaseLocal);
		const FTransform WeaponTransform(
			WeaponRotation,
			WeaponTranslation,
			CurrentWeaponTransform.GetScale3D());
		// Project convention:
		// WeaponWorld = WeaponToHand * HandWorld.
		const FTransform HandTransform = Equipment->GetWeaponToHand().Inverse() * WeaponTransform;
		const FVector Hand = HandTransform.GetLocation();
		const FArmExtensionMetrics ExtensionMetrics = CalculateArmExtensionMetrics(
				ShoulderWorld, Hand, UpperArmLength, ForearmLength);
		if (ExtensionMetrics.Distance < ExtensionMetrics.MinReach ||
			ExtensionMetrics.Distance > ExtensionMetrics.MaxReach)
		{
			const float ReachDeficit = FMath::Max(
				ExtensionMetrics.MinReach - ExtensionMetrics.Distance,
				ExtensionMetrics.Distance - ExtensionMetrics.MaxReach);
			if (ReachDeficit < LastDiagnostics.BestAnatomyReachDeficitCm)
			{
				LastDiagnostics.BestAnatomyReachDeficitCm = ReachDeficit;
				LastDiagnostics.FailedCandidateHandDistanceCm = ExtensionMetrics.Distance;
				LastDiagnostics.FailedCandidateMinReachCm = ExtensionMetrics.MinReach;
				LastDiagnostics.FailedCandidateMaxReachCm = ExtensionMetrics.MaxReach;
				LastDiagnostics.FailedCandidateIncomingFraction = IncomingBladeFraction;
			}
			continue;
		}
		const FVector NeutralForearmDir = CalculateNeutralForearmDirection(HandTransform);
		const float WristDeviation = CalculateMinimumWristDeviationAnalytical(
				ShoulderWorld,
				Hand,
				NeutralForearmDir,
				UpperArmLength,
				ForearmLength);
		const float WristComfort = 1.f - FMath::Clamp(WristDeviation / PI, 0.f, 1.f);
		// For tactically identical rolls, prioritize the wrist strongly and
		// use elbow-circle freedom as the secondary preference.
		const float RollComfort = WristComfort * 0.80f +
			ExtensionMetrics.GeometricFreedom * 0.20f;
		if (!bFoundRoll || RollComfort > BestRollComfort)
		{
			bFoundRoll = true;
			BestRollComfort = RollComfort;
			BestWeaponTransform = WeaponTransform;
			BestHandTransform = HandTransform;
			BestHand = Hand;
		}
	}
	if (!bFoundRoll)
	{
		++LastDiagnostics.AnatomyRejected;
		return false;
	}
	const FTransform& WeaponTransform = BestWeaponTransform;
	const FTransform& HandTransform = BestHandTransform;
	const FVector Hand = BestHand;
	// Crossing is defined from the exact two planned blade lines that debug draws:
	// red = IncomingBaseWorld->IncomingTipWorld, cyan = transformed defender BladeBase->BladeTip.
	const FVector ExactDefenseBase = WeaponTransform.TransformPosition(Equipment->Definition->BladeBase);
	const FVector ExactDefenseTip = WeaponTransform.TransformPosition(Equipment->Definition->BladeTip);
	const FVector ExactDefenseDir = (ExactDefenseTip - ExactDefenseBase).GetSafeNormal();
	if (ExactDefenseDir.IsNearlyZero()) return false;
	const float ExactAbsDot = FMath::Clamp(FMath::Abs(FVector::DotProduct(PlannedIncomingDir, ExactDefenseDir)), 0.f, 1.f);
	const float Crossing = FMath::RadiansToDegrees(FMath::Acos(ExactAbsDot));
	constexpr float HardMinimumCrossingAngleDegrees = 80.f;
	if (Crossing < FMath::Max(LocalConfig->MinimumIntersectionAngleDegrees, HardMinimumCrossingAngleDegrees))
	{
		++LastDiagnostics.AngleRejected;
		return false;
	}
	const float TimeUntilContact = IncomingTime - CurrentSourceTime;
	if (TimeUntilContact < LocalConfig->MinimumTimeToContact)
	{
		++LastDiagnostics.TimingRejected;
		return false;
	}
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	const FVector CurrentHand = Mesh->GetSocketLocation(ResolveHandBone());
	const float HandDistance = FVector::Distance(CurrentHand, Hand);
	// Only blade-axis reorientation belongs in the tactical blade angular
	// speed. Axial roll does not alter the interception geometry.
	const float RotationDot = FMath::Clamp(
		FMath::Abs(FVector::DotProduct(CurrentDefenseDirection, DefenseDir)), 0.f, 1.f);
	const float RotationDegrees = FMath::RadiansToDegrees(FMath::Acos(RotationDot));
	const float HandSpeed = HandDistance / TimeUntilContact;
	const float BladeSpeed = RotationDegrees / TimeUntilContact;
	if (HandSpeed > LocalConfig->MaxParryHandSpeed || BladeSpeed > LocalConfig->MaxParryBladeAngularSpeed)
	{
		++LastDiagnostics.SpeedRejected;
		return false;
	}
	const float CrossingRadians = FMath::DegreesToRadians(Crossing);
	const float CrossingQuality = FMath::Pow(FMath::Clamp(FMath::Sin(CrossingRadians), 0.f, 1.f), 6.f);
	const float BladeCenter = 0.55f;
	const float BladeCenterQuality = 1.f - FMath::Clamp(
			FMath::Abs(DefenderBladeFraction - BladeCenter) / 0.45f,
			0.f, 1.f);
	const float DramaticPoseQuality = FMath::Clamp(
			HandDistance / FMath::Max(LocalConfig->PreferredVisibleHandTravel, 1.f),
			0.f, 1.f);
	const float IncomingRange = FMath::Max(
			LocalConfig->MaximumIncomingBladeFraction - LocalConfig->MinimumIncomingBladeFraction,
			0.01f);
	const float IncomingTipContactQuality = 1.f - FMath::Clamp(
			FMath::Abs(IncomingBladeFraction - LocalConfig->PreferredIncomingBladeFraction) /
				IncomingRange,
			0.f, 1.f);
	constexpr float CrossingPreferenceBoost = 3.f;
	const float TacticalQuality = CrossingPreferenceBoost * LocalConfig->CrossingQualityWeight * CrossingQuality +
		LocalConfig->BladeCenterQualityWeight * BladeCenterQuality +
		LocalConfig->DramaticPoseQualityWeight * DramaticPoseQuality +
		LocalConfig->IncomingTipContactQualityWeight * IncomingTipContactQuality;
	Out.ContactPoint = IncomingContactWorld;
	Out.IncomingBase = IncomingBaseWorld;
	Out.IncomingTip = IncomingTipWorld;
	Out.DefenseBase = ExactDefenseBase;
	Out.DefenseTip = ExactDefenseTip;
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
	Out.Quality = TacticalQuality;
	Out.bValid = true;
	return true;
}

bool UCombatExecutor_ProceduralParry::FindBestParryCandidate(FParryCandidate& OutCandidate) const
{
	OutCandidate = FParryCandidate();
	LastDiagnostics = FParryDiagnostics();
	LastParryEarlyExitReason.Reset();

	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	if (!Mesh || !LocalConfig)
	{
		LastParryEarlyExitReason = !Mesh
			? TEXT("defender skeletal mesh unavailable")
			: TEXT("procedural parry config unavailable");
		return false;
	}

	const FCombatThreat& Threat = Request.ThreatContext.Threat;
	AActor* Attacker = Threat.Attacker;
	if (!Attacker)
	{
		LastParryEarlyExitReason = TEXT("threat attacker invalid");
		return false;
	}

	const UCombatEquipmentComponent* Equipment = GetFighter()->FindComponentByClass<UCombatEquipmentComponent>();
	const UStaticMeshComponent* Weapon = Equipment ? Equipment->GetWeapon() : nullptr;
	if (!Equipment || !Equipment->bReady || !Equipment->Definition || !Weapon)
	{
		LastParryEarlyExitReason = TEXT("defender weapon geometry unavailable");
		return false;
	}
	const FTransform CurrentWeaponTransform = Weapon->GetComponentTransform();
	const FVector CurrentDefenseBase = CurrentWeaponTransform.TransformPosition(Equipment->Definition->BladeBase);
	const FVector CurrentDefenseTip = CurrentWeaponTransform.TransformPosition(Equipment->Definition->BladeTip);
	const FVector CurrentDefenseDirection = (CurrentDefenseTip - CurrentDefenseBase).GetSafeNormal();
	const float BladeLength = FVector::Distance(CurrentDefenseBase, CurrentDefenseTip);
	if (CurrentDefenseDirection.IsNearlyZero() || BladeLength <= KINDA_SMALL_NUMBER)
	{
		LastParryEarlyExitReason = TEXT("defender blade geometry invalid");
		return false;
	}
	const float MinFraction = FMath::Clamp(LocalConfig->MinParryBladeFraction, 0.f, 1.f);
	const float MaxFraction = FMath::Clamp(LocalConfig->MaxParryBladeFraction, MinFraction, 1.f);
	const FVector Shoulder = Mesh->GetSocketLocation(ResolveUpperArmBone());
	const FBladeTrajectory& Trajectory = Threat.SourceTrajectory;
	const FTransform& AttackerTransform = Threat.AttackerTransform;
	float CurrentSourceTime = Threat.CurrentSourceTime;
	if (!GetAttackerSourcePlaybackTime(CurrentSourceTime))
	{
		LastParryEarlyExitReason = TEXT("attacker montage source time unavailable");
		return false;
	}
	bool bBodyAlreadyIntersected = false;
	const float BodyIntersectionTime = CombatBodyProbes::FindBodyIntersectionTime(
			Mesh,
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
	const int32 IncomingContactCount = FMath::Max(2, LocalConfig->IncomingBladeContactSamples);
	const int32 BladeFractionCount = FMath::Max(2, LocalConfig->DefenderBladeFractionSamples);
	const int32 OrientationCount = FMath::Max(8, LocalConfig->DefenseOrientationSamples);
	const int32 CrossingCount = FMath::Max(2, LocalConfig->DefenseCrossingAngleSamples);
	TArray<FParryCandidate> Candidates;
	float BestTacticalQuality = -TNumericLimits<float>::Max();
	for (const FBladeSegment& Segment : Trajectory.Segments)
	{
		++LastDiagnostics.IncomingSegments;
		const float TimeUntil = Segment.TimeSeconds - CurrentSourceTime;
		if (TimeUntil < LocalConfig->MinimumTimeToContact)
		{
			++LastDiagnostics.ExpiredSegments;
			continue;
		}
		// Never accept a parry after the incoming blade has already entered
		// the defender's configured head, torso, arm, or hand envelope.
		if (Segment.TimeSeconds >=
			BodyIntersectionTime - LocalConfig->BodySafetyMarginSeconds)
		{
			++LastDiagnostics.TimingRejected;
			continue;
		}
		const FVector IncomingBase = AttackerTransform.TransformPosition(Segment.Base);
		const FVector IncomingTip = AttackerTransform.TransformPosition(Segment.Tip);
		const FVector IncomingDir = (IncomingTip - IncomingBase).GetSafeNormal();
		if (IncomingDir.IsNearlyZero()) continue;
		FVector BasisA = FVector::CrossProduct(IncomingDir, FVector::UpVector);
		if (BasisA.IsNearlyZero())
			BasisA = FVector::CrossProduct(IncomingDir, FVector::ForwardVector);
		BasisA.Normalize();
		const FVector BasisB = FVector::CrossProduct(IncomingDir, BasisA).GetSafeNormal();
		for (int32 ContactIndex = 0; ContactIndex < IncomingContactCount; ++ContactIndex)
		{
			const float ContactSampleAlpha = float(ContactIndex) / float(IncomingContactCount - 1);
			const float IncomingAlpha = FMath::Lerp(
					FMath::Clamp(LocalConfig->MinimumIncomingBladeFraction, 0.f, 1.f),
					FMath::Clamp(LocalConfig->MaximumIncomingBladeFraction,
						LocalConfig->MinimumIncomingBladeFraction, 1.f),
					ContactSampleAlpha);
			const FVector Contact = FMath::Lerp(IncomingBase, IncomingTip, IncomingAlpha);
			++LastDiagnostics.ContactPoints;
			const float MaxPossibleReach = ArmLength + BladeLength * MaxFraction;
			if (FVector::Distance(Shoulder, Contact) > MaxPossibleReach)
			{
				++LastDiagnostics.BroadReachRejected;
				continue;
			}
			for (int32 FractionIndex = 0; FractionIndex < BladeFractionCount; ++FractionIndex)
			{
				const float FractionAlpha = float(FractionIndex) / float(BladeFractionCount - 1);
				const float DefenderFraction = FMath::Lerp(MinFraction, MaxFraction, FractionAlpha);
				for (int32 CrossingIndex = 0; CrossingIndex < CrossingCount; ++CrossingIndex)
				{
					const float CrossingAlpha = float(CrossingIndex) / float(CrossingCount - 1);
					constexpr float HardMinimumCrossingAngleDegrees = 80.f;
					const float CrossingDeg = FMath::Lerp(FMath::Max(LocalConfig->MinimumIntersectionAngleDegrees, HardMinimumCrossingAngleDegrees), 90.f, CrossingAlpha);
					const float CrossingRad = FMath::DegreesToRadians(CrossingDeg);
					for (int32 OrientationIndex = 0; OrientationIndex < OrientationCount; ++OrientationIndex)
					{
						const float Around = 2.f * PI * float(OrientationIndex) / float(OrientationCount);
						const FVector Ring = BasisA * FMath::Cos(Around) + BasisB * FMath::Sin(Around);
						const FVector DefenseDirection = (IncomingDir * FMath::Cos(CrossingRad) +
							 Ring * FMath::Sin(CrossingRad)).GetSafeNormal();
						++LastDiagnostics.GeneratedCandidates;
						FParryCandidate Candidate;
						if (!EvaluateCandidate(
							Shoulder,
							IncomingBase, IncomingTip, Contact, IncomingDir,
							Segment.TimeSeconds, IncomingAlpha,
							DefenseDirection, DefenderFraction,
							BladeLength, CurrentWeaponTransform,
							CurrentDefenseDirection, Candidate))
							continue;
						const float BodyTimingMargin = FMath::Clamp(
								(BodyIntersectionTime - Segment.TimeSeconds) / 0.75f,
								0.f,
								1.f);
						const float CandidateBodyClearance = CombatBodyProbes::FindMinimumBodyClearance(
								Mesh, Candidate.DefenseBase, Candidate.DefenseTip);
						const float CandidateBodyClearanceQuality = FMath::Clamp(
								CandidateBodyClearance /
									FMath::Max(LocalConfig->BodyClearancePreferenceDistance, 1.f),
								0.f, 1.f);
						Candidate.BodyClearance = CandidateBodyClearance;
						Candidate.Quality +=
							2.f * BodyTimingMargin +
							LocalConfig->BodyClearanceQualityWeight * CandidateBodyClearanceQuality;
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
	// Calculate final quality with policy
	for (auto& Candidate : Candidates)
	{
		constexpr float CrossingPreferenceBoost = 3.f;
		const float TacticalMaxScore = FMath::Max(CrossingPreferenceBoost * LocalConfig->CrossingQualityWeight + LocalConfig->BladeCenterQualityWeight + LocalConfig->DramaticPoseQualityWeight + LocalConfig->IncomingTipContactQualityWeight + LocalConfig->BodyClearanceQualityWeight + 2.f, KINDA_SMALL_NUMBER);
		FCandidateRawMetrics RawMetrics;
		RawMetrics.TacticalQuality = FMath::Clamp(Candidate.Quality / TacticalMaxScore, 0.f, 1.f);
		RawMetrics.WristDeviationRadians = 0.f;
		RawMetrics.GeometricFreedom = 0.f;
		RawMetrics.bGeometricallyValid = true;
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
		GetTypeHash(GetFighter()),
		GetTypeHash(Attacker));
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

void UCombatExecutor_ProceduralParry::LogDiagnosticsOnce() const
{
	if (bDiagnosticsLogged) return;
	bDiagnosticsLogged = true;
	UE_LOG(LogTemp, Warning,
		TEXT("ParryDiag [%s] gen=%d valid=%d | broadReach=%d angle=%d anatomy=%d body=%d time=%d speed=%d | nearestArmFail=%.1fcm hand=%.1fcm arm=[%.1f,%.1f] incomingFraction=%.2f | bestQ=%.2f t=%.3f handSpeed=%.0f bladeSpeed=%.0f | %s"),
		*GetNameSafe(GetFighter()),
		LastDiagnostics.GeneratedCandidates, LastDiagnostics.ValidCandidates,
		LastDiagnostics.BroadReachRejected, LastDiagnostics.AngleRejected,
		LastDiagnostics.AnatomyRejected, LastDiagnostics.BodyRejected,
		LastDiagnostics.TimingRejected, LastDiagnostics.SpeedRejected,
		LastDiagnostics.BestAnatomyReachDeficitCm < TNumericLimits<float>::Max()
			? LastDiagnostics.BestAnatomyReachDeficitCm : -1.f,
		LastDiagnostics.FailedCandidateHandDistanceCm,
		LastDiagnostics.FailedCandidateMinReachCm,
		LastDiagnostics.FailedCandidateMaxReachCm,
		LastDiagnostics.FailedCandidateIncomingFraction,
		LastDiagnostics.BestQuality, LastDiagnostics.BestTimeUntilContact,
		LastDiagnostics.BestRequiredHandSpeed,
		LastDiagnostics.BestRequiredBladeAngularSpeed,
		LastParryEarlyExitReason.IsEmpty() ? TEXT("candidate locked") : *LastParryEarlyExitReason);
}

// ============================================================================
// Execution lifecycle
// ============================================================================

bool UCombatExecutor_ProceduralParry::OnInitialize(
	const FCombatTechniqueRequest& InRequest)
{
	UE_LOG(LogIronboundCombat, Log,
		TEXT("[PARRY] REQUEST_RECEIVED defender=%s technique=%s attacker=%s attackerPlan=%d hasContext=%d predictedContact=%d tti=%.3fs"),
		*GetNameSafe(GetFighter()), *InRequest.TechniqueId.ToString(),
		*GetNameSafe(InRequest.ThreatContext.Threat.Attacker),
		InRequest.ThreatContext.Threat.AttackerPlanId,
		InRequest.ThreatContext.bHasThreat ? 1 : 0,
		InRequest.ThreatContext.Threat.bHasPredictedContact ? 1 : 0,
		InRequest.ThreatContext.Threat.TimeToImpact);
	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	if (!LocalConfig)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("ProceduralParry [%s | %s]: row does not use a procedural-parry execution config"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString());

		return false;
	}

	if (!InRequest.ThreatContext.bHasThreat)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("ProceduralParry [%s | %s]: request carries no threat context"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString());

		return false;
	}

	if (!InRequest.ThreatContext.Threat.bHasPredictedContact)
	{
		UE_LOG(
			LogIronboundCombat,
			Log,
			TEXT("ProceduralParry [%s | %s]: incoming blade already intersected defender body"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString());

		return false;
	}

	AActor* Attacker = InRequest.ThreatContext.Threat.Attacker;
	if (!IsValid(Attacker))
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("[PARRY] REQUEST_REJECTED stage=ThreatValidation reason=attacker invalid technique=%s plan=%d"),
			*InRequest.TechniqueId.ToString(),
			InRequest.ThreatContext.Threat.AttackerPlanId);
		return false;
	}

	UCombatEquipmentComponent* Equipment = GetEquipment();
	if (!Equipment || !Equipment->bReady || !Equipment->Definition)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("[PARRY] REQUEST_REJECTED stage=Equipment reason=no ready equipment defender=%s technique=%s plan=%d"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString(),
			InRequest.ThreatContext.Threat.AttackerPlanId);

		return false;
	}

	if (!CalculateArmDimensions())
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT("[PARRY] REQUEST_REJECTED stage=Anatomy reason=required parry arm bones unavailable defender=%s technique=%s plan=%d"),
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString(),
			InRequest.ThreatContext.Threat.AttackerPlanId);

		return false;
	}

	// Parry skill is a learned proficiency read-through; the config value is
	// the parity fallback for fighters without the skill.
	Policy.TacticalWeight = LocalConfig->TacticalWeight;
	Policy.WristSharpness = LocalConfig->WristSharpness;
	Policy.FreedomSharpness = LocalConfig->FreedomSharpness;
	Policy.QualityBandWidth = LocalConfig->QualityBandWidth;
	Policy.ParrySkill = GetSkillProficiency(
		TEXT("Parry"),
		LocalConfig->ParrySkillFallback);

	if (!FindBestParryCandidate(ActiveParryCandidate))
	{
		UE_LOG(
			LogIronboundCombat,
			Log,
			TEXT("[PARRY] REQUEST_REJECTED stage=CandidateGeneration reason=%s defender=%s technique=%s attacker=%s plan=%d"),
			LastParryEarlyExitReason.IsEmpty() ? TEXT("no feasible candidate survived constraints") : *LastParryEarlyExitReason,
			*GetNameSafe(GetFighter()),
			*InRequest.TechniqueId.ToString(), *GetNameSafe(Attacker),
			InRequest.ThreatContext.Threat.AttackerPlanId);

		return false;
	}

	InitializeExecutionPose();

	if (UCombatBodyComponent* Body = GetBody())
	{
		Body->BeginParryBrace();
		bBraceActive = true;
	}
	UE_LOG(LogIronboundCombat, Log,
		TEXT("[PARRY] BODY_BRACE_ACTIVE defender=%s attacker=%s plan=%d handTarget=%s"),
		*GetNameSafe(GetFighter()), *GetNameSafe(Attacker),
		InRequest.ThreatContext.Threat.AttackerPlanId,
		*ActiveParryCandidate.RequiredHandTransform.GetLocation().ToCompactString());

	UE_LOG(LogTemp, Warning,
		TEXT("Parry [%s]: LOCKED quality=%.2f t=%.3f hand=%.0fcm/s blade=%.0fdeg/s cross=%.0f enemyBlade=%.0f%%"),
		*GetNameSafe(GetFighter()), ActiveParryCandidate.Quality, ActiveParryCandidate.TimeUntilContact,
		ActiveParryCandidate.RequiredHandSpeed, ActiveParryCandidate.RequiredBladeAngularSpeed,
		ActiveParryCandidate.IntersectionAngleDegrees, ActiveParryCandidate.IncomingBladeFraction * 100.f);

	Phase = EParryExecutionPhase::Intercept;

	// Hard bound so a cancelled/finished attacker cannot leave the executor
	// waiting for a contact that will never come.
	UWorld* World = GetWorld();
	InterceptDeadlineWorldTime =
		World
			? World->GetTimeSeconds() + FMath::Max(ActiveParryCandidate.TimeUntilContact, 0.f) + 1.f
			: 0.f;

	return true;
}

void UCombatExecutor_ProceduralParry::OnTick(float DeltaTime)
{
	AActor* Fighter = GetFighter();
	UWorld* World = GetWorld();
	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	if (!Fighter || !World || !LocalConfig)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Phase == EParryExecutionPhase::Intercept)
	{
		const FCombatThreat& PlannedThreat = Request.ThreatContext.Threat;
		UCombatExecutionComponent* AttackerExecution = PlannedThreat.Attacker
			? PlannedThreat.Attacker->FindComponentByClass<UCombatExecutionComponent>()
			: nullptr;
		const FCombatCommittedStrike LiveStrike = AttackerExecution
			? AttackerExecution->GetCommittedStrike()
			: FCombatCommittedStrike();
		if (!LiveStrike.bValid || LiveStrike.PlanId != PlannedThreat.AttackerPlanId ||
			LiveStrike.TechniqueId != PlannedThreat.TechniqueId)
		{
			UE_LOG(LogIronboundCombat, Log,
				TEXT("[PARRY] EXECUTION_ABORTED reason=incoming attack ended cancelled or changed defender=%s attacker=%s plannedPlan=%d livePlan=%d"),
				*GetNameSafe(Fighter), *GetNameSafe(PlannedThreat.Attacker),
				PlannedThreat.AttackerPlanId, LiveStrike.bValid ? LiveStrike.PlanId : 0);
			FinishExecution(TEXT("incoming attack ended, was cancelled, or changed"));
			return;
		}
	}

	UpdateExecutionPose(DeltaTime);

	if (!ActualSample.bTracking)
	{
		ActualSample = FActualParrySample();
		ActualSample.bTracking = true;
		ActualSample.PlannedCross = ActiveParryCandidate.IntersectionAngleDegrees;
	}

	float ActualBladeDistance = 0.f;
	const float ActualCross = GetActualBladeCrossing(GetFighter(), Request.ThreatContext.Threat.Attacker, ActualBladeDistance);
	if (ActualCross >= 0.f && ActualBladeDistance < ActualSample.ClosestDistance)
	{
		ActualSample.ClosestDistance = ActualBladeDistance;
		ActualSample.CrossAtClosest = ActualCross;
	}

	const FCombatThreat& Threat = Request.ThreatContext.Threat;
	UCombatEquipmentComponent* DefenderEquipment = GetEquipment();
	UCombatEquipmentComponent* AttackerEquipment = Threat.Attacker
		? Threat.Attacker->FindComponentByClass<UCombatEquipmentComponent>() : nullptr;
	UStaticMeshComponent* DefenderWeapon = DefenderEquipment ? DefenderEquipment->GetWeapon() : nullptr;
	UStaticMeshComponent* AttackerWeapon = AttackerEquipment ? AttackerEquipment->GetWeapon() : nullptr;
	if (DefenderEquipment && DefenderEquipment->Definition && DefenderWeapon &&
		AttackerEquipment && AttackerEquipment->Definition && AttackerWeapon)
	{
		const FTransform DefenderTransform = DefenderWeapon->GetComponentTransform();
		const FTransform AttackerTransform = AttackerWeapon->GetComponentTransform();
		const FVector DefenderBase = DefenderTransform.TransformPosition(DefenderEquipment->Definition->BladeBase);
		const FVector DefenderTip = DefenderTransform.TransformPosition(DefenderEquipment->Definition->BladeTip);
		const FVector AttackerBase = AttackerTransform.TransformPosition(AttackerEquipment->Definition->BladeBase);
		const FVector AttackerTip = AttackerTransform.TransformPosition(AttackerEquipment->Definition->BladeTip);
		if (ActualSample.bHasPreviousWeaponSamples)
		{
			float ClosestSweptDistance = TNumericLimits<float>::Max();
			float CrossingAtClosest = -1.f;
			constexpr int32 TemporalSamples = 4;
			for (int32 SampleIndex = 0; SampleIndex <= TemporalSamples; ++SampleIndex)
			{
				const float Alpha = static_cast<float>(SampleIndex) / TemporalSamples;
				const FVector SampleDefenderBase = FMath::Lerp(ActualSample.PreviousDefenderBase, DefenderBase, Alpha);
				const FVector SampleDefenderTip = FMath::Lerp(ActualSample.PreviousDefenderTip, DefenderTip, Alpha);
				const FVector SampleAttackerBase = FMath::Lerp(ActualSample.PreviousAttackerBase, AttackerBase, Alpha);
				const FVector SampleAttackerTip = FMath::Lerp(ActualSample.PreviousAttackerTip, AttackerTip, Alpha);
				FVector ClosestDefenderPoint, ClosestAttackerPoint;
				FMath::SegmentDistToSegmentSafe(
					SampleDefenderBase, SampleDefenderTip,
					SampleAttackerBase, SampleAttackerTip,
					ClosestDefenderPoint, ClosestAttackerPoint);
				const float Distance = FVector::Distance(ClosestDefenderPoint, ClosestAttackerPoint);
				if (Distance < ClosestSweptDistance)
				{
					ClosestSweptDistance = Distance;
					const FVector DefenderDirection = (SampleDefenderTip - SampleDefenderBase).GetSafeNormal();
					const FVector AttackerDirection = (SampleAttackerTip - SampleAttackerBase).GetSafeNormal();
					const float AbsDot = FMath::Clamp(
						FMath::Abs(FVector::DotProduct(DefenderDirection, AttackerDirection)), 0.f, 1.f);
					CrossingAtClosest = FMath::RadiansToDegrees(FMath::Acos(AbsDot));
				}
			}

			if (ClosestSweptDistance < ActualSample.ClosestDistance)
			{
				ActualSample.ClosestDistance = ClosestSweptDistance;
				ActualSample.CrossAtClosest = CrossingAtClosest;
			}

			float CurrentSourceTime = Threat.CurrentSourceTime;
			GetAttackerSourcePlaybackTime(CurrentSourceTime);
			const float TimeUntilCandidate = ActiveParryCandidate.IncomingTime - CurrentSourceTime;
			const bool bAtPredictedInterceptTime = Phase != EParryExecutionPhase::Recover &&
				TimeUntilCandidate <= 0.15f && TimeUntilCandidate >= -0.15f;
			constexpr float ActualBladeContactToleranceCm = 4.f;
			if (!ActualSample.bInterceptionConfirmed && bAtPredictedInterceptTime &&
				ClosestSweptDistance <= ActualBladeContactToleranceCm && CrossingAtClosest >= 60.f)
			{
				ActualSample.bInterceptionConfirmed = true;
				ActualSample.ConfirmedDistance = ClosestSweptDistance;
				ActualSample.ConfirmedCross = CrossingAtClosest;
				if (UCombatExecutionComponent* Execution = GetExecutionComponent())
				{
					Execution->ConfirmParryInterception(
						Threat.Attacker,
						Threat.AttackerPlanId,
						Threat.TechniqueId,
						GetRequest().TechniqueId,
						ClosestSweptDistance,
						CrossingAtClosest);
				}
			}
		}
		else
		{
			ActualSample.bHasPreviousWeaponSamples = true;
		}
		ActualSample.PreviousDefenderBase = DefenderBase;
		ActualSample.PreviousDefenderTip = DefenderTip;
		ActualSample.PreviousAttackerBase = AttackerBase;
		ActualSample.PreviousAttackerTip = AttackerTip;
	}

	switch (Phase)
	{
	case EParryExecutionPhase::Intercept:
	{
		// Live contact timing from the attacker's actual playback.
		float CurrentSourceTime = Request.ThreatContext.Threat.CurrentSourceTime;
		GetAttackerSourcePlaybackTime(CurrentSourceTime);

		const float TimeUntilContact =
			ActiveParryCandidate.IncomingTime - CurrentSourceTime;

		if (TimeUntilContact <= 0.f)
		{
			Phase = EParryExecutionPhase::Contact;
			ContactAtWorldTime = Now;
			ContactDeadlineWorldTime = Now + LocalConfig->LeadHoldSeconds;

			if (!bContactResolved)
			{
				MarkRecordContactResolved();
				bContactResolved = true;
			}
		}
		else if (Now > InterceptDeadlineWorldTime)
		{
			FinishExecution(TEXT("contact never arrived"));
			return;
		}

		break;
	}

	case EParryExecutionPhase::Contact:
	{
		if (Now >= ContactDeadlineWorldTime)
		{
			Phase = EParryExecutionPhase::Recover;
			EndParryBrace();
		}

		break;
	}

	case EParryExecutionPhase::Recover:
	{
		FinishExecution(TEXT("parry complete"));
		return;
	}
	}

	if (LocalConfig->bDrawParryDebug)
	{
		DrawAnatomyDebug();
		DrawParrySolutionDebug();
		DrawDiagnosticsDebug();
	}
}

void UCombatExecutor_ProceduralParry::OnFinish()
{
	EndParryBrace();

	// Per-execution actual-crossing diagnostics (was the global ParryActual map).
	if (ActualSample.bTracking)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("ParryActual [%s]: result=%s plannedCross=%.0f actualCrossAtClosest=%.1f closest=%.1fcm confirmedDistance=%.1fcm confirmedCross=%.1fdeg"),
			*GetNameSafe(GetFighter()),
			ActualSample.bInterceptionConfirmed ? TEXT("ConfirmedInterception") : TEXT("NoInterception"),
			ActualSample.PlannedCross, ActualSample.CrossAtClosest,
			ActualSample.ClosestDistance, ActualSample.ConfirmedDistance, ActualSample.ConfirmedCross);

		ActualSample = FActualParrySample();
	}
}

void UCombatExecutor_ProceduralParry::EndParryBrace()
{
	if (!bBraceActive)
	{
		return;
	}

	if (UCombatBodyComponent* Body = GetBody())
	{
		if (Body->IsParryBraced())
		{
			Body->EndParryBrace();
		}
	}

	bBraceActive = false;
}

void UCombatExecutor_ProceduralParry::InitializeExecutionPose()
{
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	if (!Mesh || !LocalConfig) return;
	ExecutedHandTransform = Mesh->GetSocketTransform(ResolveHandBone(), RTS_World);
}

void UCombatExecutor_ProceduralParry::UpdateExecutionPose(float DeltaTime)
{
	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	if (!LocalConfig) return;
	const FVector CurrentLocation = ExecutedHandTransform.GetLocation();
	const FVector TargetLocation = ActiveParryCandidate.RequiredHandTransform.GetLocation();
	const FVector NewLocation = FMath::VInterpConstantTo(
		CurrentLocation, TargetLocation, DeltaTime, LocalConfig->ParryMovementSpeed);
	const FQuat CurrentRotation = ExecutedHandTransform.GetRotation();
	const FQuat TargetRotation = ActiveParryCandidate.RequiredHandTransform.GetRotation();
	const float AngleRadians = CurrentRotation.AngularDistance(TargetRotation);
	FQuat NewRotation = TargetRotation;
	if (AngleRadians > KINDA_SMALL_NUMBER)
	{
		const float MaxStepRadians = FMath::DegreesToRadians(LocalConfig->ParryRotationSpeedDegrees) * DeltaTime;
		const float Alpha = FMath::Clamp(MaxStepRadians / AngleRadians, 0.f, 1.f);
		NewRotation = FQuat::Slerp(CurrentRotation, TargetRotation, Alpha).GetNormalized();
	}
	ExecutedHandTransform.SetLocation(NewLocation);
	ExecutedHandTransform.SetRotation(NewRotation);
	ExecutedHandTransform.SetScale3D(FVector::OneVector);
}

bool UCombatExecutor_ProceduralParry::GetHandTarget(FTransform& OutHandTarget) const
{
	OutHandTarget = ExecutedHandTransform;
	return true;
}

// ============================================================================
// Debug presentation
// ============================================================================

void UCombatExecutor_ProceduralParry::DrawAnatomyDebug() const
{
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	const UExecConfig_ProceduralParry* LocalConfig = ParryConfig();
	if (!Mesh || !LocalConfig) return;
	const FVector Shoulder = Mesh->GetSocketLocation(ResolveUpperArmBone());
	const FVector Elbow = Mesh->GetSocketLocation(ResolveLowerArmBone());
	const FVector Hand = Mesh->GetSocketLocation(ResolveHandBone());
	DrawDebugSphere(GetWorld(), Shoulder, 4.f, 10, FColor::Cyan, false, 0.f, 0, 1.f);
	DrawDebugSphere(GetWorld(), Elbow, 4.f, 10, FColor::Yellow, false, 0.f, 0, 1.f);
	DrawDebugSphere(GetWorld(), Hand, 4.f, 10, FColor::Green, false, 0.f, 0, 1.f);
}

void UCombatExecutor_ProceduralParry::DrawParrySolutionDebug() const
{
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	UWorld* World = GetWorld();
	if (!Mesh || !World) return;
	const FParryCandidate& C = ActiveParryCandidate;
	// Paused view: only the cyan selected defender blade target.
	if (World->IsPaused())
	{
		DrawDebugLine(
			World, C.DefenseBase, C.DefenseTip,
			FColor::Cyan, false, 0.f, 0, 6.f);
		return;
	}
	DrawDebugLine(World, C.IncomingBase, C.IncomingTip,
		FColor::Red, false, 0.f, 0, 6.f);
	DrawDebugLine(World, C.DefenseBase, C.DefenseTip,
		FColor::Cyan, false, 0.f, 0, 6.f);
	DrawDebugSphere(World, C.ContactPoint, 6.f, 12,
		FColor::Green, false, 0.f, 0, 2.f);
	const FVector Shoulder = Mesh->GetSocketLocation(ResolveUpperArmBone());
	DrawDebugLine(World, Shoulder, C.RequiredHandPosition,
		FColor::Orange, false, 0.f, 0, 3.f);
	DrawDebugSphere(World, ExecutedHandTransform.GetLocation(), 4.f, 10,
		FColor::Blue, false, 0.f, 0, 2.f);
	const FString Text = FString::Printf(
		TEXT("Q %.2f | cross %.0f | enemyBlade %.0f%% | t %.3f | %s"),
		C.Quality, C.IntersectionAngleDegrees, C.IncomingBladeFraction * 100.f,
		C.TimeUntilContact,
		Phase == EParryExecutionPhase::Contact ? TEXT("CONTACT") : TEXT("MOVE"));
	DrawDebugString(
		World, C.ContactPoint + FVector(0.f, 0.f, 14.f),
		Text, nullptr, FColor::White, 0.f, true);
}

void UCombatExecutor_ProceduralParry::DrawDiagnosticsDebug() const
{
	UWorld* World = GetWorld();
	AActor* Fighter = GetFighter();
	if (!World || !Fighter) return;
	const FString Text = FString::Printf(
		TEXT("Parry gen=%d valid=%d | anatomy=%d body=%d speed=%d | Q=%.2f"),
		LastDiagnostics.GeneratedCandidates,
		LastDiagnostics.ValidCandidates,
		LastDiagnostics.AnatomyRejected,
		LastDiagnostics.BodyRejected,
		LastDiagnostics.SpeedRejected,
		LastDiagnostics.BestQuality);
	DrawDebugString(
		World,
		Fighter->GetActorLocation() + FVector(0.f, 0.f, 220.f),
		Text, nullptr,
		LastDiagnostics.ValidCandidates > 0 ? FColor::Green : FColor::Red,
		0.f, true);
}
