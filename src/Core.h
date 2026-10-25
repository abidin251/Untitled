// Mystic Crafting for Nexus - shared types and utilities.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mc
{
	using i64 = std::int64_t;

	std::string FormatCoins(i64 aCopper);
	std::string ToLower(std::string aValue);
	std::string Join(const std::vector<std::string>& aValues, const char* aSeparator);
	std::string Trim(const std::string& aValue);
	std::string FormatNumber(i64 aValue);

	struct ItemInfo
	{
		int Id = 0;
		std::string Name;
		std::string Icon;
		std::string Description;
		std::string Rarity;
		std::string Type;       // Item.type (Weapon, Armor, ...)
		std::string Subtype;    // details.type (Greatsword, Coat, Ring, ...)
		std::string WeightClass;
		std::string ChatLink;
		int Level = 0;
		int VendorValue = 0;
		bool Tradable = true;
		std::vector<std::string> Flags;
	};

	struct PriceInfo
	{
		int Id = 0;
		i64 Buy = 0;      // highest buy order (what a buy order costs)
		i64 Sell = 0;     // lowest sell listing (instant buy price)
		int BuyQuantity = 0;
		int SellQuantity = 0;
	};

	struct CurrencyInfo
	{
		int Id = 0;
		std::string Name;
		std::string Icon;
		std::string Description;
	};

	struct AccountData
	{
		bool Valid = false;
		std::string Name;
		std::vector<std::string> Permissions;
		std::map<int, int> Items;            // item id -> amount owned
		std::map<int, i64> Currencies;       // currency id -> amount owned
		std::map<std::string, int> Sources;  // human readable source -> item count
		i64 SyncedAt = 0;
		std::string Error;

		bool HasPermission(const char* aPermission) const;
	};

	using ItemDb = std::map<int, ItemInfo>;
	using PriceDb = std::map<int, PriceInfo>;
	using CurrencyDb = std::map<int, CurrencyInfo>;

	enum class SourceKind
	{
		Unknown = 0,
		Craft,
		MysticForge,
		Vendor,
		Achievement,
		TradingPost,
		Currency,
		Container,
	};		const char* SourceKindLabel(SourceKind aKind);
	std::string ItemTypeLabel(const ItemInfo& aItem); 

	enum class Preference
	{
		Auto = 0,
		Craft = 1,
		Buy = 2,
	};

	// One node of the acquisition graph returned by the recipe data provider.
	struct RecipeNode
	{
		int Id = 0;
		std::string Type;       // "Recipe", "Item" or "Currency"
		double Quantity = 1.0;  // amount required by the parent
		double Output = 1.0;    // average amount produced per craft
		std::vector<RecipeNode> Components;
		std::vector<std::string> Disciplines;
		std::string MerchantName;
		std::string MerchantLocations;
		int AchievementId = 0;
		int MultipleRecipes = 1;
		int DailyCap = 0;
		int WeeklyCap = 0;

		bool IsCurrency() const { return Type == "Currency"; }
		bool IsRecipe() const { return Type == "Recipe" && !Components.empty(); }
	};

	struct PlanOptions
	{
		bool PreferBuyingTradable = true;
		bool BuyOrderPrices = false;
		bool IncludeAccountItems = true;
		bool ShowCompleted = true;
		int MaxDepth = 48;
		int MaxNodes = 25000;
	};

	struct ShoppingItem
	{
		int Id = 0;
		std::string Name;
		std::string Icon;
		int Quantity = 0;
		i64 UnitCost = 0;
		i64 TotalCost = 0;
		SourceKind Source = SourceKind::TradingPost;
		bool Currency = false;
	};

	struct PlanNode
	{
		int Id = 0;
		bool IsCurrency = false;
		std::string Name;
		std::string Icon;
		std::string ChatLink;
		std::string Rarity;
		std::string ItemType;
		int Required = 0;
		int Owned = 0;
		int Remaining = 0;
		int Crafts = 0;
		double Output = 1.0;
		SourceKind Source = SourceKind::Unknown;
		std::string SourceDetail;
		std::string Discipline;
		i64 UnitCost = 0;
		i64 TotalCost = 0;
		bool Complete = false;
		bool Tradable = false;
		bool Purchasable = false;
		int MultipleRecipes = 1;
		int Depth = 0;
		std::vector<PlanNode> Children;

		bool IsLeaf() const { return Children.empty(); }
	};

	struct Plan
	{
		int GoalId = 0;
		std::string GoalName;
		std::string GoalIcon;
		std::string GoalType;
		std::string GoalRarity;
		PlanNode Root;
		i64 RemainingCost = 0;
		i64 FullCost = 0;
		std::vector<ShoppingItem> Shopping;
		std::map<int, i64> CurrencyRequired; // total amount the plan asks for
		std::map<int, i64> CurrencyNeeded;   // amount still missing after the wallet
		std::map<int, i64> CurrencyOwned;
		int NodeCount = 0;
		int GoalCount = 1;                    // >1 for a combined tracked plan
		bool Complete = false;
		i64 BuiltAt = 0;

		float Progress() const
		{
			if (FullCost <= 0)
			{
				return Complete ? 1.0f : 0.0f;
			}
			float value = 1.0f - static_cast<float>(RemainingCost) / static_cast<float>(FullCost);
			if (value < 0.0f) value = 0.0f;
			if (value > 1.0f) value = 1.0f;
			return value;
		}
	};

	enum class CraftAction
	{
		Craft = 0,
		MysticForge,
		Buy,
		Vendor,
		Gather,
		Achievement,
		Obtain,
	};

	// One actionable line of a dependency-ordered crafting plan.
	struct CraftStep
	{
		int Id = 0;
		bool Currency = false;
		std::string Name;
		std::string Icon;
		std::string ChatLink;
		std::string Detail;      // discipline, merchant or achievement source
		CraftAction Action = CraftAction::Craft;
		int Quantity = 0;        // crafts for craft steps, amount to acquire otherwise
		int Remaining = 0;
		int Owned = 0;
		int Required = 0;
		double OutputPerCraft = 1.0;
		i64 Cost = 0;            // purchases and vendor costs; 0 for crafts and currencies
	};

	struct CatalogEntry
	{
		int Id = 0;
		std::string Name;
		std::string Icon;
		std::string TypeLabel;
		std::string Rarity;
		bool Owned = false;
	};

	i64 NowUnix();
}
