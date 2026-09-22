#include "Combat/FighterComponent.h"

#include "Combat/CombatTechniqueRow.h"
#include "Engine/DataTable.h"

UFighterComponent::UFighterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFighterComponent::InitializeFighter(const FFighter& InFighterData)
{
	FighterData = InFighterData;

	// Battle state starts from the persistent attributes. The persistent copy is never written
	// back, so nothing that happens in this battle changes what the fighter owns afterwards.
	BattleAttributes = FighterData.Attributes;

	// A battle starts unteamed: registration is what puts the fighter on a side.
	// BattleRepertoire is deliberately left alone - the battle loadout is chosen separately,
	// and learning a skill does not automatically prepare any technique.
	SetBattleTeamId(0);
}

void UFighterComponent::SetBattleTeamId(int32 InTeamId)
{
	// Single team authority: readers (focus, vitals, body, battle manager) all
	// come through this component; no duplicated team state is maintained.
	BattleTeamId = FMath::Max(0, InTeamId);
}

void UFighterComponent::SetBattleAttributes(const FFighterAttributes& InAttributes)
{
	BattleAttributes = InAttributes;
}

bool UFighterComponent::LearnSkill(const FLearnedSkill& Skill)
{
	if (Skill.SkillId.IsNone())
	{
		return false;
	}

	if (FLearnedSkill* Existing = FighterData.LearnedSkills.FindByPredicate(
		[&Skill](const FLearnedSkill& Candidate) { return Candidate.SkillId == Skill.SkillId; }))
	{
		*Existing = Skill;
		return true;
	}

	FighterData.LearnedSkills.Add(Skill);
	return true;
}

bool UFighterComponent::HasLearnedSkill(FName SkillId) const
{
	if (SkillId.IsNone())
	{
		return false;
	}

	return FighterData.LearnedSkills.ContainsByPredicate(
		[SkillId](const FLearnedSkill& Candidate) { return Candidate.SkillId == SkillId; });
}

bool UFighterComponent::FindLearnedSkill(FName SkillId, FLearnedSkill& OutSkill) const
{
	OutSkill = FLearnedSkill();

	if (const FLearnedSkill* Found = FighterData.LearnedSkills.FindByPredicate(
		[SkillId](const FLearnedSkill& Candidate) { return Candidate.SkillId == SkillId; }))
	{
		OutSkill = *Found;
		return true;
	}

	return false;
}

bool UFighterComponent::SetWeaponProficiency(const FFighterWeaponProficiency& Proficiency)
{
	if (!Proficiency.FamilyTag.IsValid())
	{
		return false;
	}

	if (FFighterWeaponProficiency* Existing = FighterData.WeaponProficiencies.FindByPredicate(
		[&Proficiency](const FFighterWeaponProficiency& Candidate)
		{ return Candidate.FamilyTag == Proficiency.FamilyTag; }))
	{
		*Existing = Proficiency;
		return true;
	}

	FighterData.WeaponProficiencies.Add(Proficiency);
	return true;
}

float UFighterComponent::GetWeaponProficiency(
	const FGameplayTag& FamilyTag,
	float FallbackProficiency) const
{
	if (!FamilyTag.IsValid())
	{
		return FallbackProficiency;
	}

	const FFighterWeaponProficiency* Found = FighterData.WeaponProficiencies.FindByPredicate(
		[&FamilyTag](const FFighterWeaponProficiency& Candidate)
		{ return Candidate.FamilyTag.MatchesTag(FamilyTag); });

	return Found ? FMath::Clamp(Found->Proficiency, 0.f, 1.f) : FallbackProficiency;
}

bool UFighterComponent::SelectBattleTechnique(FName TechniqueId)
{
	if (TechniqueId.IsNone())
	{
		return false;
	}

	// Techniques belong to DT_CombatTechniques; validate their required
	// skills against what this fighter actually learned.
	if (BattleTechniquesTable)
	{
		const FCombatTechniqueRow* Row = BattleTechniquesTable->FindRow<FCombatTechniqueRow>(
			TechniqueId, TEXT("SelectBattleTechnique"), /*bWarnIfRowMissing=*/false);

		if (!Row)
		{
			return false;
		}

		for (const FName SkillId : Row->RequiredSkills)
		{
			if (!HasLearnedSkill(SkillId))
			{
				return false;
			}
		}
	}

	if (!BattleRepertoire.Contains(TechniqueId))
	{
		BattleRepertoire.Add(TechniqueId);
		++RepertoireRevision;
	}

	return true;
}

bool UFighterComponent::RemoveBattleTechnique(FName TechniqueId)
{
	const bool bRemoved = BattleRepertoire.Remove(TechniqueId) > 0;
	if (bRemoved)
	{
		++RepertoireRevision;
	}
	return bRemoved;
}

bool UFighterComponent::IsBattleTechniqueSelected(FName TechniqueId) const
{
	return BattleRepertoire.Contains(TechniqueId);
}

void UFighterComponent::ClearBattleRepertoire()
{
	if (!BattleRepertoire.IsEmpty())
	{
		BattleRepertoire.Reset();
		++RepertoireRevision;
	}
}

bool UFighterComponent::SelectBattleSkill(FName SkillId)
{
	FLearnedSkill Learned;
	if (!FindLearnedSkill(SkillId, Learned))
	{
		// Only a skill the fighter has actually learned can be taken into a battle.
		return false;
	}

	const bool bAlreadySelected = BattleSkills.ContainsByPredicate(
		[SkillId](const FLearnedSkill& Candidate) { return Candidate.SkillId == SkillId; });

	if (!bAlreadySelected)
	{
		BattleSkills.Add(Learned);
	}

	return true;
}

bool UFighterComponent::RemoveBattleSkill(FName SkillId)
{
	return BattleSkills.RemoveAll(
		[SkillId](const FLearnedSkill& Candidate) { return Candidate.SkillId == SkillId; }) > 0;
}

bool UFighterComponent::IsBattleSkillSelected(FName SkillId) const
{
	return BattleSkills.ContainsByPredicate(
		[SkillId](const FLearnedSkill& Candidate) { return Candidate.SkillId == SkillId; });
}

void UFighterComponent::ClearBattleSkills()
{
	BattleSkills.Reset();
}

bool UFighterComponent::IsAlly(const AActor* Other) const
{
	if (Other == GetOwner())
	{
		return false;
	}

	const int32 OtherTeam = GetFighterTeamId(Other);
	return BattleTeamId > 0 && OtherTeam == BattleTeamId;
}

bool UFighterComponent::IsEnemy(const AActor* Other) const
{
	const int32 OtherTeam = GetFighterTeamId(Other);

	// Team 0 is the observer/unassigned side and is never an enemy; self resolves to the same
	// team and is therefore excluded as well.
	return BattleTeamId > 0 && OtherTeam > 0 && OtherTeam != BattleTeamId;
}

int32 UFighterComponent::GetFighterTeamId(const AActor* FighterActor)
{
	if (!IsValid(FighterActor))
	{
		return 0;
	}

	const UFighterComponent* Fighter = FighterActor->FindComponentByClass<UFighterComponent>();
	return Fighter ? Fighter->BattleTeamId : 0;
}
