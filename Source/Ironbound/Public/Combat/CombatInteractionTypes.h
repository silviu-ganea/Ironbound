#pragma once

#include "CoreMinimal.h"
#include "CombatInteractionTypes.generated.h"

class UPrimitiveComponent;

/**
 * Typed combat interaction between two combat participants.
 *
 * This preserves the physical information the future physical damage model
 * will need (contact point, normal, impulse, weapon velocity/mass, body
 * region, source technique, participants). It is deliberately NOT resolved by
 * a world-level combat interaction manager: routing stays with the contact
 * site (attacker->victim for weapon/body, direct cross-actor notify for
 * weapon/weapon) and resolution goes through UCombatInteractionLibrary.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatInteraction
{
	GENERATED_BODY()

	/** Actor that produced the interaction (attacker). */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	TObjectPtr<AActor> Source = nullptr;

	/** Actor that received the interaction (victim). */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	TObjectPtr<AActor> Receiver = nullptr;

	/** Source technique id. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	FName SourceTechniqueId;

	/** Receiving technique id when the receiver was executing one (parry contact, ...). */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	FName ReceiverTechniqueId;

	/** Source component: weapon component or body mesh. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	TObjectPtr<UPrimitiveComponent> SourceComponent = nullptr;

	/** Receiver component: weapon component or body mesh. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	TObjectPtr<UPrimitiveComponent> ReceiverComponent = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	FVector ContactPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	FVector ContactNormal = FVector::ZeroVector;

	/** Measured or requested physical impulse carried by the contact. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	FVector Impulse = FVector::ZeroVector;

	/** Weapon tip speed at contact, cm/s (0 when not a weapon contact). */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	float WeaponTipSpeed = 0.f;

	/** Weapon mass at contact, kg (0 when not a weapon contact). */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	float WeaponMassKg = 0.f;

	/** Resolved row name from DT_CombatTargets for the actual contact region. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	FName BodyRegion;

	/** Actual hit bone, or the closest configured target bone for capsule hits. */
	UPROPERTY(BlueprintReadWrite, Category="Combat|Interaction")
	FName BodyBone;
};

/**
 * Typed outcome of resolving an interaction. Today this carries the
 * transitional CompatibilityDamage; tomorrow the physical damage model
 * replaces the damage field without changing the contract shape.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatInteractionResult
{
	GENERATED_BODY()

	/** Damage accepted by the receiver for this interaction (transitional compatibility value). */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Interaction")
	float Damage = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Combat|Interaction")
	FVector Impulse = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Combat|Interaction")
	bool bAccepted = false;
};
