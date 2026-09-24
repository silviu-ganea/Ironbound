#include "Combat/CombatExecutor_ProceduralStrike.h"

#include "Combat/CombatBodyComponent.h"
#include "Combat/CombatBodyProbes.h"
#include "Combat/CombatEquipmentComponent.h"
#include "Combat/CombatExecutionComponent.h"
#include "Combat/CombatTarget.h"
#include "Combat/CombatTechniqueExecutionConfigs.h"
#include "Combat/WeaponDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "Math/RotationMatrix.h"
#include "Ironbound.h"

namespace
{
	FTransform BlendHandTransforms(const FTransform& A, const FTransform& B, float Alpha)
	{
		FTransform Result;
		Result.SetLocation(FMath::Lerp(A.GetLocation(), B.GetLocation(), Alpha));
		Result.SetRotation(FQuat::Slerp(A.GetRotation(), B.GetRotation(), Alpha).GetNormalized());
		Result.SetScale3D(FVector::OneVector);
		return Result;
	}

	FTransform WithUnitScale(FTransform Transform)
	{
		Transform.SetScale3D(FVector::OneVector);
		return Transform;
	}

	float YawToward(const FVector& From, const FVector& To, float FallbackYaw)
	{
		const FVector Direction = (To - From).GetSafeNormal2D();
		return Direction.IsNearlyZero() ? FallbackYaw : Direction.Rotation().Yaw;
	}

	/**
	 * Root placement that puts the attacker's shoulder inside its own reach
	 * window of the target point. Everything is derived from the measured
	 * reach; no distances are authored.
	 */
	bool SolveStance(
		const FProceduralStrikeArm& Arm,
		const UExecConfig_ProceduralStrike& Config,
		const FTransform& Root,
		const FVector& ShoulderWorld,
		const FVector& TargetPoint,
		const FVector& TargetActorLocation,
		float MinSeparationCm,
		FTransform& OutStance)
	{
		const float Reach = Arm.GetReach();
		if (Reach <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const FVector ShoulderLocal = Root.InverseTransformPosition(ShoulderWorld);
		const float RootZ = Root.GetLocation().Z;
		auto ShoulderAt = [&](const FVector& Location, float Yaw)
		{
			return FTransform(FRotator(0.f, Yaw, 0.f), Location, Root.GetScale3D())
				.TransformPosition(ShoulderLocal);
		};
		auto InWindow = [&](const FVector& Shoulder)
		{
			const float Distance = FVector::Dist(Shoulder, TargetPoint);
			return Distance >= Config.MinReachFraction * Reach &&
				   Distance <= Config.MaxReachFraction * Reach;
		};
		auto Separated = [&](const FVector& Location)
		{
			return FVector::Dist2D(Location, TargetActorLocation) >= MinSeparationCm;
		};

		// Prefer standing still when the current position already works.
		FVector Location = Root.GetLocation();
		float Yaw = YawToward(Location, TargetActorLocation, Root.Rotator().Yaw);
		if (InWindow(ShoulderAt(Location, Yaw)) && Separated(Location))
		{
			OutStance = FTransform(FRotator(0.f, Yaw, 0.f), Location, Root.GetScale3D());
			return true;
		}

		const float Desired = Config.PreferredReachFraction * Reach;
		for (int32 Iteration = 0; Iteration < 6; ++Iteration)
		{
			const FVector Shoulder = ShoulderAt(Location, Yaw);
			const float HeightDelta = TargetPoint.Z - Shoulder.Z;
			const float HorizontalSq = FMath::Square(Desired) - FMath::Square(HeightDelta);
			if (HorizontalSq <= KINDA_SMALL_NUMBER)
			{
				// Target point is vertically farther than the preferred reach.
				return false;
			}

			FVector Away = (Shoulder - TargetPoint).GetSafeNormal2D();
			if (Away.IsNearlyZero())
			{
				Away = -Root.GetRotation().GetForwardVector().GetSafeNormal2D();
			}

			const FVector DesiredShoulder = TargetPoint + Away * FMath::Sqrt(HorizontalSq);
			Location.X += DesiredShoulder.X - Shoulder.X;
			Location.Y += DesiredShoulder.Y - Shoulder.Y;
			Location.Z = RootZ;
			Yaw = YawToward(Location, TargetActorLocation, Yaw);
		}

		if (!Separated(Location))
		{
			FVector FromTarget = Location - TargetActorLocation;
			FromTarget.Z = 0.f;
			const FVector Direction = FromTarget.IsNearlyZero()
				? -Root.GetRotation().GetForwardVector().GetSafeNormal2D()
				: FromTarget.GetSafeNormal();
			Location = TargetActorLocation + Direction * MinSeparationCm;
			Location.Z = RootZ;
			Yaw = YawToward(Location, TargetActorLocation, Yaw);
		}

		if (!InWindow(ShoulderAt(Location, Yaw)))
		{
			return false;
		}

		OutStance = FTransform(FRotator(0.f, Yaw, 0.f), Location, Root.GetScale3D());
		return true;
	}
}

// ============================================================================
// Arm model
// ============================================================================

bool UCombatExecutor_ProceduralStrike::BuildArmModel(
	const UExecConfig_ProceduralStrike* InConfig,
	AActor* Fighter,
	FProceduralStrikeArm& OutArm)
{
	OutArm = FProceduralStrikeArm();
	if (!InConfig || !Fighter)
	{
		return false;
	}

	UCombatEquipmentComponent* Equipment = Fighter->FindComponentByClass<UCombatEquipmentComponent>();
	USkeletalMeshComponent* Mesh = Fighter->FindComponentByClass<USkeletalMeshComponent>();
	if (!Equipment || !Equipment->bReady || !Equipment->Definition || !Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		return false;
	}

	auto Resolve = [](FName FromGrip, FName Fallback) { return FromGrip.IsNone() ? Fallback : FromGrip; };
	OutArm.UpperArmBone = Resolve(Equipment->GetUpperArmBone(), InConfig->UpperArmBone);
	OutArm.LowerArmBone = Resolve(Equipment->GetLowerArmBone(), InConfig->LowerArmBone);
	OutArm.HandBone = Resolve(Equipment->GetHandBone(), InConfig->HandBone);

	const int32 UpperIndex = Mesh->GetBoneIndex(OutArm.UpperArmBone);
	const int32 LowerIndex = Mesh->GetBoneIndex(OutArm.LowerArmBone);
	const int32 HandIndex = Mesh->GetBoneIndex(OutArm.HandBone);
	if (UpperIndex == INDEX_NONE || LowerIndex == INDEX_NONE || HandIndex == INDEX_NONE)
	{
		return false;
	}

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

	const FTransform UpperCS = GetCS(UpperIndex);
	const FTransform LowerCS = GetCS(LowerIndex);
	const FTransform HandCS = GetCS(HandIndex);

	// Reference skeleton is component space; the fighter's world size is its scale.
	const float MeshScale = FMath::Max(Mesh->GetComponentScale().GetAbsMax(), KINDA_SMALL_NUMBER);
	OutArm.UpperArmLength = FVector::Distance(UpperCS.GetLocation(), LowerCS.GetLocation()) * MeshScale;
	OutArm.ForearmLength = FVector::Distance(LowerCS.GetLocation(), HandCS.GetLocation()) * MeshScale;
	OutArm.NeutralWristRelationship = HandCS.GetRelativeTransform(LowerCS);
	OutArm.ReferenceForearmDirLocal = LowerCS.InverseTransformVectorNoScale(
		(HandCS.GetLocation() - LowerCS.GetLocation()).GetSafeNormal()).GetSafeNormal();

	const UWeaponDefinition* Definition = Equipment->Definition;
	OutArm.BladeBaseLocal = Definition->BladeBase;
	OutArm.BladeTipLocal = Definition->BladeTip;
	OutArm.BladeAxisLocal = (Definition->BladeTip - Definition->BladeBase).GetSafeNormal();
	OutArm.ContactLocal = FMath::Lerp(
		Definition->BladeBase, Definition->BladeTip,
		FMath::Clamp(InConfig->ContactBladeFraction, 0.f, 1.f));
	OutArm.WeaponToHand = Equipment->GetWeaponToHand();
	OutArm.WeaponScale = Equipment->GetWeapon()
		? Equipment->GetWeapon()->GetComponentScale() : FVector::OneVector;

	if (OutArm.BladeAxisLocal.IsNearlyZero() ||
		OutArm.UpperArmLength <= KINDA_SMALL_NUMBER ||
		OutArm.ForearmLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Grip-to-contact distance is rotation invariant; measure it once.
	const FTransform ProbeWeapon(FQuat::Identity, FVector::ZeroVector, OutArm.WeaponScale);
	const FTransform ProbeHand = OutArm.WeaponToHand.Inverse() * ProbeWeapon;
	OutArm.HandToContactLength = FVector::Distance(
		ProbeWeapon.TransformPosition(OutArm.ContactLocal), ProbeHand.GetLocation());

	OutArm.bValid = true;
	return true;
}

// ============================================================================
// Opportunity query (controller-facing, geometry only)
// ============================================================================

void UCombatExecutor_ProceduralStrike::FindProceduralStrikeOpportunities(
	const UExecConfig_ProceduralStrike* InConfig,
	FName TechniqueId,
	AActor* Attacker,
	AActor* Target,
	FName RequiredRegion,
	float MaxMoveCm,
	TArray<FCombatAttackOpportunity>& OutOpportunities)
{
	OutOpportunities.Reset();
	if (!InConfig || !InConfig->CombatTargets || !IsValid(Attacker) || !IsValid(Target))
	{
		return;
	}

	USkeletalMeshComponent* AttackerMesh = Attacker->FindComponentByClass<USkeletalMeshComponent>();
	USkeletalMeshComponent* TargetMesh = Target->FindComponentByClass<USkeletalMeshComponent>();
	FProceduralStrikeArm LocalArm;
	if (!AttackerMesh || !TargetMesh || !BuildArmModel(InConfig, Attacker, LocalArm))
	{
		return;
	}

	const FTransform Root = Attacker->GetActorTransform();
	const FVector Shoulder = AttackerMesh->GetSocketLocation(LocalArm.UpperArmBone);
	const FVector TargetLocation = Target->GetActorLocation();
	const float MinSeparation = Attacker->GetSimpleCollisionRadius() + Target->GetSimpleCollisionRadius();
	const float MoveLimit = FMath::Max(MaxMoveCm, 1.f);

	for (const FName RegionName : InConfig->CombatTargets->GetRowNames())
	{
		if (!RequiredRegion.IsNone() && RegionName != RequiredRegion)
		{
			continue;
		}

		const FCombatTargetRow* TargetRow = InConfig->CombatTargets->FindRow<FCombatTargetRow>(
			RegionName, TEXT("ProceduralStrike opportunity"), false);
		if (!TargetRow)
		{
			continue;
		}

		FCombatAttackOpportunity Best;
		for (const FName Bone : TargetRow->Bones)
		{
			if (TargetMesh->GetBoneIndex(Bone) == INDEX_NONE)
			{
				continue;
			}

			const FVector TargetPoint = TargetMesh->GetSocketLocation(Bone);
			FTransform Stance;
			if (!SolveStance(LocalArm, *InConfig, Root, Shoulder, TargetPoint,
					TargetLocation, MinSeparation, Stance))
			{
				continue;
			}

			const float Move = FVector::Dist2D(Stance.GetLocation(), Root.GetLocation());
			if (Move > MoveLimit)
			{
				continue;
			}

			const float Quality =
				FMath::Clamp(TargetRow->Score / 100.f, 0.f, 1.f) - 0.1f * (Move / MoveLimit);
			if (Best.bFeasible && Quality <= Best.Quality)
			{
				continue;
			}

			Best = FCombatAttackOpportunity();
			Best.bFeasible = true;
			Best.TechniqueId = TechniqueId;
			Best.Region = RegionName;
			Best.Bone = Bone;
			Best.Stance = Stance;
			Best.TargetLocationAtQuery = TargetLocation;
			Best.ContactSample = 0;
			Best.MissCm = 0.f;
			Best.ContactScore = TargetRow->Score;
			Best.Quality = Quality;
			Best.MovementCostCm = Move;
			Best.StandoffCm = FVector::Dist2D(Stance.GetLocation(), TargetLocation);
			Best.AimPointAlongBlade = InConfig->ContactBladeFraction;
			Best.ContactFraction = InConfig->ContactBladeFraction;
		}

		if (Best.bFeasible)
		{
			OutOpportunities.Add(Best);
		}
	}
}

// ============================================================================
// Helpers
// ============================================================================

const UExecConfig_ProceduralStrike* UCombatExecutor_ProceduralStrike::StrikeConfig() const
{
	return Cast<UExecConfig_ProceduralStrike>(Config);
}

float UCombatExecutor_ProceduralStrike::Ease(float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	return T * T * (3.f - 2.f * T);
}

bool UCombatExecutor_ProceduralStrike::GetLiveTargetPoint(FVector& OutPoint) const
{
	const AActor* Target = PlannedTarget.Get();
	const USkeletalMeshComponent* TargetMesh = Target
		? Target->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (!TargetMesh || TargetMesh->GetBoneIndex(PlannedBone) == INDEX_NONE)
	{
		return false;
	}

	OutPoint = TargetMesh->GetSocketLocation(PlannedBone);
	return true;
}

FVector UCombatExecutor_ProceduralStrike::GetShoulderWorld() const
{
	const USkeletalMeshComponent* Mesh = GetFighterMesh();
	return Mesh ? Mesh->GetSocketLocation(Arm.UpperArmBone) : FVector::ZeroVector;
}

FVector UCombatExecutor_ProceduralStrike::GetFacingDirectionToTarget() const
{
	const AActor* Fighter = GetFighter();
	const AActor* Target = PlannedTarget.Get();
	if (!Fighter || !Target)
	{
		return FVector::ZeroVector;
	}

	return (Target->GetActorLocation() - Fighter->GetActorLocation()).GetSafeNormal2D();
}

float UCombatExecutor_ProceduralStrike::ComputeFacingError() const
{
	const AActor* Fighter = GetFighter();
	const FVector Direction = GetFacingDirectionToTarget();
	if (!Fighter || Direction.IsNearlyZero())
	{
		return 0.f;
	}

	return FMath::FindDeltaAngleDegrees(Fighter->GetActorRotation().Yaw, Direction.Rotation().Yaw);
}

bool UCombatExecutor_ProceduralStrike::IsInReachWindow(
	const FVector& Shoulder,
	const FVector& TargetPoint) const
{
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	const float Reach = Arm.GetReach();
	const float Distance = FVector::Dist(Shoulder, TargetPoint);
	return Cfg &&
		   Distance >= Cfg->MinReachFraction * Reach &&
		   Distance <= Cfg->MaxReachFraction * Reach;
}

// ============================================================================
// Pose model
// ============================================================================

bool UCombatExecutor_ProceduralStrike::ComputePoseOnPath(
	const FVector& Shoulder,
	const FVector& ContactPoint,
	const FVector& Direction,
	const FVector& LocalEdge,
	FTransform& OutWeapon,
	FTransform& OutHand) const
{
	// Blade axis: from the shoulder side outward, perpendicular to travel, so
	// the edge (not the point) leads through the straight strike line.
	const FVector ToContact = ContactPoint - Shoulder;
	FVector BladeAxis = ToContact - FVector::DotProduct(ToContact, Direction) * Direction;
	if (BladeAxis.SizeSquared() < 1.f)
	{
		return false;
	}
	BladeAxis.Normalize();

	const FQuat WorldFrame = FRotationMatrix::MakeFromXY(BladeAxis, Direction).ToQuat();
	const FQuat LocalFrame = FRotationMatrix::MakeFromXY(Arm.BladeAxisLocal, LocalEdge).ToQuat();
	const FQuat WeaponRotation = (WorldFrame * LocalFrame.Inverse()).GetNormalized();

	const FVector WeaponLocation =
		ContactPoint - WeaponRotation.RotateVector(Arm.WeaponScale * Arm.ContactLocal);
	OutWeapon = FTransform(WeaponRotation, WeaponLocation, Arm.WeaponScale);

	// Project convention: WeaponWorld = WeaponToHand * HandWorld.
	OutHand = Arm.WeaponToHand.Inverse() * OutWeapon;
	return !OutHand.ContainsNaN();
}

float UCombatExecutor_ProceduralStrike::ComputeOwnBodyClearance(
	const FVector& BladeBase,
	const FVector& BladeTip) const
{
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	if (!Mesh)
	{
		return -TNumericLimits<float>::Max();
	}

	int32 ProbeCount = 0;
	const CombatBodyProbes::FBodyProbe* Probes = CombatBodyProbes::GetBodyProbes(ProbeCount);
	float Minimum = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < ProbeCount; ++Index)
	{
		const CombatBodyProbes::FBodyProbe& Probe = Probes[Index];

		// The weapon arm necessarily touches its own blade grip.
		if (Probe.Bone == Arm.UpperArmBone || Probe.Bone == Arm.LowerArmBone ||
			Probe.Bone == Arm.HandBone || Mesh->GetBoneIndex(Probe.Bone) == INDEX_NONE)
		{
			continue;
		}

		const FVector BodyPoint = Mesh->GetSocketLocation(Probe.Bone);
		const FVector Closest = FMath::ClosestPointOnSegment(BodyPoint, BladeBase, BladeTip);
		Minimum = FMath::Min(Minimum, FVector::Distance(BodyPoint, Closest) - Probe.Radius);
	}

	return Minimum;
}

float UCombatExecutor_ProceduralStrike::ComputeWristDeviationRadians(
	const FVector& Shoulder,
	const FTransform& Hand) const
{
	// Same analytical model as the procedural parry: the smallest wrist
	// deviation over every elbow position the two-link arm allows.
	const FTransform UnitHand = WithUnitScale(Hand);
	const FTransform Forearm = Arm.NeutralWristRelationship.Inverse() * UnitHand;
	const FVector NeutralDir = Forearm.TransformVectorNoScale(Arm.ReferenceForearmDirLocal).GetSafeNormal();

	const FVector HandLocation = UnitHand.GetLocation();
	const FVector ShoulderToHand = HandLocation - Shoulder;
	const float D = ShoulderToHand.Size();
	const float A = Arm.UpperArmLength;
	const float B = Arm.ForearmLength;
	if (NeutralDir.IsNearlyZero() || D <= KINDA_SMALL_NUMBER || D > A + B || D < FMath::Abs(A - B))
	{
		return PI;
	}

	const FVector Axis = ShoulderToHand / D;
	const float Along = (A * A - B * B + D * D) / (2.f * D);
	const float Radius = FMath::Sqrt(FMath::Max(0.f, A * A - Along * Along));
	const FVector Center = Shoulder + Axis * Along;
	const FVector NeutralElbow = HandLocation - NeutralDir * B;

	FVector Elbow = Center;
	if (Radius > KINDA_SMALL_NUMBER)
	{
		const FVector ToNeutral = NeutralElbow - Center;
		const FVector Planar = ToNeutral - FVector::DotProduct(ToNeutral, Axis) * Axis;
		FVector Orthogonal1, Orthogonal2;
		Axis.FindBestAxisVectors(Orthogonal1, Orthogonal2);
		Elbow = Center + (Planar.IsNearlyZero() ? Orthogonal1 : Planar.GetSafeNormal()) * Radius;
	}

	const FVector ForearmDir = (HandLocation - Elbow).GetSafeNormal();
	return ForearmDir.IsNearlyZero()
		? PI
		: FMath::Acos(FMath::Clamp(FVector::DotProduct(NeutralDir, ForearmDir), -1.f, 1.f));
}

bool UCombatExecutor_ProceduralStrike::CheckPose(
	const FVector& Shoulder,
	const FVector& Forward,
	const FTransform& Weapon,
	const FTransform& Hand,
	float& OutComfort) const
{
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	const float ArmLength = Arm.GetArmLength();
	const FVector HandLocation = Hand.GetLocation();
	const float Distance = FVector::Distance(Shoulder, HandLocation);

	// Reach: the two-link arm must be able to place the hand here.
	if (Distance < Cfg->MinArmExtensionFraction * ArmLength ||
		Distance > Cfg->MaxArmExtensionFraction * ArmLength)
	{
		return false;
	}

	// Hand may not travel far behind the shoulder plane.
	if (FVector::DotProduct(HandLocation - Shoulder, Forward) <
		-Cfg->MaxHandBehindShoulderFraction * ArmLength)
	{
		return false;
	}

	const float WristDeviation = ComputeWristDeviationRadians(Shoulder, Hand);
	const float MaxWrist = FMath::DegreesToRadians(Cfg->MaxWristDeviationDegrees);
	if (WristDeviation > MaxWrist)
	{
		return false;
	}

	// Blade must not pass through the fighter's own body envelope.
	const FVector BladeBase = Weapon.TransformPosition(Arm.BladeBaseLocal);
	const FVector BladeTip = Weapon.TransformPosition(Arm.BladeTipLocal);
	if (ComputeOwnBodyClearance(BladeBase, BladeTip) < 0.f)
	{
		return false;
	}

	const float A = Arm.UpperArmLength;
	const float B = Arm.ForearmLength;
	const float CosJoint = FMath::Clamp((Distance * Distance - A * A - B * B) / (2.f * A * B), -1.f, 1.f);
	const float Freedom = FMath::Sqrt(FMath::Max(0.f, 1.f - CosJoint * CosJoint));
	const float WristComfort = 1.f - FMath::Clamp(WristDeviation / MaxWrist, 0.f, 1.f);

	OutComfort = 0.6f * WristComfort + 0.4f * Freedom;
	return true;
}

bool UCombatExecutor_ProceduralStrike::EvaluateStrikePath(
	const FVector& Shoulder,
	const FVector& Forward,
	const FVector& Start,
	const FVector& End,
	const FVector& LocalEdge,
	float& OutComfort) const
{
	const FVector Direction = (End - Start).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	const int32 Samples = FMath::Max(2, StrikeConfig()->PathValidationSamples);
	float MinimumComfort = 1.f;
	for (int32 Index = 0; Index <= Samples; ++Index)
	{
		const FVector Contact = FMath::Lerp(Start, End, float(Index) / float(Samples));
		FTransform Weapon, Hand;
		float Comfort = 0.f;
		if (!ComputePoseOnPath(Shoulder, Contact, Direction, LocalEdge, Weapon, Hand) ||
			!CheckPose(Shoulder, Forward, Weapon, Hand, Comfort))
		{
			return false;
		}
		MinimumComfort = FMath::Min(MinimumComfort, Comfort);
	}

	OutComfort = MinimumComfort;
	return true;
}

bool UCombatExecutor_ProceduralStrike::EvaluateChargeTravel(
	const FVector& Shoulder,
	const FVector& Forward,
	const FTransform& FromHand,
	const FTransform& ToHand) const
{
	// The travel starts from whatever pose animation currently holds, so only
	// hard constraints (reach and self-intersection) apply along the way.
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	const int32 Samples = FMath::Max(2, Cfg->PathValidationSamples);
	const float ArmLength = Arm.GetArmLength();
	for (int32 Index = 1; Index < Samples; ++Index)
	{
		FTransform Hand = BlendHandTransforms(FromHand, ToHand, float(Index) / float(Samples));
		Hand.SetScale3D(ToHand.GetScale3D());
		FTransform Weapon = Arm.WeaponToHand * Hand;
		Weapon.SetScale3D(Arm.WeaponScale);

		const float Distance = FVector::Distance(Shoulder, Hand.GetLocation());
		if (Distance > Cfg->MaxArmExtensionFraction * ArmLength)
		{
			return false;
		}

		if (ComputeOwnBodyClearance(
				Weapon.TransformPosition(Arm.BladeBaseLocal),
				Weapon.TransformPosition(Arm.BladeTipLocal)) < 0.f)
		{
			return false;
		}
	}

	return true;
}

void UCombatExecutor_ProceduralStrike::BuildLocalEdgeCandidates(TArray<FVector>& OutEdges) const
{
	OutEdges.Reset();
	const FVector Axis = Arm.BladeAxisLocal;

	const UCombatEquipmentComponent* Equipment = GetEquipment();
	const UWeaponDefinition* Definition = Equipment ? Equipment->Definition.Get() : nullptr;
	if (Definition && !Definition->StrikeEdgeDirection.IsNearlyZero())
	{
		const FVector Edge = (Definition->StrikeEdgeDirection -
			FVector::DotProduct(Definition->StrikeEdgeDirection, Axis) * Axis).GetSafeNormal();
		if (!Edge.IsNearlyZero())
		{
			OutEdges.Add(Edge);
			if (Definition->bDoubleEdged)
			{
				OutEdges.Add(-Edge);
			}
			return;
		}
	}

	FVector Orthogonal1, Orthogonal2;
	Axis.FindBestAxisVectors(Orthogonal1, Orthogonal2);
	const int32 Rolls = FMath::Max(1, StrikeConfig()->UnknownEdgeRollSamples);
	for (int32 Index = 0; Index < Rolls; ++Index)
	{
		const float Angle = 2.f * PI * float(Index) / float(Rolls);
		OutEdges.Add(FQuat(Axis, Angle).RotateVector(Orthogonal1));
	}
}

// ============================================================================
// Lifecycle
// ============================================================================

bool UCombatExecutor_ProceduralStrike::OnInitialize(const FCombatTechniqueRequest& InRequest)
{
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	AActor* Fighter = GetFighter();
	if (!Cfg || !Cfg->CombatTargets || !Fighter)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("ProceduralStrike [%s | %s]: missing procedural-strike config or CombatTargets"),
			*GetNameSafe(Fighter), *InRequest.TechniqueId.ToString());
		return false;
	}

	if (!IsValid(InRequest.Target))
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("ProceduralStrike [%s | %s]: request has no valid target"),
			*GetNameSafe(Fighter), *InRequest.TechniqueId.ToString());
		return false;
	}

	if (!BuildArmModel(Cfg, Fighter, Arm))
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("ProceduralStrike [%s | %s]: arm or weapon geometry unavailable"),
			*GetNameSafe(Fighter), *InRequest.TechniqueId.ToString());
		return false;
	}

	PlannedTarget = InRequest.Target;

	if (InRequest.PlannedOpportunity.bFeasible)
	{
		if (!AdoptOpportunity(InRequest.PlannedOpportunity))
		{
			UE_LOG(LogIronboundCombat, Log,
				TEXT("ProceduralStrike [%s | %s]: planned opportunity rejected (plan %d)"),
				*GetNameSafe(Fighter), *InRequest.TechniqueId.ToString(), InRequest.PlanId);
			return false;
		}
	}
	else if (InRequest.PlanId > 0)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("[AI] EXECUTION PlanId=%d Result=Refused Reason=MissingSelectedOpportunity"), InRequest.PlanId);
		return false;
	}
	else
	{
		// Unplanned request (player input): choose the best region ourselves.
		const FName Region = InRequest.TargetRegion.IsNone() ? Cfg->DefaultTargetRegion : InRequest.TargetRegion;
		TArray<FCombatAttackOpportunity> Opportunities;
		FindProceduralStrikeOpportunities(Cfg, InRequest.TechniqueId, Fighter, InRequest.Target,
			Region, 2.f * Arm.GetReach(), Opportunities);

		const FCombatAttackOpportunity* Best = nullptr;
		for (const FCombatAttackOpportunity& Opportunity : Opportunities)
		{
			if (!Best || Opportunity.Quality > Best->Quality)
			{
				Best = &Opportunity;
			}
		}

		if (!Best || !AdoptOpportunity(*Best))
		{
			UE_LOG(LogIronboundCombat, Log,
				TEXT("ProceduralStrike [%s | %s]: no reachable target region (reach %.1fcm)"),
				*GetNameSafe(Fighter), *InRequest.TechniqueId.ToString(), Arm.GetReach());
			return false;
		}
	}

	UWorld* World = GetWorld();
	RequestWorldTime = World ? World->GetTimeSeconds() : 0.f;
	Phase = EPhase::Approach;

	CachedRequirement = FCombatEngagementRequirement();
	CachedRequirement.bHasRequirement = true;
	CachedRequirement.DesiredLocation = PlannedStance.GetLocation();
	CachedRequirement.DesiredFacing = PlannedStance.Rotator();
	CachedRequirement.ArrivalTolerance = FMath::Max(1.f, Cfg->ArrivalToleranceFraction * Arm.GetReach());
	CachedRequirement.FacingTolerance = Cfg->FacingToleranceDegrees;
	CachedRequirement.bMayMoveDuringExecution = true;

	UE_LOG(LogIronboundCombat, Log,
		TEXT("ProceduralStrike [%s | %s] planned: plan=%d region=%s bone=%s arm=%.1fcm gripToContact=%.1fcm reach=%.1fcm stance=%s yaw=%.1f"),
		*GetNameSafe(Fighter), *InRequest.TechniqueId.ToString(), InRequest.PlanId,
		*PlannedRegion.ToString(), *PlannedBone.ToString(), Arm.GetArmLength(),
		Arm.HandToContactLength, Arm.GetReach(),
		*PlannedStance.GetLocation().ToCompactString(), PlannedStance.Rotator().Yaw);

	return true;
}

bool UCombatExecutor_ProceduralStrike::AdoptOpportunity(const FCombatAttackOpportunity& Opportunity)
{
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	const AActor* Target = PlannedTarget.Get();
	const USkeletalMeshComponent* TargetMesh = Target
		? Target->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (!Cfg || !TargetMesh ||
		Opportunity.TechniqueId != GetRequest().TechniqueId ||
		TargetMesh->GetBoneIndex(Opportunity.Bone) == INDEX_NONE ||
		!Cfg->CombatTargets->FindRow<FCombatTargetRow>(Opportunity.Region, TEXT("ProceduralStrike adopt"), false) ||
		Opportunity.Stance.GetLocation().ContainsNaN())
	{
		return false;
	}

	const FName RequiredRegion = GetRequest().TargetRegion.IsNone()
		? Cfg->DefaultTargetRegion : GetRequest().TargetRegion;
	if (!RequiredRegion.IsNone() && RequiredRegion != Opportunity.Region)
	{
		return false;
	}

	PlannedRegion = Opportunity.Region;
	PlannedBone = Opportunity.Bone;
	PlannedStance = Opportunity.Stance;
	return true;
}

void UCombatExecutor_ProceduralStrike::OnTick(float DeltaTime)
{
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	AActor* Fighter = GetFighter();
	UWorld* World = GetWorld();
	if (!Cfg || !Fighter || !World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const float Elapsed = Now - PhaseStartWorldTime;
	const FTransform Root = Fighter->GetActorTransform();

	switch (Phase)
	{
	case EPhase::Approach:
	{
		UpdateApproach(Now);
		break;
	}

	case EPhase::Charge:
	{
		const float Alpha = Ease(Elapsed / Cfg->ChargeDurationSeconds);
		ExecutedHand = BlendHandTransforms(PhaseStartHandLocal, ChargeHandLocal, Alpha) * Root;
		ExecutedHand.SetScale3D(FVector::OneVector);

		if (Elapsed >= Cfg->ChargeDurationSeconds && !BeginStrike())
		{
			EnterRecovery(TEXT("strike line no longer anatomically feasible"));
		}
		break;
	}

	case EPhase::Strike:
	{
		const float Alpha = Ease(Elapsed / Cfg->StrikeDurationSeconds);
		const FVector Contact = FMath::Lerp(StrikeStart, StrikeEnd, Alpha);
		FTransform Weapon, Hand;
		if (ComputePoseOnPath(StrikeShoulder, Contact, StrikeDirection, ActiveCharge.LocalEdge, Weapon, Hand))
		{
			ExecutedHand = WithUnitScale(Hand);
		}

		if (Elapsed >= Cfg->StrikeDurationSeconds)
		{
			Phase = EPhase::Hold;
			PhaseStartWorldTime = Now;
		}
		break;
	}

	case EPhase::Hold:
	{
		if (Elapsed >= Cfg->FollowThroughHoldSeconds)
		{
			EnterRecovery(TEXT("strike complete"));
		}
		break;
	}

	case EPhase::Recover:
	{
		const float Alpha = Ease(Elapsed / Cfg->RecoverySeconds);
		ExecutedHand = BlendHandTransforms(PhaseStartHandLocal, RestHandLocal, Alpha) * Root;
		ExecutedHand.SetScale3D(FVector::OneVector);

		if (Elapsed >= Cfg->RecoverySeconds)
		{
			FinishExecution(TEXT("recovered"));
			return;
		}
		break;
	}
	}

	if (Cfg->bDrawDebug)
	{
		DrawDebug();
	}
}

void UCombatExecutor_ProceduralStrike::UpdateApproach(float Now)
{
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	AActor* Fighter = GetFighter();

	FVector TargetPoint;
	if (Now - RequestWorldTime > Cfg->PreparationTimeoutSeconds || !GetLiveTargetPoint(TargetPoint))
	{
		UE_LOG(LogIronboundCombat, Log,
			TEXT("ProceduralStrike [%s | %s] preparation abandoned: plan=%d inPosition=%d yawError=%.1f"),
			*GetNameSafe(Fighter), *GetRequest().TechniqueId.ToString(), GetRequest().PlanId,
			bInPosition ? 1 : 0, FacingErrorDegrees);
		FinishExecution(TEXT("preparation timed out or target lost"));
		return;
	}

	const float Tolerance = FMath::Max(1.f, Cfg->ArrivalToleranceFraction * Arm.GetReach());
	const bool bAtStance =
		FVector::Dist2D(Fighter->GetActorLocation(), PlannedStance.GetLocation()) <= Tolerance;
	const bool bInWindow = IsInReachWindow(GetShoulderWorld(), TargetPoint);

	FacingErrorDegrees = ComputeFacingError();
	bInPosition = bAtStance || bInWindow;

	// Once the target point is inside the fighter's reach, stop moving and
	// only square up; the stance itself stays stable for the controller.
	CachedRequirement.bMayMoveDuringExecution = !bInWindow;

	if (bAtStance && !bInWindow)
	{
		if (OutOfWindowSinceWorldTime <= 0.f)
		{
			OutOfWindowSinceWorldTime = Now;
		}
		if (Now - OutOfWindowSinceWorldTime > 0.75f)
		{
			FinishExecution(TEXT("target left reach window at stance"));
			return;
		}
	}
	else
	{
		OutOfWindowSinceWorldTime = 0.f;
	}

	if (!bInWindow || FMath::Abs(FacingErrorDegrees) > Cfg->FacingToleranceDegrees)
	{
		return;
	}

	if (SolveChargePose())
	{
		BeginCharge();
	}
	else if (Now >= NextSolveLogWorldTime)
	{
		NextSolveLogWorldTime = Now + 1.f;
		UE_LOG(LogIronboundCombat, Log,
			TEXT("ProceduralStrike [%s | %s]: no anatomically valid chamber for %s/%s yet (distance %.1f / reach %.1f)"),
			*GetNameSafe(Fighter), *GetRequest().TechniqueId.ToString(),
			*PlannedRegion.ToString(), *PlannedBone.ToString(),
			FVector::Dist(GetShoulderWorld(), TargetPoint), Arm.GetReach());
	}
}

bool UCombatExecutor_ProceduralStrike::SolveChargePose()
{
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	const AActor* Fighter = GetFighter();
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	FVector TargetPoint;
	if (!Cfg || !Fighter || !Mesh || !GetLiveTargetPoint(TargetPoint))
	{
		return false;
	}

	const FVector Shoulder = GetShoulderWorld();
	const FVector Forward = Fighter->GetActorForwardVector();
	const FVector ReachAxis = (TargetPoint - Shoulder).GetSafeNormal();
	if (ReachAxis.IsNearlyZero())
	{
		return false;
	}

	FVector UpAxis = FVector::UpVector - FVector::DotProduct(FVector::UpVector, ReachAxis) * ReachAxis;
	if (UpAxis.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		UpAxis = Fighter->GetActorRightVector();
	}
	UpAxis.Normalize();
	const FVector SideAxis = FVector::CrossProduct(ReachAxis, UpAxis).GetSafeNormal();

	const float Reach = Arm.GetReach();
	const float ChargeDistance = Cfg->ChargeDistanceFraction * Reach;
	const float FollowThrough = Cfg->FollowThroughFraction * Reach;
	const FTransform CurrentHand = Mesh->GetSocketTransform(Arm.HandBone, RTS_World);

	TArray<FVector> Edges;
	BuildLocalEdgeCandidates(Edges);

	TArray<float> PullBacks = Cfg->ChargePullBackAnglesDegrees;
	if (PullBacks.IsEmpty())
	{
		PullBacks.Add(0.f);
	}

	TArray<FChargeCandidate> Candidates;
	const int32 AngleSamples = FMath::Max(4, Cfg->ChargeAngleSamples);
	for (int32 AngleIndex = 0; AngleIndex < AngleSamples; ++AngleIndex)
	{
		const float Around = 2.f * PI * float(AngleIndex) / float(AngleSamples);
		const FVector Sweep = FMath::Cos(Around) * SideAxis + FMath::Sin(Around) * UpAxis;

		for (const float PullBackDegrees : PullBacks)
		{
			const float PullBack = FMath::DegreesToRadians(PullBackDegrees);
			const FVector Offset = (FMath::Cos(PullBack) * Sweep - FMath::Sin(PullBack) * ReachAxis).GetSafeNormal();
			const FVector ChargeContact = TargetPoint + Offset * ChargeDistance;
			const FVector Direction = (TargetPoint - ChargeContact).GetSafeNormal();
			const FVector End = TargetPoint + Direction * FollowThrough;

			for (const FVector& Edge : Edges)
			{
				float Comfort = 0.f;
				if (!EvaluateStrikePath(Shoulder, Forward, ChargeContact, End, Edge, Comfort))
				{
					continue;
				}

				FTransform ChargeWeapon, ChargeHand;
				if (!ComputePoseOnPath(Shoulder, ChargeContact, Direction, Edge, ChargeWeapon, ChargeHand) ||
					!EvaluateChargeTravel(Shoulder, Forward, CurrentHand, ChargeHand))
				{
					continue;
				}

				FChargeCandidate& Candidate = Candidates.AddDefaulted_GetRef();
				Candidate.ChargeContact = ChargeContact;
				Candidate.LocalEdge = Edge;
				Candidate.ChargeHand = ChargeHand;
				Candidate.Comfort = Comfort;
			}
		}
	}

	if (Candidates.IsEmpty())
	{
		return false;
	}

	float BestComfort = 0.f;
	for (const FChargeCandidate& Candidate : Candidates)
	{
		BestComfort = FMath::Max(BestComfort, Candidate.Comfort);
	}

	// Variety: choose randomly (comfort weighted) among near-best chambers.
	const float Threshold = BestComfort * FMath::Clamp(Cfg->SelectionQualityBand, 0.f, 1.f);
	float TotalWeight = 0.f;
	for (const FChargeCandidate& Candidate : Candidates)
	{
		if (Candidate.Comfort >= Threshold)
		{
			TotalWeight += FMath::Max(Candidate.Comfort, KINDA_SMALL_NUMBER);
		}
	}

	FRandomStream Random(static_cast<int32>(HashCombine(GetTypeHash(Fighter), GetTypeHash(GetRecordId()))));
	float Pick = Random.FRand() * TotalWeight;
	const FChargeCandidate* Selected = nullptr;
	for (const FChargeCandidate& Candidate : Candidates)
	{
		if (Candidate.Comfort < Threshold)
		{
			continue;
		}
		Selected = &Candidate;
		Pick -= FMath::Max(Candidate.Comfort, KINDA_SMALL_NUMBER);
		if (Pick <= 0.f)
		{
			break;
		}
	}

	ActiveCharge = *Selected;
	StrikeTargetPoint = TargetPoint;

	UE_LOG(LogIronboundCombat, Log,
		TEXT("ProceduralStrike [%s | %s] chamber solved: candidates=%d comfort=%.2f/%.2f chargeOffset=%s target=%s/%s"),
		*GetNameSafe(Fighter), *GetRequest().TechniqueId.ToString(), Candidates.Num(),
		ActiveCharge.Comfort, BestComfort,
		*Fighter->GetActorTransform().InverseTransformVector(ActiveCharge.ChargeContact - TargetPoint).ToCompactString(),
		*PlannedRegion.ToString(), *PlannedBone.ToString());

	return true;
}

void UCombatExecutor_ProceduralStrike::BeginCharge()
{
	const AActor* Fighter = GetFighter();
	USkeletalMeshComponent* Mesh = GetFighterMesh();
	UWorld* World = GetWorld();
	if (!Fighter || !Mesh || !World)
	{
		return;
	}

	const FTransform Root = Fighter->GetActorTransform();
	const FTransform CurrentHand = WithUnitScale(Mesh->GetSocketTransform(Arm.HandBone, RTS_World));

	RestHandLocal = CurrentHand.GetRelativeTransform(Root);
	PhaseStartHandLocal = RestHandLocal;
	ChargeHandLocal = WithUnitScale(ActiveCharge.ChargeHand).GetRelativeTransform(Root);
	ChargeContactLocal = Root.InverseTransformPosition(ActiveCharge.ChargeContact);

	ExecutedHand = CurrentHand;
	bHandTargetActive = true;

	CachedRequirement = FCombatEngagementRequirement();
	SetRecordState(ECombatExecutionState::Committed);
	BeginBrace();

	Phase = EPhase::Charge;
	PhaseStartWorldTime = World->GetTimeSeconds();
}

bool UCombatExecutor_ProceduralStrike::BeginStrike()
{
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	const AActor* Fighter = GetFighter();
	UWorld* World = GetWorld();
	if (!Cfg || !Fighter || !World)
	{
		return false;
	}

	const FTransform Root = Fighter->GetActorTransform();
	const FVector Shoulder = GetShoulderWorld();
	const FVector Forward = Fighter->GetActorForwardVector();
	const FVector ChargeContact = Root.TransformPosition(ChargeContactLocal);
	const float FollowThrough = Cfg->FollowThroughFraction * Arm.GetReach();

	// Drive through the body part where it is NOW; fall back to the chambered
	// line if the live one would break the arm constraints.
	auto TryLine = [&](const FVector& TargetPoint) -> bool
	{
		const FVector Delta = TargetPoint - ChargeContact;
		if (Delta.Size() < 1.f)
		{
			return false;
		}
		const FVector Direction = Delta.GetSafeNormal();
		const FVector End = TargetPoint + Direction * FollowThrough;
		float Comfort = 0.f;
		if (!EvaluateStrikePath(Shoulder, Forward, ChargeContact, End, ActiveCharge.LocalEdge, Comfort))
		{
			return false;
		}

		StrikeShoulder = Shoulder;
		StrikeStart = ChargeContact;
		StrikeTargetPoint = TargetPoint;
		StrikeEnd = End;
		StrikeDirection = Direction;
		StrikeContactAlpha = Delta.Size() / FMath::Max(FVector::Distance(ChargeContact, End), 1.f);
		return true;
	};

	FVector LiveTarget;
	const FVector PlannedPoint = StrikeTargetPoint;
	if (!(GetLiveTargetPoint(LiveTarget) && TryLine(LiveTarget)) && !TryLine(PlannedPoint))
	{
		return false;
	}

	FBladeTrajectory Trajectory;
	if (!BuildStrikeTrajectory(Trajectory, Root))
	{
		return false;
	}

	MarkRecordCommitted(Trajectory, Root);
	SetRecordStrikeWindow(true);

	Phase = EPhase::Strike;
	PhaseStartWorldTime = World->GetTimeSeconds();

	UE_LOG(LogIronboundCombat, Log,
		TEXT("ProceduralStrike [%s | %s] strike: plan=%d start=%s target=%s end=%s contactAlpha=%.2f peakTip=%.0fcm/s"),
		*GetNameSafe(Fighter), *GetRequest().TechniqueId.ToString(), GetRequest().PlanId,
		*StrikeStart.ToCompactString(), *StrikeTargetPoint.ToCompactString(),
		*StrikeEnd.ToCompactString(), StrikeContactAlpha, Trajectory.PeakTipSpeed);

	return true;
}

bool UCombatExecutor_ProceduralStrike::BuildStrikeTrajectory(
	FBladeTrajectory& OutTrajectory,
	const FTransform& Root) const
{
	// Synthetic root-local trajectory of the procedural line so the shared
	// committed-strike seam (contact gate, observation) sees a real path.
	OutTrajectory = FBladeTrajectory();
	const UExecConfig_ProceduralStrike* Cfg = StrikeConfig();
	const int32 Samples = FMath::Max(8, Cfg->PathValidationSamples * 2);
	const float Duration = Cfg->StrikeDurationSeconds;
	const float SampleDelta = Duration / float(Samples);

	OutTrajectory.ReachMin = TNumericLimits<float>::Max();
	OutTrajectory.ReachMax = 0.f;
	OutTrajectory.HeightMin = TNumericLimits<float>::Max();
	OutTrajectory.HeightMax = -TNumericLimits<float>::Max();

	float BestContactError = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index <= Samples; ++Index)
	{
		const float Time = float(Index) / float(Samples);
		const float Alpha = Ease(Time);
		FTransform Weapon, Hand;
		if (!ComputePoseOnPath(StrikeShoulder, FMath::Lerp(StrikeStart, StrikeEnd, Alpha),
				StrikeDirection, ActiveCharge.LocalEdge, Weapon, Hand))
		{
			continue;
		}

		FBladeSegment& Segment = OutTrajectory.Segments.AddDefaulted_GetRef();
		Segment.Base = Root.InverseTransformPosition(Weapon.TransformPosition(Arm.BladeBaseLocal));
		Segment.Tip = Root.InverseTransformPosition(Weapon.TransformPosition(Arm.BladeTipLocal));
		Segment.TimeSeconds = Time * Duration;
		Segment.NormalizedTime = Time;

		const int32 SegmentIndex = OutTrajectory.Segments.Num() - 1;
		if (SegmentIndex > 0)
		{
			Segment.TipSpeed = FVector::Distance(
				OutTrajectory.Segments[SegmentIndex - 1].Tip, Segment.Tip) / FMath::Max(SampleDelta, KINDA_SMALL_NUMBER);
			OutTrajectory.PeakTipSpeed = FMath::Max(OutTrajectory.PeakTipSpeed, Segment.TipSpeed);
		}

		for (const FVector& Point : { Segment.Base, Segment.Tip })
		{
			const float Horizontal = FVector2D(Point.X, Point.Y).Size();
			OutTrajectory.ReachMin = FMath::Min(OutTrajectory.ReachMin, Horizontal);
			OutTrajectory.ReachMax = FMath::Max(OutTrajectory.ReachMax, Horizontal);
			OutTrajectory.HeightMin = FMath::Min(OutTrajectory.HeightMin, Point.Z);
			OutTrajectory.HeightMax = FMath::Max(OutTrajectory.HeightMax, Point.Z);
		}

		const float ContactError = FMath::Abs(Alpha - StrikeContactAlpha);
		if (ContactError < BestContactError)
		{
			BestContactError = ContactError;
			OutTrajectory.StrikeSample = SegmentIndex;
		}
	}

	OutTrajectory.ActiveStartTime = 0.f;
	OutTrajectory.ActiveEndTime = Duration;
	OutTrajectory.bValid = OutTrajectory.Segments.Num() >= 2;
	return OutTrajectory.bValid;
}

void UCombatExecutor_ProceduralStrike::EnterRecovery(const TCHAR* Reason)
{
	const AActor* Fighter = GetFighter();
	UWorld* World = GetWorld();

	SetRecordStrikeWindow(false);

	if (!bHandTargetActive || !Fighter || !World)
	{
		FinishExecution(Reason);
		return;
	}

	SetRecordState(ECombatExecutionState::Recovering);
	PhaseStartHandLocal = ExecutedHand.GetRelativeTransform(Fighter->GetActorTransform());
	Phase = EPhase::Recover;
	PhaseStartWorldTime = World->GetTimeSeconds();

	UE_LOG(LogIronboundCombat, Log,
		TEXT("ProceduralStrike [%s | %s] recovering: %s"),
		*GetNameSafe(Fighter), *GetRequest().TechniqueId.ToString(), Reason);
}

void UCombatExecutor_ProceduralStrike::OnExternalFinishRequest()
{
	if (Phase == EPhase::Approach)
	{
		FinishExecution(TEXT("external finish"));
		return;
	}

	if (Phase != EPhase::Recover)
	{
		EnterRecovery(TEXT("external finish"));
	}
}

void UCombatExecutor_ProceduralStrike::OnFinish()
{
	EndBrace();
	bHandTargetActive = false;
	CachedRequirement = FCombatEngagementRequirement();
}

void UCombatExecutor_ProceduralStrike::BeginBrace()
{
	// Reuse the weapon-arm brace so the physical arm keeps up with the fast
	// procedural target. Never take ownership of a brace a parry holds.
	UCombatBodyComponent* Body = GetBody();
	if (Body && !Body->IsParryBraced())
	{
		Body->BeginParryBrace();
		bBraceOwned = true;
	}
}

void UCombatExecutor_ProceduralStrike::EndBrace()
{
	if (!bBraceOwned)
	{
		return;
	}

	UCombatBodyComponent* Body = GetBody();
	if (Body && Body->IsParryBraced())
	{
		Body->EndParryBrace();
	}
	bBraceOwned = false;
}

// ============================================================================
// Contracts
// ============================================================================

void UCombatExecutor_ProceduralStrike::GetEngagementRequirement(
	FCombatEngagementRequirement& OutRequirement) const
{
	OutRequirement = Phase == EPhase::Approach ? CachedRequirement : FCombatEngagementRequirement();
}

bool UCombatExecutor_ProceduralStrike::GetFacingIntent(FVector& OutIntent) const
{
	OutIntent = Phase == EPhase::Recover ? FVector::ZeroVector : GetFacingDirectionToTarget();
	return !OutIntent.IsNearlyZero();
}

bool UCombatExecutor_ProceduralStrike::IsAwaitingAlignment() const
{
	return Phase == EPhase::Approach && bInPosition;
}

float UCombatExecutor_ProceduralStrike::GetFacingDeltaDegrees() const
{
	return IsAwaitingAlignment() ? FacingErrorDegrees : 0.f;
}

bool UCombatExecutor_ProceduralStrike::GetHandTarget(FTransform& OutHandTarget) const
{
	OutHandTarget = bHandTargetActive ? ExecutedHand : FTransform::Identity;
	return bHandTargetActive;
}

// ============================================================================
// Debug
// ============================================================================

void UCombatExecutor_ProceduralStrike::DrawDebug() const
{
	UWorld* World = GetWorld();
	const AActor* Fighter = GetFighter();
	if (!World || !Fighter)
	{
		return;
	}

	const FVector Shoulder = GetShoulderWorld();
	FVector TargetPoint;
	if (GetLiveTargetPoint(TargetPoint))
	{
		DrawDebugSphere(World, TargetPoint, 6.f, 10, FColor::Green, false, 0.f, 0, 1.f);
	}

	if (Phase == EPhase::Approach)
	{
		DrawDebugSphere(World, PlannedStance.GetLocation(), 10.f, 10, FColor::Orange, false, 0.f, 0, 1.f);
		DrawDebugCircle(World, Shoulder, StrikeConfig()->MaxReachFraction * Arm.GetReach(), 32,
			FColor::Silver, false, 0.f, 0, 0.5f, FVector::RightVector, FVector::ForwardVector, false);
		return;
	}

	if (Phase == EPhase::Charge)
	{
		DrawDebugSphere(World, Fighter->GetActorTransform().TransformPosition(ChargeContactLocal),
			5.f, 8, FColor::Yellow, false, 0.f, 0, 1.f);
	}
	else
	{
		DrawDebugLine(World, StrikeStart, StrikeEnd, FColor::Red, false, 0.f, 0, 1.5f);
		DrawDebugSphere(World, StrikeTargetPoint, 4.f, 8, FColor::Red, false, 0.f, 0, 1.f);
	}

	if (bHandTargetActive)
	{
		DrawDebugCoordinateSystem(World, ExecutedHand.GetLocation(), ExecutedHand.Rotator(), 10.f, false, 0.f, 0, 1.f);
	}
}
