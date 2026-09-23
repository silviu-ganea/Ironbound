#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTechniqueExecutor.h"
#include "CombatExecutor_ProceduralParry.generated.h"

class UCombatBodyComponent;
class UCombatEquipmentComponent;
class UExecConfig_ProceduralParry;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/** Parry solver internal candidate: one anatomically valid interception. */
USTRUCT()
struct FParryCandidate
{
	GENERATED_BODY()

	FVector ContactPoint = FVector::ZeroVector;
	FVector IncomingBase = FVector::ZeroVector;
	FVector IncomingTip = FVector::ZeroVector;
	FVector DefenseBase = FVector::ZeroVector;
	FVector DefenseTip = FVector::ZeroVector;

	FTransform RequiredWeaponTransform = FTransform::Identity;
	FTransform RequiredHandTransform = FTransform::Identity;
	FVector RequiredHandPosition = FVector::ZeroVector;

	float IncomingTime = 0.f;
	float TimeUntilContact = 0.f;
	float DefenderBladeFraction = 0.f;
	float IncomingBladeFraction = 0.f;
	float IntersectionAngleDegrees = 0.f;
	float RequiredHandSpeed = 0.f;
	float RequiredBladeAngularSpeed = 0.f;
	float BodyClearance = -1.f;
	float Quality = -TNumericLimits<float>::Max();

	bool bValid = false;
};

/** Solver internal per-candidate biomechanics before policy weighting. */
USTRUCT()
struct FArmExtensionMetrics
{
	GENERATED_BODY()

	float Distance = 0.f;
	float MaxReach = 0.f;
	float ExtensionRatio = 0.f;
	float MinReach = 0.f;
	float UpperArmLength = 0.f;
	float ForearmLength = 0.f;
	float GeometricFreedom = 0.f; // |sin(joint angle)|: 0 at folded/extended singularities
};

USTRUCT()
struct FCandidateRawMetrics
{
	GENERATED_BODY()

	float TacticalQuality = 0.f;      // 0-1, derived from crossing angles, timing, etc.
	float WristDeviationRadians = 0.f; // 0-PI biomechanical metric
	float GeometricFreedom = 0.f;     // 0-1 biomechanical metric
	float ExtensionRatio = 0.f;       // shoulder-to-hand distance / maximum two-link reach
	bool bGeometricallyValid = false; // Passes reach constraints
};

/** Selection weighting policy (built from the authored execution config). */
USTRUCT()
struct FParryPolicy
{
	GENERATED_BODY()

	float TacticalWeight = 0.7f;      // Weight for tactical quality in combined score
	float WristSharpness = 1.5f;     // Wrist deviation curve steepness
	float FreedomSharpness = 2.0f;   // Geometric freedom curve steepness
	float QualityBandWidth = 0.95f;  // Candidate quality selection band (95% of best)
	float ParrySkill = 0.5f;         // 0.0 = low skill, 1.0 = high skill
};

USTRUCT()
struct FParryDiagnostics
{
	GENERATED_BODY()

	int32 IncomingSegments = 0;
	int32 ExpiredSegments = 0;
	int32 ContactPoints = 0;
	int32 BroadReachRejected = 0;
	int32 GeneratedCandidates = 0;
	int32 AngleRejected = 0;
	int32 AnatomyRejected = 0;
	int32 WristRejected = 0;
	int32 BodyRejected = 0;
	int32 TimingRejected = 0;
	int32 SpeedRejected = 0;
	int32 ValidCandidates = 0;

	float FirstBodyIntersectionTime = -1.f;
	float BestBodyClearance = -1.f;
	float BestTimeUntilContact = -1.f;
	float BestRequiredHandSpeed = -1.f;
	float BestRequiredBladeAngularSpeed = -1.f;
	float BestQuality = -1.f;
	float BestAnatomyReachDeficitCm = TNumericLimits<float>::Max();
	float FailedCandidateHandDistanceCm = -1.f;
	float FailedCandidateMinReachCm = -1.f;
	float FailedCandidateMaxReachCm = -1.f;
	float FailedCandidateIncomingFraction = -1.f;
};

/**
 * Reactive parry execution: solve, intercept, contact, recover.
 *
 * Created only after Sword_Parry has been requested and admitted. It never
 * waits for attacks on its own and never decides "I should parry": the
 * controller observed the threat (UCombatThreatComponent) and chose the
 * technique; the pawn validated it authoritatively; this executor then solves
 * and performs the interception until terminal.
 */
UCLASS()
class IRONBOUND_API UCombatExecutor_ProceduralParry : public UCombatTechniqueExecutor
{
	GENERATED_BODY()

public:
	virtual bool GetHandTarget(FTransform& OutHandTarget) const override;

protected:
	virtual bool OnInitialize(const FCombatTechniqueRequest& InRequest) override;
	virtual void OnTick(float DeltaTime) override;
	virtual void OnFinish() override;

private:
	enum class EParryExecutionPhase : uint8
	{
		Intercept,
		Contact,
		Recover
	};

	const UExecConfig_ProceduralParry* ParryConfig() const;
	FName ResolveUpperArmBone() const;
	FName ResolveLowerArmBone() const;
	FName ResolveHandBone() const;

	// ===== Solver (migrated verbatim from the retired parry component) =====

	bool FindBestParryCandidate(FParryCandidate& OutCandidate) const;
	bool EvaluateCandidate(
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
		FParryCandidate& OutCandidate) const;

	FArmExtensionMetrics CalculateArmExtensionMetrics(
		const FVector& Shoulder,
		const FVector& Hand,
		float UpperArmLengthParam,
		float ForearmLengthParam) const;

	float CalculateMinimumWristDeviationAnalytical(
		const FVector& Shoulder,
		const FVector& Hand,
		const FVector& NeutralForearmDir,
		float UpperArmLengthParam,
		float ForearmLengthParam) const;

	FVector CalculateReferenceForearmDirection() const;
	FTransform CalculateNeutralWristRelationship() const;
	FVector CalculateNeutralForearmDirection(const FTransform& RequiredHandTransform) const;

	float ApplyPolicyToRawMetrics(const FCandidateRawMetrics& Raw, const FParryPolicy& Policy) const;

	void SelectComfortableCandidate(
		const TArray<FParryCandidate>& Candidates,
		FParryCandidate& OutSelected,
		uint32 SelectionSeed,
		const FParryPolicy& Policy) const;

	bool GetAttackerSourcePlaybackTime(float& OutSourceTime) const;

	// ===== Execution =====

	bool CalculateArmDimensions();
	void InitializeExecutionPose();
	void UpdateExecutionPose(float DeltaTime);
	void EndParryBrace();
	void LogDiagnosticsOnce() const;

	void DrawAnatomyDebug() const;
	void DrawParrySolutionDebug() const;
	void DrawDiagnosticsDebug() const;

	EParryExecutionPhase Phase = EParryExecutionPhase::Intercept;

	FParryCandidate ActiveParryCandidate;
	FTransform ExecutedHandTransform = FTransform::Identity;

	mutable FParryDiagnostics LastDiagnostics;
	FParryPolicy Policy;

	float UpperArmLength = 0.f;
	float ForearmLength = 0.f;
	float ArmLength = 0.f;

	float ContactAtWorldTime = 0.f;
	float ContactDeadlineWorldTime = 0.f;
	float InterceptDeadlineWorldTime = 0.f;
	bool bBraceActive = false;
	bool bContactResolved = false;

	mutable bool bDiagnosticsLogged = false;
	mutable FString LastParryEarlyExitReason;

	// Per-execution actual-crossing tracking (was a global component map).
	struct FActualParrySample
	{
		float PlannedCross = 0.f;
		float ClosestDistance = TNumericLimits<float>::Max();
		float CrossAtClosest = -1.f;
		float ConfirmedDistance = -1.f;
		float ConfirmedCross = -1.f;
		FVector PreviousAttackerBase = FVector::ZeroVector;
		FVector PreviousAttackerTip = FVector::ZeroVector;
		FVector PreviousDefenderBase = FVector::ZeroVector;
		FVector PreviousDefenderTip = FVector::ZeroVector;
		bool bHasPreviousWeaponSamples = false;
		bool bInterceptionConfirmed = false;
		bool bTracking = false;
	};

	FActualParrySample ActualSample;
};
