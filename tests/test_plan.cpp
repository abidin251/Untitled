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

		Check(steps.size() == 1, "only crafts are listed, purchases are left to the shopping list");
		Check(steps[0].Action == CraftAction::Craft && steps[0].Quantity == 1, "the goal craft is the step");

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
		Check(steps.size() == 2, "crafting steps cover every tracked goal");
		Check(steps[0].Action == CraftAction::Craft && steps[0].Name == "Sword", "the first goal craft comes first");
		Check(steps[1].Action == CraftAction::Craft && steps[1].Name == "Staff", "the second goal craft follows");
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

		// The achievement items are warned about before the steps, with the very
		// shortfall the plan still needs.
		Check(plan.AchievementNeeds.size() == 1, "the plan reports the achievement item it needs");
		if (!plan.AchievementNeeds.empty())
		{
			Check(plan.AchievementNeeds[0].ItemId == 204, "the warning names the item");
			Check(plan.AchievementNeeds[0].Quantity == 3, "the warning carries the amount still missing");
			Check(plan.AchievementNeeds[0].AchievementId == 2438, "the warning carries the achievement id");
			Check(plan.AchievementNeeds[0].Detail == "Achievement #2438", "the warning is readable on its own");
		}

		// An owned achievement item is not a warning, and no steps claim it.
		AccountData owned;
		owned.Valid = true;
		owned.Items[204] = 3;
		const Plan ready = BuildPlan(Recipe(100, 1, 1, { trophy }, "Weaponsmith"), 1, items, {}, {}, owned, options, {});
		Check(ready.AchievementNeeds.empty(), "an owned achievement item raises no warning");

		// Two goals needing the same achievement item give one warning with the
		// summed amount.
		const std::vector<RecipeNode> roots = {
			Recipe(100, 1, 1, { trophy }, "Weaponsmith"),
			Recipe(101, 1, 1, { ItemNode(204, 2) }, "Artificer")
		};
		items[101].Id = 101;
		items[101].Name = "Staff";
		RecipeNode other = ItemNode(204, 2);
		other.AchievementId = 2438;
		const std::vector<RecipeNode> summed = {
			Recipe(100, 1, 1, { trophy }, "Weaponsmith"),
			Recipe(101, 1, 1, { other }, "Artificer")
		};
		const Plan combined = BuildCombinedPlan(summed, items, {}, {}, {}, options, {});
		Check(combined.AchievementNeeds.size() == 1 && combined.AchievementNeeds[0].Quantity == 5, "a shared achievement item is one summed warning");
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
		const Plan plan = BuildPlan(root, 1, items, prices, currencies, {}, options, {});

		Check(plan.ResearchNotesRequired == 250 && plan.ResearchNotesNeeded == 250, "the plan reports the research notes it needs");
		Check(plan.ResearchNoteCost == 0, "notes without a priced craft cost nothing");
		Check(plan.RemainingCost == 200, "unpriced notes add no gold cost");
		Check(plan.FullCost == 200, "unpriced notes add no from-scratch cost");

		bool found = false;
		for (const auto& entry : plan.Shopping)
		{
			if (entry.Id != kResearchNoteCurrency)
			{
				continue;
			}
			found = true;
			Check(entry.Currency && entry.Source == SourceKind::Currency, "research notes are listed as a wallet currency line");
			Check(entry.Name == "Research Note" && entry.Quantity == 250 && entry.TotalCost == 0, "the note line carries the quantity without a price");
		}
		Check(found, "research notes appear on the shopping list");

		// Notes already in the wallet are not paid for twice.
		AccountData account;
		account.Valid = true;
		account.Currencies[kResearchNoteCurrency] = 50;
		const Plan partlyOwned = BuildPlan(root, 1, items, prices, currencies, account, options, {});
		Check(partlyOwned.ResearchNotesNeeded == 200, "owned notes reduce the missing notes");

		// With the option off the notes are still reported, just not priced.
		options.IncludeResearchNotes = false;
		const Plan unpriced = BuildPlan(root, 1, items, prices, currencies, {}, options, {});
		Check(unpriced.ResearchNotesRequired == 250 && unpriced.ResearchNoteCost == 0, "notes are reported but not priced when the option is off");
		Check(unpriced.RemainingCost == 200, "unpriced notes add no gold cost");
	}

	// Research notes are earned by crafting the source item at a station, so the
	// plan prices the missing notes from that craft's ingredients.
	void TestResearchNotesFromSourceRecipe()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Cured Leather";
		items[100].Id = 100;
		items[100].Name = "Obsidian Armor";
		items[48601].Id = 48601;
		items[48601].Name = "Rabid Draconic Gauntlets";
		items[48601].Rarity = "Exotic";
		items[48601].Level = 80;
		items[48601].Type = "Armor";

		CurrencyDb currencies;
		currencies[kResearchNoteCurrency].Id = kResearchNoteCurrency;
		currencies[kResearchNoteCurrency].Name = "Research Note";

		// The finished item sells on the Trading Post, but notes only come from
		// crafting it, so the craft price is the one that must be used.
		PriceDb prices;
		prices[1].Id = 1;
		prices[1].Sell = 100;
		prices[48601].Id = 48601;
		prices[48601].Sell = 500;

		RecipeNode notes;
		notes.Id = kResearchNoteCurrency;
		notes.Type = "Currency";
		notes.Quantity = 250;

		const RecipeNode root = Recipe(100, 1, 1, { notes }, "Armorsmith");
		const RecipeNode source = Recipe(48601, 1, 1, { ItemNode(1, 10) }, "Leatherworker");

		std::vector<RecipeNode> sources = { source };
		PlanOptions options;
		options.IncludeResearchNotes = true;
		options.ResearchNoteSources = &sources;

		const Plan plan = BuildPlan(root, 1, items, prices, currencies, {}, options, {});

		// A level-80 exotic armor craft is measured at about 75 notes, so 250 of
		// them need four crafts of 10 leather at 1s each: 40s, 16c a note.
		Check(plan.ResearchNotesRequired == 250 && plan.ResearchNotesNeeded == 250, "the source recipe prices the notes the plan needs");
		Check(plan.ResearchNoteCrafts == 4 && plan.ResearchNoteYield == 75, "the plan reports the crafts and the measured yield");
		Check(plan.ResearchNoteSourceName == "Rabid Draconic Gauntlets", "the source item is named in the plan");
		Check(plan.ResearchNoteCost == 4000, "the note cost is the ingredient cost of the source recipe, not the Trading Post price");
		Check(plan.RemainingCost == 4000, "the note cost is part of the remaining cost");

		bool foundNotes = false;
		bool foundIngredients = false;
		for (const auto& entry : plan.Shopping)
		{
			if (entry.Id == kResearchNoteCurrency)
			{
				foundNotes = true;
				Check(entry.UnitCost == 16 && entry.TotalCost == 4000, "the note line carries the per-note ingredient cost");
				Check(entry.SourceDetail.find("Rabid Draconic Gauntlets") != std::string::npos, "the note line names the item it is crafted from");
			}
			if (entry.Id == 1)
			{
				foundIngredients = true;
				Check(entry.Quantity == 40 && entry.TotalCost == 4000, "the source ingredients are listed for the whole batch");
				Check(entry.ResearchNotes, "the source ingredients are marked as being for research notes");
			}
		}
		Check(foundNotes, "the priced notes appear on the shopping list");
		Check(foundIngredients, "the ingredients that produce the notes appear on the shopping list");

		// The craft that turns into the notes is part of the work to do, flagged so
		// the interface can label it, and grouped with the station it happens at.
		const std::vector<CraftStep> steps = BuildCraftingSteps(plan);
		bool foundSourceCraft = false;
		int flagged = 0;
		for (const auto& step : steps)
		{
			flagged += step.ResearchNotes ? 1 : 0;
			Check(step.Action == CraftAction::Craft || step.Action == CraftAction::MysticForge, "purchases are not part of the crafting steps");
			if (step.Action == CraftAction::Craft && step.Name == "Rabid Draconic Gauntlets")
			{
				foundSourceCraft = step.ResearchNotes && step.Quantity == 4;
			}
		}
		Check(foundSourceCraft, "the source craft joins the crafting steps");
		Check(flagged == 1, "only the research note craft is flagged");

		const std::vector<CraftStepGroup> groups = BuildStepGroups(plan);
		bool foundNoteStation = false;
		for (const auto& group : groups)
		{
			if (group.Title == "Leatherworker")
			{
				foundNoteStation = group.ResearchNotes && group.Steps.size() == 1;
			}
			if (group.Title == "Armorsmith")
			{
				Check(!group.ResearchNotes, "a station group that also crafts the goal is not marked as notes only");
			}
		}
		Check(foundNoteStation, "the note craft gets its own station group");

		// Two candidate crafts: the cheaper bill for the notes wins.
		items[48602].Id = 48602;
		items[48602].Name = "Dire Draconic Gauntlets";
		items[48602].Rarity = "Exotic";
		items[48602].Level = 80;
		items[48602].Type = "Armor";
		const RecipeNode cheaper = Recipe(48602, 1, 1, { ItemNode(1, 4) }, "Leatherworker");
		sources.push_back(cheaper);
		const Plan cheapest = BuildPlan(root, 1, items, prices, currencies, {}, options, {});
		Check(cheapest.ResearchNoteSourceName == "Dire Draconic Gauntlets", "the cheapest source is picked automatically");
		Check(cheapest.ResearchNoteCost == 1600, "the notes are charged at the cheapest source's ingredient cost");

		// Notes already in the wallet only pay for the crafts still needed.
		AccountData account;
		account.Valid = true;
		account.Currencies[kResearchNoteCurrency] = 50;
		const Plan partlyOwned = BuildPlan(root, 1, items, prices, currencies, account, options, {});
		Check(partlyOwned.ResearchNotesNeeded == 200 && partlyOwned.ResearchNoteCrafts == 3, "owned notes reduce the crafts needed");
		Check(partlyOwned.ResearchNoteCost == 1200, "only the missing notes are priced");

		// Without a candidate source the notes are listed without a price.
		PlanOptions fallback;
		fallback.IncludeResearchNotes = true;
		const Plan flat = BuildPlan(root, 1, items, prices, currencies, {}, fallback, {});
		Check(flat.ResearchNoteCost == 0 && flat.ResearchNoteSourceName.empty(), "notes stay unpriced when no source recipe is available");
	}

	// The measured salvage research decides what a source craft returns, so the
	// cheapest note source is a matter of notes per craft and craft cost together.
	void TestNoteYieldAndCheapestSource()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Scrap";
		items[100].Id = 100;
		items[100].Name = "Obsidian Armor";
		items[200].Id = 200;
		items[200].Name = "Draconic Gauntlets";
		items[200].Rarity = "Exotic";
		items[200].Level = 80;
		items[300].Id = 300;
		items[300].Name = "Barbaric Helm";
		items[300].Rarity = "Fine";
		items[300].Level = 70;

		CurrencyDb currencies;
		currencies[kResearchNoteCurrency].Id = kResearchNoteCurrency;
		currencies[kResearchNoteCurrency].Name = "Research Note";

		PriceDb prices;
		prices[1].Id = 1;
		prices[1].Sell = 100;

		RecipeNode notes;
		notes.Id = kResearchNoteCurrency;
		notes.Type = "Currency";
		notes.Quantity = 20;

		const RecipeNode root = Recipe(100, 1, 1, { notes }, "Armorsmith");
		// The exotic gives five notes per craft at 10s of materials: 2s a note.
		const RecipeNode exotic = Recipe(200, 1, 1, { ItemNode(1, 10) }, "Leatherworker");
		// The fine piece gives one note per craft at 1s of materials: 1s a note,
		// so it wins even though a craft yields fewer notes.
		const RecipeNode fine = Recipe(300, 1, 1, { ItemNode(1, 1) }, "Armorsmith");

		std::vector<RecipeNode> sources = { exotic, fine };
		PlanOptions options;
		options.IncludeResearchNotes = true;
		options.ResearchNoteSources = &sources;

		const Plan plan = BuildPlan(root, 1, items, prices, currencies, {}, options, {});
		Check(plan.ResearchNotesNeeded == 20, "the plan prices the notes it needs");
		Check(plan.ResearchNoteYield == 5, "a level-70 fine source is priced at its measured yield");
		Check(plan.ResearchNoteSourceName == "Barbaric Helm", "a cheap five-note craft beats an exotic one for a small batch");
		Check(plan.ResearchNoteCrafts == 4, "twenty notes need four crafts at five notes each");
		Check(plan.ResearchNoteCost == 400, "the winning source is charged at its own ingredient cost");

		// A level-65 utility gives a single note, so a twenty-note batch costs
		// twenty crafts of it.
		PlanOptions utilityOptions = options;
		items[400].Id = 400;
		items[400].Name = "Potent Potion";
		items[400].Rarity = "Fine";
		items[400].Level = 65;
		items[400].Type = "Consumable";
		const RecipeNode utility = Recipe(400, 1, 1, { ItemNode(1, 10) }, "Artificer");
		std::vector<RecipeNode> utilitySources = { utility };
		utilityOptions.ResearchNoteSources = &utilitySources;
		const Plan utilityPlan = BuildPlan(root, 1, items, prices, currencies, {}, utilityOptions, {});
		Check(utilityPlan.ResearchNoteYield == 1 && utilityPlan.ResearchNoteCrafts == 20, "a crafted utility is researched one note at a time");
		Check(utilityPlan.ResearchNoteCost == 20000, "twenty crafts of a utility are charged at their own cost");

		// The measured exceptions carry their own figure: Peer Reviewed Research
		// hands over 150 notes for a single craft.
		PlanOptions researchOptions = options;
		items[104934].Id = 104934;
		items[104934].Name = "Peer Reviewed Research";
		items[104934].Rarity = "Fine";
		items[104934].Level = 0;
		items[104934].Type = "Container";
		const RecipeNode research = Recipe(104934, 1, 1, { ItemNode(1, 2) }, "Scribe");
		std::vector<RecipeNode> researchSources = { research, exotic };
		researchOptions.ResearchNoteSources = &researchSources;
		const Plan researchPlan = BuildPlan(root, 1, items, prices, currencies, {}, researchOptions, {});
		Check(researchPlan.ResearchNoteSourceName == "Peer Reviewed Research", "the measured source is priced from its own figure");
		Check(researchPlan.ResearchNoteYield == 150 && researchPlan.ResearchNoteCrafts == 1, "its 150 notes cover the small batch in one craft");
		Check(researchPlan.ResearchNoteCost == 200, "it is charged at its own ingredient cost");

		// The model behind those figures, at the levels, rarities and types the
		// wiki's salvage research reports.
		Check(ResearchNoteYieldFor(70, "Fine", "Armor", "Coat") == 5, "fine level-70 gear returns about five notes");
		Check(ResearchNoteYieldFor(75, "Masterwork", "Trinket", "Earring") == 5, "masterwork level-75 jewellery returns five");
		Check(ResearchNoteYieldFor(65, "Rare", "Trinket", "Amulet") == 3, "level-65 gear returns three");
		Check(ResearchNoteYieldFor(80, "Exotic", "Armor", "Coat") == 75, "level-80 exotic armor returns about seventy-five");
		Check(ResearchNoteYieldFor(80, "Exotic", "Weapon", "Sword") == 45, "level-80 exotic weapons return about forty-five");
		Check(ResearchNoteYieldFor(80, "Exotic", "Trinket", "Earring") == 80, "exotic trinkets return about eighty");
		Check(ResearchNoteYieldFor(65, "Fine", "Consumable", "Utility") == 1, "crafted utilities return a single note");
		Check(ResearchNoteYieldFor(50, "Exotic", "Armor", "Coat") == 1, "gear below level 60 returns a single note");
		Check(ResearchNoteYieldOverride(104934) == 150, "Peer Reviewed Research keeps its measured 150");
		Check(ResearchNoteYieldOverride(13435) == 10, "a measured earring keeps its own figure");
		Check(ResearchNoteYieldOverride(48601) == 0, "unmeasured items follow level, rarity and type");
	}

	void TestStepGroups()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Ore";
		items[100].Id = 100;
		items[100].Name = "Gift";
		items[101].Id = 101;
		items[101].Name = "Staff";
		items[200].Id = 200;
		items[200].Name = "Ingot";

		PriceDb prices;
		prices[1].Id = 1;
		prices[1].Sell = 100;

		PlanOptions options;
		options.PreferBuyingTradable = false;

		// Both crafts happen at the same station, so they collapse into one block
		// that keeps dependency order.
		const RecipeNode ingot = Recipe(200, 2, 1, { ItemNode(1, 2) }, "Weaponsmith");
		const RecipeNode root = Recipe(100, 1, 1, { ingot, ItemNode(1, 4) }, "Weaponsmith");
		const Plan plan = BuildPlan(root, 1, items, prices, {}, {}, options, {});

		const std::vector<CraftStepGroup> groups = BuildStepGroups(plan);
		Check(groups.size() == 1, "crafts at the same station become a single group");
		if (groups.size() == 1)
		{
			Check(groups[0].Title == "Weaponsmith" && groups[0].Action == CraftAction::Craft, "the block is named after the discipline");
			Check(groups[0].Steps.size() == 2, "both crafts are in the same block");
			Check(groups[0].Steps[0].Name == "Ingot" && groups[0].Steps[1].Name == "Gift", "the block keeps dependency order");
			Check(groups[0].Steps[0].Quantity == 2, "an ingredient craft reports its crafts");
		}

		// The Mystic Forge is its own block, even inside a crafted plan.
		const RecipeNode forge = Recipe(201, 1, 1, { ItemNode(1, 1) }, "Mystic Forge");
		const RecipeNode mixed = Recipe(101, 1, 1, { forge }, "Artificer");
		const Plan mixedPlan = BuildPlan(mixed, 1, items, prices, {}, {}, options, {});
		const std::vector<CraftStepGroup> mixedGroups = BuildStepGroups(mixedPlan);
		Check(mixedGroups.size() == 2, "the Mystic Forge becomes its own block");
		if (mixedGroups.size() == 2)
		{
			Check(mixedGroups[0].Title == "Mystic Forge" && mixedGroups[0].Action == CraftAction::MysticForge, "the forge block comes first when it is needed first");
			Check(mixedGroups[1].Title == "Artificer" && mixedGroups[1].Steps.size() == 1, "the station block follows");
		}

		// A craft both tracked goals need is listed once with the summed crafts.
		const RecipeNode shared = Recipe(200, 1, 1, { ItemNode(1, 1) }, "Weaponsmith");
		const std::vector<RecipeNode> roots = {
			Recipe(100, 1, 1, { shared }, "Weaponsmith"),
			Recipe(101, 1, 1, { shared }, "Weaponsmith")
		};
		const Plan combined = BuildCombinedPlan(roots, items, prices, {}, {}, options, {});
		const std::vector<CraftStep> combinedSteps = BuildCraftingSteps(combined);
		Check(combinedSteps.size() == 3, "a craft shared by two goals becomes one step");
		if (combinedSteps.size() == 3)
		{
			Check(combinedSteps[0].Name == "Ingot" && combinedSteps[0].Quantity == 2, "the repeated craft is combined");
			Check(combinedSteps[1].Name == "Gift" && combinedSteps[2].Name == "Staff", "the goal crafts follow");
		}
		const std::vector<CraftStepGroup> combinedGroups = BuildStepGroups(combined);
		Check(combinedGroups.size() == 1 && combinedGroups[0].Steps.size() == 3, "shared work stays in one station block");

		// Merchant purchases belong to the checklist and keep their place in the
		// order, with the merchant, the map and the cost preserved.
		CurrencyDb currencies;
		currencies[1].Id = 1;
		currencies[1].Name = "Coin";
		const RecipeNode icy = MerchantRecipe(202, 2, 1, 10000, "Rojan the Penitent", "Earthshake Basin, Frostgorge Sound");
		const RecipeNode rune = MerchantRecipe(203, 1, 1, 30000, "Rojan the Penitent", "Earthshake Basin, Frostgorge Sound");
		const RecipeNode vendorRoot = Recipe(100, 1, 1, { icy, ingot, rune }, "Weaponsmith");
		const Plan vendorPlan = BuildPlan(vendorRoot, 1, items, prices, currencies, {}, options, {});
		// Each place is worked through once: both purchases happen in a single
		// merchant visit and both crafts in a single station visit.
		const std::vector<CraftStepGroup> vendorGroups = BuildStepGroups(vendorPlan);
		Check(vendorGroups.size() == 2, "a merchant and a station are one visit each");
		if (vendorGroups.size() == 2)
		{
			Check(vendorGroups[0].Action == CraftAction::Vendor && vendorGroups[0].Title == "Rojan the Penitent", "the merchant visit leads the list");
			Check(vendorGroups[0].Detail == "Earthshake Basin, Frostgorge Sound", "the merchant block keeps the map");
			Check(vendorGroups[0].Cost == 50000, "the merchant block carries the cost of every purchase there");
			Check(vendorGroups[0].Steps.size() == 2, "both purchases happen in the one visit");
			Check(vendorGroups[0].Steps[0].Detail.find("Rojan the Penitent") != std::string::npos, "the vendor step keeps its merchant detail");
			Check(vendorGroups[1].Title == "Weaponsmith" && vendorGroups[1].Visit == 1, "the station is visited once");
			Check(vendorGroups[1].Steps.size() == 2, "both crafts happen in the one station visit");
			Check(vendorGroups[1].Steps[0].Name == "Ingot" && vendorGroups[1].Steps[1].Name == "Gift", "the station keeps dependency order");
		}

		// Every row carries what it consumes and, for a merchant, where they are:
		// the craft needs its ore, the purchase carries the map and the coin price.
		const std::vector<CraftStep> detailedSteps = BuildCraftingSteps(vendorPlan);
		bool sawCraftNeeds = false;
		bool sawMerchantDetail = false;
		for (const auto& step : detailedSteps)
		{
			if (step.Id == 200)
			{
				sawCraftNeeds = step.Needs.size() == 1 && step.Needs[0].Name == "Ore" && step.Needs[0].Amount == 4;
			}
			if (step.Id == 202)
			{
				// Two runestones at one gold each, so the row says what the merchant
				// charges for the whole purchase.
				sawMerchantDetail = step.Location == "Earthshake Basin, Frostgorge Sound"
					&& step.Needs.size() == 1 && step.Needs[0].Name == "Coin" && step.Needs[0].Amount == 20000;
			}
		}
		Check(sawCraftNeeds, "a craft step lists the ingredients it consumes");
		Check(sawMerchantDetail, "a merchant step carries the location and what the merchant charges");

		const Plan empty;
		Check(BuildStepGroups(empty).empty(), "an empty plan has no step groups");
	}

	// A craft that only has to be made because an earlier goal used up the stock
	// still has to be listed before every line that consumes it.
	void TestStepsFollowDependencies()
	{
		ItemDb items;
		items[1].Id = 1;
		items[1].Name = "Ore";
		items[100].Id = 100;
		items[100].Name = "Gift";
		items[101].Id = 101;
		items[101].Name = "Staff";
		items[200].Id = 200;
		items[200].Name = "Ingot";
		items[300].Id = 300;
		items[300].Name = "Plate";

		PriceDb prices;
		prices[1].Id = 1;
		prices[1].Sell = 100;

		PlanOptions options;
		options.PreferBuyingTradable = false;

		const RecipeNode ingot = Recipe(200, 1, 1, { ItemNode(1, 2) }, "Weaponsmith");
		const RecipeNode plate = Recipe(300, 1, 1, { ingot }, "Armorsmith");
		const std::vector<RecipeNode> roots = {
			Recipe(100, 1, 1, { plate }, "Weaponsmith"),
			Recipe(101, 1, 1, { plate }, "Weaponsmith")
		};

		// One ingot in the bank covers the first goal's plate, so that goal never
		// crafts one; the second goal must craft the plate and, with it, the ingot.
		AccountData account;
		account.Valid = true;
		account.Items[200] = 1;
		const Plan combined = BuildCombinedPlan(roots, items, prices, {}, account, options, {});

		const std::vector<CraftStep> steps = BuildCraftingSteps(combined);
		Check(steps.size() == 4, "every craft of both goals is listed once");
		if (steps.size() == 4)
		{
			Check(steps[0].Name == "Ingot", "an ingredient first made for the second goal still leads the list");
			Check(steps[1].Name == "Plate", "the plate that needs the ingot follows it");
			Check(steps[2].Name == "Gift" && steps[3].Name == "Staff", "the goal crafts close the list in goal order");
		}

		const std::vector<CraftStepGroup> groups = BuildStepGroups(combined);
		Check(groups.size() == 3, "the blocks follow the dependency order");
		if (groups.size() == 3)
		{
			Check(groups[0].Title == "Weaponsmith" && groups[0].Steps.size() == 1 && groups[0].Steps[0].Name == "Ingot", "the shared ingredient opens the list");
			Check(groups[1].Title == "Armorsmith" && groups[1].Steps[0].Name == "Plate", "the plate is crafted at its own station");
			Check(groups[2].Title == "Weaponsmith" && groups[2].Steps.size() == 2, "both goal crafts close the list at the weaponsmith");
			Check(groups[2].Visit == 2, "a place that has to be worked twice is marked as a later visit");
		}
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

	// A merchant exchange paid in items puts what is handed over on the list; the
	// item itself is not listed again, since its ingredients are already there.
	void TestVendorExchangeWithListedIngredients()
	{
		ItemDb items;
		items[100].Id = 100;
		items[100].Name = "Mystic Gift";
		items[300].Id = 300;
		items[300].Name = "Mystic Runestone";
		items[1].Id = 1;
		items[1].Name = "Orichalcum Ingot";

		PriceDb prices;
		prices[1].Id = 1;
		prices[1].Sell = 100;

		RecipeNode exchange;
		exchange.Id = 300;
		exchange.Type = "Recipe";
		exchange.Quantity = 2;
		exchange.Output = 1;
		exchange.Disciplines = { "Merchant" };
		exchange.MerchantName = "Miyani";
		exchange.MerchantLocations = "Lion's Arch";
		exchange.Components = { ItemNode(1, 2) };

		const RecipeNode root = Recipe(100, 1, 1, { exchange }, "Weaponsmith");
		PlanOptions options;
		options.PreferBuyingTradable = false;
		const Plan plan = BuildPlan(root, 1, items, prices, {}, {}, options, {});

		Check(plan.RemainingCost == 400, "a merchant exchange is costed from what it consumes");
		bool foundItem = false;
		bool foundIngredient = false;
		for (const auto& entry : plan.Shopping)
		{
			foundItem = foundItem || entry.Id == 300;
			if (entry.Id == 1)
			{
				foundIngredient = true;
				Check(entry.Quantity == 4 && entry.TotalCost == 400, "the exchange ingredients are listed for the whole batch");
			}
		}
		Check(foundIngredient, "the ingredients a merchant exchange consumes appear on the list");
		Check(!foundItem, "an item whose ingredients are listed is not listed again");

		// A merchant that just charges coin keeps its own line: the coin it asks
		// for is not an item on the shopping list.
		const RecipeNode coin = MerchantRecipe(301, 1, 1, 5000, "Rojan the Penitent", "Earthshake Basin");
		items[301].Id = 301;
		items[301].Name = "Icy Runestone";
		const Plan paid = BuildPlan(Recipe(100, 1, 1, { coin }, "Weaponsmith"), 1, items, prices, {}, {}, options, {});
		bool foundCoinItem = false;
		for (const auto& entry : paid.Shopping)
		{
			foundCoinItem = foundCoinItem || entry.Id == 301;
		}
		Check(foundCoinItem, "an item bought with coin stays on the shopping list");
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
	TestVendorExchangeWithListedIngredients();
	TestAchievementIngredients();
	TestResearchNoteCost();
	TestResearchNotesFromSourceRecipe();
	TestNoteYieldAndCheapestSource();
	TestStepGroups();
	TestStepsFollowDependencies();
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
