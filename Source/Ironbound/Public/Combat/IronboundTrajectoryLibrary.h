// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "IronboundTrajectoryLibrary.generated.h"

/**
 * One slice of a swing: the blade treated as a straight line between its base and its tip,
 * expressed in the attacker's root-local space at one instant of the animation.
 */
USTRUCT(BlueprintType)
struct FBladeSegment
{
	GENERATED_BODY()

	/** Blade base (the end nearest the guard), in attacker root-local space. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	FVector Base = FVector::ZeroVector;

	/** Blade tip, in attacker root-local space. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	FVector Tip = FVector::ZeroVector;

	/** Position within the sampled window, normalised 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float NormalizedTime = 0.f;
};

/**
 * The intended blade path of one attack, derived from the animation itself rather than baked
 * offline. Because every trajectory family (vertical chop, horizontal sweep, diagonal) is just
 * a set of line segments, a single point-to-segment test covers all of them - nothing here is
 * special-cased per attack type.
 *
 * The samples describe the INTENDED move, i.e. what the animation does under perfect tracking.
 * What the fighter actually achieves is whatever Physics Control delivers at runtime; the gap
 * between the two is the mechanic, not an error.
 */
USTRUCT(BlueprintType)
struct FBladeTrajectory
{
	GENERATED_BODY()

	/** The blade, sampled through the strike window. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	TArray<FBladeSegment> Segments;

	/** Smallest horizontal distance from the root to any blade point, in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float ReachMin = 0.f;

	/** Largest horizontal distance from the root to any blade point, in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float ReachMax = 0.f;

	/** Lowest blade height relative to the root, in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float HeightMin = 0.f;

	/** Highest blade height relative to the root, in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float HeightMax = 0.f;

	/**
	 * Yaw of the swing plane's normal in character space, in degrees. Diagnostic only: the
	 * alignment solve searches yaw directly rather than trusting this, because a plane normal is
	 * sign-ambiguous (N and -N describe the same plane).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	float PlaneYawOffset = 0.f;

	/** Index of the fastest-moving sample - the strike moment. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	int32 StrikeSample = 0;

	/** True once BuildBladeTrajectory has populated this. */
	UPROPERTY(BlueprintReadOnly, Category = "Ironbound|Trajectory")
	bool bValid = false;
};

/**
 * Runtime derivation and alignment solving for melee attacks.
 *
 * Both entry points are self-contained: Blueprint sees a blade path go in and a pose to stand at
 * come out, and never has to deal with a segment, a plane or a bone list.
 */
UCLASS()
class IRONBOUND_API UIronboundTrajectoryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Derives the intended blade path for one attack by sampling the animation, at runtime.
	 *
	 * A hidden evaluation mesh plays the sequence in single-node mode and is stepped to each
	 * sample time, so nothing is baked to disk and no pre-run preparation is needed: change or
	 * swap the animation and the next run derives the new path automatically.
	 *
	 * Called once per move during warm-up, the cost is one pose evaluation per sample. Do not
	 * call it on the frame an attack is committed.
	 *
	 * @param SourceMesh	The fighter's skeletal mesh. Its owner becomes the derivation basis.
	 * @param SwordComponent	The weapon mesh; grip comes from the initialized equipment definition.
	 * @param Sequence		The attack animation to sample.
	 * @param HandBoneName	Bone the sword is gripped by ("hand_r").
	 * @param BladeBaseLocal	Blade base in sword-local space. Pass zero on both points to auto-derive.
	 * @param BladeTipLocal	Blade tip in sword-local space. Pass zero on both points to auto-derive.
	 * @param StartTime		Window start, in seconds.
	 * @param EndTime		Window end, in seconds.
	 * @param NumSamples	How many slices to take. Use enough samples for the move; the current strike uses 32.
	 * @param OutTrajectory	Populated on success.
	 * @return True if a usable path was derived.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ironbound|Combat")
	static bool BuildBladeTrajectory(
		USkeletalMeshComponent* SourceMesh,
		UStaticMeshComponent* SwordComponent,
		UAnimSequenceBase* Sequence,
		FName HandBoneName,
		FVector BladeBaseLocal,
		FVector BladeTipLocal,
		float StartTime,
		float EndTime,
		int32 NumSamples,
		FBladeTrajectory& OutTrajectory);

	/** Explicit authored grip variant used by equipment; independent of live physics. */
	static bool BuildBladeTrajectoryWithGrip(USkeletalMeshComponent* SourceMesh,
		UStaticMeshComponent* SwordComponent, UAnimSequenceBase* Sequence, FName HandBoneName,
		const FTransform& WeaponToHand, FVector BladeBaseLocal, FVector BladeTipLocal,
		float StartTime, float EndTime, int32 NumSamples, FBladeTrajectory& OutTrajectory);

	/** Same contact metric as the planner, evaluated at a supplied (usually actual) root pose. */
	static float EvaluateContact(const FBladeTrajectory& Trajectory, const FTransform& RootTransform,
		USkeletalMeshComponent* VictimMesh, const TArray<FName>& AllowedBones,
		FName& OutBone, int32& OutSample);

	/**
	 * Finds the attacker pose from which one of the allowed victim bones would actually be struck.
	 *
	 * Searches yaw and stand-off distance, and for each candidate takes the minimum
	 * point-to-segment distance over EVERY allowed bone against EVERY blade segment. The move is
	 * therefore satisfied by touching anything it can reach - it is never required to reach one
	 * nominated part, so a chest-height horizontal sweep resolves to a torso bone and counts as a
	 * success rather than stalling on a head it cannot reach.
	 *
	 * Returns the best pose found even when it is out of tolerance, so the caller can walk toward it
	 * and re-solve; the return value reports whether that pose is actually good enough to commit.
	 *
	 * @param AttackerMesh	The attacker's skeletal mesh.
	 * @param VictimMesh	The victim's skeletal mesh.
	 * @param Trajectory	A trajectory from BuildBladeTrajectory.
	 * @param AllowedBones	Bones that count as a hit. Legs are deliberately excluded while they are kinematic.
	 * @param AcceptanceRadius	How close the blade must pass, in cm, to count as contact.
	 * @param OutAttackerTransform	Best stand-and-face pose found, in world space.
	 * @param OutBone		The bone that pose would actually contact.
	 * @param OutSampleIndex	Which slice of the swing makes contact.
	 * @param OutPredictedDistance	How close the blade would pass, in cm.
	 * @return True when OutPredictedDistance is within AcceptanceRadius.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ironbound|Combat")
	static bool SolveAttackAlignment(
		USkeletalMeshComponent* AttackerMesh,
		USkeletalMeshComponent* VictimMesh,
		const FBladeTrajectory& Trajectory,
		const TArray<FName>& AllowedBones,
		float AcceptanceRadius,
		FTransform& OutAttackerTransform,
		FName& OutBone,
		int32& OutSampleIndex,
		float& OutPredictedDistance);
};
