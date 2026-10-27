#pragma once

#include <map>

#include "Core.h"

namespace mc
{
	// Builds a fully costed acquisition plan for a recipe graph.
	// aPreferences maps item ids to a forced source choice (Auto/Craft/Buy).
	Plan BuildPlan(
		const RecipeNode& aRoot,
		int aGoalQuantity,
		const ItemDb& aItems,
		const PriceDb& aPrices,
		const CurrencyDb& aCurrencies,
		const AccountData& aAccount,
		const PlanOptions& aOptions,
		const std::map<int, Preference>& aPreferences);

	// Linearizes a plan into dependency-ordered actions: ingredients first, the
	// goal last. Already-owned nodes and coin costs are omitted. A combined plan
	// lists the steps of every tracked goal it was built from.
	std::vector<CraftStep> BuildCraftingSteps(const Plan& aPlan);

	// Folds those steps into visits: everything to do at one merchant, crafting
	// station or Mystic Forge becomes a single block, so each place is worked
	// through once. A place that has to be visited again because a later craft
	// feeds it gets a second block, and only when a dependency forces it.
	std::vector<CraftStepGroup> BuildStepGroups(const Plan& aPlan);

	// Builds one plan per goal against a shared ownership pool, then merges the
	// shopping lists and currency requirements into a single plan. Owned items
	// are consumed in goal order so shared materials are never counted twice.
	Plan BuildCombinedPlan(
		const std::vector<RecipeNode>& aRoots,
		const ItemDb& aItems,
		const PriceDb& aPrices,
		const CurrencyDb& aCurrencies,
		const AccountData& aAccount,
		const PlanOptions& aOptions,
		const std::map<int, Preference>& aPreferences);
}
