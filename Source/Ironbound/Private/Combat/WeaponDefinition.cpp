#include "Combat/WeaponDefinition.h"

const FWeaponGrip* UWeaponDefinition::FindGrip(FName GripId) const
{
	return Grips.FindByPredicate(
		[GripId](const FWeaponGrip& Grip) { return Grip.GripId == GripId; });
}

const FWeaponSurface* UWeaponDefinition::FindSurfaceByRole(const FGameplayTag& Role) const
{
	if (!Role.IsValid())
	{
		return nullptr;
	}

	return Surfaces.FindByPredicate(
		[&Role](const FWeaponSurface& Surface) { return Surface.Role.MatchesTag(Role); });
}
