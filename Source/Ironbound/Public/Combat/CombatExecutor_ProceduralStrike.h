#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTechniqueExecutor.h"
#include "CombatExecutor_ProceduralStrike.generated.h"

class UExecConfig_ProceduralStrike;
class USkeletalMeshComponent;

/**
 * Measured arm + weapon geometry of one fighter. Everything the procedural
 * strike needs about body size and weapon length comes from here, so the
 * technique config itself stays dimensionless.
 */
struct FProceduralStrikeArm
{
	FName UpperArmBone;
	FName LowerArmBone;
	FName HandBone;

	float UpperArmLength = 0.f;
	float ForearmLength = 0.f;

	/** Distance from the hand to the driven blade point (rigid grip). */
	float HandToContactLength = 0.f;

	/** Weapon-local blade geometry. */
	FVector BladeBaseLocal = FVector::ZeroVector;
	FVector BladeTipLocal = FVector::ZeroVector;
	FVector ContactLocal = FVector::ZeroVector;
	FVector BladeAxisLocal = FVector::ForwardVector;

	FTransform WeaponToHand = FTransform::Identity;
	FVector WeaponScale = FVector::OneVector;

	/** Reference-pose hand relative to forearm, and forearm direction in forearm space. */
	FTransform NeutralWristRelationship = FTransform::Identity;
	FVector ReferenceForearmDirLocal = FVector::ForwardVector;

	bool bValid = false;

	float GetArmLength() const { return UpperArmLength + ForearmLength; }

	/** Maximum shoulder-to-contact-point distance: arm plus grip-to-contact. */
	float GetReach() const { return GetArmLength() + HandToContactLength; }
};

/**
 * Procedural, animation-free strike (Half Sword style).
 *
 * Phases: Approach (emit a stance requirement derived from the fighter's own
 * reach) -> Charge (move the weapon into an anatomically valid chamber
 * position) -> Strike (drive the contact point of the blade in a straight
 * line through the selected target body part) -> Hold -> Recover.
 *
 * The executor only publishes a hand target; CCDIK in the AnimGraph solves
 * the arm and the physical body tracks it, exactly like the procedural parry.
 * Contact/damage resolution is the shared execution-component sweep.
 */
UCLASS()
class IRONBOUND_API UCombatExecutor_ProceduralStrike : public UCombatTechniqueExecutor
{
	GENERATED_BODY()

public:
	/**
	 * Geometry-only opportunity query usable by any controller. Returns at
	 * most one feasible opportunity per target region, each with a stance
	 * whose reach window is derived from the attacker's own arm and weapon.
	 */
	static void FindProceduralStrikeOpportunities(
		const UExecConfig_ProceduralStrike* InConfig,
		FName TechniqueId,
		AActor* Attacker,
		AActor* Target,
		FName RequiredRegion,
		float MaxMoveCm,
		TArray<FCombatAttackOpportunity>& OutOpportunities);

	/** Measures arm and weapon geometry for a fighter. */
	static bool BuildArmModel(
		const UExecConfig_ProceduralStrike* InConfig,
		AActor* Fighter,
		FProceduralStrikeArm& OutArm);

	virtual void GetEngagementRequirement(FCombatEngagementRequirement& OutRequirement) const override;
	virtual bool GetFacingIntent(FVector& OutIntent) const override;
	virtual bool IsAwaitingAlignment() const override;
	virtual float GetFacingDeltaDegrees() const override;
	virtual bool GetHandTarget(FTransform& OutHandTarget) const override;
	virtual void OnExternalFinishRequest() override;

protected:
	virtual bool OnInitialize(const FCombatTechniqueRequest& InRequest) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnFinish() override;

private:
	enum class EPhase : uint8
	{
		Approach,
		Charge,
		Strike,
		Hold,
		Recover
	};

	struct FChargeCandidate
	{
		FVector ChargeContact = FVector::ZeroVector;
		FVector LocalEdge = FVector::ZeroVector;
		FTransform ChargeHand = FTransform::Identity;
		float Comfort = 0.f;
	};

	const UExecConfig_ProceduralStrike* StrikeConfig() const;

	bool GetLiveTargetPoint(FVector& OutPoint) const;
	FVector GetShoulderWorld() const;
	FVector GetFacingDirectionToTarget() const;
	float ComputeFacingError() const;
	bool IsInReachWindow(const FVector& Shoulder, const FVector& TargetPoint) const;

	// ===== Pose model =====

	bool ComputePoseOnPath(
		const FVector& Shoulder,
		const FVector& ContactPoint,
		const FVector& StrikeDirection,
		const FVector& LocalEdge,
		FTransform& OutWeapon,
		FTransform& OutHand) const;

	bool CheckPose(
		const FVector& Shoulder,
		const FVector& Forward,
		const FTransform& Weapon,
		const FTransform& Hand,
		float& OutComfort) const;

	bool EvaluateStrikePath(
		const FVector& Shoulder,
		const FVector& Forward,
		const FVector& Start,
		const FVector& End,
		const FVector& LocalEdge,
		float& OutComfort) const;

	bool EvaluateChargeTravel(
		const FVector& Shoulder,
		const FVector& Forward,
		const FTransform& FromHand,
		const FTransform& ToHand) const;

	float ComputeOwnBodyClearance(const FVector& BladeBase, const FVector& BladeTip) const;
	float ComputeWristDeviationRadians(const FVector& Shoulder, const FTransform& Hand) const;
	void BuildLocalEdgeCandidates(TArray<FVector>& OutEdges) const;

	// ===== Phases =====

	bool AdoptOpportunity(const FCombatAttackOpportunity& Opportunity);
	bool SolveChargePose();
	void BeginCharge();
	bool BeginStrike();
	void EnterRecovery(const TCHAR* Reason);
	bool BuildStrikeTrajectory(FBladeTrajectory& OutTrajectory, const FTransform& Root) const;
	void UpdateApproach(float Now);

	void BeginBrace();
	void EndBrace();

	void DrawDebug() const;

	static float Ease(float Alpha);

	EPhase Phase = EPhase::Approach;

	FProceduralStrikeArm Arm;

	TWeakObjectPtr<AActor> PlannedTarget;
	FName PlannedRegion = NAME_None;
	FName PlannedBone = NAME_None;
	FTransform PlannedStance = FTransform::Identity;

	FCombatEngagementRequirement CachedRequirement;

	// Charge plan (actor-local so a small root yaw correction carries the chamber with it).
	FChargeCandidate ActiveCharge;
	FVector ChargeContactLocal = FVector::ZeroVector;
	FTransform ChargeHandLocal = FTransform::Identity;
	FTransform RestHandLocal = FTransform::Identity;
	FTransform PhaseStartHandLocal = FTransform::Identity;

	// Strike line (world space, fixed for the short strike).
	FVector StrikeShoulder = FVector::ZeroVector;
	FVector StrikeStart = FVector::ZeroVector;
	FVector StrikeTargetPoint = FVector::ZeroVector;
	FVector StrikeEnd = FVector::ZeroVector;
	FVector StrikeDirection = FVector::ZeroVector;
	float StrikeContactAlpha = 0.f;

	FTransform ExecutedHand = FTransform::Identity;
	bool bHandTargetActive = false;

	float RequestWorldTime = 0.f;
	float PhaseStartWorldTime = 0.f;
	float OutOfWindowSinceWorldTime = 0.f;
	float NextSolveLogWorldTime = 0.f;
	float FacingErrorDegrees = 0.f;

	bool bInPosition = false;
	bool bBraceOwned = false;
};
