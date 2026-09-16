#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/IronboundTrajectoryLibrary.h"
#include "IronboundCombatExecutionComponent.generated.h"

class USkeletalMeshComponent;
class UAnimSequenceBase;

UENUM(BlueprintType)
enum class EIronboundAttackPhase : uint8 { Idle, Approaching, Aligning, Committed, Recovery };

/** Movement/action arbitration. AI selects a target and move; this component executes the request. */
UCLASS(ClassGroup=(Ironbound), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UIronboundCombatExecutionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UIronboundCombatExecutionComponent();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	EIronboundAttackPhase Phase = EIronboundAttackPhase::Idle;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	FTransform PlannedTransform;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	float CurrentPredictedDistance = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	float FacingErrorDegrees = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	bool bStrikeWindowOpen = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	float MaxCommittedFacingError = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0.1"))
	float ArrivalTolerance = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat", meta=(ClampMin="0.1"))
	float FacingTolerance = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat")
	float SettledSpeed = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat")
	float RecoverySeconds = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat")
	bool bDrawActualBlade = true;

	/** Returns true once, only after the ACTUAL pose is aligned, stopped and predicts contact. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	bool PrepareAttack(USkeletalMeshComponent* TargetMesh, UAnimSequenceBase* Sequence,
		float StartTime, float EndTime, int32 NumSamples, const TArray<FName>& AllowedBones, float AcceptanceRadius);
	/** Feed this into Mover's orientation input; never SetActorRotation for combat. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Combat")
	FVector ResolveOrientationIntent(FVector LocomotionIntent) const;
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	void FinishAttack();
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	void CancelAttack();
	UFUNCTION(BlueprintPure, Category="Ironbound|Combat")
	bool CanPlayAttack() const { return Phase == EIronboundAttackPhase::Committed; }
	/** Authoritative busy state, including recovery; legacy animation flags must not gate new requests. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Combat")
	bool IsCommitted() const { return Phase == EIronboundAttackPhase::Committed || Phase == EIronboundAttackPhase::Recovery; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TWeakObjectPtr<AActor> PlannedTarget;
	FBladeTrajectory CommittedTrajectory;
	FTransform CommittedTransform;
	FVector DesiredFacing = FVector::ForwardVector;
	FVector PreviousTip = FVector::ZeroVector;
	FVector LastLocation = FVector::ZeroVector;
	float MeasuredSpeed = 0.f;
	float RecoveryUntil = 0.f;
	float CommitDeadline = 0.f;
	float LastRequestTime = 0.f;
	bool bHasPreviousTip = false;
};
