#include "Planner.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace mc
{
	namespace
	{
		struct PlanContext
		{
			const ItemDb& Items;
			const PriceDb& Prices;
			const CurrencyDb& Currencies;
			const AccountData& Account;
			const PlanOptions& Options;
			const std::map<int, Preference>& Preferences;

			std::map<int, int> Pool;
			std::map<int, i64> CurrencyPool;
			std::vector<ShoppingItem> Shopping;
			std::map<int, i64> CurrencyRequired;
			std::map<int, i64> CurrencyNeeded;
			int NodeCount = 0;
			bool Truncated = false;
		};

		i64 UnitPrice(const PlanContext& aContext, int aItemId)
		{
			auto it = aContext.Prices.find(aItemId);
			if (it == aContext.Prices.end())
			{
				return 0;
			}
			return aContext.Options.BuyOrderPrices ? it->second.Buy : it->second.Sell;
		}

		Preference GetPreference(const PlanContext& aContext, int aItemId)
		{
			auto it = aContext.Preferences.find(aItemId);
			return it == aContext.Preferences.end() ? Preference::Auto : it->second;
		}

		SourceKind KindForRecipe(const RecipeNode& aNode)
		{
			for (const auto& discipline : aNode.Disciplines)
			{
				if (discipline == "Mystic Forge") return SourceKind::MysticForge;
				if (discipline == "Merchant") return SourceKind::Vendor;
				if (discipline == "Achievement") return SourceKind::Achievement;
				if (discipline == "Double Click") return SourceKind::Container;
			}
			return SourceKind::Craft;
		}

		std::string RecipeDetail(const RecipeNode& aNode, SourceKind aKind)
		{
			std::string detail;
			switch (aKind)
			{
			case SourceKind::Vendor:
				detail = aNode.MerchantName.empty() ? "Merchant" : aNode.MerchantName;
				if (!aNode.MerchantLocations.empty())
				{
					detail += " (" + aNode.MerchantLocations + ")";
				}
				break;
			case SourceKind::Achievement:
				detail = aNode.AchievementId != 0
					? "Achievement #" + std::to_string(aNode.AchievementId)
					: "Achievement";
				break;
			case SourceKind::MysticForge:
				detail = "Mystic Forge";
				break;
			case SourceKind::Container:
				detail = "Contained in / unlocked by an item";
				break;
			default:
				detail = aNode.Disciplines.empty() ? "Craft" : Join(aNode.Disciplines, ", ");
				break;
			}

			if (aNode.MultipleRecipes > 1)
			{
				detail += " (1 of " + std::to_string(aNode.MultipleRecipes) + " sources)";
			}
			if (aNode.DailyCap > 0)
			{
				detail += " - " + std::to_string(aNode.DailyCap) + "/day";
			}
			else if (aNode.WeeklyCap > 0)
			{
				detail += " - " + std::to_string(aNode.WeeklyCap) + "/week";
			}
			return detail;
		}

		std::string CurrencyPriceText(const PlanContext& aContext, int aCurrencyId, i64 aAmount)
		{
			auto it = aContext.Currencies.find(aCurrencyId);
			const std::string name = it != aContext.Currencies.end() && !it->second.Name.empty()
				? it->second.Name
				: "Currency #" + std::to_string(aCurrencyId);
			return FormatNumber(aAmount) + " " + name;
		}

		void AddShoppingRow(
			PlanContext& aContext,
			const PlanNode& aNode,
			SourceKind aSource,
			i64 aUnitCost,
			i64 aTotalCost,
			bool aFromVendor = false,
			bool aFromTradingPost = false)
		{
			ShoppingItem row;
			row.Id = aNode.Id;
			row.Name = aNode.Name;
			row.Icon = aNode.Icon;
			row.Quantity = aNode.Remaining;
			row.UnitCost = aUnitCost;
			row.TotalCost = aTotalCost;
			row.Source = aSource;
			row.SourceDetail = aNode.SourceDetail;
			row.FromVendor = aFromVendor;
			row.FromTradingPost = aFromTradingPost;
			aContext.Shopping.push_back(std::move(row));
		}

		// Lists an ingredient the player has to buy from a merchant, together with
		// the merchant price: coin when the merchant charges coin, otherwise the
		// currency the merchant asks for (karma, laurels, ...).
		void AddVendorRow(PlanContext& aContext, const PlanNode& aNode, const RecipeNode& aRecipe)
		{
			AddShoppingRow(
				aContext,
				aNode,
				SourceKind::Vendor,
				aNode.Remaining > 0 ? aNode.TotalCost / aNode.Remaining : 0,
				aNode.TotalCost,
				true,
				aNode.Tradable);

			// The merchant won, but the item can still be bought with gold, so the
			// shopping line reports the alternative price next to the chosen one.
			if (aNode.Tradable)
			{
				const i64 tradingPostUnit = UnitPrice(aContext, aNode.Id);
				if (tradingPostUnit > 0)
				{
					ShoppingItem& alternative = aContext.Shopping.back();
					alternative.SourceDetail += " - Trading Post " + FormatCoins(tradingPostUnit) + " each";
				}
			}

			if (aRecipe.MerchantCurrencyId == 0 || aRecipe.MerchantCurrencyId == kCoinCurrency)
			{
				return;
			}

			ShoppingItem& row = aContext.Shopping.back();
			row.UnitCost = 0;
			row.TotalCost = 0;
			row.PriceText = CurrencyPriceText(aContext, aRecipe.MerchantCurrencyId, static_cast<i64>(aRecipe.MerchantCurrencyAmount + 0.5));
			row.TotalPriceText = CurrencyPriceText(aContext, aRecipe.MerchantCurrencyId, static_cast<i64>(aRecipe.MerchantCurrencyAmount * aNode.Remaining + 0.5));
		}

		void FillItemMetadata(PlanContext& aContext, PlanNode& aNode, int aItemId)
		{
			auto it = aContext.Items.find(aItemId);
			if (it == aContext.Items.end())
			{
				aNode.Name = "Item #" + std::to_string(aItemId);
				return;
			}

			const ItemInfo& item = it->second;
			aNode.Name = item.Name.empty() ? "Item #" + std::to_string(aItemId) : item.Name;
			aNode.Icon = item.Icon;
			aNode.ChatLink = item.ChatLink;
			aNode.Rarity = item.Rarity;
			aNode.ItemType = ItemTypeLabel(item);
		}

		int CeilToInt(double aValue)
		{
			if (aValue <= 0.0)
			{
				return 0;
			}
			return static_cast<int>(std::ceil(aValue - 1e-9));
		}

		PlanNode BuildNode(PlanContext& aContext, const RecipeNode& aNode, int aDepth)
		{
			PlanNode node;
			node.Id = aNode.Id;
			node.Depth = aDepth;
			node.Output = aNode.Output;
			node.MultipleRecipes = aNode.MultipleRecipes;

			if (aContext.NodeCount >= aContext.Options.MaxNodes || aDepth > aContext.Options.MaxDepth)
			{
				aContext.Truncated = true;
				node.Name = "Item #" + std::to_string(aNode.Id);
				node.Source = SourceKind::Unknown;
				node.SourceDetail = "Tree truncated";
				return node;
			}
			++aContext.NodeCount;

			const int required = CeilToInt(aNode.Quantity);

			if (aNode.IsCurrency())
			{
				node.IsCurrency = true;
				node.Required = required;
				aContext.CurrencyRequired[aNode.Id] += required;
				auto currency = aContext.Currencies.find(aNode.Id);
				node.Name = currency != aContext.Currencies.end() && !currency->second.Name.empty()
					? currency->second.Name
					: "Currency #" + std::to_string(aNode.Id);
				if (currency != aContext.Currencies.end())
				{
					node.Icon = currency->second.Icon;
				}

				if (aContext.Options.IncludeAccountItems)
				{
					const int available = static_cast<int>(aContext.CurrencyPool[aNode.Id]);
					node.Owned = std::min(required, available);
					aContext.CurrencyPool[aNode.Id] -= node.Owned;
				}
				node.Remaining = std::max(0, required - node.Owned);
				node.Complete = node.Remaining == 0;
				node.Source = SourceKind::Currency;
				node.SourceDetail = "Wallet";
				if (aNode.Id == 1)
				{
					node.UnitCost = 1;
					node.TotalCost = node.Remaining;
				}
				if (node.Remaining > 0)
				{
					aContext.CurrencyNeeded[aNode.Id] += node.Remaining;
				}
				return node;
			}

			FillItemMetadata(aContext, node, aNode.Id);
			node.Required = required;

			if (aContext.Options.IncludeAccountItems)
			{
				const int available = aContext.Pool[aNode.Id];
				node.Owned = std::min(required, available);
				aContext.Pool[aNode.Id] -= node.Owned;
			}
			node.Remaining = std::max(0, required - node.Owned);
			node.Complete = node.Remaining == 0;

			if (node.Complete)
			{
				node.SourceDetail = "Already owned";
				return node;
			}

			const i64 unitPrice = UnitPrice(aContext, aNode.Id);
			node.Tradable = unitPrice > 0;

			if (aNode.IsRecipe())
			{
				const int crafts = aNode.Output > 0.0
					? std::max(1, static_cast<int>(std::ceil(static_cast<double>(node.Remaining) / aNode.Output - 1e-9)))
					: node.Remaining;
				node.Crafts = crafts;

				PlanContext scratch = aContext;
				i64 craftCost = 0;
				for (const auto& component : aNode.Components)
				{
					RecipeNode scaled = component;
					scaled.Quantity = component.Quantity * static_cast<double>(crafts);
					PlanNode child = BuildNode(scratch, scaled, aDepth + 1);
					craftCost += child.TotalCost;
					node.Children.push_back(std::move(child));
				}

				const bool vendorNode = KindForRecipe(aNode) == SourceKind::Vendor;
				const Preference preference = GetPreference(aContext, aNode.Id);
				const i64 buyCost = unitPrice * node.Remaining;
				bool buy = false;
				if (unitPrice > 0)
				{
					if (preference == Preference::Buy)
					{
						buy = true;
					}
					else if (preference == Preference::Auto)
					{
						// Buying from a merchant and buying from the Trading Post are both
						// purchases, so the cheaper of the two is always used. Crafting
						// still follows the user's preference.
						buy = vendorNode
							? buyCost <= craftCost
							: (aContext.Options.PreferBuyingTradable && buyCost < craftCost);
					}
				}

				if (buy)
				{
					node.Children.clear();
					node.Source = SourceKind::TradingPost;
					node.SourceDetail = vendorNode && !aNode.MerchantName.empty()
						? "Buy from Trading Post - cheaper than " + aNode.MerchantName
						: "Buy from Trading Post";
					node.UnitCost = unitPrice;
					node.TotalCost = buyCost;
					node.Purchasable = true;
					AddShoppingRow(aContext, node, SourceKind::TradingPost, unitPrice, buyCost, vendorNode, true);
				}
				else
				{
					aContext.Pool = std::move(scratch.Pool);
					aContext.CurrencyPool = std::move(scratch.CurrencyPool);
					aContext.Shopping = std::move(scratch.Shopping);
					aContext.CurrencyRequired = std::move(scratch.CurrencyRequired);
					aContext.CurrencyNeeded = std::move(scratch.CurrencyNeeded);
					aContext.NodeCount = scratch.NodeCount;
					aContext.Truncated = scratch.Truncated;
					node.Source = KindForRecipe(aNode);
					node.Discipline = aNode.Disciplines.empty() ? "" : aNode.Disciplines.front();
					node.SourceDetail = RecipeDetail(aNode, node.Source);
					node.TotalCost = craftCost;
					node.UnitCost = node.Remaining > 0 ? craftCost / node.Remaining : 0;
					node.Crafts = crafts;
					if (node.Source == SourceKind::Vendor)
					{
						AddVendorRow(aContext, node, aNode);
					}
				}
				return node;
			}

			if (unitPrice > 0)
			{
				node.Source = SourceKind::TradingPost;
				node.SourceDetail = "Buy from Trading Post";
				node.UnitCost = unitPrice;
				node.TotalCost = unitPrice * node.Remaining;
				node.Purchasable = true;
				AddShoppingRow(aContext, node, SourceKind::TradingPost, unitPrice, node.TotalCost, !aNode.MerchantName.empty(), true);
				return node;
			}

			if (aNode.AchievementId != 0 || KindForRecipe(aNode) == SourceKind::Achievement)
			{
				node.Source = SourceKind::Achievement;
				node.SourceDetail = aNode.AchievementId != 0
					? "Achievement #" + std::to_string(aNode.AchievementId)
					: "Achievement";
				// Nothing to buy, but the ingredient still belongs on the shopping list.
				AddShoppingRow(aContext, node, SourceKind::Achievement, 0, 0);
			}
			else if (!aNode.MerchantName.empty())
			{
				node.Source = SourceKind::Vendor;
				node.SourceDetail = aNode.MerchantName;
				if (!aNode.MerchantLocations.empty())
				{
					node.SourceDetail += " (" + aNode.MerchantLocations + ")";
				}
				AddVendorRow(aContext, node, aNode);
			}
			else
			{
				node.Source = SourceKind::Unknown;
				node.SourceDetail = "Gathered or obtained another way";
			}

			return node;
		}

		std::vector<ShoppingItem> AggregateShopping(const std::vector<ShoppingItem>& aEntries)
		{
			std::map<int, ShoppingItem> aggregated;
			for (const auto& entry : aEntries)
			{
				auto& target = aggregated[entry.Id];
				if (target.Id == 0)
				{
					target = entry;
					target.Quantity = 0;
					target.TotalCost = 0;
				}
				target.Quantity += entry.Quantity;
				target.TotalCost += entry.TotalCost;
			}

			std::vector<ShoppingItem> result;
			result.reserve(aggregated.size());
			for (auto& pair : aggregated)
			{
				result.push_back(std::move(pair.second));
			}
			std::sort(result.begin(), result.end(), [](const ShoppingItem& a, const ShoppingItem& b) {
				if (a.TotalCost != b.TotalCost)
				{
					return a.TotalCost > b.TotalCost;
				}
				return a.Name < b.Name;
			});
			return result;
		}

		// Research notes are a wallet currency earned by crafting and salvaging
		// gear, so the plan can optionally price the notes it still needs.
		void ApplyResearchNotes(Plan& aPlan, PlanContext& aContext, i64 aRequired, i64 aFullRequired)
		{
			aPlan.ResearchNotesRequired = aRequired;
			aPlan.ResearchNotesNeeded = aContext.CurrencyNeeded[kResearchNoteCurrency];
			if (!aContext.Options.IncludeResearchNotes || aContext.Options.ResearchNoteUnitCost <= 0 || aPlan.ResearchNotesNeeded <= 0)
			{
				return;
			}

			const i64 cost = aPlan.ResearchNotesNeeded * aContext.Options.ResearchNoteUnitCost;
			aPlan.ResearchNoteCost = cost;
			aPlan.RemainingCost += cost;
			aPlan.FullCost += aFullRequired * aContext.Options.ResearchNoteUnitCost;

			ShoppingItem row;
			row.Id = kResearchNoteCurrency;
			row.Name = "Research Note";
			row.Quantity = static_cast<int>(aPlan.ResearchNotesNeeded);
			row.UnitCost = aContext.Options.ResearchNoteUnitCost;
			row.TotalCost = cost;
			row.Source = SourceKind::Currency;
			row.Currency = true;
			row.SourceDetail = "Crafted and salvaged with a research kit";
			auto currency = aContext.Currencies.find(kResearchNoteCurrency);
			if (currency != aContext.Currencies.end())
			{
				if (!currency->second.Name.empty())
				{
					row.Name = currency->second.Name;
				}
				row.Icon = currency->second.Icon;
			}
			aContext.Shopping.push_back(std::move(row));
		}
	}

	Plan BuildPlan(
		const RecipeNode& aRoot,
		int aGoalQuantity,
		const ItemDb& aItems,
		const PriceDb& aPrices,
		const CurrencyDb& aCurrencies,
		const AccountData& aAccount,
		const PlanOptions& aOptions,
		const std::map<int, Preference>& aPreferences)
	{
		Plan plan;
		plan.GoalId = aRoot.Id;
		plan.BuiltAt = NowUnix();

		auto info = aItems.find(aRoot.Id);
		if (info != aItems.end())
		{
			plan.GoalName = info->second.Name;
			plan.GoalIcon = info->second.Icon;
			plan.GoalType = ItemTypeLabel(info->second);
			plan.GoalRarity = info->second.Rarity;
		}
		else
		{
			plan.GoalName = "Item #" + std::to_string(aRoot.Id);
		}

		RecipeNode root = aRoot;
		root.Quantity = std::max(1, aGoalQuantity);

		PlanContext context{
			aItems,
			aPrices,
			aCurrencies,
			aAccount,
			aOptions,
			aPreferences,
			{},
			{},
			{},
			{},
			{},
			0,
			false
		};

		if (aOptions.IncludeAccountItems)
		{
			context.Pool = aAccount.Items;
			context.CurrencyPool = aAccount.Currencies;
		}

		plan.Root = BuildNode(context, root, 0);
		plan.RemainingCost = plan.Root.TotalCost;
		plan.CurrencyRequired = context.CurrencyRequired;
		plan.CurrencyNeeded = context.CurrencyNeeded;
		plan.CurrencyOwned = aAccount.Currencies;
		plan.NodeCount = context.NodeCount;
		plan.Complete = plan.Root.Complete;

		// A second pass from scratch gives the denominator for progress and the
		// "total cost if you owned nothing" figure.
		PlanOptions fullOptions = aOptions;
		fullOptions.IncludeAccountItems = false;
		AccountData emptyAccount;
		PlanContext fullContext{
			aItems,
			aPrices,
			aCurrencies,
			emptyAccount,
			fullOptions,
			aPreferences,
			{},
			{},
			{},
			{},
			{},
			0,
			false
		};
		const PlanNode fullRoot = BuildNode(fullContext, root, 0);
		plan.FullCost = fullRoot.TotalCost;

		ApplyResearchNotes(plan, context, context.CurrencyRequired[kResearchNoteCurrency], fullContext.CurrencyRequired[kResearchNoteCurrency]);
		plan.Shopping = AggregateShopping(context.Shopping);

		return plan;
	}

	namespace
	{
		CraftAction ActionForNode(const PlanNode& aNode)
		{
			if (aNode.IsCurrency)
			{
				return CraftAction::Gather;
			}
			switch (aNode.Source)
			{
			case SourceKind::TradingPost: return CraftAction::Buy;
			case SourceKind::Vendor: return CraftAction::Vendor;
			case SourceKind::MysticForge: return CraftAction::MysticForge;
			case SourceKind::Achievement: return CraftAction::Achievement;
			case SourceKind::Container: return CraftAction::Obtain;
			case SourceKind::Unknown: return CraftAction::Obtain;
			case SourceKind::Craft:
			default: return CraftAction::Craft;
			}
		}

		void CollectSteps(const PlanNode& aNode, std::vector<CraftStep>& aOut)
		{
			if (aNode.Remaining <= 0)
			{
				return;
			}

			// Coin is already counted in the gold totals, so it is not a separate step.
			if (aNode.IsCurrency && aNode.Id == 1)
			{
				return;
			}

			// Sub-components are crafted or bought first, so the list is safe to follow top to bottom.
			for (const auto& child : aNode.Children)
			{
				CollectSteps(child, aOut);
			}

			CraftStep step;
			step.Id = aNode.Id;
			step.Currency = aNode.IsCurrency;
			step.Name = aNode.Name;
			step.Icon = aNode.Icon;
			step.ChatLink = aNode.ChatLink;
			step.Detail = aNode.SourceDetail;
			step.Action = ActionForNode(aNode);
			step.Remaining = aNode.Remaining;
			step.Owned = aNode.Owned;
			step.Required = aNode.Required;
			step.OutputPerCraft = aNode.Output;

			// Buying from the Trading Post and gathering currency need no extra explanation.
			if (step.Action == CraftAction::Buy || step.Action == CraftAction::Gather)
			{
				step.Detail.clear();
			}
			// A recipe can be known by many disciplines; the first one is enough for a step.
			else if (step.Action == CraftAction::Craft && !aNode.Discipline.empty())
			{
				step.Detail = aNode.Discipline;
			}

			const bool crafted = step.Action == CraftAction::Craft || step.Action == CraftAction::MysticForge || step.Action == CraftAction::Vendor;
			step.Quantity = crafted && aNode.Crafts > 0 ? aNode.Crafts : aNode.Remaining;

			if (step.Action == CraftAction::Buy || step.Action == CraftAction::Vendor)
			{
				step.Cost = aNode.TotalCost;
			}

			aOut.push_back(std::move(step));
		}
	}

	// Linearizes a plan into dependency-ordered actions: ingredients first, the
	// goal last. A combined plan lists the steps of every tracked goal in turn.
	std::vector<CraftStep> BuildCraftingSteps(const Plan& aPlan)
	{
		std::vector<CraftStep> steps;
		if (!aPlan.GoalRoots.empty())
		{
			for (const auto& goal : aPlan.GoalRoots)
			{
				CollectSteps(goal, steps);
			}
		}
		else
		{
			CollectSteps(aPlan.Root, steps);
		}
		return steps;
	}

	Plan BuildCombinedPlan(
		const std::vector<RecipeNode>& aRoots,
		const ItemDb& aItems,
		const PriceDb& aPrices,
		const CurrencyDb& aCurrencies,
		const AccountData& aAccount,
		const PlanOptions& aOptions,
		const std::map<int, Preference>& aPreferences)
	{
		Plan combined;
		combined.BuiltAt = NowUnix();
		combined.GoalCount = static_cast<int>(aRoots.size());

		if (aRoots.empty())
		{
			combined.GoalName = "No tracked plans";
			return combined;
		}

		// Owned materials are consumed in goal order; each plan only sees what is
		// left after the previous goals, so shared ingredients are not double-counted.
		AccountData pool = aAccount;
		std::vector<ShoppingItem> entries;
		std::vector<std::string> names;

		for (const auto& root : aRoots)
		{
			const Plan plan = BuildPlan(root, 1, aItems, aPrices, aCurrencies, pool, aOptions, aPreferences);
			combined.RemainingCost += plan.RemainingCost;
			combined.FullCost += plan.FullCost;
			combined.NodeCount += plan.NodeCount;
			combined.ResearchNotesRequired += plan.ResearchNotesRequired;
			combined.ResearchNotesNeeded += plan.ResearchNotesNeeded;
			combined.ResearchNoteCost += plan.ResearchNoteCost;
			combined.GoalRoots.push_back(plan.Root);
			entries.insert(entries.end(), plan.Shopping.begin(), plan.Shopping.end());
			if (!plan.GoalName.empty())
			{
				names.push_back(plan.GoalName);
			}

			for (const auto& pair : plan.CurrencyRequired)
			{
				combined.CurrencyRequired[pair.first] += pair.second;
			}
			for (const auto& pair : plan.CurrencyNeeded)
			{
				combined.CurrencyNeeded[pair.first] += pair.second;
			}

			std::function<void(const PlanNode&)> consume = [&](const PlanNode& aNode) {				if (aNode.Owned > 0)
				{
					if (aNode.IsCurrency)
					{
						auto it = pool.Currencies.find(aNode.Id);
						if (it != pool.Currencies.end())
						{
							it->second = std::max<i64>(0, it->second - aNode.Owned);
						}
					}
					else
					{
						auto it = pool.Items.find(aNode.Id);
						if (it != pool.Items.end())
						{
							it->second = std::max(0, it->second - aNode.Owned);
						}
					}
				}
				for (const auto& child : aNode.Children)
				{
					consume(child);
				}
			};
			consume(plan.Root);
		}

		combined.Shopping = AggregateShopping(entries);
		combined.CurrencyOwned = aAccount.Currencies;
		combined.Complete = combined.RemainingCost == 0;
		if (names.size() == 1)
		{
			combined.GoalName = names.front();
		}
		else
		{
			combined.GoalName = std::to_string(names.size()) + " tracked plans";
		}
		return combined;
	}
}
