#include "Planner.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace mc
{
	// Declared here so the step collector, which lives in the anonymous namespace
	// below, can label a merchant step with the map this splits out.
	void SplitPlaceDetail(const std::string& aDetail, std::string& aPlace, std::string& aWhere);

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

		// The merchant line without the "(1 of N sources)" suffix, so a step can
		// name the merchant and map that the block header splits apart again.
		std::string MerchantLabel(const RecipeNode& aNode)
		{
			std::string label = aNode.MerchantName.empty() ? "Merchant" : aNode.MerchantName;
			if (!aNode.MerchantLocations.empty())
			{
				label += " (" + aNode.MerchantLocations + ")";
			}
			return label;
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
						node.MerchantDetail = MerchantLabel(aNode);
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
				node.MerchantDetail = MerchantLabel(aNode);
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

		// Research notes are a wallet currency earned by crafting at a crafting
		// station, so the plan prices the notes it still needs from the cost of
		// crafting the recipe that gets researched. The configured price per note
		// is the fallback when no source recipe is available.
		void ApplyResearchNotes(Plan& aPlan, PlanContext& aContext, i64 aRequired, i64 aFullRequired)
		{
			aPlan.ResearchNotesRequired = aRequired;
			aPlan.ResearchNotesNeeded = aContext.CurrencyNeeded[kResearchNoteCurrency];
			const i64 needed = aPlan.ResearchNotesNeeded;
			if (!aContext.Options.IncludeResearchNotes || needed <= 0)
			{
				return;
			}

			// Every candidate source is costed for the notes still missing, and the
			// one with the lowest cost per note wins.
			PlanOptions sourceOptions = aContext.Options;
			sourceOptions.IncludeResearchNotes = false;
			sourceOptions.IncludeAccountItems = false;
			sourceOptions.ResearchNoteSources = nullptr;

			const RecipeNode* bestSource = nullptr;
			Plan bestPlan;
			int bestYield = 0;
			i64 bestCrafts = 0;
			i64 bestUnitCost = 0;
			if (aContext.Options.ResearchNoteSources)
			{
				for (const RecipeNode& candidate : *aContext.Options.ResearchNoteSources)
				{
					if (candidate.Id == 0 || !candidate.IsRecipe())
					{
						continue;
					}
					auto info = aContext.Items.find(candidate.Id);
					if (info == aContext.Items.end())
					{
						continue;
					}

					const int candidateYield = ResearchNoteYieldFor(info->second.Level, info->second.Rarity);
					const i64 candidateCrafts = (needed + candidateYield - 1) / candidateYield;

					// Notes only come from crafting the item at a station, so buying
					// the finished item is never the route priced here.
					std::map<int, Preference> sourcePreferences = aContext.Preferences;
					sourcePreferences[candidate.Id] = Preference::Craft;

					const Plan candidatePlan = BuildPlan(
						candidate,
						static_cast<int>(std::min<i64>(candidateCrafts, std::numeric_limits<int>::max())),
						aContext.Items,
						aContext.Prices,
						aContext.Currencies,
						AccountData{},
						sourceOptions,
						sourcePreferences);

					if (candidatePlan.FullCost <= 0)
					{
						continue;
					}
					const i64 candidateUnit = candidatePlan.FullCost / (candidateCrafts * candidateYield);
					if (bestSource == nullptr || candidateUnit < bestUnitCost)
					{
						bestSource = &candidate;
						bestPlan = candidatePlan;
						bestYield = candidateYield;
						bestCrafts = candidateCrafts;
						bestUnitCost = candidateUnit;
					}
				}
			}

			i64 unitCost = 0;
			std::string sourceName;
			std::vector<ShoppingItem> ingredients;
			const bool priced = bestSource != nullptr;
			if (priced)
			{
				sourceName = bestPlan.GoalName;
				unitCost = bestPlan.FullCost / needed;
				aPlan.ResearchNoteCost = bestPlan.FullCost;
				aPlan.RemainingCost += bestPlan.FullCost;
				aPlan.FullCost += (bestPlan.FullCost / std::max<i64>(1, bestCrafts)) * ((aFullRequired + bestYield - 1) / bestYield);
				aPlan.ResearchNoteSourceName = sourceName;
				aPlan.ResearchNoteYield = bestYield;
				aPlan.ResearchNoteCrafts = static_cast<int>(std::min<i64>(bestCrafts, std::numeric_limits<int>::max()));
				// The rows are flagged so the interface can mark them as being there
				// only to produce research notes, and the craft itself joins the
				// crafting steps so the checklist stays complete.
				ingredients = bestPlan.Shopping;
				for (auto& row : ingredients)
				{
					row.ResearchNotes = true;
				}
				aPlan.NoteRoots.push_back(bestPlan.Root);
			}

			ShoppingItem row;
			row.Id = kResearchNoteCurrency;
			row.Name = "Research Note";
			row.Quantity = static_cast<int>(needed);
			row.UnitCost = unitCost;
			row.TotalCost = aPlan.ResearchNoteCost;
			row.Source = SourceKind::Currency;
			row.Currency = true;
			row.ResearchNotes = true;
			row.SourceDetail = priced
				? "Crafted at a crafting station from " + sourceName + " (" + std::to_string(bestYield) + " notes per craft)"
				: "Crafted at a crafting station and converted into research notes";
			auto currency = aContext.Currencies.find(kResearchNoteCurrency);
			if (currency != aContext.Currencies.end())
			{
				if (!currency->second.Name.empty())
				{
					row.Name = currency->second.Name;
				}
				row.Icon = currency->second.Icon;
			}

			// The notes first, then the ingredients that produce them; both are
			// appended after the list is aggregated so they stay on their own lines.
			aPlan.Shopping.push_back(std::move(row));
			for (auto& ingredient : ingredients)
			{
				aPlan.Shopping.push_back(std::move(ingredient));
			}
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

		plan.Shopping = AggregateShopping(context.Shopping);
		// Research note lines are appended after aggregation so their own cost
		// lines never merge with the ingredient they happen to share an id with.
		ApplyResearchNotes(plan, context, context.CurrencyRequired[kResearchNoteCurrency], fullContext.CurrencyRequired[kResearchNoteCurrency]);

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

		// The checklist keeps the work the player performs: crafts, Mystic Forge
		// combines and the merchant purchases that cannot be crafted. Trading Post
		// buying and wallet top-ups stay on the shopping list, since the checklist
		// assumes those ingredients are already in the bag.
		bool IsListedNode(const PlanNode& aNode)
		{
			switch (ActionForNode(aNode))
			{
			case CraftAction::Craft:
			case CraftAction::MysticForge:
			case CraftAction::Vendor:
				return true;
			default:
				return false;
			}
		}

		CraftStep StepForNode(const PlanNode& aNode, bool aForResearchNotes)
		{
			CraftStep step;
			step.Id = aNode.Id;
			step.Currency = aNode.IsCurrency;
			step.Name = aNode.Name;
			step.Icon = aNode.Icon;
			step.ChatLink = aNode.ChatLink;
			step.Action = ActionForNode(aNode);
			step.Remaining = aNode.Remaining;
			step.Owned = aNode.Owned;
			step.Required = aNode.Required;
			step.OutputPerCraft = aNode.Output;
			step.ResearchNotes = aForResearchNotes;

			switch (step.Action)
			{
			case CraftAction::Vendor:
				// A merchant line keeps the merchant, the map and the coin cost,
				// so the block below it says who to visit and what it costs.
				step.Detail = aNode.MerchantDetail.empty() ? aNode.SourceDetail : aNode.MerchantDetail;
				step.Quantity = aNode.Remaining;
				step.Cost = aNode.TotalCost;
				break;
			case CraftAction::Craft:
				// A recipe can be known by many disciplines; the first is enough.
				step.Detail = aNode.Discipline;
				step.Quantity = aNode.Crafts > 0 ? aNode.Crafts : aNode.Remaining;
				break;
			default:
				step.Quantity = aNode.Crafts > 0 ? aNode.Crafts : aNode.Remaining;
				break;
			}

			// What the step consumes: the ingredients of a craft, or the coins and
			// currencies a merchant asks for. The row then stands on its own.
			for (const auto& child : aNode.Children)
			{
				const int amount = child.Required > 0 ? child.Required : child.Remaining;
				if (amount <= 0 || child.Name.empty())
				{
					continue;
				}
				StepNeed need;
				need.Name = child.Name;
				need.Amount = amount;
				step.Needs.push_back(std::move(need));
			}

			// A purchase says where the merchant stands, since finding them is the
			// part of the errand the plan does not otherwise spell out.
			if (step.Action == CraftAction::Vendor)
			{
				std::string place;
				SplitPlaceDetail(
					aNode.MerchantDetail.empty() ? aNode.SourceDetail : aNode.MerchantDetail,
					place,
					step.Location);
			}

			return step;
		}

		// Identifies one line of the checklist, so a craft that several goals need
		// becomes a single line instead of one line per goal.
		struct StepKey
		{
			int Id = 0;
			int Action = 0;

			bool operator<(const StepKey& aOther) const
			{
				return Id != aOther.Id ? Id < aOther.Id : Action < aOther.Action;
			}
		};

		// The checklist with the dependencies between its lines, so the caller can
		// order the work and group it into visits.
		struct StepList
		{
			std::vector<CraftStep> Steps;
			std::vector<std::vector<std::size_t>> Dependencies; // indices into Steps
		};

		// Walks the plan tree and records, for every line, the lines that have to
		// be done before it. The checklist is then ordered from those dependencies
		// rather than from the shape of the walk, so an ingredient never lands
		// after the item that consumes it - not even when the ingredient only has
		// to be made for a later goal, because an earlier one used up the stock.
		class StepCollector
		{
		public:
			StepList Collect(const Plan& aPlan)
			{
				// Research notes are an ingredient of the goal, so the crafts that
				// produce them come before the goal's own work.
				for (const auto& noteRoot : aPlan.NoteRoots)
				{
					CollectNode(noteRoot, true);
				}
				if (!aPlan.GoalRoots.empty())
				{
					for (const auto& goal : aPlan.GoalRoots)
					{
						CollectNode(goal, false);
					}
				}
				else
				{
					CollectNode(aPlan.Root, false);
				}
				return Order();
			}

		private:
			struct Entry
			{
				CraftStep Step;
				std::vector<StepKey> Dependencies;
			};

			std::vector<Entry> Entries;
			std::map<StepKey, std::size_t> Positions;

			static void AddKey(std::vector<StepKey>& aKeys, const StepKey& aKey)
			{
				for (const StepKey& key : aKeys)
				{
					if (!(key < aKey) && !(aKey < key))
					{
						return;
					}
				}
				aKeys.push_back(aKey);
			}

			// Returns every line this node needs, which is what its parent has to wait
			// for. A node the checklist does not list passes its children's lines up.
			std::vector<StepKey> CollectNode(const PlanNode& aNode, bool aForResearchNotes)
			{
				if (aNode.Remaining <= 0)
				{
					return {};
				}

				std::vector<StepKey> provided;
				for (const auto& child : aNode.Children)
				{
					for (const StepKey& key : CollectNode(child, aForResearchNotes))
					{
						AddKey(provided, key);
					}
				}

				if (!IsListedNode(aNode))
				{
					return provided;
				}

				const StepKey key{aNode.Id, static_cast<int>(ActionForNode(aNode))};
				const CraftStep step = StepForNode(aNode, aForResearchNotes);
				auto found = Positions.find(key);
				if (found == Positions.end())
				{
					Positions[key] = Entries.size();
					Entry entry;
					entry.Step = step;
					entry.Dependencies = provided;
					Entries.push_back(std::move(entry));
				}
				else
				{
					// The same craft can be needed by more than one goal: it is listed
					// once, with the amounts and the dependencies added together.
					Entry& entry = Entries[found->second];
					entry.Step.Quantity += step.Quantity;
					entry.Step.Remaining += step.Remaining;
					entry.Step.Required += step.Required;
					entry.Step.Owned += step.Owned;
					entry.Step.Cost += step.Cost;
					entry.Step.ResearchNotes = entry.Step.ResearchNotes && step.ResearchNotes;
					if (entry.Step.Location.empty())
					{
						entry.Step.Location = step.Location;
					}
					for (const StepNeed& need : step.Needs)
					{
						StepNeed* known = nullptr;
						for (StepNeed& existing : entry.Step.Needs)
						{
							if (existing.Name == need.Name)
							{
								known = &existing;
								break;
							}
						}
						if (known != nullptr)
						{
							known->Amount += need.Amount;
						}
						else
						{
							entry.Step.Needs.push_back(need);
						}
					}
					for (const StepKey& dependency : provided)
					{
						AddKey(entry.Dependencies, dependency);
					}
				}

				AddKey(provided, key);
				return provided;
			}

			// Dependency order: a line comes out once every line it needs is out.
			// Among the lines that are ready, the plan's own order wins, so the walk
			// above still decides the sequence wherever dependencies allow it. The
			// dependencies come back with the lines, remapped to the final order, so
			// the work can also be folded into visits.
			StepList Order() const
			{
				const std::size_t count = Entries.size();
				std::vector<int> waiting(count, 0);
				std::vector<std::vector<std::size_t>> dependents(count);
				std::vector<std::vector<std::size_t>> dependencies(count);
				for (std::size_t i = 0; i < count; ++i)
				{
					for (const StepKey& dependency : Entries[i].Dependencies)
					{
						auto found = Positions.find(dependency);
						if (found == Positions.end() || found->second == i)
						{
							continue;
						}
						dependencies[i].push_back(found->second);
						++waiting[i];
						dependents[found->second].push_back(i);
					}
				}

				std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<std::size_t>> ready;
				for (std::size_t i = 0; i < count; ++i)
				{
					if (waiting[i] == 0)
					{
						ready.push(i);
					}
				}

				StepList list;
				list.Steps.reserve(count);
				std::vector<std::size_t> position(count, 0);
				std::vector<bool> emitted(count, false);
				while (!ready.empty())
				{
					const std::size_t index = ready.top();
					ready.pop();
					position[index] = list.Steps.size();
					list.Steps.push_back(Entries[index].Step);
					emitted[index] = true;
					for (std::size_t dependent : dependents[index])
					{
						if (--waiting[dependent] == 0)
						{
							ready.push(dependent);
						}
					}
				}

				// A dependency loop would leave lines behind: they are kept at the end
				// in plan order rather than dropped.
				for (std::size_t i = 0; i < count; ++i)
				{
					if (!emitted[i])
					{
						position[i] = list.Steps.size();
						list.Steps.push_back(Entries[i].Step);
					}
				}

				list.Dependencies.resize(list.Steps.size());
				for (std::size_t i = 0; i < count; ++i)
				{
					for (std::size_t dependency : dependencies[i])
					{
						list.Dependencies[position[i]].push_back(position[dependency]);
					}
				}
				return list;
			}
		};
	}

	// Linearizes a plan into dependency-ordered steps: every ingredient is listed
	// before the item that consumes it, and a combined plan closes with the goal
	// crafts. A craft that several goals need becomes one line with the summed
	// amount, placed where its own ingredients allow it.
	std::vector<CraftStep> BuildCraftingSteps(const Plan& aPlan)
	{
		return StepCollector().Collect(aPlan).Steps;
	}

	// Splits a place label like "Rojan the Penitent (Lions Arch)" into the
	// merchant and the location text that follows it.
	void SplitPlaceDetail(const std::string& aDetail, std::string& aPlace, std::string& aWhere)
	{
		const std::size_t open = aDetail.rfind(" (");
		if (open == std::string::npos || aDetail.back() != ')')
		{
			aPlace = aDetail;
			aWhere.clear();
			return;
		}
		aPlace = aDetail.substr(0, open);
		aWhere = aDetail.substr(open + 2, aDetail.size() - open - 3);
	}

	std::string StepGroupKey(const CraftStep& aStep)
	{
		switch (aStep.Action)
		{
		case CraftAction::Vendor:
		{
			std::string place;
			std::string where;
			SplitPlaceDetail(aStep.Detail, place, where);
			return "vendor:" + (place.empty() ? std::string("Merchant") : place);
		}
		case CraftAction::MysticForge:
			return "forge";
		default:
			return "craft:" + (aStep.Detail.empty() ? std::string("Crafting station") : aStep.Detail);
		}
	}

	// Folds the steps into visits: everything to do at one merchant, crafting
	// station or Mystic Forge becomes a single block, so each place is worked
	// through once. The blocks are ordered so a place that feeds another comes
	// first; whatever is still not ready when its place comes round waits for the
	// next round, which is the only case where a place is visited twice.
	std::vector<CraftStepGroup> BuildStepGroups(const Plan& aPlan)
	{
		const StepList list = StepCollector().Collect(aPlan);
		if (list.Steps.empty())
		{
			return {};
		}

		auto startVisit = [](const CraftStep& aStep, int aVisit) {
			CraftStepGroup group;
			group.Action = aStep.Action;
			group.ResearchNotes = aStep.ResearchNotes;
			group.Visit = aVisit;
			if (aStep.Action == CraftAction::Vendor)
			{
				std::string place;
				SplitPlaceDetail(aStep.Detail, place, group.Detail);
				group.Title = place.empty() ? "Merchant" : place;
			}
			else if (aStep.Action == CraftAction::MysticForge)
			{
				group.Title = "Mystic Forge";
				group.Detail = "Combine the ingredients in the Mystic Forge";
			}
			else
			{
				group.Title = aStep.Detail.empty() ? "Crafting station" : aStep.Detail;
				group.Detail = "Craft at the " + group.Title + " station";
			}
			return group;
		};

		// The place every step happens at, and the places in the order they are
		// first needed.
		std::vector<std::string> keys(list.Steps.size());
		std::vector<std::string> places;
		std::map<std::string, std::size_t> placeIndex;
		std::vector<std::vector<std::size_t>> slots;
		for (std::size_t i = 0; i < list.Steps.size(); ++i)
		{
			keys[i] = StepGroupKey(list.Steps[i]);
			if (placeIndex.find(keys[i]) == placeIndex.end())
			{
				placeIndex[keys[i]] = places.size();
				places.push_back(keys[i]);
				slots.emplace_back();
			}
			slots[placeIndex[keys[i]]].push_back(i);
		}

		// Which place has to be visited before which: a step at one place that
		// needs a step at another puts that other place first.
		const std::size_t placeCount = places.size();
		std::vector<int> waiting(placeCount, 0);
		std::vector<std::vector<std::size_t>> next(placeCount);
		for (std::size_t i = 0; i < list.Steps.size(); ++i)
		{
			// A place waits for every place that makes something its steps need.
			const std::size_t dependent = placeIndex[keys[i]];
			for (std::size_t dependency : list.Dependencies[i])
			{
				const std::size_t provider = placeIndex[keys[dependency]];
				if (provider == dependent)
				{
					continue;
				}
				bool known = false;
				for (std::size_t other : next[provider])
				{
					if (other == dependent)
					{
						known = true;
						break;
					}
				}
				if (known)
				{
					continue;
				}
				next[provider].push_back(dependent);
				++waiting[dependent];
			}
		}

		std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<std::size_t>> ready;
		std::vector<std::size_t> placeOrder;
		placeOrder.reserve(placeCount);
		for (std::size_t i = 0; i < placeCount; ++i)
		{
			if (waiting[i] == 0)
			{
				ready.push(i);
			}
		}
		while (!ready.empty())
		{
			const std::size_t place = ready.top();
			ready.pop();
			placeOrder.push_back(place);
			for (std::size_t other : next[place])
			{
				if (--waiting[other] == 0)
				{
					ready.push(other);
				}
			}
		}
		// Two places that feed each other cannot both go first: the rest keeps its
		// first-need order and the rounds below split those visits.
		for (std::size_t i = 0; i < placeCount; ++i)
		{
			if (std::find(placeOrder.begin(), placeOrder.end(), i) == placeOrder.end())
			{
				placeOrder.push_back(i);
			}
		}

		std::vector<CraftStepGroup> groups;
		std::map<std::string, int> visits;
		std::vector<bool> done(list.Steps.size(), false);
		std::size_t remaining = list.Steps.size();
		while (remaining > 0)
		{
			bool progress = false;
			for (std::size_t place : placeOrder)
			{
				bool started = false;
				for (std::size_t index : slots[place])
				{
					if (done[index])
					{
						continue;
					}
					bool readyNow = true;
					for (std::size_t dependency : list.Dependencies[index])
					{
						if (!done[dependency])
						{
							readyNow = false;
							break;
						}
					}
					if (!readyNow)
					{
						continue;
					}
					if (!started)
					{
						groups.push_back(startVisit(list.Steps[index], visits[keys[index]] + 1));
						++visits[keys[index]];
						started = true;
					}
					CraftStepGroup& group = groups.back();
					group.Cost += list.Steps[index].Cost;
					group.ResearchNotes = group.ResearchNotes && list.Steps[index].ResearchNotes;
					group.Steps.push_back(list.Steps[index]);
					done[index] = true;
					--remaining;
					progress = true;
				}
			}

			if (!progress)
			{
				// A dependency loop would starve the rounds: the rest is listed in plan
				// order, one line per visit, so nothing is dropped.
				for (std::size_t i = 0; i < list.Steps.size(); ++i)
				{
					if (done[i])
					{
						continue;
					}
					CraftStepGroup group = startVisit(list.Steps[i], visits[keys[i]] + 1);
					++visits[keys[i]];
					group.Cost += list.Steps[i].Cost;
					group.Steps.push_back(list.Steps[i]);
					groups.push_back(std::move(group));
					done[i] = true;
					--remaining;
				}
			}
		}

		return groups;
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
			if (combined.ResearchNoteSourceName.empty() && !plan.ResearchNoteSourceName.empty())
			{
				combined.ResearchNoteSourceName = plan.ResearchNoteSourceName;
				combined.ResearchNoteYield = plan.ResearchNoteYield;
			}
			combined.ResearchNoteCrafts += plan.ResearchNoteCrafts;
			for (const auto& noteRoot : plan.NoteRoots)
			{
				combined.NoteRoots.push_back(noteRoot);
			}
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
