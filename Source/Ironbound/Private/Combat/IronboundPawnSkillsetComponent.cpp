#include "Combat/IronboundPawnSkillsetComponent.h"

UIronboundPawnSkillsetComponent::UIronboundPawnSkillsetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UIronboundPawnSkillsetComponent::HasAttacks() const
{
	for (const FDataTableRowHandle& Attack : AttackMoves)
	{
		if (Attack.DataTable && !Attack.RowName.IsNone())
		{
			return true;
		}
	}

	return false;
}

int32 UIronboundPawnSkillsetComponent::GetAttackCount() const
{
	return AttackMoves.Num();
}

bool UIronboundPawnSkillsetComponent::GetAttack(
	int32 Index,
	FDataTableRowHandle& OutAttack) const
{
	OutAttack = FDataTableRowHandle();

	UE_LOG(LogTemp, Warning,
		TEXT("SkillSet %s owner=%s AttackMoves.Num=%d requested Index=%d"),
		*GetName(),
		*GetNameSafe(GetOwner()),
		AttackMoves.Num(),
		Index);

	if (!AttackMoves.IsValidIndex(Index))
	{
		UE_LOG(LogTemp, Warning, TEXT("GetAttack FAILED: invalid index"));
		return false;
	}

	const FDataTableRowHandle& Attack = AttackMoves[Index];

	UE_LOG(LogTemp, Warning,
		TEXT("Attack[%d]: Table=%s Row=%s"),
		Index,
		*GetNameSafe(Attack.DataTable),
		*Attack.RowName.ToString());

	if (!Attack.DataTable || Attack.RowName.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("GetAttack FAILED: invalid row handle"));
		return false;
	}

	OutAttack = Attack;
	return true;
}