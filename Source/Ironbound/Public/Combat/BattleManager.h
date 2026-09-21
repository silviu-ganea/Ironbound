#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/FighterTypes.h"
#include "BattleManager.generated.h"

class UFighterComponent;

/**
 * Authoritative registry of who is fighting for which side in the current battle.
 *
 * The manager stores spawned fighter actors only. Identity, attributes and skills stay on
 * UFighterComponent, so there is a single source of truth for both. Registration is the
 * only place a runtime BattleTeamId is assigned, and teams are maintained in C++ rather
 * than by rebuilding arrays in Blueprint.
 *
 * Team 0 is reserved for unteamed observers: it is never a valid side for registration and
 * never counts as an ally or an enemy.
 */
UCLASS(ClassGroup=(Ironbound), BlueprintType, Blueprintable)
class IRONBOUND_API ABattleManager : public AActor
{
	GENERATED_BODY()

public:
	ABattleManager();

	/** Every team known to this battle. Teams are created on demand by RegisterFighter. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Battle")
	TArray<FBattleTeam> Teams;

	/**
	 * Puts the actor's fighter component on TeamId.
	 *
	 * Validates the actor and its fighter component, moves the actor out of any previous team,
	 * assigns the component's runtime BattleTeamId, creates TeamId when it does not exist and
	 * adds the actor to it uniquely. Returns false for a null/unteamed actor, a TeamId <= 0, or
	 * an actor with no fighter component.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Battle")
	bool RegisterFighter(AActor* FighterActor, int32 TeamId);

	/**
	 * Removes the actor from every team and clears its runtime BattleTeamId.
	 * Returns true when the actor was actually registered on a team.
	 */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Battle")
	bool UnregisterFighter(AActor* FighterActor);

	/** Creates TeamId if it does not exist. Returns true when the team was created. */
	UFUNCTION(BlueprintCallable, Category="Ironbound|Battle")
	bool EnsureTeam(int32 TeamId);

	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	bool HasTeam(int32 TeamId) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	int32 GetTeamCount() const { return Teams.Num(); }

	/** Copies TeamId's record out. Returns false when the team does not exist. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	bool GetTeam(int32 TeamId, FBattleTeam& OutTeam) const;

	/** Live fighters on TeamId, skipping any that have been destroyed. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	TArray<AActor*> GetTeamFighters(int32 TeamId) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	int32 GetTeamFighterCount(int32 TeamId) const;

	/** Every live fighter in the battle, across all teams, without duplicates. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	TArray<AActor*> GetAllFighters() const;

	/** Team the actor is registered on, or 0 when it is not registered. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	int32 GetRegisteredTeamId(const AActor* FighterActor) const;

	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	bool IsRegistered(const AActor* FighterActor) const;

	/** Both actors registered on the same valid team. Team 0 is never an ally. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	bool AreAllies(const AActor* FighterA, const AActor* FighterB) const;

	/** Both actors registered on different valid teams. Team 0 is never an enemy. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Battle")
	bool AreEnemies(const AActor* FighterA, const AActor* FighterB) const;

	/** Finds this battle's manager for an object that only has a world context. */
	UFUNCTION(BlueprintPure, Category="Ironbound|Battle", meta=(WorldContext="WorldContextObject"))
	static ABattleManager* FindBattleManager(const UObject* WorldContextObject);

private:
	FBattleTeam* FindTeam(int32 TeamId);
	const FBattleTeam* FindTeam(int32 TeamId) const;

	/** Drops destroyed actors so a team never keeps a stale membership. */
	static void PruneInvalidFighters(FBattleTeam& Team);
};
