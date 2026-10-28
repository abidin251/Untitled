#include "Parse.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace mc
{
	using nlohmann::json;

	namespace
	{
		int GetInt(const json& aObject, const char* aKey, int aDefault = 0)
		{
			auto it = aObject.find(aKey);
			if (it == aObject.end() || it->is_null() || !it->is_number())
			{
				return aDefault;
			}
			return it->get<int>();
		}

		double GetDouble(const json& aObject, const char* aKey, double aDefault = 0.0)
		{
			auto it = aObject.find(aKey);
			if (it == aObject.end() || it->is_null() || !it->is_number())
			{
				return aDefault;
			}
			return it->get<double>();
		}

		std::string GetString(const json& aObject, const char* aKey, const std::string& aDefault = {})
		{
			auto it = aObject.find(aKey);
			if (it == aObject.end() || it->is_null() || !it->is_string())
			{
				return aDefault;
			}
			return it->get<std::string>();
		}

		RecipeNode ParseRecipeNode(const json& aNode)
		{
			RecipeNode node;
			node.Id = GetInt(aNode, "id");
			node.Type = GetString(aNode, "type");
			node.Quantity = GetDouble(aNode, "quantity", 1.0);
			node.Output = GetDouble(aNode, "output", 1.0);
			node.AchievementId = GetInt(aNode, "achievement_id");
			node.MultipleRecipes = std::max(1, GetInt(aNode, "multipleRecipeCount", 1));
			node.DailyCap = GetInt(aNode, "daily_purchase_cap");
			node.WeeklyCap = GetInt(aNode, "weekly_purchase_cap");

			if (aNode.contains("disciplines") && aNode["disciplines"].is_array())
			{
				for (const auto& discipline : aNode["disciplines"])
				{
					if (discipline.is_string())
					{
						node.Disciplines.push_back(discipline.get<std::string>());
					}
				}
			}

			if (aNode.contains("merchant") && aNode["merchant"].is_object())
			{
				node.MerchantName = GetString(aNode["merchant"], "name");
				if (aNode["merchant"].contains("locations") && aNode["merchant"]["locations"].is_array())
				{
					std::vector<std::string> locations;
					for (const auto& location : aNode["merchant"]["locations"])
					{
						if (location.is_string())
						{
							locations.push_back(location.get<std::string>());
						}
					}
					node.MerchantLocations = Join(locations, ", ");
				}
			}

			if (aNode.contains("components") && aNode["components"].is_array())
			{
				for (const auto& component : aNode["components"])
				{
					node.Components.push_back(ParseRecipeNode(component));
				}
			}

			// A merchant entry is a recipe whose only component is the currency the
			// merchant charges, e.g. 10000 coin for an Icy Runestone or 2100 karma
			// for an Obsidian Shard.
			if (!node.MerchantName.empty() && node.Components.size() == 1 && node.Components.front().IsCurrency())
			{
				node.MerchantCurrencyId = node.Components.front().Id;
				node.MerchantCurrencyAmount = node.Components.front().Quantity;
			}

			return node;
		}

		void ParseItemInto(const json& aObject, ItemInfo& aItem)
		{
			aItem.Id = GetInt(aObject, "id");
			aItem.Name = GetString(aObject, "name");
			aItem.Icon = GetString(aObject, "icon");
			aItem.Description = GetString(aObject, "description");
			aItem.Rarity = GetString(aObject, "rarity");
			aItem.Type = GetString(aObject, "type");
			aItem.ChatLink = GetString(aObject, "chat_link");
			aItem.Level = GetInt(aObject, "level");
			aItem.VendorValue = GetInt(aObject, "vendor_value");

			if (aObject.contains("details") && aObject["details"].is_object())
			{
				aItem.Subtype = GetString(aObject["details"], "type");
				aItem.WeightClass = GetString(aObject["details"], "weight_class");
			}

			aItem.Tradable = true;
			if (aObject.contains("flags") && aObject["flags"].is_array())
			{
				for (const auto& flag : aObject["flags"])
				{
					if (!flag.is_string())
					{
						continue;
					}
					const std::string value = flag.get<std::string>();
					aItem.Flags.push_back(value);
					if (value == "AccountBound" || value == "AccountBindOnUse" || value == "SoulbindOnUse")
					{
						aItem.Tradable = false;
					}
				}
			}
		}
	}

	bool ParseRecipeTree(const std::string& aJson, RecipeNode& aOut, std::string& aError)
	{
		try
		{
			const json parsed = json::parse(aJson);
			if (parsed.is_array())
			{
				if (parsed.empty())
				{
					aError = "No recipe data was returned for this item.";
					return false;
				}
				aOut = ParseRecipeNode(parsed.front());
				return true;
			}
			if (parsed.is_object())
			{
				aOut = ParseRecipeNode(parsed);
				return true;
			}
			aError = "Unexpected recipe data.";
			return false;
		}
		catch (const std::exception& ex)
		{
			aError = std::string("Could not parse recipe data: ") + ex.what();
			return false;
		}
	}

	std::vector<ItemInfo> ParseItems(const std::string& aJson)
	{
		std::vector<ItemInfo> result;
		try
		{
			const json parsed = json::parse(aJson);
			if (!parsed.is_array())
			{
				return result;
			}
			for (const auto& object : parsed)
			{
				if (!object.is_object())
				{
					continue;
				}
				ItemInfo item;
				ParseItemInto(object, item);
				if (item.Id != 0)
				{
					result.push_back(std::move(item));
				}
			}
		}
		catch (const std::exception&)
		{
		}
		return result;
	}

	std::vector<PriceInfo> ParsePrices(const std::string& aJson)
	{
		std::vector<PriceInfo> result;
		try
		{
			const json parsed = json::parse(aJson);
			if (!parsed.is_array())
			{
				return result;
			}
			for (const auto& object : parsed)
			{
				if (!object.is_object())
				{
					continue;
				}
				PriceInfo price;
				price.Id = GetInt(object, "id");
				if (object.contains("buys") && object["buys"].is_object())
				{
					price.Buy = GetInt(object["buys"], "unit_price");
					price.BuyQuantity = GetInt(object["buys"], "quantity");
				}
				if (object.contains("sells") && object["sells"].is_object())
				{
					price.Sell = GetInt(object["sells"], "unit_price");
					price.SellQuantity = GetInt(object["sells"], "quantity");
				}
				if (price.Id != 0)
				{
					result.push_back(price);
				}
			}
		}
		catch (const std::exception&)
		{
		}
		return result;
	}

	std::vector<CurrencyInfo> ParseCurrencies(const std::string& aJson)
	{
		std::vector<CurrencyInfo> result;
		try
		{
			const json parsed = json::parse(aJson);
			if (!parsed.is_array())
			{
				return result;
			}
			for (const auto& object : parsed)
			{
				if (!object.is_object())
				{
					continue;
				}
				CurrencyInfo currency;
				currency.Id = GetInt(object, "id");
				currency.Name = GetString(object, "name");
				currency.Icon = GetString(object, "icon");
				currency.Description = GetString(object, "description");
				if (currency.Id != 0)
				{
					result.push_back(std::move(currency));
				}
			}
		}
		catch (const std::exception&)
		{
		}
		return result;
	}

	std::vector<AchievementInfo> ParseAchievements(const std::string& aJson)
	{
		std::vector<AchievementInfo> result;
		try
		{
			const json parsed = json::parse(aJson);
			if (!parsed.is_array())
			{
				return result;
			}
			for (const auto& object : parsed)
			{
				if (!object.is_object())
				{
					continue;
				}
				AchievementInfo achievement;
				achievement.Id = GetInt(object, "id");
				achievement.Name = GetString(object, "name");
				if (achievement.Id != 0)
				{
					result.push_back(std::move(achievement));
				}
			}
		}
		catch (const std::exception&)
		{
		}
		return result;
	}

	std::vector<int> ParseIntArray(const std::string& aJson)
	{
		std::vector<int> result;
		try
		{
			const json parsed = json::parse(aJson);
			if (!parsed.is_array())
			{
				return result;
			}
			for (const auto& value : parsed)
			{
				if (value.is_number_integer())
				{
					result.push_back(value.get<int>());
				}
			}
		}
		catch (const std::exception&)
		{
		}
		return result;
	}

	std::vector<std::string> ParseStringArray(const std::string& aJson)
	{
		std::vector<std::string> result;
		try
		{
			const json parsed = json::parse(aJson);
			if (!parsed.is_array())
			{
				return result;
			}
			for (const auto& value : parsed)
			{
				if (value.is_string())
				{
					result.push_back(value.get<std::string>());
				}
			}
		}
		catch (const std::exception&)
		{
		}
		return result;
	}
}
