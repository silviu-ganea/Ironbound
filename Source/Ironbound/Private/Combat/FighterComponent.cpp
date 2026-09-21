#include "Combat/FighterComponent.h"

#include "Combat/IronboundCombatFocusComponent.h"

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
	// BattleSkills is deliberately left alone - the battle loadout is chosen separately, and
	// learning a skill does not make it an active one.
	SetBattleTeamId(0);
}

void UFighterComponent::SetBattleTeamId(int32 InTeamId)
{
	BattleTeamId = FMath::Max(0, InTeamId);

	// Compatibility bridge. The existing combat systems still read their side from
	// UIronboundCombatFocusComponent (see COMBAT_ARCHITECTURE.md). Team assignment therefore
	// has exactly one authority - this component - and the legacy reader is kept in step
	// instead of being refactored. Team is written directly so Focus's target selection and
	// alive state are left untouched.
	if (const AActor* Owner = GetOwner())
	{
		if (UIronboundCombatFocusComponent* Focus = Owner->FindComponentByClass<UIronboundCombatFocusComponent>())
		{
			Focus->Team = BattleTeamId;
		}
	}
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
