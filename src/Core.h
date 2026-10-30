// Mystic Crafting for Nexus - shared types and utilities.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mc
{
	using i64 = std::int64_t;

	// Wallet currency ids the planner treats specially.
	constexpr int kCoinCurrency = 1;
	constexpr int kGuildCommendationCurrency = 16;
	constexpr int kResearchNoteCurrency = 61;

	// Crafted items that are cheap to make and are researched into notes: the
	// cheap fine and masterwork jewellery, the fine, masterwork and rare gear, the
	// level-80 exotics and Peer Reviewed Research, which hands its notes over as it
	// is crafted. The planner prices every one of them and keeps the cheapest.
	const std::vector<int>& ResearchNoteSourceCandidates();

	// Notes a research kit returns for a salvaged crafted item of this level,
	// rarity and type. The figures follow the salvage research the community
	// measured and the wiki records: fine, masterwork and rare gear of level 70
	// and up gives about five notes and level 60 to 69 gear about three, level-80
	// exotic armor and back items about seventy-five, exotic weapons about
	// forty-five, exotic trinkets about eighty, and crafted food, utilities and
	// potions a single note.
	int ResearchNoteYieldFor(int aLevel, const std::string& aRarity, const std::string& aType, const std::string& aSubtype);

	// Measured notes for the items whose salvage research is known item by item
	// (Peer Reviewed Research hands its notes over directly), or zero when the
	// level, rarity and type are the guide.
	int ResearchNoteYieldOverride(int aItemId);

	// Items the acquisition graph does not label as achievements although they are
	// earned rather than crafted or bought - the WvW Gift of Battle reward track is
	// the one every legendary gift needs. Returns what awards the item, or null for
	// everything else.
	const char* AchievementItemDetail(int aItemId);

	// One material a Mystic Forge promotion consumes.
	struct PromotionIngredient
	{
		int ItemId = 0;
		int Count = 0;
	};

	// A Mystic Forge promotion: cheaper materials plus a little dust and a stone
	// turn into more of a dearer material. The acquisition provider lists the
	// core-to-lodestone promotions but has no entry for the fine and rare material
	// ones, so those come from the wiki's promotion tables and the planner weighs
	// them against the Trading Post like any other recipe.
	struct PromotionRecipe
	{
		int ItemId = 0;   // the promoted material
		int Output = 1;   // how many one combine returns
		std::vector<PromotionIngredient> Ingredients;
	};

	const std::vector<PromotionRecipe>& PromotionRecipes();

	// The promotion for this item, or null when the Forge has no such recipe.
	const PromotionRecipe* FindPromotion(int aItemId);

	// A Mystic Forge recipe whose main material can be any one of a family of
	// interchangeable items. The acquisition graph only ever carries one of those
	// variants (the Amalgamated Gemstone combine has fifty, one per crest, orb and
	// doubloon), so the planner builds a route for every choice and plans the
	// cheapest - the same combine with a different gemstone in it.
	struct ChoiceRecipe
	{
		int ItemId = 0;       // what the combine makes
		int Output = 1;       // how many one combine returns
		int ChoiceCount = 0;  // how many of the chosen item it consumes
		int DustItemId = 0;   // the fixed second ingredient
		int DustCount = 0;
		std::vector<int> Choices; // interchangeable items, any one of them works
	};

	const std::vector<ChoiceRecipe>& ChoiceRecipes();

	// Every choice recipe that makes this item, one entry per fixed shape.
	std::vector<const ChoiceRecipe*> FindChoiceRecipes(int aItemId);

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
	};

	const char* SourceKindLabel(SourceKind aKind);
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
		int MerchantCurrencyId = 0;          // what a merchant charges (1 = coin)
		double MerchantCurrencyAmount = 0.0; // amount of that currency per item
		// Bought rather than worked on, whatever recipe it belongs to: a promotion
		// consumes one of the item it makes, and that seed is simply bought.
		bool LeafOnly = false;

		bool IsCurrency() const { return Type == "Currency"; }
		bool IsRecipe() const { return Type == "Recipe" && !Components.empty(); }
	};

	struct PlanOptions
	{
		bool PreferBuyingTradable = true;
		bool BuyOrderPrices = false;
		bool IncludeAccountItems = true;
		bool ShowCompleted = true;
		bool IncludeResearchNotes = false;   // price the research notes a plan needs
		// Leave guild commendation purchases out of the plan instead of counting
		// them, for players who do not want to spend the commendations.
		bool ExcludeGuildCommendations = false;
		// Crafted items whose craft can be turned into research notes; the plan
		// prices each of them and charges the cheapest one per note.
		const std::vector<RecipeNode>* ResearchNoteSources = nullptr;
		int MaxDepth = 48;
		int MaxNodes = 25000;
	};

	struct ShoppingItem
	{
		int Id = 0;
		std::string Name;
		std::string Icon;
		int Quantity = 0;
		i64 UnitCost = 0;             // coin price per unit; 0 when only another currency buys it
		i64 TotalCost = 0;            // coin price for the whole quantity
		SourceKind Source = SourceKind::TradingPost;
		bool Currency = false;
		std::string SourceDetail;     // merchant, achievement or acquisition note
		std::string PriceText;        // per unit price in a non-coin currency, e.g. "2,100 Karma"
		std::string TotalPriceText;   // the same price for the whole quantity
		bool FromVendor = false;      // a merchant sells it
		bool FromTradingPost = false; // the Trading Post sells it
		bool ResearchNotes = false;   // a line that exists to produce research notes
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
		std::string MerchantDetail; // clean "merchant (map)" for a vendor purchase
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

	// An ingredient that only an achievement unlocks. The steps are work at a
	// station, so these are listed before them: a plan cannot be started without
	// knowing that part of it has to be unlocked first.
	struct AchievementNeed
	{
		int ItemId = 0;
		std::string ItemName;
		std::string Icon;
		int Quantity = 0;
		int AchievementId = 0;
		std::string Detail;         // "Achievement #2438", the fallback
		std::string AchievementName; // resolved through the API when it is known
	};

	// Work that can only be done a limited number of times per day or week. The
	// plan says how many days the batch takes at that cap, so the steps do not
	// pretend a time-gated craft can be finished in one sitting.
	struct TimeGatedNeed
	{
		int ItemId = 0;
		std::string ItemName;
		std::string Icon;
		int Quantity = 0;   // amount still to make
		int Crafts = 0;     // crafts the amount needs
		int PerDay = 0;     // crafts allowed per day, 0 when the cap is weekly
		int PerWeek = 0;    // crafts allowed per week, 0 when the cap is daily
		int Days = 0;       // days of crafting at that cap
	};

	struct AchievementInfo
	{
		int Id = 0;
		std::string Name;
	};

	using AchievementDb = std::map<int, AchievementInfo>;

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
		std::vector<PlanNode> GoalRoots;      // one tree per tracked goal in a combined plan
		std::vector<PlanNode> NoteRoots;      // trees crafted to produce the research notes
		// Achievement items the plan still needs, listed before the steps.
		std::vector<AchievementNeed> AchievementNeeds;
		// Time-gated work the plan still needs, listed with the steps.
		std::vector<TimeGatedNeed> TimeGatedNeeds;
		i64 ResearchNotesRequired = 0;
		i64 ResearchNotesNeeded = 0;
		i64 ResearchNoteCost = 0;             // coin added for the missing research notes
		std::string ResearchNoteSourceName;   // item whose craft produces the notes
		int ResearchNoteYield = 0;            // notes per craft of that item
		int ResearchNoteCrafts = 0;           // crafts the missing notes need
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

	// One ingredient a step consumes, so a row can be read on its own instead of
	// being traced back through the plan tree.
	struct StepNeed
	{
		std::string Name;
		int Amount = 0;
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
		bool ResearchNotes = false; // the step exists to produce research notes
		std::string Location;    // the map a merchant stands on
		std::vector<StepNeed> Needs; // what the step consumes
	};

	// One place to go: a merchant, a crafting station or the Mystic Forge, with
	// every step that happens there. Each group keeps its steps in dependency
	// order and carries its combined coin cost, so a visit can be worked through
	// without coming back - Visit is above 1 only when a craft at this place needs
	// something another place makes later.
	struct CraftStepGroup
	{
		std::string Title;       // merchant name, discipline, "Trading Post", ...
		std::string Detail;      // map, location or what happens there
		CraftAction Action = CraftAction::Craft;
		i64 Cost = 0;
		int Visit = 1;           // 2 for the second time this place has to be worked
		bool ResearchNotes = false; // every step in the group exists for research notes
		std::vector<CraftStep> Steps;
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
