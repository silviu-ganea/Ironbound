#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IronboundCombatFocusComponent.generated.h"

/** Persistent combat intent. Taking a hit never changes this fighter's selected opponent. */
UCLASS(ClassGroup=(Ironbound), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UIronboundCombatFocusComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UIronboundCombatFocusComponent();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") int32 Team = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") bool bDead = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat") TObjectPtr<AActor> CombatTarget;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") bool bAutoAcquireTarget = true;
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat") void InitializeFocus(int32 FighterTeam);
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat") bool SetCombatTarget(AActor* Target);
	UFUNCTION(BlueprintPure, Category="Ironbound|Combat") AActor* GetCombatTarget() const;
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat") void MarkDead();
	bool IsEnemy(const AActor* Candidate) const;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
};
