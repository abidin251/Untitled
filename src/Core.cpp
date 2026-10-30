#include "Core.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>

namespace mc
{
	i64 NowUnix()
	{
		using namespace std::chrono;
		return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
	}

	// Crafted items that a research kit turns into notes, one cheap batch per
	// rarity and kind so a plan is never left choosing between exotic gear alone:
	// the fine and masterwork jewellery the community farms notes from, fine,
	// masterwork and rare gear, the Draconic and Emblazoned exotics, and Peer
	// Reviewed Research, which hands its notes over as it is crafted. The ids were
	// screened against the live market as the cheapest craft per note of their
	// kind, and the planner re-prices every one of them against the prices in
	// force when the plan runs, keeping whichever comes to least for the notes
	// still missing.
	const std::vector<int>& ResearchNoteSourceCandidates()
	{
		static const std::vector<int> candidates = {
			// Fine - cheap level 70-75 gear (cheapest: Carrion Barbaric Helm).
			10675, 10676, 13996, 14503, 14552, 13997, 14497, 14546, 10673, 10500,
			10570, 10605, 13999, 14498, 14547, 13912, 13961, 13913, 13962, 10501,
			// Masterwork - level-80 jewellery and level-70 gear (cheapest: Beryl
			// Mithril Earring).
			13448, 41451, 13446, 45926, 13395, 36784, 41455, 13406, 45913, 13445,
			13442, 13443, 13447, 13479, 36751, 41447, 13490, 45939, 13403, 13393,
			// The mithril earrings, rings and amulets the community researches: a
			// cheap craft of about five notes each, measured at five or so.
			13435, 13436, 13437, 13438, 13439, 13440, 13441, 45925, 45912, 45938,
			41446, 41450, 41454, 13482, 13483, 13484, 13485, 13444, 13477, 13478,
			13480, 13481,
			// Peer Reviewed Research: the craft itself grants 150 research notes.
			104934,
			// Rare - level-80 weapons (cheapest: Carrion Krait Trident).
			14008, 14511, 14560, 13924, 13973, 15388, 38153, 14013, 14516, 14565,
			13889, 14595, 15346, 15423, 15507, 15549, 13929, 13978, 15393, 15262,
			// Exotic - the Draconic and Emblazoned level-80 sets (cheapest: Rabid
			// Draconic Gauntlets).
			48601, 48615, 48580, 48594, 48699, 48622, 48587, 48602, 48657, 48616,
			48650, 48581, 48595, 48700, 48608, 48623, 48588, 48658, 48643, 48651,
			48607, 48621, 48586, 48600, 48705, 48609, 48644, 48628, 48678, 48593
		};
		return candidates;
	}

	// What a research kit returns for a salvaged crafted item. The numbers are the
	// salvage research the community measured and the wiki records: fine,
	// masterwork and rare gear of level 70 and up averages about five notes (the
	// mithril earrings, the Barbaric pieces), level 60 to 69 gear about three (the
	// platinum and snowflake jewellery), level-80 exotic armor and back items
	// reach seventy-five and its weapons forty-five, exotic trinkets average about
	// eighty, and crafted food, utilities and potions give a single note. Recipes
	// below Novice rating and items below level 60 give one as well.
	int ResearchNoteYieldFor(int aLevel, const std::string& aRarity, const std::string& aType, const std::string& aSubtype)
	{
		(void)aSubtype;
		if (aLevel < 60)
		{
			return 1;
		}
		if (aType == "Consumable")
		{
			return 1;
		}
		if (aRarity == "Exotic")
		{
			if (aType == "Weapon")
			{
				return 45;
			}
			if (aType == "Trinket")
			{
				return 80;
			}
			return 75;
		}
		// Level 60 to 69 gear was measured at three notes, level 70 and up at five.
		if (aLevel < 70)
		{
			return 3;
		}
		return 5;
	}

	int ResearchNoteYieldOverride(int aItemId)
	{
		switch (aItemId)
		{
		case 104934: // Peer Reviewed Research: the craft grants the notes itself
			return 150;
		case 13435: // Chrysocola Mithril Earring, measured at about ten and a half
			return 10;
		case 13438: // Sapphire Mithril Earring, measured at about five
		case 13436: // Emerald Mithril Earring
		case 13437: // Beryl Mithril Earring
			return 5;
		default:
			return 0;
		}
	}

	const char* AchievementItemDetail(int aItemId)
	{
		switch (aItemId)
		{
		case 19678: // Gift of Battle, handed over by the WvW reward track of its name
			return "Gift of Battle Reward Track";
		default:
			return nullptr;
		}
	}

	// The Mystic Forge promotions of fine and rare crafting materials. The
	// acquisition provider has no entry for these, so a plan that needs orichalcum
	// ore or powerful blood only ever saw the Trading Post listing; with the table
	// here each of them is priced from what the Forge consumes and the cheaper
	// route wins. The figures are the wiki's promotion tables (Philosopher's
	// Stone/crafting materials), one combine each: the amount it returns, the
	// cheaper material it is made from, the seed of the material itself, the dust
	// of the tier, and the stones or crystals the combine burns.
	const std::vector<PromotionRecipe>& PromotionRecipes()
	{
		// result, output, lower id, lower count, seed id, seed count, dust id,
		// dust count, stone id, stone count.
		static const int rows[][10] = {
			{ 24358, 5, 24341, 50, 24358, 1, 24277, 5, 20796, 5 },
			{ 19725, 10, 19722, 250, 19725, 1, 24277, 5, 20796, 5 },
			{ 19712, 10, 19709, 250, 19712, 1, 24277, 5, 20796, 10 },
			{ 24289, 5, 24288, 50, 24289, 1, 24277, 5, 20796, 5 },
			{ 19742, 40, 19740, 250, 19742, 1, 24274, 5, 20796, 4 },
			{ 19746, 10, 19747, 250, 19746, 1, 24277, 5, 20796, 10 },
			{ 19744, 40, 19742, 250, 19744, 1, 24274, 5, 20796, 6 },
			{ 19747, 40, 19744, 250, 19747, 1, 24276, 5, 20796, 8 },
			{ 19740, 40, 19720, 250, 19740, 1, 24273, 5, 20796, 2 },
			{ 24344, 7, 24343, 50, 24344, 1, 24274, 5, 20796, 2 },
			{ 24343, 7, 24342, 50, 24343, 1, 24273, 5, 20796, 1 },
			{ 24348, 7, 24347, 50, 24348, 1, 24274, 5, 20796, 2 },
			{ 19730, 40, 19728, 250, 19730, 1, 24274, 5, 20796, 2 },
			{ 19741, 40, 19739, 250, 19741, 1, 24274, 5, 20796, 2 },
			{ 19734, 40, 19733, 250, 19734, 1, 24274, 5, 20796, 4 },
			{ 19737, 10, 19735, 250, 19737, 1, 24277, 5, 20796, 10 },
			{ 19736, 40, 19734, 250, 19736, 1, 24274, 5, 20796, 6 },
			{ 19735, 40, 19736, 250, 19735, 1, 24276, 5, 20796, 8 },
			{ 19733, 40, 19738, 250, 19733, 1, 24273, 5, 20796, 2 },
			{ 24300, 5, 24299, 50, 24300, 1, 24277, 5, 20796, 5 },
			{ 19722, 40, 19724, 250, 19722, 1, 24276, 5, 20796, 4 },
			{ 19709, 40, 19711, 250, 19709, 1, 24276, 5, 20796, 8 },
			{ 24363, 7, 24298, 50, 24363, 1, 24275, 5, 20796, 3 },
			{ 24354, 7, 24353, 50, 24354, 1, 24274, 5, 20796, 2 },
			{ 24281, 7, 24280, 50, 24281, 1, 24275, 5, 20796, 3 },
			{ 19682, 40, 19687, 250, 19682, 1, 24274, 5, 20796, 4 },
			{ 19698, 40, 19703, 250, 19698, 1, 24274, 5, 20796, 2 },
			{ 19745, 10, 19748, 250, 19745, 1, 24277, 5, 20796, 5 },
			{ 19724, 40, 19727, 250, 19724, 1, 24274, 5, 20796, 3 },
			{ 19711, 40, 19714, 250, 19711, 1, 24274, 5, 20796, 6 },
			{ 19732, 10, 19729, 250, 19732, 1, 24277, 5, 20796, 5 },
			{ 24345, 7, 24344, 50, 24345, 1, 24275, 5, 20796, 3 },
			{ 24299, 7, 24363, 50, 24299, 1, 24276, 5, 20796, 4 },
			{ 19683, 40, 19679, 250, 19683, 1, 24273, 5, 20796, 2 },
			{ 19699, 40, 19697, 250, 19699, 1, 24273, 5, 20796, 1 },
			{ 24341, 7, 24345, 50, 24341, 1, 24276, 5, 20796, 4 },
			{ 24350, 7, 24349, 50, 24350, 1, 24276, 5, 20796, 4 },
			{ 24356, 7, 24355, 50, 24356, 1, 24276, 5, 20796, 4 },
			{ 24288, 6, 24287, 50, 24288, 1, 24276, 5, 20796, 4 },
			{ 19743, 40, 19741, 250, 19743, 1, 24274, 5, 20796, 3 },
			{ 19684, 40, 19681, 250, 19684, 1, 24275, 5, 20796, 6 },
			{ 19700, 40, 19702, 250, 19700, 1, 24276, 5, 20796, 4 },
			{ 19685, 10, 19684, 250, 19685, 1, 24277, 5, 20796, 10 },
			{ 19701, 10, 19700, 250, 19701, 1, 24277, 5, 20796, 5 },
			{ 19686, 40, 19682, 250, 19686, 1, 24275, 5, 20796, 6 },
			{ 19702, 20, 19699, 250, 19702, 1, 24274, 5, 20796, 2 },
			{ 24282, 7, 24281, 50, 24282, 1, 24276, 5, 20796, 4 },
			{ 24283, 5, 24282, 50, 24283, 1, 24277, 5, 20796, 5 },
			{ 19731, 40, 19730, 250, 19731, 1, 24274, 5, 20796, 3 },
			{ 24286, 7, 24285, 50, 24286, 1, 24274, 5, 20796, 2 },
			{ 19727, 40, 19726, 250, 19727, 1, 24274, 5, 20796, 2 },
			{ 19714, 40, 19713, 250, 19714, 1, 24274, 5, 20796, 4 },
			{ 24349, 7, 24348, 50, 24349, 1, 24275, 5, 20796, 3 },
			{ 24355, 7, 24354, 50, 24355, 1, 24275, 5, 20796, 3 },
			{ 19748, 40, 19743, 250, 19748, 1, 24276, 5, 20796, 4 },
			{ 19687, 40, 19680, 250, 19687, 1, 24273, 5, 20796, 2 },
			{ 24347, 7, 24346, 50, 24347, 1, 24273, 5, 20796, 1 },
			{ 24353, 7, 24352, 50, 24353, 1, 24273, 5, 20796, 1 },
			{ 24285, 7, 24284, 50, 24285, 1, 24273, 5, 20796, 1 },
			{ 24297, 7, 24296, 50, 24297, 1, 24273, 5, 20796, 1 },
			{ 24279, 7, 24278, 50, 24279, 1, 24273, 5, 20796, 1 },
			{ 24287, 7, 24286, 50, 24287, 1, 24275, 5, 20796, 3 },
			{ 19726, 40, 19723, 250, 19726, 1, 24273, 5, 20796, 1 },
			{ 19713, 40, 19710, 250, 19713, 1, 24273, 5, 20796, 2 },
			{ 19688, 40, 19683, 250, 19688, 1, 24274, 5, 20796, 4 },
			{ 19729, 40, 19731, 250, 19729, 1, 24276, 5, 20796, 4 },
			{ 19728, 40, 19719, 250, 19728, 1, 24273, 5, 20796, 1 },
			{ 24298, 7, 24297, 50, 24298, 1, 24274, 5, 20796, 2 },
			{ 24280, 7, 24279, 50, 24280, 1, 24274, 5, 20796, 2 },
			{ 24292, 7, 24291, 50, 24292, 1, 24274, 5, 20796, 2 },
			{ 24294, 6, 24293, 50, 24294, 1, 24276, 5, 20796, 4 },
			{ 24295, 5, 24294, 50, 24295, 1, 24277, 5, 20796, 5 },
			{ 24293, 7, 24292, 50, 24293, 1, 24275, 5, 20796, 3 },
			{ 24291, 7, 24290, 50, 24291, 1, 24273, 5, 20796, 1 },
			{ 24351, 5, 24350, 50, 24351, 1, 24277, 5, 20796, 5 },
			{ 24357, 5, 24356, 50, 24357, 1, 24277, 5, 20796, 5 },
			{ 19739, 40, 19718, 250, 19739, 1, 24273, 5, 20796, 1 },
			{ 24277, 6, 24276, 250, 24277, 1, 20799, 5, 20796, 5 },
			{ 24276, 40, 24275, 250, 24276, 1, 20799, 4, 20796, 4 },
			{ 24275, 40, 24274, 250, 24275, 1, 20799, 3, 20796, 3 },
			{ 24274, 40, 24273, 250, 24274, 1, 20799, 2, 20796, 2 },
			{ 24273, 40, 24272, 250, 24273, 1, 20799, 1, 20796, 1 }
		};

		static const std::vector<PromotionRecipe> recipes = []() {
			std::vector<PromotionRecipe> result;
			result.reserve(sizeof(rows) / sizeof(rows[0]));
			for (const auto& row : rows)
			{
				PromotionRecipe recipe;
				recipe.ItemId = row[0];
				recipe.Output = row[1];
				recipe.Ingredients.push_back({ row[2], row[3] });
				recipe.Ingredients.push_back({ row[4], row[5] });
				recipe.Ingredients.push_back({ row[6], row[7] });
				recipe.Ingredients.push_back({ row[8], row[9] });
				result.push_back(std::move(recipe));
			}
			return result;
		}();
		return recipes;
	}

	const PromotionRecipe* FindPromotion(int aItemId)
	{
		static const std::map<int, const PromotionRecipe*> lookup = []() {
			std::map<int, const PromotionRecipe*> result;
			for (const auto& recipe : PromotionRecipes())
			{
				result[recipe.ItemId] = &recipe;
			}
			return result;
		}();
		auto it = lookup.find(aItemId);
		return it == lookup.end() ? nullptr : it->second;
	}

	// Amalgamated Gemstone is made at the Mystic Forge from nine of any single
	// tier-6 crest, orb or doubloon plus a pile of crystalline dust, or seventy-five
	// of one for ten gemstones. The provider models fifty recipes for it, one per
	// gemstone, but only ever returns one, so the family is listed here: without it
	// the plan compared the single variant it was given (whichever gemstone happened
	// to be dearest) with the listing and told the player to buy a gemstone that the
	// Forge makes for less.
	const std::vector<ChoiceRecipe>& ChoiceRecipes()
	{
		static const std::vector<ChoiceRecipe> recipes = []() {
			const std::vector<int> gems = {
				24514, // Crest of the Assassin
				24518, // Crest of the Magi
				24532, // Crest of the Rabid
				24533, // Crest of the Shaman
				24524, // Crest of the Soldier
				72436, // Agate Orb
				42010, // Azurite Orb
				24520, // Beryl Orb
				76491, // Black Diamond
				24512, // Chrysocola Orb
				24510, // Coral Orb
				75654, // Ebony Orb
				24515, // Emerald Orb
				74988, // Flax Blossom
				76179, // Freshwater Pearl
				72315, // Maguuma Burl
				70957, // Maguuma Lily
				72504, // Moonstone Orb
				24522, // Opal Orb
				24508, // Ruby Orb
				24516, // Sapphire Orb
				24884, // Copper Doubloon
				24502, // Silver Doubloon
				24772, // Gold Doubloon
				24773  // Platinum Doubloon
			};

			std::vector<ChoiceRecipe> result;
			ChoiceRecipe single;
			single.ItemId = 68063;
			single.Output = 1;
			single.ChoiceCount = 9;
			single.DustItemId = 24277; // Pile of Crystalline Dust
			single.DustCount = 1;
			single.Choices = gems;
			result.push_back(std::move(single));

			ChoiceRecipe bulk;
			bulk.ItemId = 68063;
			bulk.Output = 10;
			bulk.ChoiceCount = 75;
			bulk.DustItemId = 24277;
			bulk.DustCount = 1;
			bulk.Choices = gems;
			result.push_back(std::move(bulk));
			return result;
		}();
		return recipes;
	}

	std::vector<const ChoiceRecipe*> FindChoiceRecipes(int aItemId)
	{
		std::vector<const ChoiceRecipe*> result;
		for (const auto& recipe : ChoiceRecipes())
		{
			if (recipe.ItemId == aItemId)
			{
				result.push_back(&recipe);
			}
		}
		return result;
	}

	std::string ToLower(std::string aValue)
	{
		std::transform(aValue.begin(), aValue.end(), aValue.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return aValue;
	}

	std::string Trim(const std::string& aValue)
	{
		const auto begin = aValue.find_first_not_of(" \t\r\n");
		if (begin == std::string::npos)
		{
			return {};
		}
		const auto end = aValue.find_last_not_of(" \t\r\n");
		return aValue.substr(begin, end - begin + 1);
	}

	std::string Join(const std::vector<std::string>& aValues, const char* aSeparator)
	{
		std::string result;
		for (std::size_t i = 0; i < aValues.size(); ++i)
		{
			if (i > 0)
			{
				result += aSeparator;
			}
			result += aValues[i];
		}
		return result;
	}

	std::string FormatNumber(i64 aValue)
	{
		std::string raw = std::to_string(aValue);
		bool negative = false;
		if (!raw.empty() && raw[0] == '-')
		{
			negative = true;
			raw.erase(raw.begin());
		}

		std::string result;
		int counter = 0;
		for (auto it = raw.rbegin(); it != raw.rend(); ++it)
		{
			if (counter != 0 && counter % 3 == 0)
			{
				result.push_back(',');
			}
			result.push_back(*it);
			++counter;
		}
		std::reverse(result.begin(), result.end());
		return negative ? "-" + result : result;
	}

	std::string FormatCoins(i64 aCopper)
	{
		if (aCopper == 0)
		{
			return "0c";
		}

		const bool negative = aCopper < 0;
		if (negative)
		{
			aCopper = -aCopper;
		}

		const i64 gold = aCopper / 10000;
		const i64 silver = (aCopper % 10000) / 100;
		const i64 copper = aCopper % 100;

		std::ostringstream stream;
		if (gold > 0) stream << FormatNumber(gold) << "g";
		if (silver > 0)
		{
			if (gold > 0) stream << " ";
			stream << silver << "s";
		}
		if (copper > 0 || (gold == 0 && silver == 0))
		{
			if (gold > 0 || silver > 0) stream << " ";
			stream << copper << "c";
		}

		return (negative ? "-" : "") + stream.str();
	}

	const char* SourceKindLabel(SourceKind aKind)
	{
		switch (aKind)
		{
		case SourceKind::Craft: return "Craft";
		case SourceKind::MysticForge: return "Mystic Forge";
		case SourceKind::Vendor: return "Vendor";
		case SourceKind::Achievement: return "Achievement";
		case SourceKind::TradingPost: return "Trading Post";
		case SourceKind::Currency: return "Currency";
		case SourceKind::Container: return "Container";
		default: return "Other";
		}
	}

	std::string ItemTypeLabel(const ItemInfo& aItem)
	{
		if (aItem.Type == "Armor" && !aItem.Subtype.empty())
		{
			if (!aItem.WeightClass.empty())
			{
				return aItem.WeightClass + " " + aItem.Subtype;
			}
			return aItem.Subtype;
		}
		if (!aItem.Subtype.empty())
		{
			return aItem.Subtype;
		}
		if (aItem.Type == "Back")
		{
			return "Back Item";
		}
		if (!aItem.Type.empty())
		{
			return aItem.Type;
		}
		return "Item";
	}

	bool AccountData::HasPermission(const char* aPermission) const
	{
		if (!aPermission)
		{
			return false;
		}
		return std::find(Permissions.begin(), Permissions.end(), aPermission) != Permissions.end();
	}
}
