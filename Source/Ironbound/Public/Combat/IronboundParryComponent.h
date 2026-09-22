#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IronboundParryComponent.generated.h"

/**
 * RETIRED: the responsibilities of this component were split by ownership.
 *
 * - Incoming-attack observation moved to UCombatThreatComponent (objective
 *   measurement, attacker-independent, never decides a response).
 * - Parry solving/execution moved to UCombatExecutor_ProceduralParry, created
 *   only after Sword_Parry is requested and admitted through
 *   UCombatExecutionComponent::RequestTechnique.
 *
 * The class remains only as an empty deprecated placeholder so assets that
 * still hold an instance (pawn SCS component, level instances) keep loading
 * before the manual Blueprint cleanup. It has no behavior. After the pawn BP
 * drops this component, delete this class entirely.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent, DeprecatedProperty))
class IRONBOUND_API UIronboundParryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIronboundParryComponent()
	{
		PrimaryComponentTick.bCanEverTick = false;
	}
};
