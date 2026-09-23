// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatTrajectoryLibrary.h"
#include "Combat/CombatTarget.h"
#include "Combat/CombatEquipmentComponent.h"
#include "Combat/WeaponDefinition.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Ironbound.h"

namespace
{
	static const FName GBladeEvalComponentName(
		TEXT("IronboundBladeEvalMesh"));

	/*
	 * Internal analysis resolution.
	 *
	 * This is deliberately a property of the trajectory analyzer, not attack
	 * content. At 60 Hz a 1-second attack receives ~61 samples.
	 */
	static constexpr float GAnalysisHz = 60.f;
	static constexpr int32 GMinAnalysisSamples = 16;
	static constexpr int32 GMaxAnalysisSamples = 512;

	/*
	 * The active swing is the contiguous region around peak blade speed where
	 * speed remains at least this fraction of peak.
	 *
	 * Again: analyzer behaviour, not authored move data.
	 */
	static constexpr float GActiveSpeedFraction = 0.35f;

	/*
	 * Contact tolerance outside the target-region volume. The region radius is
	 * authored on FCombatTargetRow (with a migration fallback), so this value is
	 * only a small geometric allowance rather than a substitute for anatomy.
	 *
	 * This replaces the per-move AcceptanceRadius. Later this should disappear
	 * when contact is evaluated against Physics Asset/body geometry.
	 */
	static constexpr float GContactToleranceCm = 25.f;

	static constexpr int32 GDistanceProbes = 5;
	static constexpr float GStandoffClearance = 15.f;
	static constexpr float GStandoffBeyondReach = 60.f;
	static constexpr float GSimilarStandoffCm = 5.f;

	static TAutoConsoleVariable<int32> CVarDebugTrajectory(
		TEXT("Ironbound.DebugTrajectory"),
		1,
		TEXT(
			"Draw derived blade trajectory and solved attack stance. "
			"0 = off, 1 = on."),
		ECVF_Cheat);


	static float PointSegmentDistanceSquared(
		const FVector& P,
		const FVector& A,
		const FVector& B)
	{
		const FVector AB = B - A;
		const double LenSq = AB.SizeSquared();

		if (LenSq < KINDA_SMALL_NUMBER)
		{
			return static_cast<float>(
				FVector::DistSquared(P, A));
		}

		const double T =
			FMath::Clamp(
				FVector::DotProduct(P - A, AB) / LenSq,
				0.0,
				1.0);

		const FVector Closest = A + AB * T;

		return static_cast<float>(
			FVector::DistSquared(P, Closest));
	}


	static void AutoDeriveBladeAxis(
		const UStaticMeshComponent* SwordComponent,
		FVector& OutBase,
		FVector& OutTip)
	{
		OutBase = FVector::ZeroVector;
		OutTip = FVector::ZeroVector;

		if (!SwordComponent)
		{
			return;
		}

		const UStaticMesh* Mesh =
			SwordComponent->GetStaticMesh();

		if (!Mesh)
		{
			return;
		}

		const FBoxSphereBounds Bounds =
			Mesh->GetBounds();

		const FVector Extent =
			Bounds.BoxExtent;

		int32 Axis = 0;

		if (Extent.Y > Extent[Axis])
		{
			Axis = 1;
		}

		if (Extent.Z > Extent[Axis])
		{
			Axis = 2;
		}

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


	static USkeletalMeshComponent* FindOrCreateEvalMesh(
		USkeletalMeshComponent* SourceMesh)
	{
		if (!SourceMesh)
		{
			return nullptr;
		}

		AActor* Owner =
			SourceMesh->GetOwner();

		if (!Owner)
		{
			return nullptr;
		}

		USkeletalMesh* Mesh =
			SourceMesh->GetSkeletalMeshAsset();

		if (!Mesh)
		{
			return nullptr;
		}

		for (UActorComponent* Component :
			 Owner->GetComponents())
		{
			if (Component &&
				Component->GetFName() ==
					GBladeEvalComponentName)
			{
				USkeletalMeshComponent* Existing =
					Cast<USkeletalMeshComponent>(
						Component);

				if (Existing &&
					Existing->GetSkeletalMeshAsset() !=
						Mesh)
				{
					Existing->SetSkeletalMesh(Mesh);
					Existing->SetAnimationMode(
						EAnimationMode::
							AnimationSingleNode);
				}

				return Existing;
			}
		}

		USkeletalMeshComponent* Ghost =
			NewObject<USkeletalMeshComponent>(
				Owner,
				GBladeEvalComponentName,
				RF_Transient);

		if (!Ghost)
		{
			return nullptr;
		}

		if (USceneComponent* Root =
			Owner->GetRootComponent())
		{
			Ghost->SetupAttachment(Root);
		}

		Ghost->SetSkeletalMesh(Mesh);
		Ghost->SetRelativeTransform(
			SourceMesh->GetRelativeTransform());

		Ghost->SetVisibility(false, true);
		Ghost->SetHiddenInGame(true);

		Ghost->SetCollisionEnabled(
			ECollisionEnabled::NoCollision);

		Ghost->SetGenerateOverlapEvents(false);
		Ghost->SetComponentTickEnabled(false);

		Ghost->ComponentTags.Add(
			TEXT("BladeEval"));

		Ghost->RegisterComponent();

		Ghost->SetAnimationMode(
			EAnimationMode::AnimationSingleNode);

		UE_LOG(
			LogIronboundCombat,
			Log,
			TEXT(
				"BladeTrajectory: created hidden "
				"evaluation mesh for %s"),
			*GetNameSafe(Owner));

		return Ghost;
	}


	/**
	 * Returns whether A is a better candidate than B.
	 *
	 * Feasible targets are primarily ordered by authored target score.
	 * Within the same score, closer blade contact wins.
	 */
	static bool IsBetterContact(
		float CandidateScore,
		float CandidateMiss,
		float BestScore,
		float BestMiss)
	{
		if (CandidateScore >
			BestScore + KINDA_SMALL_NUMBER)
		{
			return true;
		}

		if (FMath::IsNearlyEqual(
				CandidateScore,
				BestScore) &&
			CandidateMiss <
				BestMiss)
		{
			return true;
		}

		return false;
	}

	// Temporary Step 1 policy: prefer contact on the axial body/shoulder over
	// distal hands. The target table still supplies desirability within a tier.
	static bool IsPrimaryContactBone(FName Bone)
	{
		const FString Name = Bone.ToString();
		return Bone == TEXT("head") || Bone == TEXT("pelvis") ||
			Name.StartsWith(TEXT("spine_"), ESearchCase::IgnoreCase) ||
			Name.StartsWith(TEXT("neck_"), ESearchCase::IgnoreCase) ||
			Name.StartsWith(TEXT("clavicle_"), ESearchCase::IgnoreCase);
	}
}


bool UCombatTrajectoryLibrary::BuildBladeTrajectory(
	USkeletalMeshComponent* SourceMesh,
	UStaticMeshComponent* SwordComponent,
	UAnimSequenceBase* Sequence,
	FName HandBoneName,
	FVector BladeBaseLocal,
	FVector BladeTipLocal,
	FBladeTrajectory& OutTrajectory)
{
	const auto* Equipment =
		SourceMesh &&
		SourceMesh->GetOwner()
			? SourceMesh->GetOwner()
				  ->FindComponentByClass<
					  UCombatEquipmentComponent>()
			: nullptr;

	if (!Equipment ||
		!Equipment->bReady ||
		!Equipment->Definition)
	{
		OutTrajectory =
			FBladeTrajectory();

		return false;
	}

	return BuildBladeTrajectoryWithGrip(
		SourceMesh,
		SwordComponent,
		Sequence,
		HandBoneName,
		Equipment->GetWeaponToHand(),
		BladeBaseLocal,
		BladeTipLocal,
		OutTrajectory);
}


bool UCombatTrajectoryLibrary::
	BuildBladeTrajectoryWithGrip(
		USkeletalMeshComponent* SourceMesh,
		UStaticMeshComponent* SwordComponent,
		UAnimSequenceBase* Sequence,
		FName HandBoneName,
		const FTransform& GripToHand,
		FVector BladeBaseLocal,
		FVector BladeTipLocal,
		FBladeTrajectory& OutTrajectory)
{
	OutTrajectory =
		FBladeTrajectory();

	if (!SourceMesh ||
		!SwordComponent ||
		!Sequence)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT(
				"BuildBladeTrajectory: missing mesh, "
				"sword or sequence."));

		return false;
	}

	const float PlayLength =
		Sequence->GetPlayLength();

	if (!FMath::IsFinite(PlayLength) ||
		PlayLength <= SMALL_NUMBER)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT(
				"BuildBladeTrajectory: invalid animation "
				"length for %s."),
			*GetNameSafe(Sequence));

		return false;
	}

	AActor* Owner =
		SourceMesh->GetOwner();

	USceneComponent* RootComponent =
		Owner
			? Owner->GetRootComponent()
			: nullptr;

	if (!RootComponent)
	{
		return false;
	}

	USkeletalMeshComponent* Eval =
		FindOrCreateEvalMesh(SourceMesh);

	if (!Eval)
	{
		return false;
	}

	FVector BaseLocal =
		BladeBaseLocal;

	FVector TipLocal =
		BladeTipLocal;

	if (BaseLocal.IsNearlyZero() &&
		TipLocal.IsNearlyZero())
	{
		AutoDeriveBladeAxis(
			SwordComponent,
			BaseLocal,
			TipLocal);

		if (BaseLocal.IsNearlyZero() &&
			TipLocal.IsNearlyZero())
		{
			UE_LOG(
				LogIronboundCombat,
				Warning,
				TEXT(
					"BuildBladeTrajectory: could not "
					"derive blade axis from %s."),
				*GetNameSafe(SwordComponent));

			return false;
		}
	}

	if (!SourceMesh->DoesSocketExist(
			HandBoneName))
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT(
				"BuildBladeTrajectory: grip bone '%s' "
				"not found."),
			*HandBoneName.ToString());

		return false;
	}

	const FTransform RootToWorld =
		RootComponent->GetComponentTransform();

	const FTransform MeshToRoot =
		SourceMesh->GetComponentTransform()
			.GetRelativeTransform(
				RootToWorld);

	Eval->SetRelativeTransform(
		SourceMesh->GetRelativeTransform());

	Eval->SetAnimationMode(
		EAnimationMode::AnimationSingleNode);

	Eval->SetAnimation(Sequence);

	/*
	 * Sample the complete animation.
	 */
	const int32 NumSamples =
		FMath::Clamp(
			FMath::CeilToInt(
				PlayLength * GAnalysisHz) + 1,
			GMinAnalysisSamples,
			GMaxAnalysisSamples);

	const float DeltaTime =
		PlayLength /
		static_cast<float>(
			NumSamples - 1);

	TArray<FBladeSegment> FullSamples;
	FullSamples.Reserve(NumSamples);

	for (int32 Index = 0;
		 Index < NumSamples;
		 ++Index)
	{
		const float SampleTime =
			FMath::Min(
				PlayLength,
				DeltaTime * Index);

		Eval->SetPosition(
			SampleTime,
			false);

		Eval->TickAnimation(
			0.f,
			false);

		Eval->RefreshBoneTransforms(
			nullptr);

		const FTransform HandInComponent =
			Eval->GetSocketTransform(
				HandBoneName,
				RTS_Component);

		const FVector BaseInHandSpace =
			GripToHand.TransformPosition(
				BaseLocal);

		const FVector TipInHandSpace =
			GripToHand.TransformPosition(
				TipLocal);

		const FVector BaseInComponent =
			HandInComponent.TransformPosition(
				BaseInHandSpace);

		const FVector TipInComponent =
			HandInComponent.TransformPosition(
				TipInHandSpace);

		FBladeSegment Segment;

		Segment.Base =
			MeshToRoot.TransformPosition(
				BaseInComponent);

		Segment.Tip =
			MeshToRoot.TransformPosition(
				TipInComponent);

		Segment.TimeSeconds =
			SampleTime;

		Segment.NormalizedTime =
			PlayLength > SMALL_NUMBER
				? SampleTime / PlayLength
				: 0.f;

		FullSamples.Add(Segment);
	}

	/*
	 * Calculate actual velocity rather than relying on sample-to-sample
	 * displacement.
	 */
	float PeakSpeed = 0.f;
	int32 PeakIndex = 0;

	for (int32 Index = 1;
		 Index < FullSamples.Num();
		 ++Index)
	{
		const float Dt =
			FullSamples[Index].TimeSeconds -
			FullSamples[Index - 1].TimeSeconds;

		const float Speed =
			Dt > SMALL_NUMBER
				? FVector::Distance(
					  FullSamples[Index - 1].Tip,
					  FullSamples[Index].Tip) /
					  Dt
				: 0.f;

		FullSamples[Index].TipSpeed =
			Speed;

		if (Speed > PeakSpeed)
		{
			PeakSpeed = Speed;
			PeakIndex = Index;
		}
	}

	if (PeakSpeed <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(
			LogIronboundCombat,
			Warning,
			TEXT(
				"BladeTrajectory: %s has no meaningful "
				"blade movement."),
			*GetNameSafe(Sequence));

		return false;
	}

	/*
	 * Find the contiguous high-speed region around the peak.
	 */
	const float ActiveThreshold =
		PeakSpeed *
		GActiveSpeedFraction;

	int32 ActiveStart =
		PeakIndex;

	int32 ActiveEnd =
		PeakIndex;

	while (ActiveStart > 1 &&
		   FullSamples[ActiveStart - 1].TipSpeed >=
			   ActiveThreshold)
	{
		--ActiveStart;
	}

	while (ActiveEnd <
			   FullSamples.Num() - 1 &&
		   FullSamples[ActiveEnd + 1].TipSpeed >=
			   ActiveThreshold)
	{
		++ActiveEnd;
	}

	/*
	 * Keep one sample before the detected start where possible. This gives
	 * the first retained blade segment a proper incoming edge.
	 */
	ActiveStart =
		FMath::Max(
			0,
			ActiveStart - 1);

	OutTrajectory.Segments.Reserve(
		ActiveEnd - ActiveStart + 1);

	for (int32 Index = ActiveStart;
		 Index <= ActiveEnd;
		 ++Index)
	{
		OutTrajectory.Segments.Add(
			FullSamples[Index]);
	}

	if (OutTrajectory.Segments.Num() < 2)
	{
		return false;
	}

	OutTrajectory.ActiveStartTime =
		FullSamples[ActiveStart].TimeSeconds;

	OutTrajectory.ActiveEndTime =
		FullSamples[ActiveEnd].TimeSeconds;

	OutTrajectory.PeakTipSpeed =
		PeakSpeed;

	OutTrajectory.StrikeSample =
		PeakIndex - ActiveStart;

	/*
	 * Derive geometry from the automatically detected active trajectory.
	 */
	float ReachMin =
		TNumericLimits<float>::Max();

	float ReachMax =
		-TNumericLimits<float>::Max();

	float HeightMin =
		TNumericLimits<float>::Max();

	float HeightMax =
		-TNumericLimits<float>::Max();

	for (const FBladeSegment& Segment :
		 OutTrajectory.Segments)
	{
		const FVector Base2D(
			Segment.Base.X,
			Segment.Base.Y,
			0.f);

		const FVector Tip2D(
			Segment.Tip.X,
			Segment.Tip.Y,
			0.f);

		const float BaseReach =
			Base2D.Size();

		const float TipReach =
			Tip2D.Size();

		ReachMin =
			FMath::Min(
				ReachMin,
				FMath::Sqrt(
					PointSegmentDistanceSquared(
						FVector::ZeroVector,
						Base2D,
						Tip2D)));

		ReachMax =
			FMath::Max(
				ReachMax,
				FMath::Max(
					BaseReach,
					TipReach));

		HeightMin =
			FMath::Min(
				HeightMin,
				FMath::Min(
					Segment.Base.Z,
					Segment.Tip.Z));

		HeightMax =
			FMath::Max(
				HeightMax,
				FMath::Max(
					Segment.Base.Z,
					Segment.Tip.Z));
	}

	OutTrajectory.ReachMin =
		ReachMin;

	OutTrajectory.ReachMax =
		ReachMax;

	OutTrajectory.HeightMin =
		HeightMin;

	OutTrajectory.HeightMax =
		HeightMax;

	FVector PlaneNormal =
		FVector::ZeroVector;

	for (int32 Index = 1;
		 Index < OutTrajectory.Segments.Num();
		 ++Index)
	{
		PlaneNormal +=
			FVector::CrossProduct(
				OutTrajectory.Segments[Index - 1].Tip,
				OutTrajectory.Segments[Index].Tip);
	}

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
	FBox TipBounds(EForceInit::ForceInit);
	for (const FBladeSegment& Segment : OutTrajectory.Segments)
	{
		TipBounds += Segment.Tip;
	}

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT(
			"BladeTrajectory: %s automatically analyzed | "
			"animation %.3fs | samples %d | active %.3f..%.3f | "
			"peak %.1f cm/s | active segments %d | "
			"reach %.1f..%.1f | tip bounds %s..%s | endpoint tip %s"),
		*GetNameSafe(Sequence),
		PlayLength,
		NumSamples,
		OutTrajectory.ActiveStartTime,
		OutTrajectory.ActiveEndTime,
		OutTrajectory.PeakTipSpeed,
		OutTrajectory.Segments.Num(),
		OutTrajectory.ReachMin,
		OutTrajectory.ReachMax,
		*TipBounds.Min.ToCompactString(),
		*TipBounds.Max.ToCompactString(),
		*OutTrajectory.Segments.Last().Tip.ToCompactString());

	return true;
}


bool UCombatTrajectoryLibrary::ShouldRetainFarthestNearbyOpportunity(
	const FCombatAttackOpportunity* Farthest,
	const FCombatAttackOpportunity* QualityBest)
{
	return Farthest && Farthest->bFeasible && QualityBest &&
		Farthest->MissCm <= UCombatTrajectoryLibrary::MaxOpportunityContactMissCm;
}


bool UCombatTrajectoryLibrary::IsContactFeasible(
	float SignedMissCm,
	float AcceptedMissToleranceCm,
	float PenetrationMarginCm)
{
	return FMath::IsFinite(SignedMissCm) &&
		SignedMissCm <= AcceptedMissToleranceCm &&
		SignedMissCm <= -FMath::Max(0.f, PenetrationMarginCm);
}


float UCombatTrajectoryLibrary::EvaluateBladeContactMiss(
	const FBladeSegment& Segment,
	const FTransform& RootTransform,
	const FVector& TargetLocation,
	float TargetRadiusCm,
	float AimPointAlongBlade,
	float& OutContactFraction)
{
	const bool bUseWholeBlade = AimPointAlongBlade < 0.f;
	const float FirstFraction = bUseWholeBlade
		? 0.f
		: FMath::Clamp(AimPointAlongBlade, 0.f, 1.f);
	const float LastFraction = bUseWholeBlade ? 1.f : FirstFraction;
	const FVector BladeBase = RootTransform.TransformPosition(Segment.Base);
	const FVector BladeTip = RootTransform.TransformPosition(Segment.Tip);
	const FVector ContactStart = FMath::Lerp(BladeBase, BladeTip, FirstFraction);
	const FVector ContactEnd = FMath::Lerp(BladeBase, BladeTip, LastFraction);
	const FVector ClosestContact = FMath::ClosestPointOnSegment(
		TargetLocation, ContactStart, ContactEnd);
	const FVector ContactPath = ContactEnd - ContactStart;
	const double ContactPathLengthSquared = ContactPath.SizeSquared();
	const float LocalContactFraction = ContactPathLengthSquared > KINDA_SMALL_NUMBER
		? static_cast<float>(FMath::Clamp(
			FVector::DotProduct(TargetLocation - ContactStart, ContactPath) /
			ContactPathLengthSquared,
			0.0,
			1.0))
		: 0.f;

	OutContactFraction = FMath::Lerp(
		FirstFraction, LastFraction, LocalContactFraction);
	return FVector::Distance(TargetLocation, ClosestContact) -
		FMath::Max(0.f, TargetRadiusCm);
}


float UCombatTrajectoryLibrary::EvaluateTrajectoryContactMiss(
	const FBladeTrajectory& Trajectory, const FTransform& RootTransform,
	const FVector& TargetLocation, float TargetRadiusCm,
	float AimPointAlongBlade, float AimWindowStartFraction,
	float AimWindowEndFraction, int32& OutSample,
	float& OutContactFraction)
{
	OutSample = INDEX_NONE;
	OutContactFraction = -1.f;
	if (!Trajectory.bValid || Trajectory.Segments.IsEmpty())
	{
		return TNumericLimits<float>::Max();
	}
	const float FirstFraction = FMath::Clamp(
		FMath::Min(AimWindowStartFraction, AimWindowEndFraction), 0.f, 1.f);
	const float LastFraction = FMath::Clamp(
		FMath::Max(AimWindowStartFraction, AimWindowEndFraction), 0.f, 1.f);
	const int32 FirstSample = FMath::Clamp(
		FMath::CeilToInt(FirstFraction * float(Trajectory.Segments.Num() - 1)),
		0, Trajectory.Segments.Num() - 1);
	const int32 LastSample = FMath::Clamp(
		FMath::FloorToInt(LastFraction * float(Trajectory.Segments.Num() - 1)),
		FirstSample, Trajectory.Segments.Num() - 1);
	float BestMiss = TNumericLimits<float>::Max();
	const float BladeFraction = AimPointAlongBlade < 0.f
		? 1.f : FMath::Clamp(AimPointAlongBlade, 0.f, 1.f);
	auto ContactPoint = [&](int32 Index)
	{
		const FBladeSegment& Segment = Trajectory.Segments[Index];
		return RootTransform.TransformPosition(
			FMath::Lerp(Segment.Base, Segment.Tip, BladeFraction));
	};
	for (int32 Index = FirstSample; Index <= LastSample; ++Index)
	{
		const FVector Start = ContactPoint(Index);
		const FVector End = Index < LastSample ? ContactPoint(Index + 1) : Start;
		const float Miss = FVector::Distance(TargetLocation,
			FMath::ClosestPointOnSegment(TargetLocation, Start, End)) -
			FMath::Max(0.f, TargetRadiusCm);
		if (Miss < BestMiss)
		{
			BestMiss = Miss;
			OutSample = Index;
			OutContactFraction = BladeFraction;
		}
	}
	return BestMiss;
}


bool UCombatTrajectoryLibrary::PlaceTrajectoryContactAtTarget(
	const FBladeSegment& First, const FBladeSegment& Last,
	const FVector& TargetLocation, float RootHeight,
	const FVector& RootScale, float YawDegrees,
	float AimPointAlongBlade, FTransform& OutStance)
{
	const float BladeFraction = AimPointAlongBlade < 0.f
		? 1.f : FMath::Clamp(AimPointAlongBlade, 0.f, 1.f);
	const FVector FirstPoint = FMath::Lerp(First.Base, First.Tip, BladeFraction) * RootScale;
	const FVector LastPoint = FMath::Lerp(Last.Base, Last.Tip, BladeFraction) * RootScale;
	const float HeightDelta = LastPoint.Z - FirstPoint.Z;
	const float TargetHeight = TargetLocation.Z - RootHeight;
	if (FMath::IsNearlyZero(HeightDelta) &&
		!FMath::IsNearlyEqual(TargetHeight, FirstPoint.Z, 0.1f))
	{
		return false;
	}
	const float TimeFraction = FMath::IsNearlyZero(HeightDelta)
		? 0.5f : (TargetHeight - FirstPoint.Z) / HeightDelta;
	if (TimeFraction < 0.f || TimeFraction > 1.f)
	{
		return false;
	}
	const FVector ContactPoint = FMath::Lerp(FirstPoint, LastPoint, TimeFraction);
	const FRotator Yaw(0.f, YawDegrees, 0.f);
	const FVector Offset = Yaw.RotateVector(ContactPoint);
	OutStance = FTransform(Yaw,
		FVector(TargetLocation.X - Offset.X, TargetLocation.Y - Offset.Y,
			RootHeight), RootScale);
	return true;
}


int32 UCombatTrajectoryLibrary::SelectPreferredAttackOpportunityIndex(
	const TArray<FCombatAttackOpportunity>& Candidates,
	FString* OutSelectionReason)
{
	if (OutSelectionReason)
	{
		OutSelectionReason->Empty();
	}
	if (Candidates.IsEmpty())
	{
		return INDEX_NONE;
	}

	FName Region = Candidates[0].Region;
	for (const FCombatAttackOpportunity& Candidate : Candidates)
	{
		if (!Candidate.Region.IsNone())
		{
			Region = Candidate.Region;
			break;
		}
	}
	float FarthestFeasibleStandoff = -TNumericLimits<float>::Max();
	int32 FeasibleCount = 0;
	for (const FCombatAttackOpportunity& Candidate : Candidates)
	{
		if (Candidate.Region == Region && Candidate.ContactSample != INDEX_NONE &&
			Candidate.bFeasible &&
			Candidate.MissCm <= UCombatTrajectoryLibrary::MaxOpportunityContactMissCm)
		{
			FarthestFeasibleStandoff = FMath::Max(
				FarthestFeasibleStandoff, Candidate.StandoffCm);
			++FeasibleCount;
		}
	}

	if (FeasibleCount > 0)
	{
		int32 SelectedIndex = INDEX_NONE;
		FString SelectionReason = FeasibleCount == 1
			? TEXT("only feasible stance")
			: TEXT("greatest standoff among feasible stances");
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FCombatAttackOpportunity& Candidate = Candidates[Index];
			if (Candidate.Region != Region || Candidate.ContactSample == INDEX_NONE ||
				!Candidate.bFeasible ||
				Candidate.MissCm > UCombatTrajectoryLibrary::MaxOpportunityContactMissCm ||
				Candidate.StandoffCm < FarthestFeasibleStandoff - GSimilarStandoffCm)
			{
				continue;
			}

			if (SelectedIndex == INDEX_NONE)
			{
				SelectedIndex = Index;
				continue;
			}

			const FCombatAttackOpportunity& Selected = Candidates[SelectedIndex];
			if (Candidate.MissCm < Selected.MissCm - 1.f)
			{
				SelectedIndex = Index;
				SelectionReason = TEXT("better contact quality among similarly ranged stances");
			}
			else if (FMath::IsNearlyEqual(Candidate.MissCm, Selected.MissCm, 1.f) &&
				Candidate.MovementCostCm < Selected.MovementCostCm - 1.f)
			{
				SelectedIndex = Index;
				SelectionReason = TEXT("lower movement cost among similarly ranged, similarly accurate stances");
			}
			else if (FMath::IsNearlyEqual(Candidate.MissCm, Selected.MissCm, 1.f) &&
				FMath::IsNearlyEqual(Candidate.MovementCostCm, Selected.MovementCostCm, 1.f) &&
				Candidate.StandoffCm > Selected.StandoffCm + 0.1f)
			{
				SelectedIndex = Index;
				SelectionReason = TEXT("greater standoff among similarly accurate, similarly mobile stances");
			}
		}

		if (OutSelectionReason)
		{
			*OutSelectionReason = MoveTemp(SelectionReason);
		}
		return SelectedIndex;
	}

	int32 ClosestIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FCombatAttackOpportunity& Candidate = Candidates[Index];
		if (Candidate.Region == Region && Candidate.ContactSample != INDEX_NONE &&
			(ClosestIndex == INDEX_NONE ||
			 Candidate.MissCm < Candidates[ClosestIndex].MissCm))
		{
			ClosestIndex = Index;
		}
	}
	if (OutSelectionReason && ClosestIndex != INDEX_NONE)
	{
		*OutSelectionReason = TEXT("closest signed miss; no stance met the penetration requirement");
	}
	return ClosestIndex;
}


void UCombatTrajectoryLibrary::FindAttackOpportunities(
	USkeletalMeshComponent* AttackerMesh, USkeletalMeshComponent* VictimMesh,
	const FBladeTrajectory& Trajectory, const UDataTable* CombatTargets,
	FName TechniqueId, FName RequiredRegion, float AimPointAlongBlade,
	float AimWindowStartFraction, float AimWindowEndFraction,
	float FacingLimitDegrees, float ContactToleranceCm, float MaxNearbyMoveCm,
	TArray<FCombatAttackOpportunity>& OutOpportunities,
	float ContactPenetrationMarginCm)
{
	OutOpportunities.Reset();
	if (!AttackerMesh || !VictimMesh || !CombatTargets ||
		!Trajectory.bValid || Trajectory.Segments.IsEmpty())
	{
		return;
	}
	const AActor* Attacker = AttackerMesh->GetOwner();
	const AActor* Victim = VictimMesh->GetOwner();
	if (!Attacker || !Victim)
	{
		return;
	}
	const FVector Origin = Attacker->GetActorLocation();
	const FVector Target = Victim->GetActorLocation();
	const float CurrentDistance = FVector::Dist2D(Origin, Target);
	const float FacingLimit = FMath::Clamp(FacingLimitDegrees, 0.f, 180.f);
	const auto* AttackerCapsule = Attacker->FindComponentByClass<UCapsuleComponent>();
	const auto* VictimCapsule = Victim->FindComponentByClass<UCapsuleComponent>();
	const float MinimumDistance =
		(AttackerCapsule ? AttackerCapsule->GetScaledCapsuleRadius() : 0.f) +
		(VictimCapsule ? VictimCapsule->GetScaledCapsuleRadius() : 0.f) + GStandoffClearance;
	const float MaximumDistance = FMath::Max(MinimumDistance,
		Trajectory.ReachMax * Attacker->GetActorScale3D().GetAbsMax()) + GStandoffBeyondReach;
	const float MaxMove = FMath::Max(0.f, MaxNearbyMoveCm);
	const float MaximumMiss = FMath::Min(
		FMath::Max(0.f, ContactToleranceCm),
		UCombatTrajectoryLibrary::MaxOpportunityContactMissCm);
	const float PenetrationMargin = FMath::Max(0.f, ContactPenetrationMarginCm);

	TMap<FName, FCombatAttackOpportunity> BestCurrentByRegion;
	TMap<FName, FCombatAttackOpportunity> BestNearbyByRegion;
	TMap<FName, FCombatAttackOpportunity> FarthestNearbyByRegion;
	TMap<FName, float> FarthestFeasibleStandoffByRegion;
	TMap<FName, TArray<FCombatAttackOpportunity>> FarthestFeasibleBandByRegion;

	auto TrackFarthestFeasibleCandidate = [&](const FCombatAttackOpportunity& Candidate)
	{
		if (!Candidate.bFeasible || Candidate.ContactSample == INDEX_NONE)
		{
			return;
		}

		float* FarthestStandoff =
			FarthestFeasibleStandoffByRegion.Find(Candidate.Region);
		TArray<FCombatAttackOpportunity>& Band =
			FarthestFeasibleBandByRegion.FindOrAdd(Candidate.Region);
		if (!FarthestStandoff)
		{
			FarthestFeasibleStandoffByRegion.Add(Candidate.Region, Candidate.StandoffCm);
			Band.Add(Candidate);
			return;
		}

		if (Candidate.StandoffCm > *FarthestStandoff)
		{
			*FarthestStandoff = Candidate.StandoffCm;
			Band.RemoveAll([FarthestStandoff](const FCombatAttackOpportunity& Existing)
			{
				return Existing.StandoffCm < *FarthestStandoff - GSimilarStandoffCm;
			});
		}
		if (Candidate.StandoffCm < *FarthestStandoff - GSimilarStandoffCm)
		{
			return;
		}

		for (const FCombatAttackOpportunity& Existing : Band)
		{
			if (FVector::DistSquared2D(
					Existing.Stance.GetLocation(), Candidate.Stance.GetLocation()) < 25.f &&
				FMath::Abs(FMath::FindDeltaAngleDegrees(
					Existing.Stance.Rotator().Yaw,
					Candidate.Stance.Rotator().Yaw)) < 2.f)
			{
				return;
			}
		}
		Band.Add(Candidate);
	};

	auto IsBetterOpportunity = [](const FCombatAttackOpportunity& Candidate,
		const FCombatAttackOpportunity& CurrentBest, bool bPreferLowMovement)
	{
		if (Candidate.ContactSample == INDEX_NONE)
		{
			return false;
		}
		if (CurrentBest.ContactSample == INDEX_NONE)
		{
			return true;
		}
		if (Candidate.bFeasible != CurrentBest.bFeasible)
		{
			return Candidate.bFeasible;
		}
		if (!Candidate.bFeasible)
		{
			return Candidate.MissCm < CurrentBest.MissCm;
		}
		if (Candidate.MissCm < CurrentBest.MissCm - 1.f)
		{
			return true;
		}
		if (FMath::IsNearlyEqual(Candidate.MissCm, CurrentBest.MissCm, 1.f))
		{
			if (bPreferLowMovement &&
				Candidate.MovementCostCm < CurrentBest.MovementCostCm - 5.f)
			{
				return true;
			}
			if (!bPreferLowMovement &&
				Candidate.StandoffCm > CurrentBest.StandoffCm + 5.f)
			{
				return true;
			}
			if (Candidate.MovementCostCm < CurrentBest.MovementCostCm - 1.f)
			{
				return true;
			}
		}
		return false;
	};

	auto Evaluate = [&](const FVector& Location, float Yaw, bool bCurrent)
	{
		if ((Target - Location).IsNearlyZero())
		{
			return;
		}
		const float MoveCost = FVector::Dist2D(Origin, Location);
		if (!bCurrent && (MoveCost > MaxMove || MoveCost < 10.f))
		{
			return;
		}
		const float Standoff = FVector::Dist2D(Location, Target);
		if (Standoff < MinimumDistance)
		{
			return;
		}
		FTransform Stance(FRotator(0.f, Yaw, 0.f), Location, Attacker->GetActorScale3D());

		const TMap<FName, uint8*>& TargetRows = CombatTargets->GetRowMap();
		for (const TPair<FName, uint8*>& Pair : TargetRows)
		{
			const FName Region = Pair.Key;
			if (!RequiredRegion.IsNone() && Region != RequiredRegion)
			{
				continue;
			}

			FCombatAttackOpportunity Candidate;
			Candidate.TechniqueId = TechniqueId;
			Candidate.Stance = Stance;
			Candidate.TargetLocationAtQuery = Target;
			Candidate.MovementCostCm = MoveCost;
			Candidate.StandoffCm = Standoff;
			Candidate.AimPointAlongBlade = AimPointAlongBlade;
			Candidate.MissCm = EvaluateScoredContact(
				Trajectory, Stance, VictimMesh, CombatTargets,
				Candidate.Region, Candidate.Bone, Candidate.ContactSample,
				Candidate.ContactScore, Region, AimPointAlongBlade,
				AimWindowStartFraction, AimWindowEndFraction, NAME_None, true,
				&Candidate.ContactFraction);
			Candidate.bFeasible = Candidate.ContactSample != INDEX_NONE &&
				UCombatTrajectoryLibrary::IsContactFeasible(
					Candidate.MissCm, MaximumMiss, PenetrationMargin);
			TrackFarthestFeasibleCandidate(Candidate);
			Candidate.Quality = Candidate.bFeasible
				? FMath::Clamp(-Candidate.MissCm, -MaximumMiss, 30.f)
				: 0.f;

			TMap<FName, FCombatAttackOpportunity>& BestByRegion = bCurrent
				? BestCurrentByRegion : BestNearbyByRegion;
			FCombatAttackOpportunity* Best = BestByRegion.Find(Region);
			if (!Best || IsBetterOpportunity(Candidate, *Best, bCurrent))
			{
				BestByRegion.Add(Region, Candidate);
			}

			if (!bCurrent && Candidate.bFeasible)
			{
				FCombatAttackOpportunity* Farthest = FarthestNearbyByRegion.Find(Region);
				if (!Farthest || Candidate.StandoffCm > Farthest->StandoffCm + 1.f ||
					(FMath::IsNearlyEqual(Candidate.StandoffCm, Farthest->StandoffCm, 1.f) &&
					 Candidate.MissCm < Farthest->MissCm))
				{
					FarthestNearbyByRegion.Add(Region, Candidate);
				}
			}
		}
	};

	const float CurrentFacing = (Target - Origin).GetSafeNormal2D().Rotation().Yaw;
	Evaluate(Origin, Attacker->GetActorRotation().Yaw, true);
	for (int32 Y = -8; Y <= 8; ++Y)
	{
		Evaluate(Origin, CurrentFacing + FacingLimit * float(Y) / 8.f, true);
	}

	// Place the sampled tip path through each target bone at its actual height.
	// These candidates give the stance search a center hit even when a distance
	// or angle grid would step over the narrow path.
	const int32 FirstAimSample = FMath::Clamp(FMath::CeilToInt(
		FMath::Clamp(FMath::Min(AimWindowStartFraction, AimWindowEndFraction),
			0.f, 1.f) * float(Trajectory.Segments.Num() - 1)),
		0, Trajectory.Segments.Num() - 1);
	const int32 LastAimSample = FMath::Clamp(FMath::FloorToInt(
		FMath::Clamp(FMath::Max(AimWindowStartFraction, AimWindowEndFraction),
			0.f, 1.f) * float(Trajectory.Segments.Num() - 1)),
		FirstAimSample, Trajectory.Segments.Num() - 1);
	const FVector ActorScale = Attacker->GetActorScale3D();
	for (const TPair<FName, uint8*>& Pair : CombatTargets->GetRowMap())
	{
		if (!RequiredRegion.IsNone() && Pair.Key != RequiredRegion)
		{
			continue;
		}
		const FCombatTargetRow* Row = reinterpret_cast<const FCombatTargetRow*>(Pair.Value);
		if (!Row) continue;
		for (const FName Bone : Row->Bones)
		{
			if (!VictimMesh->DoesSocketExist(Bone)) continue;
			const FVector BoneLocation = VictimMesh->GetSocketLocation(Bone);
			for (int32 Index = FirstAimSample; Index < LastAimSample; ++Index)
			{
				for (int32 YawStep = 0; YawStep < 36; ++YawStep)
				{
					const float Yaw = CurrentFacing + float(YawStep) * 10.f;
					FTransform CenteredStance;
					if (PlaceTrajectoryContactAtTarget(
						Trajectory.Segments[Index], Trajectory.Segments[Index + 1],
						BoneLocation, Origin.Z, ActorScale, Yaw,
						AimPointAlongBlade, CenteredStance))
					{
						Evaluate(CenteredStance.GetLocation(), Yaw, false);
					}
				}
			}
		}
	}

	FVector Approach = (Origin - Target).GetSafeNormal2D();
	if (Approach.IsNearlyZero()) Approach = -Attacker->GetActorForwardVector().GetSafeNormal2D();
	for (int32 D = -1; D <= 16; ++D)
	{
		const float Distance = D < 0
			? FMath::Clamp(CurrentDistance, MinimumDistance, MaximumDistance)
			: FMath::Lerp(MinimumDistance, MaximumDistance, float(D) / 16.f);
		for (int32 A = -4; A <= 4; ++A)
		{
			const FVector Direction = Approach.RotateAngleAxis(float(A) * 15.f, FVector::UpVector);
			const FVector Location = FVector(Target.X + Direction.X * Distance,
				Target.Y + Direction.Y * Distance, Origin.Z);
			const float FacingYaw = (Target - Location).GetSafeNormal2D().Rotation().Yaw;
			for (int32 Y = -8; Y <= 8; ++Y)
			{
				Evaluate(Location, FacingYaw + FacingLimit * float(Y) / 8.f, false);
			}
		}
	}

	auto AddIfUnique = [&](const FCombatAttackOpportunity& Candidate)
	{
		if (Candidate.ContactSample == INDEX_NONE)
		{
			return;
		}
		for (const FCombatAttackOpportunity& Existing : OutOpportunities)
		{
			if (Existing.Region == Candidate.Region &&
				FVector::DistSquared2D(Existing.Stance.GetLocation(), Candidate.Stance.GetLocation()) < 25.f &&
				FMath::Abs(FMath::FindDeltaAngleDegrees(
					Existing.Stance.Rotator().Yaw, Candidate.Stance.Rotator().Yaw)) < 2.f)
			{
				return;
			}
		}
		OutOpportunities.Add(Candidate);
	};

	for (const TPair<FName, uint8*>& Pair : CombatTargets->GetRowMap())
	{
		const FName Region = Pair.Key;
		if (!RequiredRegion.IsNone() && Region != RequiredRegion)
		{
			continue;
		}

		TArray<FCombatAttackOpportunity> RegionCandidates;
		const TArray<FCombatAttackOpportunity>* FarthestBand =
			FarthestFeasibleBandByRegion.Find(Region);
		if (FarthestBand)
		{
			RegionCandidates.Append(*FarthestBand);
		}
		if (const FCombatAttackOpportunity* Farthest = FarthestNearbyByRegion.Find(Region))
		{
			const FCombatAttackOpportunity* QualityBest = BestNearbyByRegion.Find(Region);
			if (ShouldRetainFarthestNearbyOpportunity(Farthest, QualityBest) &&
				!FarthestBand)
			{
				RegionCandidates.Add(*Farthest);
			}
		}
		if (RegionCandidates.IsEmpty())
		{
			if (const FCombatAttackOpportunity* Current = BestCurrentByRegion.Find(Region))
			{
				RegionCandidates.Add(*Current);
			}
			if (const FCombatAttackOpportunity* Nearby = BestNearbyByRegion.Find(Region))
			{
				RegionCandidates.Add(*Nearby);
			}
		}

		FString SelectionReason;
		const int32 SelectedIndex = SelectPreferredAttackOpportunityIndex(
			RegionCandidates, &SelectionReason);
		if (RegionCandidates.IsValidIndex(SelectedIndex))
		{
			const FCombatAttackOpportunity& Selected = RegionCandidates[SelectedIndex];
			AddIfUnique(Selected);
			UE_LOG(LogIronboundCombat, Log,
				TEXT("Attack opportunity selected: region=%s standoff=%.1fcm sample=%d bladeFraction=%.3f signedMiss=%.1fcm reason=%s"),
				*Selected.Region.ToString(), Selected.StandoffCm,
				Selected.ContactSample, Selected.ContactFraction,
				Selected.MissCm, *SelectionReason);
		}
	}
}

float UCombatTrajectoryLibrary::EvaluateScoredContact(
	const FBladeTrajectory& Trajectory,
	const FTransform& RootTransform,
	USkeletalMeshComponent* VictimMesh,
	const UDataTable* CombatTargets,
	FName& OutRegion,
	FName& OutBone,
	int32& OutSample,
	float& OutTargetScore,
	FName RequiredRegion,
	float AimPointAlongBlade,
	float AimWindowStartFraction,
	float AimWindowEndFraction,
	FName RequiredBone,
	bool bPreferContactMargin,
	float* OutContactFraction)
{
	OutRegion = NAME_None;
	OutBone = NAME_None;
	OutSample = INDEX_NONE;
	OutTargetScore = 0.f;
	if (OutContactFraction)
	{
		*OutContactFraction = -1.f;
	}

	if (!VictimMesh ||
		!CombatTargets ||
		!Trajectory.bValid ||
		Trajectory.Segments.IsEmpty())
	{
		return TNumericLimits<float>::Max();
	}

	float BestFeasibleScore =
		-TNumericLimits<float>::Max();

	float BestFeasibleMiss =
		TNumericLimits<float>::Max();
	bool bBestFeasiblePrimary = false;

	/*
	 * Also retain the closest non-feasible result. This is useful while
	 * searching poses, because the solver needs a direction even before it
	 * has found a legal hit.
	 */
	float ClosestMiss =
		TNumericLimits<float>::Max();

	FName ClosestRegion =
		NAME_None;

	FName ClosestBone =
		NAME_None;

	int32 ClosestSample =
		INDEX_NONE;

	float ClosestFraction =
		-1.f;

	float ClosestScore =
		0.f;

	const TMap<FName, uint8*>& Rows =
		CombatTargets->GetRowMap();
	for (const TPair<FName, uint8*>& Pair :
		 Rows)
	{
		const FName RegionName =
			Pair.Key;
		if (!RequiredRegion.IsNone() && RegionName != RequiredRegion)
		{
			continue;
		}

		const FCombatTargetRow* Row =
			reinterpret_cast<
				const FCombatTargetRow*>(
					Pair.Value);

		if (!Row ||
			Row->Bones.IsEmpty())
		{
			continue;
		}

		float RegionBestMiss =
			TNumericLimits<float>::Max();

		FName RegionBestBone =
			NAME_None;

		int32 RegionBestSample =
			INDEX_NONE;
		float RegionBestFraction = -1.f;

		for (const FName Bone :
			 Row->Bones)
		{
			if (!RequiredBone.IsNone() && Bone != RequiredBone)
			{
				continue;
			}
			if (!VictimMesh->DoesSocketExist(Bone))
			{
				continue;
			}

			const FVector Contact =
				VictimMesh->GetSocketLocation(Bone);
			const float ContactRadius =
				CombatTargetRules::GetContactRadiusCm(RegionName, *Row);
			const float AimRadius = RegionName == TEXT("Head")
				? ContactRadius * 0.5f : ContactRadius;

			int32 ContactSample = INDEX_NONE;
			float ContactFraction = -1.f;
			const float Miss = EvaluateTrajectoryContactMiss(
				Trajectory, RootTransform, Contact, AimRadius,
				AimPointAlongBlade, AimWindowStartFraction,
				AimWindowEndFraction, ContactSample, ContactFraction);
			if (Miss < RegionBestMiss)
			{
				RegionBestMiss = Miss;
				RegionBestBone = Bone;
				RegionBestSample = ContactSample;
				RegionBestFraction = ContactFraction;
			}
		}

		if (RegionBestSample == INDEX_NONE)
		{
			continue;
		}

		if (RegionBestMiss < ClosestMiss)
		{
			ClosestMiss =
				RegionBestMiss;

			ClosestRegion =
				RegionName;

			ClosestBone =
				RegionBestBone;

			ClosestSample =
				RegionBestSample;

			ClosestFraction =
				RegionBestFraction;

			ClosestScore =
				Row->Score;
		}

		const bool bFeasible =
			RegionBestMiss <=
				GContactToleranceCm;

		const bool bPrimary = IsPrimaryContactBone(RegionBestBone);
		const float CandidateMarginCost = RegionBestMiss - (bPrimary ? 15.f : 0.f);
		const float BestMarginCost = BestFeasibleMiss - (bBestFeasiblePrimary ? 15.f : 0.f);
		if (bFeasible &&
			(bPreferContactMargin
				? (OutSample == INDEX_NONE || CandidateMarginCost < BestMarginCost)
				: (!bBestFeasiblePrimary && bPrimary ||
					bBestFeasiblePrimary == bPrimary && IsBetterContact(
						Row->Score, RegionBestMiss, BestFeasibleScore, BestFeasibleMiss))))
		{
			bBestFeasiblePrimary = bPrimary;
			BestFeasibleScore =
				Row->Score;

			BestFeasibleMiss =
				RegionBestMiss;

			OutRegion =
				RegionName;

			OutBone =
				RegionBestBone;

			OutSample =
				RegionBestSample;

			if (OutContactFraction)
			{
				*OutContactFraction = RegionBestFraction;
			}

			OutTargetScore =
				Row->Score;
		}
	}

	if (OutSample != INDEX_NONE)
	{
		return BestFeasibleMiss;
	}

	/*
	 * No legal hit from this pose: return the closest result so the outer
	 * stance search still has useful information.
	 */
	OutRegion =
		ClosestRegion;

	OutBone =
		ClosestBone;

	OutSample =
		ClosestSample;

	if (OutContactFraction)
	{
		*OutContactFraction = ClosestFraction;
	}

	OutTargetScore =
		ClosestScore;

	return ClosestMiss;
}


bool UCombatTrajectoryLibrary::SolveAttackAlignment(
	USkeletalMeshComponent* AttackerMesh,
	USkeletalMeshComponent* VictimMesh,
	const FBladeTrajectory& Trajectory,
	UDataTable* CombatTargets,
	FTransform& OutAttackerTransform,
	FName& OutRegion,
	FName& OutBone,
	int32& OutSampleIndex,
	float& OutPredictedDistance,
	float& OutTargetScore)
{
	return SolveAttackAlignmentWithFacingLimit(
		AttackerMesh,
		VictimMesh,
		Trajectory,
		CombatTargets,
		180.f,
		OutAttackerTransform,
		OutRegion,
		OutBone,
		OutSampleIndex,
		OutPredictedDistance,
		OutTargetScore);
}

bool UCombatTrajectoryLibrary::SolveAttackAlignmentWithFacingLimit(
	USkeletalMeshComponent* AttackerMesh,
	USkeletalMeshComponent* VictimMesh,
	const FBladeTrajectory& Trajectory,
	UDataTable* CombatTargets,
	float MaxFacingDeviationDegrees,
	FTransform& OutAttackerTransform,
	FName& OutRegion,
	FName& OutBone,
	int32& OutSampleIndex,
	float& OutPredictedDistance,
	float& OutTargetScore,
	FName RequiredRegion,
	float AimPointAlongBlade,
	float AimWindowStartFraction,
	float AimWindowEndFraction,
	float ContactPenetrationMarginCm)
{
	OutAttackerTransform =
		FTransform::Identity;

	OutRegion =
		NAME_None;

	OutBone =
		NAME_None;

	OutSampleIndex =
		INDEX_NONE;

	OutPredictedDistance =
		TNumericLimits<float>::Max();

	OutTargetScore =
		0.f;

	if (!AttackerMesh ||
		!VictimMesh ||
		!CombatTargets ||
		!Trajectory.bValid ||
		Trajectory.Segments.IsEmpty())
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("Attack alignment input missing: attackerMesh=%s victimMesh=%s targets=%s valid=%d segments=%d"),
			*GetNameSafe(AttackerMesh), *GetNameSafe(VictimMesh), *GetNameSafe(CombatTargets),
			Trajectory.bValid ? 1 : 0, Trajectory.Segments.Num());
		return false;
	}

	const AActor* Attacker =
		AttackerMesh->GetOwner();

	const AActor* Victim =
		VictimMesh->GetOwner();

	if (!Attacker ||
		!Victim)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("Attack alignment mesh owners missing: attacker=%s victim=%s"),
			*GetNameSafe(Attacker), *GetNameSafe(Victim));
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

	Approach.Z = 0.f;

	if (!Approach.Normalize())
	{
		Approach =
			-Attacker->GetActorForwardVector();
	}

	const auto* AC =
		Attacker->FindComponentByClass<
			UCapsuleComponent>();

	const auto* VC =
		Victim->FindComponentByClass<
			UCapsuleComponent>();

	const float Minimum =
		(AC
			? AC->GetScaledCapsuleRadius()
			: 0.f) +
		(VC
			? VC->GetScaledCapsuleRadius()
			: 0.f) +
		GStandoffClearance;

	const float Maximum =
		FMath::Max(
			Minimum,
			Trajectory.ReachMax *
				Attacker->GetActorScale3D()
					.GetAbsMax()) +
		GStandoffBeyondReach;

	const float FacingLimit = FMath::Clamp(MaxFacingDeviationDegrees, 0.f, 180.f);
	const float PenetrationMargin = FMath::Max(0.f, ContactPenetrationMarginCm);

	bool bFoundFeasible =
		false;

	float BestStandoff =
		-1.f;

	float BestYawDeviation =
		TNumericLimits<float>::Max();

	float BestScore =
		-TNumericLimits<float>::Max();
	bool bBestPrimary = false;
	bool bSelectingContact = true;
	FName SelectedRegion;
	FName SelectedBone;
	int32 ScoredCandidates = 0;
	int32 FacingRejectedCandidates = 0;
	int32 FeasibleCandidates = 0;
	float ClosestScoredMiss = TNumericLimits<float>::Max();

	auto Consider =
		[&](const FVector& CandidateApproach, float Yaw, float Distance)
	{
		FVector Location =
			Target +
			CandidateApproach * Distance;

		Location.Z =
			Origin.Z;

		const FTransform Candidate(
			FRotator(0.f, Yaw, 0.f),
			Location,
			Attacker->GetActorScale3D());

		FName Region;
		FName Bone;
		int32 Sample;
		float TargetScore;

		const float Miss =
			EvaluateScoredContact(
				Trajectory,
				Candidate,
				VictimMesh,
				CombatTargets,
				Region,
				Bone,
				Sample,
				TargetScore,
				bSelectingContact ? RequiredRegion : SelectedRegion,
				AimPointAlongBlade,
				AimWindowStartFraction,
				AimWindowEndFraction,
				bSelectingContact ? NAME_None : SelectedBone);

		if (Sample == INDEX_NONE)
		{
			return;
		}
		++ScoredCandidates;
		ClosestScoredMiss = FMath::Min(ClosestScoredMiss, Miss);

		const bool bFeasible = IsContactFeasible(
			Miss, GContactToleranceCm, PenetrationMargin);

		// Trajectory samples are already in actor-root space (MeshToRoot was
		// applied by BuildBladeTrajectoryWithGrip). Subtracting the mesh yaw
		// here rotates the desired actor facing a second time.
		const float TowardTargetYaw = (-CandidateApproach).Rotation().Yaw;
		const float YawDeviation =
			FMath::Abs(
				FMath::FindDeltaAngleDegrees(
					TowardTargetYaw,
					Yaw));

		// A mathematically zero offset can differ by a few float ULPs after
		// yaw wrapping. Keep the authored limit exact while tolerating that
		// computation noise, otherwise a 0-degree limit rejects every stance.
		if (YawDeviation > FacingLimit + KINDA_SMALL_NUMBER)
		{
			++FacingRejectedCandidates;
			return;
		}
		if (bFeasible)
		{
			++FeasibleCandidates;
		}

		bool bBetter = false;
		const bool bPrimary = IsPrimaryContactBone(Bone);

		if (bFeasible)
		{
			if (!bFoundFeasible)
			{
				bBetter = true;
			}
			else if (bSelectingContact && bPrimary != bBestPrimary)
			{
				bBetter = bPrimary;
			}
			else if (TargetScore >
					 BestScore +
						 KINDA_SMALL_NUMBER)
			{
				bBetter = true;
			}
			else if (FMath::IsNearlyEqual(
						 TargetScore,
						 BestScore))
			{
				// Contact quality decides the region/bone first. Only the
				// second pass prefers the furthest stance for that contact.
				if (bSelectingContact && Miss < OutPredictedDistance - 0.01f)
				{
					bBetter = true;
				}
				else if (!bSelectingContact && Distance > BestStandoff + 0.1f)
				{
					bBetter = true;
				}
				else if (!bSelectingContact && FMath::IsNearlyEqual(Distance, BestStandoff, 0.1f) &&
					(Miss < OutPredictedDistance - 0.01f ||
					 (FMath::IsNearlyEqual(Miss, OutPredictedDistance, 0.01f) &&
					  YawDeviation < BestYawDeviation - 0.1f)))
				{
					bBetter = true;
				}
			}
		}
		else if (!bFoundFeasible &&
				 Miss <
					 OutPredictedDistance)
		{
			bBetter = true;
		}

		if (!bBetter)
		{
			return;
		}

		bFoundFeasible =
			bFeasible;

		BestStandoff =
			Distance;

		BestYawDeviation =
			YawDeviation;

		BestScore =
			TargetScore;
		bBestPrimary = bPrimary;

		OutAttackerTransform =
			Candidate;

		OutRegion =
			Region;

		OutBone =
			Bone;

		OutSampleIndex =
			Sample;

		OutPredictedDistance =
			Miss;

		OutTargetScore =
			TargetScore;
	};

	const float CurrentDistance =
		FVector::Dist2D(
			Origin,
			Target);

	auto SearchStances = [&]()
	{
		auto ConsiderApproach = [&](const FVector& CandidateApproach, float Distance)
		{
			const float FacingYaw = (-CandidateApproach).Rotation().Yaw;
			if (FacingLimit <= KINDA_SMALL_NUMBER)
			{
				Consider(CandidateApproach, FacingYaw, Distance);
				return;
			}
			for (int32 Y = -4; Y <= 4; ++Y)
			{
				Consider(CandidateApproach,
					FacingYaw + FacingLimit * float(Y) / 4.f, Distance);
			}
		};

		for (int32 D = 0; D <= GDistanceProbes; ++D)
		{
			const float Distance = D == GDistanceProbes
				? FMath::Clamp(CurrentDistance, Minimum, Maximum)
				: FMath::Lerp(Minimum, Maximum,
					float(D) / float(GDistanceProbes - 1));
			ConsiderApproach(Approach, Distance);
			// The contact pass must inspect the full ring. The stance pass
			// uses the same reachable candidates for its selected body point.
			for (int32 Angle = 0; Angle < 36; ++Angle)
			{
				const float Radians = FMath::DegreesToRadians(10.f * Angle);
				const FVector CandidateApproach(FMath::Cos(Radians), FMath::Sin(Radians), 0.f);
				ConsiderApproach(CandidateApproach, Distance);
			}
		}
	};

	SearchStances();
	if (!bFoundFeasible)
	{
		UE_LOG(LogIronboundCombat, Log,
			TEXT("Attack alignment has no valid contact in aim window: region=%s closest=%.1fcm scored=%d facingRejected=%d"),
			*RequiredRegion.ToString(), OutPredictedDistance,
			ScoredCandidates, FacingRejectedCandidates);
		return false;
	}

	SelectedRegion = OutRegion;
	SelectedBone = OutBone;
	UE_LOG(LogIronboundCombat, Log,
		TEXT("Attack contact selected: region=%s bone=%s primary=%d score=%.1f miss=%.1fcm"),
		*SelectedRegion.ToString(), *SelectedBone.ToString(),
		bBestPrimary ? 1 : 0, OutTargetScore, OutPredictedDistance);
	bSelectingContact = false;
	bFoundFeasible = false;
	BestStandoff = -1.f;
	BestScore = -TNumericLimits<float>::Max();
	OutPredictedDistance = TNumericLimits<float>::Max();
	OutSampleIndex = INDEX_NONE;
	SearchStances();

	if (OutSampleIndex == INDEX_NONE)
	{
		UE_LOG(LogIronboundCombat, Warning,
			TEXT("Attack alignment found no stance: region=%s scored=%d facingRejected=%d feasible=%d closestMiss=%.1fcm facingLimit=%.3fdeg targetRows=%d"),
			*RequiredRegion.ToString(),
			ScoredCandidates, FacingRejectedCandidates, FeasibleCandidates,
			ClosestScoredMiss, FacingLimit, CombatTargets->GetRowMap().Num());
		return false;
	}

	/*
	 * Refine yaw around the selected coarse solution.
	 */
	const float ChosenYaw =
		OutAttackerTransform
			.Rotator()
			.Yaw;

	const float ChosenDistance =
		BestStandoff;

	for (int32 Y = -5;
		 Y <= 5 && FacingLimit > KINDA_SMALL_NUMBER;
		 ++Y)
	{
		FVector RefinedApproach = OutAttackerTransform.GetLocation() - Target;
		RefinedApproach.Z = 0.f;
		RefinedApproach.Normalize();
		Consider(
			RefinedApproach,
			ChosenYaw +
				float(Y),
			ChosenDistance);
	}

	if (!bFoundFeasible)
	{
		UE_LOG(LogIronboundCombat, Log,
			TEXT("Attack alignment has no valid contact in aim window: region=%s closest=%.1fcm stance=%.1fcm yaw=%.1fdeg aimSample=%d"),
			*RequiredRegion.ToString(), OutPredictedDistance, BestStandoff,
			OutAttackerTransform.Rotator().Yaw, OutSampleIndex);
	}

	if (CVarDebugTrajectory
			.GetValueOnGameThread() > 0)
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
				Attacker->GetActorTransform()
					.TransformPosition(A),
				Attacker->GetActorTransform()
					.TransformPosition(B),
				FColor::Cyan,
				false,
				0.55f,
				0,
				2.f);

			if (bFoundFeasible && I <= OutSampleIndex + 1)
			{
				DrawDebugLine(World,
					OutAttackerTransform.TransformPosition(A),
					OutAttackerTransform.TransformPosition(B),
					FColor::Red, false, 0.55f, 0, 3.f);
			}
		}

		// Never label or draw a red stance for a failed contact guess.
		if (!bFoundFeasible)
		{
			return false;
		}

		const FVector TargetPoint = VictimMesh->GetSocketLocation(OutBone);
		const FBladeSegment& AimSample = Trajectory.Segments[OutSampleIndex];
		const float BladeFraction = AimPointAlongBlade < 0.f
			? 1.f : FMath::Clamp(AimPointAlongBlade, 0.f, 1.f);
		const FVector ContactStart = OutAttackerTransform.TransformPosition(
			FMath::Lerp(AimSample.Base, AimSample.Tip, BladeFraction));
		const FBladeSegment& EndSample = Trajectory.Segments[
			FMath::Min(OutSampleIndex + 1, Trajectory.Segments.Num() - 1)];
		const FVector ContactEnd = OutAttackerTransform.TransformPosition(
			FMath::Lerp(EndSample.Base, EndSample.Tip, BladeFraction));
		const FVector PlannedContact = FMath::ClosestPointOnSegment(
			TargetPoint, ContactStart, ContactEnd);
		DrawDebugSphere(World, TargetPoint, 6.f, 12, FColor::Green, false, 0.55f);
		DrawDebugLine(World, PlannedContact, TargetPoint, FColor::Green,
			false, 0.55f, 0, 2.f);

		const float SolvedYaw =
			OutAttackerTransform
				.Rotator()
				.Yaw;

		const float CurrentYaw =
			Attacker
				->GetActorRotation()
				.Yaw;

		const float RequiredYaw =
			FMath::FindDeltaAngleDegrees(
				CurrentYaw,
				SolvedYaw);

		DrawDebugString(
			World,
			OutAttackerTransform.GetLocation() +
				FVector(0.f, 0.f, 120.f),
			FString::Printf(
				TEXT(
					"Attack yaw: %.1f | Region: %s | "
					"Bone: %s | Score: %.1f | Miss: %.1f cm"),
				RequiredYaw,
				*OutRegion.ToString(),
				*OutBone.ToString(),
				OutTargetScore,
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
