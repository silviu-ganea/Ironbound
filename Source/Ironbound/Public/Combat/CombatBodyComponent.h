#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CombatBodyComponent.generated.h"

class UPhysicsControlComponent;
class USkeletalMeshComponent;
class UPrimitiveComponent;
class UCombatBodyComponent;

USTRUCT()
struct FCombatBodyPostPhysicsTick : public FTickFunction
{
	GENERATED_BODY()
	UCombatBodyComponent* Target = nullptr;
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent) override;
	virtual FString DiagnosticMessage() override { return TEXT("Ironbound body tracking diagnostics"); }
};

template<> struct TStructOpsTypeTraits<FCombatBodyPostPhysicsTick> : TStructOpsTypeTraitsBase2<FCombatBodyPostPhysicsTick>
{
	enum { WithCopy = false };
};

/** Owns living physical animation and victim reactions. Legs stay animated until death. */
UCLASS(ClassGroup=(Ironbound), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UCombatBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatBodyComponent();
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Tracking")
	float WeaponTrackingStrength = 60.f;

	/** Strong temporary weapon-arm brace used while executing a parry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Parry Brace")
	float ParryWorldLinearStrength = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Parry Brace")
	float ParryWorldAngularStrength = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Parry Brace")
	float ParryParentAngularStrength = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Parry Brace", meta=(ClampMin="0.0"))
	float ParryVelocityMultiplier = 0.4f;

	/**
	 * Angular spring applied to the simulated sword at the hand during a parry.
	 * The sword remains fully simulated; this is grip stiffness, not kinematic locking.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Parry Brace|Grip", meta=(ClampMin="0.0"))
	float ParryGripAngularStiffness = 150000.f;

	/** Damping for the parry grip angular spring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Parry Brace|Grip", meta=(ClampMin="0.0"))
	float ParryGripAngularDamping = 775.f;

	/**
	 * Maximum grip correction torque.
	 * Zero means unlimited in the Chaos constraint drive, so use a finite value
	 * if you want sufficiently hard impacts to overpower the grip.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Parry Brace|Grip", meta=(ClampMin="0.0"))
	float ParryGripMaxTorque = 500000.f;

	/** Accommodate authored poses within living joint limits; restore the asset's limits on death. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Tracking")
	bool bFitJointLimitsToAnimation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Reaction")
	float RecoveryDuration = 1.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Reaction")
	float MaxReactionSpeed = 180.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Reaction")
	float ReactionWeight = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Reaction")
	int32 ReactionCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Tracking")
	float WeaponTipTrackingError = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Tracking")
	float HandTrackingError = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Tracking")
	float HandAngularTrackingError = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Tracking")
	float GripTipError = 0.f;

	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	bool InitializeBody(USkeletalMeshComponent* Mesh, UPhysicsControlComponent* Controls);

	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	bool BindExistingBody(USkeletalMeshComponent* Mesh, UPhysicsControlComponent* Controls);

	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	void SetWeaponTrackingStrength(float Strength);

	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Parry")
	void BeginParryBrace();

	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Parry")
	void EndParryBrace();

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Parry")
	bool IsParryBraced() const { return bParryBraceActive; }

	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	void ApplyHitReaction(UPrimitiveComponent* Weapon, const FHitResult& Hit);

	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat")
	void ReleaseForDeath();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;

	void MeasureTracking();

private:
	FCombatBodyPostPhysicsTick PostPhysicsTick;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> FighterMesh;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicsControlComponent> PhysicsControls;

	TArray<FName> UpperBodyBones;
	TArray<FName> WeaponArmBones;

	// Exact names returned by CreateControlsFromSkeletalMesh.
	TArray<FName> WeaponWorldControls;
	TArray<FName> WeaponParentControls;
	TArray<FName> ReactionWorldControls;
	TArray<FName> ReactionParentControls;

	float ReactionTimeRemaining = 0.f;
	bool bReleased = false;
	bool bParryBraceActive = false;

	void UpdateWeaponDrives();
	void UpdateDrives();
	void UpdateJointLimits(bool bRestore);
	void SetParryGripDrive(bool bEnabled);
};
