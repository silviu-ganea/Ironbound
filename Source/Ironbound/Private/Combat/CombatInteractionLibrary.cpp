#include "Combat/CombatInteractionLibrary.h"

#include "Combat/CombatTechniqueRow.h"
#include "Combat/CombatSkillRow.h"
#include "Combat/CombatTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"

FCombatInteractionResult UCombatInteractionLibrary::Resolve(
	const FCombatInteraction& Interaction,
	const UDataTable* TechniquesTable)
{
	return ResolveWithTargets(Interaction, TechniquesTable, nullptr);
}

FCombatInteractionResult UCombatInteractionLibrary::ResolveWithTargets(
	const FCombatInteraction& Interaction,
	const UDataTable* TechniquesTable,
	const UDataTable* CombatTargets)
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

	float DamageMultiplier = 1.f;
	if (CombatTargets && !Interaction.BodyRegion.IsNone())
	{
		if (const FCombatTargetRow* TargetRow = CombatTargets->FindRow<FCombatTargetRow>(
			Interaction.BodyRegion, TEXT("CombatInteractionDamageMultiplier"), false))
		{
			DamageMultiplier = CombatTargetRules::GetDamageMultiplier(
				Interaction.BodyRegion, *TargetRow);
		}
	}

	Result.Damage = Damage * DamageMultiplier;
	Result.Impulse = Interaction.Impulse * ImpulseScale;
	Result.bAccepted = Result.Damage > 0.f;

	return Result;
}

FName UCombatInteractionLibrary::ResolveBodyRegion(
	const UDataTable* CombatTargets,
	USkeletalMeshComponent* ReceiverMesh,
	FName HitBone,
	const FVector& ContactPoint,
	FName& OutResolvedBone,
	bool& bOutUsedFallback)
{
	OutResolvedBone = HitBone;
	bOutUsedFallback = false;
	if (!CombatTargets)
	{
		return NAME_None;
	}

	const TMap<FName, uint8*>& Rows = CombatTargets->GetRowMap();
	if (!HitBone.IsNone())
	{
		for (const TPair<FName, uint8*>& Pair : Rows)
		{
			const FCombatTargetRow* Row = reinterpret_cast<const FCombatTargetRow*>(Pair.Value);
			if (Row && Row->Bones.Contains(HitBone))
			{
				return Pair.Key;
			}
		}
	}

	// Pawn-capsule sweeps commonly have no BoneName. Resolve those hits by
	// choosing the closest configured anatomical volume to the actual impact
	// point, accounting for each region's authored/fallback radius.
	if (!ReceiverMesh)
	{
		return NAME_None;
	}

	bOutUsedFallback = true;
	float BestSurfaceDistance = TNumericLimits<float>::Max();
	FName BestRegion = NAME_None;
	FName BestBone = HitBone;
	for (const TPair<FName, uint8*>& Pair : Rows)
	{
		const FCombatTargetRow* Row = reinterpret_cast<const FCombatTargetRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}

		const float Radius = CombatTargetRules::GetContactRadiusCm(Pair.Key, *Row);
		for (const FName CandidateBone : Row->Bones)
		{
			if (!ReceiverMesh->DoesSocketExist(CandidateBone))
			{
				continue;
			}
			const float SurfaceDistance = FVector::Distance(
				ContactPoint, ReceiverMesh->GetSocketLocation(CandidateBone)) - Radius;
			if (SurfaceDistance < BestSurfaceDistance)
			{
				BestSurfaceDistance = SurfaceDistance;
				BestRegion = Pair.Key;
				BestBone = CandidateBone;
			}
		}
	}

	OutResolvedBone = BestBone;
	return BestRegion;
}
