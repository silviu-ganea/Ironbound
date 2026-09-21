#include "Combat/BattleManager.h"

#include "Combat/FighterComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

ABattleManager::ABattleManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ABattleManager::PruneInvalidFighters(FBattleTeam& Team)
{
	Team.Fighters.RemoveAll(
		[](const TObjectPtr<AActor>& Fighter) { return !IsValid(Fighter.Get()); });
}

FBattleTeam* ABattleManager::FindTeam(int32 TeamId)
{
	return Teams.FindByPredicate(
		[TeamId](const FBattleTeam& Team) { return Team.TeamId == TeamId; });
}

const FBattleTeam* ABattleManager::FindTeam(int32 TeamId) const
{
	return Teams.FindByPredicate(
		[TeamId](const FBattleTeam& Team) { return Team.TeamId == TeamId; });
}

bool ABattleManager::EnsureTeam(int32 TeamId)
{
	if (TeamId <= 0 || FindTeam(TeamId))
	{
		return false;
	}

	FBattleTeam& Created = Teams.AddDefaulted_GetRef();
	Created.TeamId = TeamId;
	return true;
}

bool ABattleManager::RegisterFighter(AActor* FighterActor, int32 TeamId)
{
	if (!IsValid(FighterActor) || TeamId <= 0)
	{
		return false;
	}

	UFighterComponent* Fighter = FighterActor->FindComponentByClass<UFighterComponent>();
	if (!Fighter)
	{
		return false;
	}

	// Re-registration moves the fighter rather than leaving a stale membership behind, so a
	// fighter can only ever be on one side.
	for (FBattleTeam& Existing : Teams)
	{
		PruneInvalidFighters(Existing);
		Existing.Fighters.Remove(FighterActor);
	}

	Fighter->SetBattleTeamId(TeamId);

	FBattleTeam* Team = FindTeam(TeamId);
	if (!Team)
	{
		FBattleTeam& Created = Teams.AddDefaulted_GetRef();
		Created.TeamId = TeamId;
		Team = &Created;
	}

	Team->Fighters.AddUnique(FighterActor);
	return true;
}

bool ABattleManager::UnregisterFighter(AActor* FighterActor)
{
	if (!IsValid(FighterActor))
	{
		return false;
	}

	bool bWasRegistered = false;
	for (FBattleTeam& Team : Teams)
	{
		PruneInvalidFighters(Team);
		bWasRegistered |= Team.Fighters.Remove(FighterActor) > 0;
	}

	if (UFighterComponent* Fighter = FighterActor->FindComponentByClass<UFighterComponent>())
	{
		Fighter->SetBattleTeamId(0);
	}

	return bWasRegistered;
}

bool ABattleManager::HasTeam(int32 TeamId) const
{
	return FindTeam(TeamId) != nullptr;
}

bool ABattleManager::GetTeam(int32 TeamId, FBattleTeam& OutTeam) const
{
	if (const FBattleTeam* Team = FindTeam(TeamId))
	{
		OutTeam = *Team;
		return true;
	}

	OutTeam = FBattleTeam();
	return false;
}

TArray<AActor*> ABattleManager::GetTeamFighters(int32 TeamId) const
{
	TArray<AActor*> Result;

	const FBattleTeam* Team = FindTeam(TeamId);
	if (!Team)
	{
		return Result;
	}

	Result.Reserve(Team->Fighters.Num());
	for (const TObjectPtr<AActor>& Fighter : Team->Fighters)
	{
		AActor* Actor = Fighter.Get();
		if (IsValid(Actor))
		{
			Result.Add(Actor);
		}
	}

	return Result;
}

int32 ABattleManager::GetTeamFighterCount(int32 TeamId) const
{
	const FBattleTeam* Team = FindTeam(TeamId);
	if (!Team)
	{
		return 0;
	}

	int32 Count = 0;
	for (const TObjectPtr<AActor>& Fighter : Team->Fighters)
	{
		if (IsValid(Fighter.Get()))
		{
			++Count;
		}
	}

	return Count;
}

TArray<AActor*> ABattleManager::GetAllFighters() const
{
	TArray<AActor*> Result;

	for (const FBattleTeam& Team : Teams)
	{
		for (const TObjectPtr<AActor>& Fighter : Team.Fighters)
		{
			AActor* Actor = Fighter.Get();
			if (IsValid(Actor))
			{
				Result.AddUnique(Actor);
			}
		}
	}

	return Result;
}

int32 ABattleManager::GetRegisteredTeamId(const AActor* FighterActor) const
{
	if (!IsValid(FighterActor))
	{
		return 0;
	}

	for (const FBattleTeam& Team : Teams)
	{
		for (const TObjectPtr<AActor>& Fighter : Team.Fighters)
		{
			if (Fighter.Get() == FighterActor)
			{
				return Team.TeamId;
			}
		}
	}

	return 0;
}

bool ABattleManager::IsRegistered(const AActor* FighterActor) const
{
	return GetRegisteredTeamId(FighterActor) > 0;
}

bool ABattleManager::AreAllies(const AActor* FighterA, const AActor* FighterB) const
{
	const int32 TeamA = GetRegisteredTeamId(FighterA);
	const int32 TeamB = GetRegisteredTeamId(FighterB);

	return TeamA > 0 && TeamA == TeamB;
}

bool ABattleManager::AreEnemies(const AActor* FighterA, const AActor* FighterB) const
{
	const int32 TeamA = GetRegisteredTeamId(FighterA);
	const int32 TeamB = GetRegisteredTeamId(FighterB);

	return TeamA > 0 && TeamB > 0 && TeamA != TeamB;
}

ABattleManager* ABattleManager::FindBattleManager(const UObject* WorldContextObject)
{
	if (!WorldContextObject || !GEngine)
	{
		return nullptr;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ABattleManager> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}

	return nullptr;
}
