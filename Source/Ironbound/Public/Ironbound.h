// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Combat diagnostics for the Ironbound duel prototype.
 *
 * Use this for anything the fighter does that the player needs to be able to reason about
 * after the fact: target acquisition, the derived blade envelope, the solved alignment,
 * the predicted contact distance, strike-window edges, and the damage/kill events.
 */
IRONBOUND_API DECLARE_LOG_CATEGORY_EXTERN(LogIronboundCombat, Log, All);
