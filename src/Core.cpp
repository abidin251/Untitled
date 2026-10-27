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
