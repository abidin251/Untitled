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
