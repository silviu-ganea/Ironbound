#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WeaponCatalogRow.generated.h"

class UWeaponDefinition;

/** Catalog entry connecting a stable weapon id to its detailed authored definition. */
USTRUCT(BlueprintType)
struct IRONBOUND_API FWeaponCatalogRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TObjectPtr<UWeaponDefinition> Definition;
};
