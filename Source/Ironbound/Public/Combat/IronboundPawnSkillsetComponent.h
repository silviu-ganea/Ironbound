#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "IronboundPawnSkillsetComponent.generated.h"

/**
 * Per-fighter combat capabilities.
 *
 * AttackMoves contains references into the global attack master table.
 * The actual move data remains owned by DT_AttackMasterMoves.
 */
UCLASS(ClassGroup=(Ironbound), meta=(BlueprintSpawnableComponent))
class IRONBOUND_API UIronboundPawnSkillsetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIronboundPawnSkillsetComponent();

	/** Attacks this fighter currently knows/can use. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Loadout")
	TArray<FDataTableRowHandle> AttackMoves;

	/** Whether this fighter is currently allowed to attempt parries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat|Loadout")
	bool bCanParry = false;

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Loadout")
	bool HasAttacks() const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Combat|Loadout")
	int32 GetAttackCount() const;

	/**
	 * Gets one attack reference by index.
	 * Returns false if the index is invalid or the row handle is incomplete.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Combat|Loadout")
	bool GetAttack(int32 Index, FDataTableRowHandle& OutAttack) const;
};