#include "Combat/CombatTechniqueComponent.h"

#include "Combat/CombatEquipmentComponent.h"
#include "Combat/WeaponDefinition.h"
#include "Combat/CombatTechniqueRow.h"
#include "Combat/CombatSkillRow.h"
#include "Combat/FighterComponent.h"
#include "Combat/FighterVitalsComponent.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "Ironbound.h"

UCombatTechniqueComponent::UCombatTechniqueComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCombatTechniqueComponent::BeginPlay()
{
	Super::BeginPlay();

	RebuildCacheIfNeeded();
}

UCombatEquipmentComponent* UCombatTechniqueComponent::GetEquipment() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UCombatEquipmentComponent>() : nullptr;
}

UFighterComponent* UCombatTechniqueComponent::GetFighter() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UFighterComponent>() : nullptr;
}

UFighterVitalsComponent* UCombatTechniqueComponent::GetVitals() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UFighterVitalsComponent>() : nullptr;
}

const FCombatTechniqueRow* UCombatTechniqueComponent::FindRow(FName TechniqueId) const
{
	if (!TechniquesTable || TechniqueId.IsNone())
	{
		return nullptr;
	}

	const FCombatSkillRow* SkillRow = TechniquesTable->FindRow<FCombatSkillRow>(
		TechniqueId,
		TEXT("CombatTechniqueFindRow"),
		/*bWarnIfRowMissing=*/false);
	return SkillRow;
}

void UCombatTechniqueComponent::RebuildCacheIfNeeded() const
{
	const UCombatEquipmentComponent* Equipment = GetEquipment();
	const int32 EquipmentRevision = Equipment ? Equipment->Revision : -1;

	const UFighterComponent* Fighter = GetFighter();
	const int32 RepertoireRevision = Fighter ? Fighter->GetRepertoireRevision() : -1;

	if (bCacheBuilt &&
		EquipmentRevision == CachedEquipmentRevision &&
		RepertoireRevision == CachedRepertoireRevision)
	{
		return;
	}

	CachedEquipmentRevision = EquipmentRevision;
	CachedRepertoireRevision = RepertoireRevision;
	bCacheBuilt = true;
	AvailableCache.Reset();

	if (!TechniquesTable)
	{
		return;
	}

	const TMap<FName, uint8*>& Rows = TechniquesTable->GetRowMap();

	for (const TPair<FName, uint8*>& Pair : Rows)
	{
		const FCombatSkillRow* SkillRow =
			reinterpret_cast<const FCombatSkillRow*>(Pair.Value);
		const FCombatTechniqueRow* Row = SkillRow;

		if (!Row)
		{
			continue;
		}

		if (IsAvailable(Pair.Key))
		{
			AvailableCache.Add(Pair.Key);
		}
	}
}

bool UCombatTechniqueComponent::IsAvailable(FName TechniqueId) const
{
	const FCombatTechniqueRow* Row = FindRow(TechniqueId);
	if (!Row)
	{
		return false;
	}

	const UFighterVitalsComponent* Vitals = GetVitals();
	if (!Vitals || Vitals->IsDead())
	{
		return false;
	}

	const UFighterComponent* Fighter = GetFighter();
	if (!Fighter)
	{
		return false;
	}

	// Battle repertoire holds technique ids; the technique must be prepared for this battle.
	if (!Fighter->IsBattleTechniqueSelected(TechniqueId))
	{
		return false;
	}

	// Every required skill must actually be learned.
	for (const FName SkillId : Row->RequiredSkills)
	{
		if (!Fighter->HasLearnedSkill(SkillId))
		{
			return false;
		}
	}

	// Filter through authored weapon taxonomy. The action catalog owns
	// compatibility rules; weapon definitions only provide their tags.
	if (Row->WeaponFamilyTag.IsValid() ||
		!Row->CompatibleWeaponFamilies.IsEmpty() ||
		!Row->CompatibleWeaponClasses.IsEmpty() ||
		Row->MinimumWeaponProficiency > 0.f)
	{
		const UCombatEquipmentComponent* Equipment = GetEquipment();
		if (!Equipment ||
			!Equipment->bReady ||
			!Equipment->Definition)
		{
			return false;
		}

		const UWeaponDefinition* Weapon = Equipment->Definition;
		if (Row->WeaponFamilyTag.IsValid() &&
			(!Weapon->FamilyTag.IsValid() || !Weapon->FamilyTag.MatchesTag(Row->WeaponFamilyTag)))
		{
			return false;
		}

		if (!Row->CompatibleWeaponFamilies.IsEmpty())
		{
			bool bFamilyMatch = false;
			for (const FGameplayTag& RequiredFamily : Row->CompatibleWeaponFamilies)
			{
				if (Weapon->FamilyTag.IsValid() && Weapon->FamilyTag.MatchesTag(RequiredFamily))
				{
					bFamilyMatch = true;
					break;
				}
			}
			if (!bFamilyMatch)
			{
				return false;
			}
		}

		if (!Row->CompatibleWeaponClasses.IsEmpty())
		{
			TArray<FGameplayTag> WeaponClasses;
			Weapon->ClassTags.GetGameplayTagArray(WeaponClasses);
			bool bClassMatch = false;
			for (const FGameplayTag& RequiredClass : Row->CompatibleWeaponClasses)
			{
				for (const FGameplayTag& WeaponClass : WeaponClasses)
				{
					if (WeaponClass.MatchesTag(RequiredClass))
					{
						bClassMatch = true;
						break;
					}
				}
				if (bClassMatch)
				{
					break;
				}
			}
			if (!bClassMatch)
			{
				return false;
			}
		}

		if (Row->MinimumWeaponProficiency > 0.f &&
			Fighter->GetEffectiveWeaponProficiency(Weapon) < Row->MinimumWeaponProficiency)
		{
			return false;
		}
	}

	return true;
}

TArray<FName> UCombatTechniqueComponent::GetAvailableTechniques() const
{
	RebuildCacheIfNeeded();
	return AvailableCache;
}

void UCombatTechniqueComponent::RefreshAvailability()
{
	bCacheBuilt = false;
	RebuildCacheIfNeeded();
}

bool UCombatTechniqueComponent::CanExecute(
	FName TechniqueId,
	const FCombatTechniqueRequest& Context) const
{
	const FCombatTechniqueRow* Row = FindRow(TechniqueId);
	if (!Row)
	{
		return false;
	}

	// Deliberate target engagement requires an explicit, valid request target.
	if (Row->bRequiresCombatTarget)
	{
		const AActor* Target = Context.Target;
		if (!IsValid(Target) || Target == GetOwner())
		{
			return false;
		}

		const UFighterComponent* Fighter = GetFighter();
		if (!Fighter || !Fighter->IsEnemy(Target))
		{
			return false;
		}
	}

	// Reactive techniques answer an observed threat; the request must carry one.
	if (Row->Kind == ECombatTechniqueKind::Reactive)
	{
		if (!Context.ThreatContext.bHasThreat)
		{
			return false;
		}

		const AActor* Attacker = Context.ThreatContext.Threat.Attacker;
		if (!IsValid(Attacker))
		{
			return false;
		}
	}

	return true;
}

float UCombatTechniqueComponent::GetSkillProficiency(
	FName SkillId,
	float FallbackProficiency) const
{
	const UFighterComponent* Fighter = GetFighter();
	FLearnedSkill Skill;
	return Fighter && Fighter->FindLearnedSkill(SkillId, Skill)
		? FMath::Clamp(Skill.Proficiency, 0.f, 1.f)
		: FallbackProficiency;
}
