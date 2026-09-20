#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IronboundCombatBodyComponent.generated.h"

class UPhysicsControlComponent;
class USkeletalMeshComponent;
class UPrimitiveComponent;
class UIronboundCombatBodyComponent;

USTRUCT()
struct FIronboundBodyPostPhysicsTick : public FTickFunction
{
	GENERATED_BODY()
	UIronboundCombatBodyComponent* Target = nullptr;
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& CompletionEvent) override;
	virtual FString DiagnosticMessage() override { return TEXT("Ironbound body tracking diagnostics"); }
};

template<> struct TStructOpsTypeTraits<FIronboundBodyPostPhysicsTick> : TStructOpsTypeTraitsBase2<FIronboundBodyPostPhysicsTick>
{
	enum { WithCopy = false };
};

/** Owns living physical animation and victim reactions. Legs stay animated until death. */
UCLASS(ClassGroup=(Ironbound), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UIronboundCombatBodyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIronboundCombatBodyComponent();

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
	FIronboundBodyPostPhysicsTick PostPhysicsTick;

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
};
