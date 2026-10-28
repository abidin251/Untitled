#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Core.h"

namespace mc
{
	struct UserSettings
	{
		std::string ApiKey;
		bool PreferBuyingTradable = true;
		bool BuyOrderPrices = false;
		bool IncludeAccountItems = true;
		bool ShowCompleted = true;
		bool CompactDensity = false;
		bool IncludeBank = true;
		bool IncludeMaterials = true;
		bool IncludeCharacters = true;
		bool IncludeSharedInventory = true;
		bool IncludeArmory = true;
		bool WindowVisible = false;
		bool CombineTracked = true;
		bool IncludeResearchNotes = false;
		int SelectedItem = 0;
		std::string RecipeBase = "https://api.gw2efficiency.com";
		std::vector<int> Tracked;
		std::map<int, int> Preferences; // item id -> Preference value
	};

	namespace store
	{
		bool EnsureDirectory(const std::filesystem::path& aPath);
		bool ReadTextFile(const std::filesystem::path& aPath, std::string& aOut);
		bool WriteTextFile(const std::filesystem::path& aPath, const std::string& aContents);
		bool FileExists(const std::filesystem::path& aPath);
		i64 FileTimestamp(const std::filesystem::path& aPath);

		bool LoadSettings(const std::filesystem::path& aPath, UserSettings& aOut, std::string& aError);
		bool SaveSettings(const std::filesystem::path& aPath, const UserSettings& aSettings);

		std::string ProtectKey(const std::string& aValue);
		std::string UnprotectKey(const std::string& aValue);

		bool LoadItems(const std::filesystem::path& aPath, ItemDb& aItems, i64& aFetchedAt);
		bool SaveItems(const std::filesystem::path& aPath, const ItemDb& aItems, i64 aFetchedAt);
		bool LoadPrices(const std::filesystem::path& aPath, PriceDb& aPrices, i64& aFetchedAt);
		bool SavePrices(const std::filesystem::path& aPath, const PriceDb& aPrices, i64 aFetchedAt);
		bool LoadCurrencies(const std::filesystem::path& aPath, CurrencyDb& aCurrencies, i64& aFetchedAt);
		bool SaveCurrencies(const std::filesystem::path& aPath, const CurrencyDb& aCurrencies, i64 aFetchedAt);
		bool LoadAchievements(const std::filesystem::path& aPath, AchievementDb& aAchievements, i64& aFetchedAt);
		bool SaveAchievements(const std::filesystem::path& aPath, const AchievementDb& aAchievements, i64 aFetchedAt);
		bool LoadAccount(const std::filesystem::path& aPath, AccountData& aAccount);
		bool SaveAccount(const std::filesystem::path& aPath, const AccountData& aAccount);
		bool LoadCatalog(const std::filesystem::path& aPath, std::vector<int>& aIds, i64& aFetchedAt);
		bool SaveCatalog(const std::filesystem::path& aPath, const std::vector<int>& aIds, i64 aFetchedAt);
		bool LoadPlanCosts(const std::filesystem::path& aPath, std::map<int, i64>& aCosts);
		bool SavePlanCosts(const std::filesystem::path& aPath, const std::map<int, i64>& aCosts);
	}
}
