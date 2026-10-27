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

	// Crafted equipment that the research kit turns into notes, one cheap batch
	// per rarity so a plan is never left choosing between exotic gear alone:
	// fine, masterwork, rare and exotic pieces, 90 in all. The ids were picked
	// from the live market as the cheapest craft per note of their rarity (level
	// 60 and up), and the planner re-prices every one of them against the prices
	// in force when the plan runs, keeping whichever is cheapest per note.
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

	// A research kit returns more notes from the same craft the higher its level
	// and the rarer the item: a level-80 exotic gives five, a masterwork two, a
	// fine piece one. Below level 60 anything gives a single note, and short of
	// level 80 the rarity's number drops by one.
	int ResearchNoteYieldFor(int aLevel, const std::string& aRarity)
	{
		int notes = 1; // fine and below
		if (aRarity == "Exotic")
		{
			notes = 5;
		}
		else if (aRarity == "Rare")
		{
			notes = 3;
		}
		else if (aRarity == "Masterwork")
		{
			notes = 2;
		}
		if (aLevel < 60)
		{
			return 1;
		}
		if (aLevel < 80)
		{
			notes = std::max(1, notes - 1);
		}
		return notes;
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
