#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IronboundParryComponent.generated.h"

class USkeletalMeshComponent;
class UIronboundCombatFocusComponent;
class UIronboundCombatBodyComponent;

UENUM(BlueprintType)
enum class EIronboundParryState : uint8
{
    Observing,
    Reacting,
    Moving,
    Holding
};

USTRUCT()
struct FIronboundParryDiagnostics
{
    GENERATED_BODY()

    int32 IncomingSegments = 0;
    int32 ExpiredSegments = 0;
    int32 ContactPoints = 0;
    int32 BroadReachRejected = 0;
    int32 GeneratedCandidates = 0;
    int32 AngleRejected = 0;
    int32 AnatomyRejected = 0;
    int32 BodyRejected = 0;
    int32 TimingRejected = 0;
    int32 SpeedRejected = 0;
    int32 ValidCandidates = 0;

    float BestTimeUntilContact = -1.f;
    float BestRequiredHandSpeed = -1.f;
    float BestRequiredBladeAngularSpeed = -1.f;
    float BestQuality = -1.f;
};

USTRUCT()
struct FIronboundParryCandidate
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
    FVector RequiredElbowPosition = FVector::ZeroVector;

    float IncomingTime = 0.f;
    float TimeUntilContact = 0.f;
    float DefenderBladeFraction = 0.f;
    float IncomingBladeFraction = 0.f;
    float IntersectionAngleDegrees = 0.f;
    float ElbowAngleDegrees = 0.f;
    float RequiredHandSpeed = 0.f;
    float RequiredBladeAngularSpeed = 0.f;
    float Quality = -TNumericLimits<float>::Max();

    bool bValid = false;
};

UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UIronboundParryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UIronboundParryComponent();

    UFUNCTION(BlueprintPure, Category="Combat|Parry")
    EIronboundParryState GetParryState() const { return ParryState; }

    UFUNCTION(BlueprintPure, Category="Combat|Parry")
    bool HasActiveParryPose() const { return bHasActiveParryCandidate; }

    // These are the EXECUTED targets, not the final locked targets.  The
    // Control Rig therefore follows a stable motion plan instead of snapping.
    UFUNCTION(BlueprintPure, Category="Combat|Parry")
    FTransform GetActiveParryHandTransform() const
    {
        return bHasActiveParryCandidate ? ExecutedHandTransform : FTransform::Identity;
    }

    UFUNCTION(BlueprintPure, Category="Combat|Parry")
    FVector GetActiveParryElbowPosition() const
    {
        return bHasActiveParryCandidate ? ExecutedElbowPosition : FVector::ZeroVector;
    }

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName UpperArmBone = TEXT("upperarm_r");

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName LowerArmBone = TEXT("lowerarm_r");

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName HandBone = TEXT("hand_r");

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy")
    FName PelvisBone = TEXT("pelvis");

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="0.0"))
    float ArmReachMargin = 1.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="8", ClampMax="64"))
    int32 ElbowCircleSamples = 24;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="-1.0", ClampMax="1.0"))
    float MinimumElbowSideDot = -0.15f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="0.0", ClampMax="180.0"))
    float MinimumElbowAngleDegrees = 55.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Anatomy", meta=(ClampMin="0.0", ClampMax="180.0"))
    float MaximumElbowAngleDegrees = 145.f;

    // Hard body-space constraints.  These replace the old "move the hand at
    // least N cm" shell, which was selecting spectacular but bad poses.
    UPROPERTY(EditAnywhere, Category="Combat|Parry|Pose")
    float MinimumHandHeightAbovePelvis = 12.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Pose")
    float MaximumHandHeightAboveShoulder = 45.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Pose", meta=(ClampMin="0.0"))
    float MinimumHandForwardFromTorso = 4.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Pose", meta=(ClampMin="0.0"))
    float MaximumHandLateralFromShoulder = 58.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Weapon", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinParryBladeFraction = 0.30f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Weapon", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MaxParryBladeFraction = 0.78f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="2", ClampMax="16"))
    int32 IncomingBladeContactSamples = 7;

    // Only intercept the dangerous outer portion of the attacking blade.
    // 0 = attacker blade base / hilt, 1 = tip.
    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinimumIncomingBladeFraction = 0.65f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MaximumIncomingBladeFraction = 0.95f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="1.0"))
    float PreferredIncomingBladeFraction = 0.82f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="2", ClampMax="12"))
    int32 DefenderBladeFractionSamples = 5;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="8", ClampMax="64"))
    int32 DefenseOrientationSamples = 24;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="2", ClampMax="12"))
    int32 DefenseCrossingAngleSamples = 6;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="90.0"))
    float MinimumIntersectionAngleDegrees = 70.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="90.0"))
    float PreferredIntersectionAngleDegrees = 90.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="0.0", ClampMax="180.0"))
    float PreferredElbowAngleDegrees = 100.f;

    // Quality weights.  Higher quality wins; displacement is only a small
    // comfort term now, never the primary objective.
    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")
    float CrossingQualityWeight = 5.0f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")
    float ElbowQualityWeight = 2.0f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")
    float BladeCenterQualityWeight = 1.0f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")
    float TimingMarginQualityWeight = 0.5f;

    // For now we explicitly prefer readable, visible defensive movement.
    // We are NOT rewarding cheap/minimal hand motion.
    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")
    float DramaticPoseQualityWeight = 3.0f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver", meta=(ClampMin="1.0"))
    float PreferredVisibleHandTravel = 38.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Solver")
    float IncomingTipContactQualityWeight = 3.0f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.0"))
    float PerceptionDelaySeconds = 0.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.0"))
    float ReactionDelaySeconds = 0.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="1.0"))
    float MaxParryHandSpeed = 500.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="1.0"))
    float MaxParryBladeAngularSpeed = 720.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Timing", meta=(ClampMin="0.001"))
    float MinimumTimeToContact = 0.03f;

    // Real execution speed, deliberately separate from feasibility limits.
    UPROPERTY(EditAnywhere, Category="Combat|Parry|Execution", meta=(ClampMin="1.0"))
    float ParryMovementSpeed = 240.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Execution", meta=(ClampMin="1.0"))
    float ParryRotationSpeedDegrees = 900.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Execution", meta=(ClampMin="0.0"))
    float LeadHoldSeconds = 0.06f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Execution", meta=(ClampMin="0.1"))
    float PositionArrivalTolerance = 2.f;

    UPROPERTY(EditAnywhere, Category="Combat|Parry|Debug")
    bool bDrawParryDebug = true;

private:
    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> FighterMesh;

    UPROPERTY()
    TObjectPtr<UIronboundCombatFocusComponent> CombatFocus;

    UPROPERTY()
    TObjectPtr<UIronboundCombatBodyComponent> CombatBody;

    TWeakObjectPtr<AActor> ObservedAttacker;

    bool bObservedCommittedAttack = false;
    bool bAttackRecognized = false;
    bool bReactionReady = false;
    float ObservationWorldTime = 0.f;
    float RecognitionWorldTime = 0.f;

    mutable bool bDiagnosticsLoggedForObservedAttack = false;
    mutable FString LastParryEarlyExitReason;

    float UpperArmLength = 0.f;
    float ForearmLength = 0.f;
    float ArmLength = 0.f;

    mutable FIronboundParryDiagnostics LastDiagnostics;

    FIronboundParryCandidate ActiveParryCandidate;
    bool bHasActiveParryCandidate = false;
    EIronboundParryState ParryState = EIronboundParryState::Observing;

    FTransform ExecutedHandTransform = FTransform::Identity;
    FVector ExecutedElbowPosition = FVector::ZeroVector;

    void ResetParryAction();
    void InitializeExecutionPose();
    void UpdateExecutionPose(float DeltaTime);

    bool CalculateArmDimensions();
    void UpdateAttackTimingState();
    bool GetIncomingSourcePlaybackTime(float& OutSourceTime) const;
    float GetTimeUntilIncomingSample(float IncomingTime) const;

    void DrawDiagnosticsDebug() const;
    void LogDiagnosticsOnce() const;
    void DrawAnatomyDebug() const;
    void DrawParrySolutionDebug() const;

    bool GetCurrentWeaponGeometry(
        FTransform& OutCurrentWeaponTransform,
        FVector& OutCurrentBaseWorld,
        FVector& OutCurrentTipWorld,
        float& OutBladeLength,
        float& OutMinFraction,
        float& OutMaxFraction) const;

    bool FindBestElbowPosition(
        const FVector& ShoulderWorld,
        const FVector& RequiredHandWorld,
        const FVector& CurrentElbowWorld,
        FVector& OutElbowWorld,
        float& OutElbowMovementCost,
        float& OutElbowAngleDegrees) const;

    bool IsDefensivePoseAnatomicallyUseful(
        const FVector& ShoulderWorld,
        const FVector& RequiredHandWorld,
        const FVector& CandidateElbow) const;

    bool FindBestParryCandidate(FIronboundParryCandidate& OutCandidate) const;

    bool EvaluateCandidate(
        const FVector& ShoulderWorld,
        const FVector& CurrentElbowWorld,
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
        const FVector& CurrentDefenseBase,
        const FVector& CurrentDefenseDirection,
        FIronboundParryCandidate& OutCandidate) const;
};
