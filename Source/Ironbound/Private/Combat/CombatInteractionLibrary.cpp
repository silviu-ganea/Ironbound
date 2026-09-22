#include "Combat/CombatInteractionLibrary.h"

#include "Combat/CombatTechniqueRow.h"
#include "Combat/CombatSkillRow.h"
#include "Engine/DataTable.h"

FCombatInteractionResult UCombatInteractionLibrary::Resolve(
	const FCombatInteraction& Interaction,
	const UDataTable* TechniquesTable)
{
	FCombatInteractionResult Result;

	const FCombatTechniqueRow* Row = nullptr;
	if (TechniquesTable && !Interaction.SourceTechniqueId.IsNone())
	{
		Row = TechniquesTable->FindRow<FCombatSkillRow>(
			Interaction.SourceTechniqueId, TEXT("CombatInteractionResolve"), false);
	}

	const float Damage =
		Row
			? Row->DamageProfile.CompatibilityDamage
			: DefaultCompatibilityDamage;

	const float ImpulseScale =
		Row
			? Row->DamageProfile.ImpulseScale
			: 1.f;

	Result.Damage = Damage;
	Result.Impulse = Interaction.Impulse * ImpulseScale;
	Result.bAccepted = Damage > 0.f;

	return Result;
}
