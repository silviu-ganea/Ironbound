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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Tracking") float WeaponTrackingStrength = 60.f;
	/** Accommodate authored poses within living joint limits; restore the asset's limits on death. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Tracking") bool bFitJointLimitsToAnimation = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Reaction") float RecoveryDuration = 0.65f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Reaction") float MaxReactionSpeed = 180.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Reaction") float ReactionWeight = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Reaction") int32 ReactionCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Tracking") float WeaponTipTrackingError = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Tracking") float HandTrackingError = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Tracking") float HandAngularTrackingError = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Tracking") float GripTipError = 0.f;
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat") bool InitializeBody(USkeletalMeshComponent* Mesh, UPhysicsControlComponent* Controls);
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat") void SetWeaponTrackingStrength(float Strength);
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat") void ApplyHitReaction(UPrimitiveComponent* Weapon, const FHitResult& Hit);
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat") void ReleaseForDeath();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	void MeasureTracking();
private:
	FIronboundBodyPostPhysicsTick PostPhysicsTick;
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> FighterMesh;
	UPROPERTY(Transient) TObjectPtr<UPhysicsControlComponent> PhysicsControls;
	TArray<FName> UpperBodyBones;
	TArray<FName> WeaponArmBones;
	float ReactionTimeRemaining = 0.f;
	bool bReleased = false;
	void UpdateDrives();
	void UpdateJointLimits(bool bRestore);
};
