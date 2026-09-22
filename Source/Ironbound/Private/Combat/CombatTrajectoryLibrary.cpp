// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/CombatTrajectoryLibrary.h"
#include "Combat/CombatTarget.h"
#include "Combat/CombatEquipmentComponent.h"

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
	 * Temporary contact tolerance while targets are represented by skeletal
	 * bone points.
	 *
	 * This replaces the per-move AcceptanceRadius. Later this should disappear
	 * when contact is evaluated against Physics Asset/body geometry.
	 */
	static constexpr float GContactToleranceCm = 25.f;

	static constexpr int32 GDistanceProbes = 5;
	static constexpr float GStandoffClearance = 15.f;
	static constexpr float GStandoffBeyondReach = 60.f;

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
		Equipment->Definition->WeaponToHand,
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

	UE_LOG(
		LogIronboundCombat,
		Log,
		TEXT(
			"BladeTrajectory: %s automatically analyzed | "
			"animation %.3fs | samples %d | active %.3f..%.3f | "
			"peak %.1f cm/s | active segments %d | "
			"reach %.1f..%.1f"),
		*GetNameSafe(Sequence),
		PlayLength,
		NumSamples,
		OutTrajectory.ActiveStartTime,
		OutTrajectory.ActiveEndTime,
		OutTrajectory.PeakTipSpeed,
		OutTrajectory.Segments.Num(),
		OutTrajectory.ReachMin,
		OutTrajectory.ReachMax);

	return true;
}


float UCombatTrajectoryLibrary::EvaluateScoredContact(
	const FBladeTrajectory& Trajectory,
	const FTransform& RootTransform,
	USkeletalMeshComponent* VictimMesh,
	const UDataTable* CombatTargets,
	FName& OutRegion,
	FName& OutBone,
	int32& OutSample,
	float& OutTargetScore)
{
	OutRegion = NAME_None;
	OutBone = NAME_None;
	OutSample = INDEX_NONE;
	OutTargetScore = 0.f;

	if (!VictimMesh ||
		!CombatTargets ||
		!Trajectory.bValid)
	{
		return TNumericLimits<float>::Max();
	}

	float BestFeasibleScore =
		-TNumericLimits<float>::Max();

	float BestFeasibleMiss =
		TNumericLimits<float>::Max();

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

	float ClosestScore =
		0.f;

	const TMap<FName, uint8*>& Rows =
		CombatTargets->GetRowMap();

	for (const TPair<FName, uint8*>& Pair :
		 Rows)
	{
		const FName RegionName =
			Pair.Key;

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

		for (const FName Bone :
			 Row->Bones)
		{
			if (!VictimMesh->DoesSocketExist(Bone))
			{
				continue;
			}

			const FVector Contact =
				VictimMesh->GetSocketLocation(Bone);

			for (int32 Index = 0;
				 Index < Trajectory.Segments.Num();
				 ++Index)
			{
				const FBladeSegment& Segment =
					Trajectory.Segments[Index];

				const float Miss =
					FMath::Sqrt(
						PointSegmentDistanceSquared(
							Contact,
							RootTransform.TransformPosition(
								Segment.Base),
							RootTransform.TransformPosition(
								Segment.Tip)));

				if (Miss < RegionBestMiss)
				{
					RegionBestMiss =
						Miss;

					RegionBestBone =
						Bone;

					RegionBestSample =
						Index;
				}
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

			ClosestScore =
				Row->Score;
		}

		const bool bFeasible =
			RegionBestMiss <=
				GContactToleranceCm;

		if (bFeasible &&
			IsBetterContact(
				Row->Score,
				RegionBestMiss,
				BestFeasibleScore,
				BestFeasibleMiss))
		{
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
		return false;
	}

	const AActor* Attacker =
		AttackerMesh->GetOwner();

	const AActor* Victim =
		VictimMesh->GetOwner();

	if (!Attacker ||
		!Victim)
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

	const float TowardTargetYaw =
		(-Approach).Rotation().Yaw;

	bool bFoundFeasible =
		false;

	float BestStandoff =
		-1.f;

	float BestYawDeviation =
		TNumericLimits<float>::Max();

	float BestScore =
		-TNumericLimits<float>::Max();

	auto Consider =
		[&](float Yaw, float Distance)
	{
		FVector Location =
			Target +
			Approach * Distance;

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
				TargetScore);

		if (Sample == INDEX_NONE)
		{
			return;
		}

		const bool bFeasible =
			Miss <=
				GContactToleranceCm;

		const float YawDeviation =
			FMath::Abs(
				FMath::FindDeltaAngleDegrees(
					TowardTargetYaw,
					Yaw));

		bool bBetter = false;

		if (bFeasible)
		{
			if (!bFoundFeasible)
			{
				bBetter = true;
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
				/*
				 * For the same anatomical value, preserve the old
				 * preference for the furthest useful stand-off.
				 */
				if (Distance >
					BestStandoff + 0.1f)
				{
					bBetter = true;
				}
				else if (
					FMath::IsNearlyEqual(
						Distance,
						BestStandoff,
						0.1f) &&
					(Miss <
						 OutPredictedDistance -
							 0.01f ||
					 (FMath::IsNearlyEqual(
						  Miss,
						  OutPredictedDistance,
						  0.01f) &&
					  YawDeviation <
						  BestYawDeviation)))
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
						  float(
							  GDistanceProbes - 1));

		for (int32 Y = 0;
			 Y < 72;
			 ++Y)
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
		 Y <= 5;
		 ++Y)
	{
		Consider(
			ChosenYaw +
				float(Y),
			ChosenDistance);
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

			DrawDebugLine(
				World,
				OutAttackerTransform
					.TransformPosition(A),
				OutAttackerTransform
					.TransformPosition(B),
				FColor::Red,
				false,
				0.55f,
				0,
				3.f);
		}

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