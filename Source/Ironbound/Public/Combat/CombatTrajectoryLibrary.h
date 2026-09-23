// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CombatTrajectoryLibrary.generated.h"

class UAnimSequenceBase;
class UDataTable;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * One sampled blade segment of an intended attack, expressed in attacker
 * root-local space.
 */
USTRUCT(BlueprintType)
struct FBladeSegment
{
	GENERATED_BODY()

	/** Blade base (nearest the guard), in attacker root-local space. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	FVector Base = FVector::ZeroVector;

	/** Blade tip, in attacker root-local space. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	FVector Tip = FVector::ZeroVector;

	/** Absolute time in the source animation, in seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float TimeSeconds = 0.f;

	/** Absolute animation time normalized across the full sequence, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float NormalizedTime = 0.f;

	/** Blade-tip speed at this sample, in cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float TipSpeed = 0.f;
};

/**
 * Intended blade path derived automatically from the attack animation.
 *
 * The analyzer samples the complete animation internally, detects the active
 * high-speed swing around peak blade speed, and retains only that active
 * trajectory for planning/contact evaluation.
 */
USTRUCT(BlueprintType)
struct FBladeTrajectory
{
	GENERATED_BODY()

	/** Blade segments retained for the automatically detected active swing. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	TArray<FBladeSegment> Segments;

	/** Smallest horizontal distance from attacker root to any blade point, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float ReachMin = 0.f;

	/** Largest horizontal distance from attacker root to any blade point, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float ReachMax = 0.f;

	/** Lowest blade height relative to attacker root, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float HeightMin = 0.f;

	/** Highest blade height relative to attacker root, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float HeightMax = 0.f;

	/** Diagnostic yaw of the derived swing-plane normal, degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float PlaneYawOffset = 0.f;

	/** Index, within Segments, of the peak-speed strike sample. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	int32 StrikeSample = 0;

	/** Automatically detected start of the active swing, seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float ActiveStartTime = 0.f;

	/** Automatically detected end of the active swing, seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float ActiveEndTime = 0.f;

	/** Peak blade-tip speed measured while analyzing the full animation, cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float PeakTipSpeed = 0.f;

	/** True once BuildBladeTrajectory has produced a usable active trajectory. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	bool bValid = false;
};

/** A geometry query result; execution still validates and admits it. */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatAttackOpportunity
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") bool bFeasible = false;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") FName TechniqueId;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") FName Region;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") FName Bone;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") FTransform Stance = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") FVector TargetLocationAtQuery = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") int32 ContactSample = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") float MissCm = 1000000.f;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") float ContactScore = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") float Quality = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") float MovementCostCm = 0.f;
	/** Root-to-target horizontal distance at this stance. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") float StandoffCm = 0.f;
	/** Blade fraction used for this opportunity's contact check. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Planning") float AimPointAlongBlade = -1.f;
};

/**
 * Runtime trajectory analysis and attack-alignment solving.
 *
 * Sampling resolution, active-window detection and temporary contact tolerance
 * are analyzer/solver policy, not per-move authored data.
 */
UCLASS()
class IRONBOUND_API UCombatTrajectoryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Query the current position and bounded nearby stances with the shared contact evaluator. */
	static void FindAttackOpportunities(
		USkeletalMeshComponent* AttackerMesh, USkeletalMeshComponent* VictimMesh,
		const FBladeTrajectory& Trajectory, const UDataTable* CombatTargets,
		FName TechniqueId, FName RequiredRegion, float AimPointAlongBlade,
		float AimWindowStartFraction, float AimWindowEndFraction,
		float FacingLimitDegrees, float ContactToleranceCm, float MaxNearbyMoveCm,
		FCombatAttackOpportunity& OutCurrent, FCombatAttackOpportunity& OutNearby);
	/**
	 * Analyze the complete source animation and derive its active blade path.
	 * Sampling count and active strike window are determined internally.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ironbound|Combat")
	static bool BuildBladeTrajectory(
		USkeletalMeshComponent* SourceMesh,
		UStaticMeshComponent* SwordComponent,
		UAnimSequenceBase* Sequence,
		FName HandBoneName,
		FVector BladeBaseLocal,
		FVector BladeTipLocal,
		FBladeTrajectory& OutTrajectory);

	/** Same derivation using an explicit authored weapon-to-hand transform. */
	static bool BuildBladeTrajectoryWithGrip(
		USkeletalMeshComponent* SourceMesh,
		UStaticMeshComponent* SwordComponent,
		UAnimSequenceBase* Sequence,
		FName HandBoneName,
		const FTransform& GripToHand,
		FVector BladeBaseLocal,
		FVector BladeTipLocal,
		FBladeTrajectory& OutTrajectory);

	/**
	 * Evaluate a supplied attacker root pose against the scored anatomical
	 * regions in the combat-target DataTable.
	 *
	 * Returns blade miss distance in cm. OutRegion/OutBone/OutSample identify
	 * the selected contact candidate; OutTargetScore is the authored score of
	 * that region.
	 */
	static float EvaluateScoredContact(
		const FBladeTrajectory& Trajectory,
		const FTransform& RootTransform,
		USkeletalMeshComponent* VictimMesh,
		const UDataTable* CombatTargets,
		FName& OutRegion,
		FName& OutBone,
		int32& OutSample,
		float& OutTargetScore,
		FName RequiredRegion = NAME_None,
		float AimPointAlongBlade = -1.f,
		float AimWindowStartFraction = 0.f,
		float AimWindowEndFraction = 1.f,
		FName RequiredBone = NAME_None,
		bool bPreferContactMargin = false);

	/**
	 * Search attacker yaw and stand-off for a feasible contact against the
	 * scored combat-target regions. Contact tolerance is solver policy rather
	 * than per-attack content.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ironbound|Combat")
	static bool SolveAttackAlignment(
		USkeletalMeshComponent* AttackerMesh,
		USkeletalMeshComponent* VictimMesh,
		const FBladeTrajectory& Trajectory,
		UDataTable* CombatTargets,
		FTransform& OutAttackerTransform,
		FName& OutRegion,
		FName& OutBone,
		int32& OutSampleIndex,
		float& OutPredictedDistance,
		float& OutTargetScore);

	/** Solver variant constrained to an authored forward-facing yaw cone. */
	static bool SolveAttackAlignmentWithFacingLimit(
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
		FName RequiredRegion = NAME_None,
		float AimPointAlongBlade = -1.f,
		float AimWindowStartFraction = 0.f,
		float AimWindowEndFraction = 1.f);
};
