#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CombatRigDefinition.generated.h"

/** Maps a semantic arm side to one fighter skeleton's anatomical bone names. */
USTRUCT(BlueprintType)
struct IRONBOUND_API FFighterArmRigProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rig|Arm")
	FName UpperArmBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rig|Arm")
	FName LowerArmBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rig|Arm")
	FName HandBone = NAME_None;
};

/** Skeleton-specific semantic anatomy; shared by all weapons used by this fighter rig. */
UCLASS(BlueprintType)
class IRONBOUND_API UCombatRigDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rig|Arms")
	FFighterArmRigProfile RightArm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rig|Arms")
	FFighterArmRigProfile LeftArm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rig|Body")
	FName PelvisBone = TEXT("pelvis");
};
