#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "CombatTechniqueExecutionConfigs.generated.h"

class UCombatTechniqueExecutor;
class UAnimMontage;
class UAnimSequenceBase;
class UDataTable;

/**
 * Authored executor binding + configuration for one execution strategy.
 *
 * A technique row points at exactly one config DataAsset; the config names
 * the concrete executor class and carries everything that executor needs.
 * Runtime state never lives here.
 */
UCLASS(BlueprintType)
class IRONBOUND_API UCombatTechniqueExecutionConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Concrete executor strategy created for techniques using this config. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Execution")
	TSubclassOf<UCombatTechniqueExecutor> ExecutorClass;

	/** Anatomical target table used to resolve hit regions; null when the strategy has none. */
	virtual const UDataTable* GetCombatTargets() const { return nullptr; }
};

/** Authored configuration for the melee strike executor. */
UCLASS(BlueprintType)
class IRONBOUND_API UExecConfig_MeleeStrike : public UCombatTechniqueExecutionConfig
{
	GENERATED_BODY()

public:
	/** Committed-strike montage played by the executor itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TObjectPtr<UAnimMontage> Montage;

	/** Source animation the blade trajectory is derived from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation")
	TObjectPtr<UAnimSequenceBase> SourceSequence;

	/** Global anatomical target definitions used by the alignment solver. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
	TObjectPtr<UDataTable> CombatTargets;

	/** Optional semantic region restriction when the request does not name one; ordinary cuts leave this empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
	FName DefaultTargetRegion;

	/** -1 = moving sword tip (cuts); 0..1 = a specified blade point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting", meta=(ClampMin="-1.0", ClampMax="1.0"))
	float AimPointAlongBlade = -1.f;

	/** Earliest point in the active swing eligible to be the planned contact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting", meta=(ClampMin="0.0", ClampMax="1.0"))
	float AimWindowStartFraction = 0.f;

	/** Latest point eligible for contact; later animation is follow-through. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting", meta=(ClampMin="0.0", ClampMax="1.0"))
	float AimWindowEndFraction = 1.f;

	/** Distance from the solved stance location considered arrived, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Engagement", meta=(ClampMin="0.1"))
	float ArrivalTolerance = 8.f;

	/** Allowed yaw error against the solved stance before committing, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Engagement", meta=(ClampMin="0.1"))
	float FacingTolerance = 3.f;

	/** Maximum planned yaw deviation from directly facing the target; zero requires a direct facing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Engagement", meta=(ClampMin="0.0", ClampMax="180.0"))
	float MaxFacingDeviationFromTargetDegrees = 45.f;

	/** Root speed below which the fighter counts as settled, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Engagement")
	float SettledSpeed = 5.f;

	/** Maximum time allowed to satisfy navigation/alignment before abandoning the request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Engagement", meta=(ClampMin="1.0"))
	float PreparationTimeoutSeconds = 25.f;

	/** Tolerance used by the prototype target-contact solver, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Engagement", meta=(ClampMin="0.1"))
	float ContactToleranceCm = 25.f;

	/** Safety deadline added after the sampled strike interval, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Engagement", meta=(ClampMin="0.0"))
	float CommitTimeoutExtraSeconds = 2.f;

	/** Recovery duration after the strike finishes, seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Engagement", meta=(ClampMin="0.0"))
	float RecoverySeconds = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug")
	bool bDrawDebugBlade = true;

	virtual const UDataTable* GetCombatTargets() const override { return CombatTargets.Get(); }
};

/**
 * Authored policy for the procedural (animation-free) strike executor.
 *
 * Contains no absolute distances. Every spatial quantity is a fraction of
 * the executing fighter's own measured reach (upper arm + forearm from the
 * reference skeleton, plus the equipped weapon's grip-to-contact distance) or
 * of its arm length, so the same technique asset works for any body size,
 * limb proportion and weapon.
 */
UCLASS(BlueprintType)
class IRONBOUND_API UExecConfig_ProceduralStrike : public UCombatTechniqueExecutionConfig
{
	GENERATED_BODY()

public:
	// ----- Targeting -----

	/** Anatomical target regions the strike may aim at (Bones + Score per row). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
	TObjectPtr<UDataTable> CombatTargets;

	/** Optional region restriction when the request does not name one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting")
	FName DefaultTargetRegion;

	/** Point along the blade (0 = BladeBase, 1 = BladeTip) that is driven through the target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Targeting", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ContactBladeFraction = 0.7f;

	// ----- Reach window (fractions of the fighter's reach R) -----

	/** Preferred shoulder-to-target distance used when choosing where to stand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reach", meta=(ClampMin="0.1", ClampMax="1.0"))
	float PreferredReachFraction = 0.8f;

	/** Closest shoulder-to-target distance from which the strike may start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reach", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinReachFraction = 0.45f;

	/** Farthest shoulder-to-target distance from which the strike may start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reach", meta=(ClampMin="0.1", ClampMax="1.0"))
	float MaxReachFraction = 0.95f;

	/** Root distance from the chosen stance considered arrived. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reach", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ArrivalToleranceFraction = 0.08f;

	// ----- Charge (chamber) search -----

	/** Distance from the target point back to the charged contact point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Charge", meta=(ClampMin="0.05", ClampMax="2.0"))
	float ChargeDistanceFraction = 0.7f;

	/** Distance the contact point keeps travelling past the target point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Charge", meta=(ClampMin="0.0", ClampMax="2.0"))
	float FollowThroughFraction = 0.3f;

	/** Number of chamber directions sampled around the reach axis (right, high, overhead, left, low...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Charge", meta=(ClampMin="4", ClampMax="48"))
	int32 ChargeAngleSamples = 12;

	/** How far each chamber direction is pulled back toward the attacker, degrees (0 = pure sweep). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Charge")
	TArray<float> ChargePullBackAnglesDegrees = { 15.f, 35.f, 55.f };

	/** Weapon roll samples when the weapon has no authored strike edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Charge", meta=(ClampMin="1", ClampMax="24"))
	int32 UnknownEdgeRollSamples = 8;

	/** Candidates within this fraction of the best comfort are eligible for random selection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Charge", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SelectionQualityBand = 0.85f;

	// ----- Anatomy constraints -----

	/** Fallback bones when the equipment grip does not resolve them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy")
	FName UpperArmBone = TEXT("upperarm_r");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy")
	FName LowerArmBone = TEXT("lowerarm_r");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy")
	FName HandBone = TEXT("hand_r");

	/** Shoulder-to-hand distance limits, as fractions of the fighter's arm length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinArmExtensionFraction = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy", meta=(ClampMin="0.1", ClampMax="1.0"))
	float MaxArmExtensionFraction = 0.98f;

	/** How far behind the shoulder (along facing) the hand may go, as a fraction of arm length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaxHandBehindShoulderFraction = 0.25f;

	/** Maximum unavoidable wrist deviation for any pose on the path, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy", meta=(ClampMin="1.0", ClampMax="180.0"))
	float MaxWristDeviationDegrees = 80.f;

	/** Poses validated along the charge travel and the strike line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Anatomy", meta=(ClampMin="2", ClampMax="32"))
	int32 PathValidationSamples = 8;

	// ----- Timing -----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.05"))
	float ChargeDurationSeconds = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.05"))
	float StrikeDurationSeconds = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.0"))
	float FollowThroughHoldSeconds = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.05"))
	float RecoverySeconds = 0.4f;

	/** Maximum time to reach the stance and face the target before abandoning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.5"))
	float PreparationTimeoutSeconds = 8.f;

	/** Allowed yaw error toward the target before charging, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.1", ClampMax="90.0"))
	float FacingToleranceDegrees = 10.f;

	// ----- Debug -----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Debug")
	bool bDrawDebug = true;

	virtual const UDataTable* GetCombatTargets() const override { return CombatTargets.Get(); }
};

/**
 * Authored policy for the procedural parry executor.
 *
 * Moved verbatim from the old parry component: the solver math is unchanged,
 * only its ownership moved into the reactive execution path.
 */
UCLASS(BlueprintType)
class IRONBOUND_API UExecConfig_ProceduralParry : public UCombatTechniqueExecutionConfig
{
	GENERATED_BODY()

public:
	// ----- Anatomy -----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Anatomy")
	FName UpperArmBone = TEXT("upperarm_r");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Anatomy")
	FName LowerArmBone = TEXT("lowerarm_r");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Anatomy")
	FName HandBone = TEXT("hand_r");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Anatomy")
	FName PelvisBone = TEXT("pelvis");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Anatomy", meta=(ClampMin="0.0"))
	float ArmReachMargin = 1.f;

	// ----- Policy -----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Policy", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ParrySkillFallback = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Policy", meta=(ClampMin="0.0", ClampMax="1.0"))
	float QualityBandWidth = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Policy", meta=(ClampMin="0.1"))
	float WristSharpness = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Policy", meta=(ClampMin="0.1"))
	float FreedomSharpness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Policy", meta=(ClampMin="0.0", ClampMax="1.0"))
	float TacticalWeight = 0.7f;

	// ----- Weapon sampling -----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Weapon", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinParryBladeFraction = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Weapon", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaxParryBladeFraction = 0.78f;

	// ----- Solver -----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="2", ClampMax="16"))
	int32 IncomingBladeContactSamples = 7;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinimumIncomingBladeFraction = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Pose", meta=(ClampMin="0.0"))
	float PreferredVisibleHandTravel = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaximumIncomingBladeFraction = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PreferredIncomingBladeFraction = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="2", ClampMax="12"))
	int32 DefenderBladeFractionSamples = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="8", ClampMax="64"))
	int32 DefenseOrientationSamples = 24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="2", ClampMax="12"))
	int32 DefenseCrossingAngleSamples = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="0.0", ClampMax="90.0"))
	float MinimumIntersectionAngleDegrees = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="0.0", ClampMax="90.0"))
	float PreferredIntersectionAngleDegrees = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver")
	float CrossingQualityWeight = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver")
	float BladeCenterQualityWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver")
	float DramaticPoseQualityWeight = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver")
	float IncomingTipContactQualityWeight = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver")
	float BodyClearanceQualityWeight = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Solver", meta=(ClampMin="1.0"))
	float BodyClearancePreferenceDistance = 75.f;

	// ----- Timing -----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Timing", meta=(ClampMin="0.0"))
	float BodySafetyMarginSeconds = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Timing", meta=(ClampMin="1.0"))
	float MaxParryHandSpeed = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Timing", meta=(ClampMin="1.0"))
	float MaxParryBladeAngularSpeed = 720.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Timing", meta=(ClampMin="0.001"))
	float MinimumTimeToContact = 0.03f;

	// ----- Execution -----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Execution", meta=(ClampMin="1.0"))
	float ParryMovementSpeed = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Execution", meta=(ClampMin="1.0"))
	float ParryRotationSpeedDegrees = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Execution", meta=(ClampMin="0.0"))
	float LeadHoldSeconds = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Execution", meta=(ClampMin="0.1"))
	float PositionArrivalTolerance = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Parry|Debug")
	bool bDrawParryDebug = true;
};
