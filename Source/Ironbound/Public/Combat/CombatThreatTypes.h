#pragma once

#include "CoreMinimal.h"
#include "Combat/CombatTrajectoryLibrary.h"
#include "CombatThreatTypes.generated.h"

/**
 * One objective incoming-attack observation.
 *
 * A threat is "an incoming attack that may affect this fighter" and is a
 * different concept from CombatFocus ("the target this fighter is currently
 * focused on"). A threat may come from any attacker; the model deliberately
 * supports multiple simultaneous incoming attacks from multiple attackers.
 *
 * Threat data is measurement only. It never decides a response and never
 * requests a technique.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatThreat
{
	GENERATED_BODY()

	/** Attacker whose committed execution produced this threat. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	TObjectPtr<AActor> Attacker = nullptr;

	/** Attacker weapon actor when known. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	TObjectPtr<AActor> Weapon = nullptr;

	/** Technique id of the committed attack. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	FName TechniqueId;

	/** Committed blade trajectory in attacker root-local space (snapshot). */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	FBladeTrajectory SourceTrajectory;

	/** Attacker root transform captured when the attack was committed. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	FTransform AttackerTransform = FTransform::Identity;

	/** Attacker playback time mapped into source-animation time at measurement. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	float CurrentSourceTime = 0.f;

	/** Predicted contact on the defender body, world space. Valid only when bHasPredictedContact. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	FVector ContactPoint = FVector::ZeroVector;

	/** Incoming blade direction at the predicted contact, world space. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	FVector Direction = FVector::ZeroVector;

	/** Seconds until the predicted contact. Negative or zero when already inside the body envelope. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	float TimeToImpact = -1.f;

	/** True when a body-envelope contact is predicted ahead of the defender. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	bool bHasPredictedContact = false;

	/** Source-animation time of the first body-envelope intersection along the trajectory. -1 when none. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	float FirstBodyIntersectionTime = -1.f;
};

/**
 * Threat payload attached to a technique request.
 *
 * The controller fills this from UCombatThreatComponent::BuildThreatContext
 * (or leaves it empty for deliberate techniques). RequestTechnique revalidates
 * against live combat state regardless of what the context says.
 */
USTRUCT(BlueprintType)
struct IRONBOUND_API FCombatThreatContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	bool bHasThreat = false;

	UPROPERTY(BlueprintReadOnly, Category="Combat|Threat")
	FCombatThreat Threat;
};
