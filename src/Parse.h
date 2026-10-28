#pragma once

#include <string>
#include <vector>

#include "Core.h"

namespace mc
{
	bool ParseRecipeTree(const std::string& aJson, RecipeNode& aOut, std::string& aError);
	std::vector<ItemInfo> ParseItems(const std::string& aJson);
	std::vector<PriceInfo> ParsePrices(const std::string& aJson);
	std::vector<CurrencyInfo> ParseCurrencies(const std::string& aJson);
	std::vector<AchievementInfo> ParseAchievements(const std::string& aJson);
	std::vector<int> ParseIntArray(const std::string& aJson);
	std::vector<std::string> ParseStringArray(const std::string& aJson);
}
