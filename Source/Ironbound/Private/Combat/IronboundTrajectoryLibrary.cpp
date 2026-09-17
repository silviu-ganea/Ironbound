// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/IronboundTrajectoryLibrary.h"
#include "Combat/IronboundEquipmentComponent.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Ironbound.h"

namespace
{
	/** Name of the hidden evaluation mesh, so it can be found again across calls. */
	static const FName GBladeEvalComponentName(TEXT("IronboundBladeEvalMesh"));

	static constexpr int32 GDistanceProbes = 5;
	static constexpr int32 GLateralProbes = 7;
	static constexpr float GMaxLateralOffset = 60.f;
	static constexpr float GMaxAttackYawOffset = 25.f;
	static constexpr float GAttackYawStep = 5.f;

	// The solver must never name a stance the two bodies cannot legally occupy, and it should be free to
	// name one further out than the blade's own reach: a swing only has to MAKE contact, not reach full
	// extension, so the useful stand-off range extends past ReachMax.
	static constexpr float GStandoffClearance = 15.f;
	static constexpr float GStandoffBeyondReach = 60.f;

	/** Diagnostic draw of the derived arc, the blade's reach envelope, and the stance the solve named. */
	static TAutoConsoleVariable<int32> CVarDebugTrajectory(
		TEXT("Ironbound.DebugTrajectory"),
		1,
		TEXT("Draw the derived blade trajectory and the solved stand-off stance. 0 = off, 1 = on."),
		ECVF_Cheat);

	/** Squared distance from P to the segment AB. */
	static float PointSegmentDistanceSquared(const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const double LenSq = AB.SizeSquared();
		if (LenSq < KINDA_SMALL_NUMBER)
		{
			return static_cast<float>(FVector::DistSquared(P, A));
		}

		const double T = FMath::Clamp(FVector::DotProduct(P - A, AB) / LenSq, 0.0, 1.0);
		const FVector Closest = A + AB * T;
		return static_cast<float>(FVector::DistSquared(P, Closest));
	}

	/**
	 * Falls back to the sword mesh's own bounds when the caller does not supply blade endpoints.
	 * The blade runs along the mesh's longest axis, and the grip is assumed to be the end nearest
	 * the mesh pivot (true for most authored weapons). Both endpoints are overridable for exactly
	 * this reason.
	 */
	static void AutoDeriveBladeAxis(const UStaticMeshComponent* SwordComponent, FVector& OutBase, FVector& OutTip)
	{
		OutBase = FVector::ZeroVector;
		OutTip = FVector::ZeroVector;

		if (!SwordComponent)
		{
			return;
		}

		const UStaticMesh* Mesh = SwordComponent->GetStaticMesh();
		if (!Mesh)
		{
			return;
		}

		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const FVector Extent = Bounds.BoxExtent;

		int32 Axis = 0;
		if (Extent.Y > Extent[Axis]) { Axis = 1; }
		if (Extent.Z > Extent[Axis]) { Axis = 2; }

		FVector EndA = Bounds.Origin;
		FVector EndB = Bounds.Origin;
		EndA[Axis] -= Extent[Axis];
		EndB[Axis] += Extent[Axis];

		if (EndA.SizeSquared() > EndB.SizeSquared())
		{
			Swap(EndA, EndB);
		}

		OutBase = EndA;
		OutTip = EndB;
	}

	/**
	 * Finds (or creates) the hidden mesh used to evaluate poses.
	 *
	 * A component is used rather than a direct pose query because the only pose evaluator reachable
	 * from gameplay code is a skeletal mesh told to play an animation; the editor-only pose library
	 * is not available in a packaged build. It is created once, hidden, non-colliding and
	 * non-ticking, then reused for every move.
	 */
	static USkeletalMeshComponent* FindOrCreateEvalMesh(USkeletalMeshComponent* SourceMesh)
	{
		if (!SourceMesh)
		{
			return nullptr;
		}

		AActor* Owner = SourceMesh->GetOwner();
		if (!Owner)
		{
			return nullptr;
		}

		USkeletalMesh* Mesh = SourceMesh->GetSkeletalMeshAsset();
		if (!Mesh)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Owner->GetComponents())
		{
			if (Component && Component->GetFName() == GBladeEvalComponentName)
			{
				USkeletalMeshComponent* Existing = Cast<USkeletalMeshComponent>(Component);
				if (Existing && Existing->GetSkeletalMeshAsset() != Mesh)
				{
					Existing->SetSkeletalMesh(Mesh);
					Existing->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				}
				return Existing;
			}
		}

		USkeletalMeshComponent* Ghost =
			NewObject<USkeletalMeshComponent>(Owner, GBladeEvalComponentName, RF_Transient);

		if (!Ghost)
		{
			return nullptr;
		}

		if (USceneComponent* Root = Owner->GetRootComponent())
		{
			Ghost->SetupAttachment(Root);
		}

		Ghost->SetSkeletalMesh(Mesh);
		Ghost->SetRelativeTransform(SourceMesh->GetRelativeTransform());
		Ghost->SetVisibility(false, true);
		Ghost->SetHiddenInGame(true);
		Ghost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ghost->SetGenerateOverlapEvents(false);
		Ghost->SetComponentTickEnabled(false);
		Ghost->ComponentTags.Add(TEXT("BladeEval"));

		Ghost->RegisterComponent();
		Ghost->SetAnimationMode(EAnimationMode::AnimationSingleNode);

		UE_LOG(LogIronboundCombat, Log,
			TEXT("BladeTrajectory: created hidden evaluation mesh for %s"),
			*GetNameSafe(Owner));

		return Ghost;
	}
}

bool UIronboundTrajectoryLibrary::BuildBladeTrajectory(
	USkeletalMeshComponent* SourceMesh,
	UStaticMeshComponent* SwordComponent,
	UAnimSequenceBase* Sequence,
	FName HandBoneName,
	FVector BladeBaseLocal,
	FVector BladeTipLocal,
	float StartTime,
	float EndTime,
	int32 NumSamples,
	FBladeTrajectory& OutTrajectory)
{
	const auto* Equipment = SourceMesh && SourceMesh->GetOwner()
		? SourceMesh->GetOwner()->FindComponentByClass<UIronboundEquipmentComponent>() : nullptr;

	if (!Equipment || !Equipment->bReady || !Equipment->Definition)
	{
		OutTrajectory = FBladeTrajectory();
		return false;
	}

	return BuildBladeTrajectoryWithGrip(
		SourceMesh,
		SwordComponent,
		Sequence,
		HandBoneName,
		Equipment->Definition->WeaponToHand,
		BladeBaseLocal,
		BladeTipLocal,
		StartTime,
		EndTime,
		NumSamples,
		OutTrajectory);
}

bool UIronboundTrajectoryLibrary::BuildBladeTrajectoryWithGrip(
	USkeletalMeshComponent* SourceMesh,
	UStaticMeshComponent* SwordComponent,
	UAnimSequenceBase* Sequence,
	FName HandBoneName,
	const FTransform& GripToHand,
	FVector BladeBaseLocal,
	FVector BladeTipLocal,
	float StartTime,
	float EndTime,
	int32 NumSamples,
	FBladeTrajectory& OutTrajectory)
{
	OutTrajectory = FBladeTrajectory();

	if (!SourceMesh || !SwordComponent || !Sequence)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("BuildBladeTrajectory: missing mesh, sword or sequence."));
		return false;
	}

	AActor* Owner = SourceMesh->GetOwner();
	USceneComponent* RootComponent = Owner ? Owner->GetRootComponent() : nullptr;

	if (!RootComponent)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("BuildBladeTrajectory: %s has no root component."),
			*GetNameSafe(SourceMesh));
		return false;
	}

	if (NumSamples < 2 ||
		NumSamples > 512 ||
		!FMath::IsFinite(StartTime) ||
		!FMath::IsFinite(EndTime) ||
		StartTime < 0.f ||
		EndTime <= StartTime ||
		EndTime > Sequence->GetPlayLength())
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("BuildBladeTrajectory: bad sampling range (samples=%d, %.3f..%.3f)."),
			NumSamples,
			StartTime,
			EndTime);
		return false;
	}

	USkeletalMeshComponent* Eval = FindOrCreateEvalMesh(SourceMesh);

	if (!Eval)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("BuildBladeTrajectory: could not obtain an evaluation mesh."));
		return false;
	}

	FVector BaseLocal = BladeBaseLocal;
	FVector TipLocal = BladeTipLocal;

	if (BaseLocal.IsNearlyZero() && TipLocal.IsNearlyZero())
	{
		AutoDeriveBladeAxis(SwordComponent, BaseLocal, TipLocal);

		if (BaseLocal.IsNearlyZero() && TipLocal.IsNearlyZero())
		{
			UE_LOG(LogIronboundCombat, Warning,
				TEXT("BuildBladeTrajectory: could not derive a blade axis from %s; supply BladeBaseLocal/BladeTipLocal explicitly."),
				*GetNameSafe(SwordComponent));
			return false;
		}

		UE_LOG(LogIronboundCombat, Log,
			TEXT("BladeTrajectory: auto-derived blade axis base=(%.1f, %.1f, %.1f) tip=(%.1f, %.1f, %.1f) from %s"),
			BaseLocal.X,
			BaseLocal.Y,
			BaseLocal.Z,
			TipLocal.X,
			TipLocal.Y,
			TipLocal.Z,
			*GetNameSafe(SwordComponent));
	}

	if (!SourceMesh->DoesSocketExist(HandBoneName))
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("BuildBladeTrajectory: grip bone '%s' not found on %s."),
			*HandBoneName.ToString(),
			*GetNameSafe(SourceMesh));
		return false;
	}

	const FTransform RootToWorld = RootComponent->GetComponentTransform();
	const FTransform MeshToRoot =
		SourceMesh->GetComponentTransform().GetRelativeTransform(RootToWorld);

	Eval->SetRelativeTransform(SourceMesh->GetRelativeTransform());
	Eval->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Eval->SetAnimation(Sequence);

	OutTrajectory.Segments.Reserve(NumSamples);

	const float Duration = EndTime - StartTime;

	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const float Alpha =
			static_cast<float>(Index) /
			static_cast<float>(NumSamples - 1);

		const float SampleTime =
			StartTime + Duration * Alpha;

		Eval->SetPosition(SampleTime, false);
		Eval->TickAnimation(0.f, false);
		Eval->RefreshBoneTransforms(nullptr);

		const FTransform HandInComponent =
			Eval->GetSocketTransform(HandBoneName, RTS_Component);

		const FVector BaseInHandSpace =
			GripToHand.TransformPosition(BaseLocal);

		const FVector TipInHandSpace =
			GripToHand.TransformPosition(TipLocal);

		const FVector BaseInComponent =
			HandInComponent.TransformPosition(BaseInHandSpace);

		const FVector TipInComponent =
			HandInComponent.TransformPosition(TipInHandSpace);

		FBladeSegment Segment;
		Segment.Base = MeshToRoot.TransformPosition(BaseInComponent);
		Segment.Tip = MeshToRoot.TransformPosition(TipInComponent);
		Segment.NormalizedTime = Alpha;

		OutTrajectory.Segments.Add(Segment);
	}

	float ReachMin = TNumericLimits<float>::Max();
	float ReachMax = -TNumericLimits<float>::Max();
	float HeightMin = TNumericLimits<float>::Max();
	float HeightMax = -TNumericLimits<float>::Max();

	for (const FBladeSegment& Segment : OutTrajectory.Segments)
	{
		const float BaseReach =
			FVector(Segment.Base.X, Segment.Base.Y, 0.f).Size();

		const float TipReach =
			FVector(Segment.Tip.X, Segment.Tip.Y, 0.f).Size();

		ReachMin = FMath::Min(
			ReachMin,
			FMath::Sqrt(
				PointSegmentDistanceSquared(
					FVector::ZeroVector,
					FVector(Segment.Base.X, Segment.Base.Y, 0.f),
					FVector(Segment.Tip.X, Segment.Tip.Y, 0.f))));

		ReachMax =
			FMath::Max(
				ReachMax,
				FMath::Max(BaseReach, TipReach));

		HeightMin =
			FMath::Min(
				HeightMin,
				FMath::Min(Segment.Base.Z, Segment.Tip.Z));

		HeightMax =
			FMath::Max(
				HeightMax,
				FMath::Max(Segment.Base.Z, Segment.Tip.Z));
	}

	OutTrajectory.ReachMin = ReachMin;
	OutTrajectory.ReachMax = ReachMax;
	OutTrajectory.HeightMin = HeightMin;
	OutTrajectory.HeightMax = HeightMax;

	FVector PlaneNormal = FVector::ZeroVector;
	float BestSpeedSquared = -1.f;
	int32 StrikeIndex = 0;

	for (int32 Index = 1;
		 Index < OutTrajectory.Segments.Num();
		 ++Index)
	{
		const FVector PreviousTip =
			OutTrajectory.Segments[Index - 1].Tip;

		const FVector CurrentTip =
			OutTrajectory.Segments[Index].Tip;

		const float SpeedSquared =
			static_cast<float>(
				FVector::DistSquared(
					PreviousTip,
					CurrentTip));

		if (SpeedSquared > BestSpeedSquared)
		{
			BestSpeedSquared = SpeedSquared;
			StrikeIndex = Index;
		}

		PlaneNormal +=
			FVector::CrossProduct(
				PreviousTip,
				CurrentTip);
	}

	OutTrajectory.StrikeSample = StrikeIndex;

	if (PlaneNormal.Normalize() &&
		FMath::Abs(PlaneNormal.Z) < 0.9f)
	{
		OutTrajectory.PlaneYawOffset =
			FMath::RadiansToDegrees(
				FMath::Atan2(
					PlaneNormal.Y,
					PlaneNormal.X));
	}

	OutTrajectory.bValid = true;

	UE_LOG(LogIronboundCombat, Log,
		TEXT("BladeTrajectory: %s derived %d segments over %.3f..%.3fs | reach %.1f..%.1f cm | height %.1f..%.1f cm | plane yaw %.1f deg | strike sample %d"),
		*GetNameSafe(Sequence),
		OutTrajectory.Segments.Num(),
		StartTime,
		EndTime,
		OutTrajectory.ReachMin,
		OutTrajectory.ReachMax,
		OutTrajectory.HeightMin,
		OutTrajectory.HeightMax,
		OutTrajectory.PlaneYawOffset,
		OutTrajectory.StrikeSample);

	return true;
}

float UIronboundTrajectoryLibrary::EvaluateContact(
	const FBladeTrajectory& Trajectory,
	const FTransform& RootTransform,
	USkeletalMeshComponent* VictimMesh,
	const TArray<FName>& AllowedBones,
	FName& OutBone,
	int32& OutSample)
{
	OutBone = NAME_None;
	OutSample = INDEX_NONE;

	float BestSquared = TNumericLimits<float>::Max();

	if (!VictimMesh || !Trajectory.bValid)
	{
		return TNumericLimits<float>::Max();
	}

	FVector Approach =
		RootTransform.GetLocation() -
		VictimMesh->GetOwner()->GetActorLocation();

	Approach.Z = 0;
	Approach.Normalize();

	const auto* Capsule =
		VictimMesh->GetOwner()->FindComponentByClass<UCapsuleComponent>();

	const float Radius =
		Capsule
			? Capsule->GetScaledCapsuleRadius()
			: 0.f;

	for (FName Bone : AllowedBones)
	{
		if (!VictimMesh->DoesSocketExist(Bone))
		{
			continue;
		}

		const FVector Contact =
			VictimMesh->GetSocketLocation(Bone) +
			Approach * Radius;

		for (int32 Index = 0;
			 Index < Trajectory.Segments.Num();
			 ++Index)
		{
			const FBladeSegment& Segment =
				Trajectory.Segments[Index];

			const float Squared =
				PointSegmentDistanceSquared(
					Contact,
					RootTransform.TransformPosition(Segment.Base),
					RootTransform.TransformPosition(Segment.Tip));

			if (Squared < BestSquared)
			{
				BestSquared = Squared;
				OutBone = Bone;
				OutSample = Index;
			}
		}
	}

	return OutSample == INDEX_NONE
		? TNumericLimits<float>::Max()
		: FMath::Sqrt(BestSquared);
}

bool UIronboundTrajectoryLibrary::SolveAttackAlignment(
	USkeletalMeshComponent* AttackerMesh,
	USkeletalMeshComponent* VictimMesh,
	const FBladeTrajectory& Trajectory,
	const TArray<FName>& AllowedBones,
	float AcceptanceRadius,
	FTransform& OutAttackerTransform,
	FName& OutBone,
	int32& OutSampleIndex,
	float& OutPredictedDistance)
{
	OutAttackerTransform = FTransform::Identity;
	OutBone = NAME_None;
	OutSampleIndex = INDEX_NONE;
	OutPredictedDistance = TNumericLimits<float>::Max();

	if (!AttackerMesh ||
		!VictimMesh ||
		!Trajectory.bValid ||
		Trajectory.Segments.IsEmpty() ||
		AllowedBones.IsEmpty() ||
		!FMath::IsFinite(AcceptanceRadius) ||
		AcceptanceRadius < 0.f)
	{
		return false;
	}

	const AActor* Attacker = AttackerMesh->GetOwner();
	const AActor* Victim = VictimMesh->GetOwner();

	if (!Attacker || !Victim)
	{
		return false;
	}

	OutAttackerTransform =
		Attacker->GetActorTransform();

	const FVector Origin =
		Attacker->GetActorLocation();

	const FVector Target =
		Victim->GetActorLocation();

	FVector Approach =
		Origin - Target;

	Approach.Z = 0;

	if (!Approach.Normalize())
	{
		Approach =
			-Attacker->GetActorForwardVector();
	}

	const auto* AC =
		Attacker->FindComponentByClass<UCapsuleComponent>();

	const auto* VC =
		Victim->FindComponentByClass<UCapsuleComponent>();

	const float Minimum =
		(AC ? AC->GetScaledCapsuleRadius() : 0.f) +
		(VC ? VC->GetScaledCapsuleRadius() : 0.f) +
		GStandoffClearance;

	const float Maximum =
		FMath::Max(
			Minimum,
			Trajectory.ReachMax *
				Attacker->GetActorScale3D().GetAbsMax()) +
		GStandoffBeyondReach;

	const float TowardTargetYaw =
		(-Approach).Rotation().Yaw;

	bool bFoundFeasible = false;
	float BestStandoff = -1.f;
	float BestYawDeviation =
		TNumericLimits<float>::Max();

	auto Consider = [&](float Yaw, float Distance)
	{
		FVector Location =
			Target + Approach * Distance;

		Location.Z = Origin.Z;

		const FTransform Candidate(
			FRotator(0, Yaw, 0),
			Location,
			Attacker->GetActorScale3D());

		FName Bone;
		int32 Sample;

		const float Miss =
			EvaluateContact(
				Trajectory,
				Candidate,
				VictimMesh,
				AllowedBones,
				Bone,
				Sample);

		if (Sample == INDEX_NONE)
		{
			return;
		}

		const bool bFeasible =
			Miss <= AcceptanceRadius;

		const float YawDeviation =
			FMath::Abs(
				FMath::FindDeltaAngleDegrees(
					TowardTargetYaw,
					Yaw));

		const bool bBetter =
			bFeasible
				? (!bFoundFeasible ||
				   Distance > BestStandoff + 0.1f ||
				   (FMath::IsNearlyEqual(
						Distance,
						BestStandoff,
						0.1f) &&
					(Miss < OutPredictedDistance - 0.01f ||
					 (FMath::IsNearlyEqual(
						  Miss,
						  OutPredictedDistance,
						  0.01f) &&
					  YawDeviation < BestYawDeviation))))
				: (!bFoundFeasible &&
				   Miss < OutPredictedDistance);

		if (bBetter)
		{
			bFoundFeasible = bFeasible;
			BestStandoff = Distance;
			BestYawDeviation = YawDeviation;
			OutAttackerTransform = Candidate;
			OutBone = Bone;
			OutSampleIndex = Sample;
			OutPredictedDistance = Miss;
		}
	};

	const float CurrentDistance =
		FVector::Dist2D(Origin, Target);

	for (int32 D = 0;
		 D <= GDistanceProbes;
		 ++D)
	{
		const float Distance =
			D == GDistanceProbes
				? FMath::Clamp(
					CurrentDistance,
					Minimum,
					Maximum)
				: FMath::Lerp(
					Minimum,
					Maximum,
					float(D) /
					float(GDistanceProbes - 1));

		for (int32 Y = 0; Y < 72; ++Y)
		{
			Consider(
				TowardTargetYaw -
					180.f +
					5.f * Y,
				Distance);
		}
	}

	if (OutSampleIndex == INDEX_NONE)
	{
		return false;
	}

	const float ChosenYaw =
		OutAttackerTransform.Rotator().Yaw;

	const float ChosenDistance =
		BestStandoff;

	for (int32 Y = -5; Y <= 5; ++Y)
	{
		Consider(
			ChosenYaw + float(Y),
			ChosenDistance);
	}

	if (CVarDebugTrajectory.GetValueOnGameThread() > 0)
	{
		UWorld* World =
			Attacker->GetWorld();

		for (int32 I = 1;
			 I < Trajectory.Segments.Num();
			 ++I)
		{
			const FVector A =
				Trajectory.Segments[I - 1].Tip;

			const FVector B =
				Trajectory.Segments[I].Tip;

			DrawDebugLine(
				World,
				Attacker->GetActorTransform().TransformPosition(A),
				Attacker->GetActorTransform().TransformPosition(B),
				FColor::Cyan,
				false,
				0.55f,
				0,
				2.f);

			DrawDebugLine(
				World,
				OutAttackerTransform.TransformPosition(A),
				OutAttackerTransform.TransformPosition(B),
				FColor::Red,
				false,
				0.55f,
				0,
				3.f);
		}

		// DEBUG ONLY:
		// Show how far the solver wants to rotate from the attacker's
		// current actor orientation.
		const float SolvedYaw =
			OutAttackerTransform.Rotator().Yaw;

		const float CurrentYaw =
			Attacker->GetActorRotation().Yaw;

		const float RequiredYaw =
			FMath::FindDeltaAngleDegrees(
				CurrentYaw,
				SolvedYaw);

		DrawDebugString(
			World,
			OutAttackerTransform.GetLocation() +
				FVector(0.f, 0.f, 120.f),
			FString::Printf(
				TEXT("Attack yaw: %.1f deg | Bone: %s | Miss: %.1f cm"),
				RequiredYaw,
				*OutBone.ToString(),
				OutPredictedDistance),
			nullptr,
			FColor::Yellow,
			0.55f,
			true);

		DrawDebugDirectionalArrow(
			World,
			OutAttackerTransform.GetLocation(),
			OutAttackerTransform.GetLocation() +
				OutAttackerTransform
					.GetRotation()
					.GetForwardVector() *
				70.f,
			15.f,
			FColor::Yellow,
			false,
			0.55f);
	}

	return bFoundFeasible;
}