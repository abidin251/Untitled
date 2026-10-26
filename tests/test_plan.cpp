#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "Core.h"
#include "Parse.h"
#include "Planner.h"

using namespace mc;

namespace
{
	int g_Failures = 0;

	void Check(bool aCondition, const char* aMessage)
	{
		if (!aCondition)
		{
			std::printf("FAIL: %s\n", aMessage);
			++g_Failures;
		}
	}

	RecipeNode ItemNode(int aId, double aQuantity)
	{
		RecipeNode node;
		node.Id = aId;
		node.Type = "Item";
		node.Quantity = aQuantity;
		return node;
	}

	RecipeNode Recipe(int aId, double aQuantity, double aOutput, const std::vector<RecipeNode>& aComponents, const char* aDiscipline)
	{
		RecipeNode node;
		node.Id = aId;
		node.Type = "Recipe";
		node.Quantity = aQuantity;
		node.Output = aOutput;
		node.Components = aComponents;
		node.Disciplines = { aDiscipline };
		return node;
	}

	// A merchant entry is a recipe whose only component is the currency the
	// merchant charges; 10000 coin is one gold, 2100 karma is an Obsidian Shard.
	RecipeNode MerchantRecipe(int aId, double aQuantity, int aCurrencyId, double aCurrencyAmount, const char* aName, const char* aLocations)
	{
		RecipeNode currency;
		currency.Id = aCurrencyId;
		currency.Type = "Currency";
		currency.Quantity = aCurrencyAmount;

		RecipeNode node;
		node.Id = aId;
		node.Type = "Recipe";
		node.Quantity = aQuantity;
		node.Components = { currency };
		node.Disciplines = { "Merchant" };
		node.MerchantName = aName;
		node.MerchantLocations = aLocations;
		node.MerchantCurrencyId = aCurrencyId;
		node.MerchantCurrencyAmount = aCurrencyAmount;
		return node;
	}

	void TestSimpleCraftCost()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Ore";
		items[2].Id = 2;
		items[2].Name = "Ingot";
		items[100].Id = 100;
		items[100].Name = "Sword";

		PriceDb prices;
		prices[1].Id = 1;
		prices[1].Sell = 100;  // 1 silver
		prices[2].Id = 2;
		prices[2].Sell = 5000;

		const RecipeNode root = Recipe(100, 1, 1, { ItemNode(1, 2) }, "Weaponsmith");
		PlanOptions options;
		options.PreferBuyingTradable = false;
		const Plan plan = BuildPlan(root, 1, items, prices, {}, {}, options, {});

		Check(plan.RemainingCost == 200, "simple craft cost is the sum of ingredient costs");
		Check(plan.FullCost == 200, "full cost equals remaining cost with an empty account");
		Check(plan.Shopping.size() == 1 && plan.Shopping[0].Quantity == 2, "shopping list aggregates ingredient quantity");
		Check(plan.Root.Children.size() == 1, "crafted root exposes its ingredient tree");
	}

	void TestOwnedMaterials()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Ore";
		items[100].Id = 100;
		items[100].Name = "Sword";

		PriceDb prices;
		prices[1].Sell = 100;

		AccountData account;
		account.Valid = true;
		account.Items[1] = 3;

		const RecipeNode root = Recipe(100, 1, 1, { ItemNode(1, 2) }, "Weaponsmith");
		PlanOptions options;
		options.PreferBuyingTradable = false;
		const Plan plan = BuildPlan(root, 1, items, prices, {}, account, options, {});

		Check(plan.Root.Children.size() == 1, "owned plan has an ingredient node");
		Check(plan.Root.Children[0].Owned == 2, "owned count is capped by the requirement");
		Check(plan.RemainingCost == 0, "fully owned ingredients make the plan free");
		Check(plan.FullCost == 200, "full cost ignores ownership");
	}

	void TestBuyVersusCraft()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Ore";
		items[2].Id = 2;
		items[2].Name = "Ingot";
		items[100].Id = 100;
		items[100].Name = "Sword";

		PriceDb prices;
		prices[1].Sell = 1000;  // crafting costs 10s
		prices[2].Sell = 300;   // buying the ingot costs 3s

		const RecipeNode root = Recipe(100, 1, 1, { Recipe(2, 2, 1, { ItemNode(1, 1) }, "Weaponsmith") }, "Weaponsmith");
		PlanOptions options;
		options.PreferBuyingTradable = true;
		const Plan plan = BuildPlan(root, 1, items, prices, {}, {}, options, {});

		Check(plan.Root.Children.size() == 1, "root has a child");
		Check(plan.Root.Children[0].Source == SourceKind::TradingPost, "cheaper tradable component is bought");
		Check(plan.RemainingCost == 600, "buying two ingots costs 6s");
	}

	void TestForcedCraftPreference()
	{
		ItemDb items;
		items[1].Id = 1;
		items[2].Id = 2;
		items[100].Id = 100;

		PriceDb prices;
		prices[1].Sell = 1000;
		prices[2].Sell = 300;

		const RecipeNode root = Recipe(100, 1, 1, { Recipe(2, 2, 1, { ItemNode(1, 1) }, "Weaponsmith") }, "Weaponsmith");
		PlanOptions options;
		options.PreferBuyingTradable = true;
		std::map<int, Preference> preferences;
		preferences[2] = Preference::Craft;
		const Plan plan = BuildPlan(root, 1, items, prices, {}, {}, options, preferences);

		Check(plan.Root.Children[0].Source == SourceKind::Craft, "forced craft ignores the cheaper buy price");
		Check(plan.RemainingCost == 2000, "forced craft uses ingredient cost");
	}

	void TestFractionalOutput()
	{
		ItemDb items;
		items[1].Id = 1;
		items[2].Id = 2;
		items[100].Id = 100;

		PriceDb prices;
		prices[1].Sell = 100;

		const RecipeNode root = Recipe(100, 1, 1, { Recipe(2, 1, 0.31, { ItemNode(1, 1) }, "Mystic Forge") }, "Mystic Forge");
		PlanOptions options;
		options.PreferBuyingTradable = true;
		const Plan plan = BuildPlan(root, 1, items, prices, {}, {}, options, {});

		const PlanNode& child = plan.Root.Children[0];
		Check(child.Crafts == 4, "fractional yields round the number of attempts up");
		Check(child.Children.size() == 1 && child.Children[0].Required == 4, "ingredients scale with the number of attempts");
		Check(plan.RemainingCost == 400, "four attempts at 1s each cost 4s");
	}

	void TestCurrencies()
	{
		ItemDb items;
		items[100].Id = 100;
		items[100].Name = "Gift";

		CurrencyDb currencies;
		currencies[2].Id = 2;
		currencies[2].Name = "Karma";

		RecipeNode currency;
		currency.Id = 2;
		currency.Type = "Currency";
		currency.Quantity = 2100;

		const RecipeNode root = Recipe(100, 1, 1, { currency }, "Merchant");

		AccountData account;
		account.Valid = true;
		account.Currencies[2] = 500;

		PlanOptions options;
		const Plan plan = BuildPlan(root, 1, items, {}, currencies, account, options, {});

		Check(plan.Root.Children[0].IsCurrency, "currency components are marked as currency");
		Check(plan.Root.Children[0].Owned == 500, "wallet balance is consumed");
		Check(plan.CurrencyRequired.at(2) == 2100, "currency requirement is recorded");
		Check(plan.CurrencyNeeded.at(2) == 1600, "currency shortfall is recorded");
		Check(plan.RemainingCost == 0, "currency does not add coin cost");
	}

	void TestCombinedPlans()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Ore";
		items[100].Id = 100;
		items[100].Name = "Sword";
		items[101].Id = 101;
		items[101].Name = "Staff";

		PriceDb prices;
		prices[1].Sell = 100;

		PlanOptions options;
		options.PreferBuyingTradable = false;

		const RecipeNode sword = Recipe(100, 1, 1, { ItemNode(1, 2) }, "Weaponsmith");
		const RecipeNode staff = Recipe(101, 1, 1, { ItemNode(1, 2) }, "Artificer");
		const std::vector<RecipeNode> roots = { sword, staff };

		Plan combined = BuildCombinedPlan(roots, items, prices, {}, {}, options, {});
		Check(combined.GoalCount == 2, "combined plan counts both goals");
		Check(combined.RemainingCost == 400, "combined remaining cost sums both goals");
		Check(combined.FullCost == 400, "combined full cost sums both goals");
		Check(combined.Shopping.size() == 1 && combined.Shopping[0].Quantity == 4, "combined shopping list merges the shared ingredient");

		// Three ore cover the first goal and one of the second goal's two ore.
		AccountData account;
		account.Valid = true;
		account.Items[1] = 3;
		combined = BuildCombinedPlan(roots, items, prices, {}, account, options, {});
		Check(combined.RemainingCost == 100, "owned materials are consumed once across goals");
		Check(combined.FullCost == 400, "combined full cost ignores ownership");
		Check(combined.Shopping.size() == 1 && combined.Shopping[0].Quantity == 1, "only the shortfall of the second goal is bought");
		Check(std::fabs(combined.Progress() - 0.75f) < 0.001f, "combined progress uses the shared ownership pool");

		const Plan empty = BuildCombinedPlan({}, items, prices, {}, account, options, {});
		Check(empty.GoalCount == 0 && empty.Shopping.empty(), "an empty tracked list builds an empty combined plan");
	}

	void TestCombinedCurrencies()
	{
		ItemDb items;
		items[100].Id = 100;
		items[100].Name = "Sword";
		items[101].Id = 101;
		items[101].Name = "Staff";

		CurrencyDb currencies;
		currencies[2].Id = 2;
		currencies[2].Name = "Karma";

		RecipeNode karma;
		karma.Id = 2;
		karma.Type = "Currency";
		karma.Quantity = 100;

		const std::vector<RecipeNode> roots = {
			Recipe(100, 1, 1, { karma }, "Merchant"),
			Recipe(101, 1, 1, { karma }, "Merchant")
		};

		AccountData account;
		account.Valid = true;
		account.Currencies[2] = 150;

		const Plan combined = BuildCombinedPlan(roots, items, {}, currencies, account, PlanOptions{}, {});
		Check(combined.CurrencyRequired.at(2) == 200, "combined currency requirement sums both goals");
		Check(combined.CurrencyNeeded.at(2) == 50, "combined currency shortfall respects the shared wallet");
		Check(combined.CurrencyOwned.at(2) == 150, "combined plan reports the wallet balance once");
	}

	void TestCraftingSteps()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Ore";
		items[100].Id = 100;
		items[100].Name = "Sword";

		CurrencyDb currencies;
		currencies[2].Id = 2;
		currencies[2].Name = "Karma";

		PriceDb prices;
		prices[1].Sell = 100;

		RecipeNode karma;
		karma.Id = 2;
		karma.Type = "Currency";
		karma.Quantity = 100;

		RecipeNode coin;
		coin.Id = 1;
		coin.Type = "Currency";
		coin.Quantity = 5000;

		const RecipeNode root = Recipe(100, 1, 1, { ItemNode(1, 2), karma, coin }, "Weaponsmith");

		PlanOptions options;
		options.PreferBuyingTradable = true;
		const Plan plan = BuildPlan(root, 1, items, prices, currencies, {}, options, {});
		const std::vector<CraftStep> steps = BuildCraftingSteps(plan);

		Check(steps.size() == 3, "steps list ingredients first and skips coin");
		Check(steps[0].Action == CraftAction::Buy && steps[0].Id == 1, "a tradable ingredient becomes a buy step");
		Check(steps[0].Remaining == 2 && steps[0].Cost == 200, "buy step reports quantity and cost");
		Check(steps[1].Action == CraftAction::Gather && steps[1].Id == 2, "a currency ingredient becomes a gather step");
		Check(steps[1].Remaining == 100, "gather step reports the missing amount");
		Check(steps[2].Action == CraftAction::Craft && steps[2].Quantity == 1, "the goal craft is the last step");

		AccountData owned;
		owned.Valid = true;
		owned.Items[1] = 2;
		owned.Currencies[2] = 100;
		owned.Currencies[1] = 5000;
		const Plan ready = BuildPlan(root, 1, items, prices, currencies, owned, options, {});
		const std::vector<CraftStep> readySteps = BuildCraftingSteps(ready);
		Check(readySteps.size() == 1 && readySteps[0].Action == CraftAction::Craft, "only the final craft remains when all ingredients are owned");

		const RecipeNode forge = Recipe(101, 1, 1, { ItemNode(1, 1) }, "Mystic Forge");
		const Plan forgePlan = BuildPlan(forge, 1, items, prices, currencies, {}, options, {});
		const std::vector<CraftStep> forgeSteps = BuildCraftingSteps(forgePlan);
		Check(!forgeSteps.empty() && forgeSteps.back().Action == CraftAction::MysticForge, "Mystic Forge steps are labelled as a forge action");

		const Plan empty;
		Check(BuildCraftingSteps(empty).empty(), "an empty plan has no crafting steps");
	}

	void TestCombinedStepsForTrackedGoals()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Ore";
		items[100].Id = 100;
		items[100].Name = "Sword";
		items[101].Id = 101;
		items[101].Name = "Staff";

		PriceDb prices;
		prices[1].Id = 1;
		prices[1].Sell = 100;

		PlanOptions options;
		options.PreferBuyingTradable = false;

		const std::vector<RecipeNode> roots = {
			Recipe(100, 1, 1, { ItemNode(1, 2) }, "Weaponsmith"),
			Recipe(101, 1, 1, { ItemNode(1, 1) }, "Artificer")
		};

		const Plan combined = BuildCombinedPlan(roots, items, prices, {}, {}, options, {});
		Check(combined.GoalRoots.size() == 2, "a combined plan keeps one tree per tracked goal");

		const std::vector<CraftStep> steps = BuildCraftingSteps(combined);
		Check(steps.size() == 4, "crafting steps cover every tracked goal");
		Check(steps[0].Action == CraftAction::Buy && steps[0].Remaining == 2, "the first goal ingredient comes first");
		Check(steps[1].Action == CraftAction::Craft && steps[1].Name == "Sword", "the first goal craft follows its ingredient");
		Check(steps[2].Action == CraftAction::Buy && steps[2].Remaining == 1, "the second goal ingredient follows");
		Check(steps[3].Action == CraftAction::Craft && steps[3].Name == "Staff", "the second goal craft is last");
	}

	void TestVendorIngredients()
	{
		ItemDb items;
		items[100].Id = 100;
		items[100].Name = "Gift";
		items[200].Id = 200;
		items[200].Name = "Icy Runestone";
		items[201].Id = 201;
		items[201].Name = "Obsidian Shard";

		CurrencyDb currencies;
		currencies[1].Id = 1;
		currencies[1].Name = "Coin";
		currencies[2].Id = 2;
		currencies[2].Name = "Karma";

		PlanOptions options;
		options.PreferBuyingTradable = false;

		// Icy Runestones cost one gold each from a merchant and cannot be traded,
		// so the merchant is the only source.
		const RecipeNode icy = MerchantRecipe(200, 2, 1, 10000, "Rojan the Penitent", "Earthshake Basin, Frostgorge Sound");
		const RecipeNode root = Recipe(100, 1, 1, { icy }, "Weaponsmith");
		const Plan plan = BuildPlan(root, 1, items, {}, currencies, {}, options, {});

		Check(plan.RemainingCost == 20000, "a merchant ingredient is costed at the merchant price");
		bool found = false;
		for (const auto& entry : plan.Shopping)
		{
			if (entry.Id != 200)
			{
				continue;
			}
			found = true;
			Check(entry.Source == SourceKind::Vendor, "merchant ingredients are listed with a vendor source");
			Check(entry.Quantity == 2 && entry.UnitCost == 10000 && entry.TotalCost == 20000, "the merchant price is carried on the shopping line");
			Check(entry.FromVendor && !entry.FromTradingPost, "the line records that only a merchant sells the item");
			Check(entry.SourceDetail.find("Rojan the Penitent") != std::string::npos, "the merchant name is kept for the shopping list");
		}
		Check(found, "merchant ingredients appear on the shopping list");

		// The same ingredient is cheaper on the Trading Post, so that becomes the
		// cheapest available method.
		PriceDb prices;
		prices[200].Id = 200;
		prices[200].Sell = 8000;
		const Plan bought = BuildPlan(root, 1, items, prices, currencies, {}, options, {});
		Check(bought.RemainingCost == 16000, "a tradable merchant ingredient uses the cheaper Trading Post price");
		for (const auto& entry : bought.Shopping)
		{
			if (entry.Id != 200)
			{
				continue;
			}
			Check(entry.Source == SourceKind::TradingPost && entry.FromVendor, "the cheapest source is reported with both options known");
			Check(entry.UnitCost == 8000 && entry.TotalCost == 16000, "the cheaper Trading Post price is counted");
		}

		// A karma merchant costs no coin, so the price is shown in karma instead.
		const RecipeNode shard = MerchantRecipe(201, 1, 2, 2100, "Tactician Deathstrider", "Straits of Devastation");
		const Plan karma = BuildPlan(Recipe(100, 1, 1, { shard }, "Weaponsmith"), 1, items, {}, currencies, {}, options, {});
		Check(karma.RemainingCost == 0, "a karma ingredient adds no coin cost");
		found = false;
		for (const auto& entry : karma.Shopping)
		{
			if (entry.Id != 201)
			{
				continue;
			}
			found = true;
			Check(entry.Source == SourceKind::Vendor, "karma ingredients are listed with a vendor source");
			Check(entry.PriceText == "2,100 Karma", "a non-coin merchant price is shown in its currency");
			Check(entry.TotalPriceText == "2,100 Karma", "the non-coin total uses the same currency");
			Check(entry.UnitCost == 0 && entry.TotalCost == 0, "a non-coin price is never counted as coin");
		}
		Check(found, "karma ingredients appear on the shopping list");
	}

	void TestAchievementIngredients()
	{
		ItemDb items;
		items[100].Id = 100;
		items[100].Name = "Gift";
		items[204].Id = 204;
		items[204].Name = "Achievement Trophy";

		RecipeNode trophy = ItemNode(204, 3);
		trophy.AchievementId = 2438;

		PlanOptions options;
		const Plan plan = BuildPlan(Recipe(100, 1, 1, { trophy }, "Weaponsmith"), 1, items, {}, {}, {}, options, {});

		Check(plan.RemainingCost == 0, "achievement ingredients do not add gold cost");
		bool found = false;
		for (const auto& entry : plan.Shopping)
		{
			if (entry.Id != 204)
			{
				continue;
			}
			found = true;
			Check(entry.Source == SourceKind::Achievement, "achievement ingredients are listed with an achievement source");
			Check(entry.Quantity == 3 && entry.TotalCost == 0, "the shortfall is listed without a price");
			Check(entry.SourceDetail == "Achievement #2438", "the achievement id is kept for the shopping list");
		}
		Check(found, "achievement ingredients appear on the shopping list");
	}

	void TestResearchNoteCost()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Ore";
		items[100].Id = 100;
		items[100].Name = "Obsidian Armor";

		CurrencyDb currencies;
		currencies[kResearchNoteCurrency].Id = kResearchNoteCurrency;
		currencies[kResearchNoteCurrency].Name = "Research Note";

		PriceDb prices;
		prices[1].Id = 1;
		prices[1].Sell = 100;

		RecipeNode notes;
		notes.Id = kResearchNoteCurrency;
		notes.Type = "Currency";
		notes.Quantity = 250;

		const RecipeNode root = Recipe(100, 1, 1, { ItemNode(1, 2), notes }, "Armorsmith");

		PlanOptions options;
		options.IncludeResearchNotes = true;
		options.ResearchNoteUnitCost = 100; // one silver per note
		const Plan plan = BuildPlan(root, 1, items, prices, currencies, {}, options, {});

		Check(plan.ResearchNotesRequired == 250 && plan.ResearchNotesNeeded == 250, "the plan reports the research notes it needs");
		Check(plan.ResearchNoteCost == 25000, "research notes are priced at the configured cost");
		Check(plan.RemainingCost == 25200, "the note cost is part of the remaining cost");
		Check(plan.FullCost == 25200, "the note cost is part of the from-scratch cost");

		bool found = false;
		for (const auto& entry : plan.Shopping)
		{
			if (entry.Id != kResearchNoteCurrency)
			{
				continue;
			}
			found = true;
			Check(entry.Currency && entry.Source == SourceKind::Currency, "research notes are listed as a wallet currency line");
			Check(entry.Name == "Research Note" && entry.Quantity == 250 && entry.TotalCost == 25000, "the note line carries the priced quantity");
		}
		Check(found, "research notes appear on the shopping list");

		// Notes already in the wallet are not paid for twice.
		AccountData account;
		account.Valid = true;
		account.Currencies[kResearchNoteCurrency] = 50;
		const Plan partlyOwned = BuildPlan(root, 1, items, prices, currencies, account, options, {});
		Check(partlyOwned.ResearchNotesNeeded == 200 && partlyOwned.ResearchNoteCost == 20000, "only the missing notes are priced");
		Check(partlyOwned.RemainingCost == 20200, "owned notes reduce the remaining cost");

		// With the option off the notes are still reported, just not priced.
		options.IncludeResearchNotes = false;
		const Plan unpriced = BuildPlan(root, 1, items, prices, currencies, {}, options, {});
		Check(unpriced.ResearchNotesRequired == 250 && unpriced.ResearchNoteCost == 0, "notes are reported but not priced when the option is off");
		Check(unpriced.RemainingCost == 200, "unpriced notes add no gold cost");
	}

	void TestParseRecipeTree()
	{
		const std::string json = R"([
			{
				"id": 30684,
				"type": "Recipe",
				"quantity": 1,
				"output": 1,
				"disciplines": ["Mystic Forge"],
				"components": [
					{ "id": 19626, "type": "Recipe", "quantity": 1, "output": 1, "disciplines": ["Mystic Forge"],
					  "components": [ { "id": 19721, "type": "Item", "quantity": 250 } ] },
					{ "id": 1, "type": "Currency", "quantity": 100 }
				]
			}
		])";

		RecipeNode root;
		std::string error;
		Check(ParseRecipeTree(json, root, error), "recipe JSON parses");
		Check(root.Id == 30684, "root id parsed");
		Check(root.Components.size() == 2, "root components parsed");
		Check(root.Components[0].Components[0].Id == 19721, "nested components parsed");
		Check(root.Components[0].Disciplines.size() == 1 && root.Components[0].Disciplines[0] == "Mystic Forge", "disciplines parsed");
	}

	void TestParseMerchantPrice()
	{
		const std::string json = R"([
			{
				"id": 19676,
				"type": "Recipe",
				"quantity": 1,
				"output": 1,
				"components": [ { "id": 1, "type": "Currency", "quantity": 10000 } ],
				"disciplines": ["Merchant"],
				"merchant": { "name": "Rojan the Penitent", "locations": ["Earthshake Basin, Frostgorge Sound"] },
				"multipleRecipeCount": 1,
				"daily_purchase_cap": 0,
				"weekly_purchase_cap": 0
			}
		])";

		RecipeNode root;
		std::string error;
		Check(ParseRecipeTree(json, root, error), "merchant recipe JSON parses");
		Check(root.MerchantName == "Rojan the Penitent", "merchant name parses");
		Check(root.MerchantLocations == "Earthshake Basin, Frostgorge Sound", "merchant locations parse");
		Check(root.MerchantCurrencyId == 1 && root.MerchantCurrencyAmount == 10000.0, "the merchant currency price parses");
		Check(root.Components.size() == 1 && root.Components[0].IsCurrency(), "the merchant price is a currency component");
	}

	void TestParseItemsAndPrices()
	{
		const std::string itemsJson = R"([
			{ "id": 30684, "name": "Frostfang", "rarity": "Legendary", "type": "Weapon",
			  "details": { "type": "Axe" }, "vendor_value": 100000,
			  "flags": ["AccountBindOnUse", "NoSell"], "chat_link": "[&AgHcdwAA]", "level": 80,
			  "icon": "https://render.guildwars2.com/file/x/1.png" }
		])";
		const auto items = ParseItems(itemsJson);
		Check(items.size() == 1, "item array parses");
		Check(items[0].Name == "Frostfang", "item name parses");
		Check(items[0].Subtype == "Axe", "item subtype parses");
		Check(!items[0].Tradable, "account-bound flags mark an item as not tradable");
		Check(ItemTypeLabel(items[0]) == "Axe", "item type label uses details.type");

		const std::string pricesJson = R"([
			{ "id": 30684, "buys": { "unit_price": 100, "quantity": 5 }, "sells": { "unit_price": 200, "quantity": 7 } }
		])";
		const auto prices = ParsePrices(pricesJson);
		Check(prices.size() == 1 && prices[0].Buy == 100 && prices[0].Sell == 200, "prices parse");
	}

	void TestFormatting()
	{
		Check(FormatCoins(0) == "0c", "zero coins");
		Check(FormatCoins(12345) == "1g 23s 45c", "coin formatting");
		Check(FormatCoins(10000) == "1g", "whole gold formatting");
		Check(FormatNumber(1234567) == "1,234,567", "number formatting");
	}
}

int main()
{
	TestSimpleCraftCost();
	TestOwnedMaterials();
	TestBuyVersusCraft();
	TestForcedCraftPreference();
	TestFractionalOutput();
	TestCurrencies();
	TestCombinedPlans();
	TestCombinedCurrencies();
	TestCraftingSteps();
	TestCombinedStepsForTrackedGoals();
	TestVendorIngredients();
	TestAchievementIngredients();
	TestResearchNoteCost();
	TestParseRecipeTree();
	TestParseMerchantPrice();
	TestParseItemsAndPrices();
	TestFormatting();

	if (g_Failures == 0)
	{
		std::printf("All planner tests passed.\n");
		return 0;
	}
	std::printf("%d test(s) failed.\n", g_Failures);
	return 1;
}
