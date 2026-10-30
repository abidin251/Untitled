#include "Store.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace mc
{
	using nlohmann::json;

	namespace
	{
		const char* Base64Characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		std::string Base64Encode(const unsigned char* aData, std::size_t aSize)
		{
			std::string result;
			result.reserve(((aSize + 2) / 3) * 4);
			for (std::size_t i = 0; i < aSize; i += 3)
			{
				const unsigned int b0 = aData[i];
				const unsigned int b1 = i + 1 < aSize ? aData[i + 1] : 0;
				const unsigned int b2 = i + 2 < aSize ? aData[i + 2] : 0;
				const unsigned int value = (b0 << 16) | (b1 << 8) | b2;
				result.push_back(Base64Characters[(value >> 18) & 0x3F]);
				result.push_back(Base64Characters[(value >> 12) & 0x3F]);
				result.push_back(i + 1 < aSize ? Base64Characters[(value >> 6) & 0x3F] : '=');
				result.push_back(i + 2 < aSize ? Base64Characters[value & 0x3F] : '=');
			}
			return result;
		}

		std::vector<unsigned char> Base64Decode(const std::string& aValue)
		{
			std::vector<unsigned char> result;
			int values[4] = {};
			int count = 0;
			for (char c : aValue)
			{
				if (c == '=')
				{
					break;
				}
				const char* found = std::strchr(Base64Characters, c);
				if (!found || c == '\0')
				{
					continue;
				}
				values[count++] = static_cast<int>(found - Base64Characters);
				if (count == 4)
				{
					const unsigned int value = (values[0] << 18) | (values[1] << 12) | (values[2] << 6) | values[3];
					result.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
					result.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
					result.push_back(static_cast<unsigned char>(value & 0xFF));
					count = 0;
				}
			}
			if (count == 2)
			{
				const unsigned int value = (values[0] << 18) | (values[1] << 12);
				result.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
			}
			else if (count == 3)
			{
				const unsigned int value = (values[0] << 18) | (values[1] << 12) | (values[2] << 6);
				result.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
				result.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
			}
			return result;
		}

		json ItemToJson(const ItemInfo& aItem)
		{
			return json{
				{ "id", aItem.Id },
				{ "name", aItem.Name },
				{ "icon", aItem.Icon },
				{ "description", aItem.Description },
				{ "rarity", aItem.Rarity },
				{ "type", aItem.Type },
				{ "subtype", aItem.Subtype },
				{ "weight", aItem.WeightClass },
				{ "chat", aItem.ChatLink },
				{ "level", aItem.Level },
				{ "vendor", aItem.VendorValue },
				{ "tradable", aItem.Tradable },
				{ "flags", aItem.Flags },
			};
		}

		ItemInfo ItemFromJson(const json& aObject)
		{
			ItemInfo item;
			item.Id = aObject.value("id", 0);
			item.Name = aObject.value("name", std::string{});
			item.Icon = aObject.value("icon", std::string{});
			item.Description = aObject.value("description", std::string{});
			item.Rarity = aObject.value("rarity", std::string{});
			item.Type = aObject.value("type", std::string{});
			item.Subtype = aObject.value("subtype", std::string{});
			item.WeightClass = aObject.value("weight", std::string{});
			item.ChatLink = aObject.value("chat", std::string{});
			item.Level = aObject.value("level", 0);
			item.VendorValue = aObject.value("vendor", 0);
			item.Tradable = aObject.value("tradable", true);
			if (aObject.contains("flags") && aObject["flags"].is_array())
			{
				for (const auto& flag : aObject["flags"])
				{
					if (flag.is_string())
					{
						item.Flags.push_back(flag.get<std::string>());
					}
				}
			}
			return item;
		}
	}

	namespace store
	{
		bool EnsureDirectory(const std::filesystem::path& aPath)
		{
			std::error_code error;
			if (std::filesystem::exists(aPath, error))
			{
				return std::filesystem::is_directory(aPath, error);
			}
			return std::filesystem::create_directories(aPath, error);
		}

		bool ReadTextFile(const std::filesystem::path& aPath, std::string& aOut)
		{
			std::ifstream stream(aPath, std::ios::binary);
			if (!stream)
			{
				return false;
			}
			aOut.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
			return true;
		}

		bool WriteTextFile(const std::filesystem::path& aPath, const std::string& aContents)
		{
			std::filesystem::path temporary = aPath;
			temporary += ".tmp";
			{
				std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
				if (!stream)
				{
					return false;
				}
				stream.write(aContents.data(), static_cast<std::streamsize>(aContents.size()));
				if (!stream)
				{
					return false;
				}
			}

			std::error_code error;
			std::filesystem::remove(aPath, error);
			error.clear();
			std::filesystem::rename(temporary, aPath, error);
			if (error)
			{
				std::filesystem::remove(temporary, error);
				return false;
			}
			return true;
		}

		bool FileExists(const std::filesystem::path& aPath)
		{
			std::error_code error;
			return std::filesystem::exists(aPath, error);
		}

		i64 FileTimestamp(const std::filesystem::path& aPath)
		{
			std::error_code error;
			const auto time = std::filesystem::last_write_time(aPath, error);
			if (error)
			{
				return 0;
			}
			const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
			return std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count();
		}

		std::string ProtectKey(const std::string& aValue)
		{
#ifdef _WIN32
			if (aValue.empty())
			{
				return {};
			}
			DATA_BLOB input{};
			input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(aValue.data()));
			input.cbData = static_cast<DWORD>(aValue.size());

			DATA_BLOB output{};
			if (!CryptProtectData(&input, L"MysticCrafting API key", nullptr, nullptr, nullptr, 0, &output))
			{
				return {};
			}
			const std::string encoded = Base64Encode(output.pbData, output.cbData);
			LocalFree(output.pbData);
			return encoded;
#else
			return aValue;
#endif
		}

		std::string UnprotectKey(const std::string& aValue)
		{
#ifdef _WIN32
			if (aValue.empty())
			{
				return {};
			}
			const std::vector<unsigned char> decoded = Base64Decode(aValue);
			if (decoded.empty())
			{
				return aValue;
			}
			DATA_BLOB input{};
			input.pbData = const_cast<BYTE*>(decoded.data());
			input.cbData = static_cast<DWORD>(decoded.size());

			DATA_BLOB output{};
			if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
			{
				return {};
			}
			std::string plain(reinterpret_cast<char*>(output.pbData), output.cbData);
			LocalFree(output.pbData);
			return plain;
#else
			return aValue;
#endif
		}

		bool LoadSettings(const std::filesystem::path& aPath, UserSettings& aOut, std::string& aError)
		{
			std::string contents;
			if (!ReadTextFile(aPath, contents))
			{
				return false;
			}
			try
			{
				const json parsed = json::parse(contents);
				aOut.PreferBuyingTradable = parsed.value("preferBuying", aOut.PreferBuyingTradable);
				aOut.BuyOrderPrices = parsed.value("buyOrderPrices", aOut.BuyOrderPrices);
				aOut.IncludeAccountItems = parsed.value("includeAccount", aOut.IncludeAccountItems);
				aOut.ShowCompleted = parsed.value("showCompleted", aOut.ShowCompleted);
				aOut.CompactDensity = parsed.value("compact", aOut.CompactDensity);
				aOut.IncludeBank = parsed.value("includeBank", aOut.IncludeBank);
				aOut.IncludeMaterials = parsed.value("includeMaterials", aOut.IncludeMaterials);
				aOut.IncludeCharacters = parsed.value("includeCharacters", aOut.IncludeCharacters);
				aOut.IncludeSharedInventory = parsed.value("includeShared", aOut.IncludeSharedInventory);
				aOut.IncludeArmory = parsed.value("includeArmory", aOut.IncludeArmory);
				aOut.WindowVisible = parsed.value("windowVisible", aOut.WindowVisible);
				aOut.CombineTracked = parsed.value("combineTracked", aOut.CombineTracked);
				aOut.IncludeResearchNotes = parsed.value("includeResearchNotes", aOut.IncludeResearchNotes);
				aOut.ExcludeGuildCommendations = parsed.value("excludeGuildCommendations", aOut.ExcludeGuildCommendations);
				aOut.SelectedItem = parsed.value("selectedItem", aOut.SelectedItem);
				aOut.RecipeBase = parsed.value("recipeBase", aOut.RecipeBase);

				const std::string encrypted = parsed.value("apiKey", std::string{});
				aOut.ApiKey = UnprotectKey(encrypted);

				if (parsed.contains("tracked") && parsed["tracked"].is_array())
				{
					aOut.Tracked.clear();
					for (const auto& value : parsed["tracked"])
					{
						if (value.is_number_integer())
						{
							aOut.Tracked.push_back(value.get<int>());
						}
					}
				}

				if (parsed.contains("preferences") && parsed["preferences"].is_object())
				{
					aOut.Preferences.clear();
					for (auto it = parsed["preferences"].begin(); it != parsed["preferences"].end(); ++it)
					{
						if (it.value().is_number_integer())
						{
							aOut.Preferences[std::stoi(it.key())] = it.value().get<int>();
						}
					}
				}

				return true;
			}
			catch (const std::exception& ex)
			{
				aError = std::string("Could not read settings: ") + ex.what();
				return false;
			}
		}

		bool SaveSettings(const std::filesystem::path& aPath, const UserSettings& aSettings)
		{
			json parsed;
			parsed["apiKey"] = ProtectKey(aSettings.ApiKey);
			parsed["preferBuying"] = aSettings.PreferBuyingTradable;
			parsed["buyOrderPrices"] = aSettings.BuyOrderPrices;
			parsed["includeAccount"] = aSettings.IncludeAccountItems;
			parsed["showCompleted"] = aSettings.ShowCompleted;
			parsed["compact"] = aSettings.CompactDensity;
			parsed["includeBank"] = aSettings.IncludeBank;
			parsed["includeMaterials"] = aSettings.IncludeMaterials;
			parsed["includeCharacters"] = aSettings.IncludeCharacters;
			parsed["includeShared"] = aSettings.IncludeSharedInventory;
			parsed["includeArmory"] = aSettings.IncludeArmory;
			parsed["windowVisible"] = aSettings.WindowVisible;
			parsed["combineTracked"] = aSettings.CombineTracked;
			parsed["includeResearchNotes"] = aSettings.IncludeResearchNotes;
			parsed["excludeGuildCommendations"] = aSettings.ExcludeGuildCommendations;
			parsed["selectedItem"] = aSettings.SelectedItem;
			parsed["recipeBase"] = aSettings.RecipeBase;
			parsed["tracked"] = aSettings.Tracked;

			json preferences = json::object();
			for (const auto& pair : aSettings.Preferences)
			{
				preferences[std::to_string(pair.first)] = pair.second;
			}
			parsed["preferences"] = preferences;

			return WriteTextFile(aPath, parsed.dump(2));
		}

		bool LoadItems(const std::filesystem::path& aPath, ItemDb& aItems, i64& aFetchedAt)
		{
			std::string contents;
			if (!ReadTextFile(aPath, contents))
			{
				return false;
			}
			try
			{
				const json parsed = json::parse(contents);
				aFetchedAt = parsed.value("fetched", static_cast<i64>(0));
				if (!parsed.contains("items") || !parsed["items"].is_array())
				{
					return false;
				}
				for (const auto& object : parsed["items"])
				{
					ItemInfo item = ItemFromJson(object);
					if (item.Id != 0)
					{
						aItems[item.Id] = std::move(item);
					}
				}
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool SaveItems(const std::filesystem::path& aPath, const ItemDb& aItems, i64 aFetchedAt)
		{
			json items = json::array();
			for (const auto& pair : aItems)
			{
				items.push_back(ItemToJson(pair.second));
			}
			json parsed{ { "fetched", aFetchedAt }, { "items", items } };
			return WriteTextFile(aPath, parsed.dump());
		}

		bool LoadPrices(const std::filesystem::path& aPath, PriceDb& aPrices, i64& aFetchedAt)
		{
			std::string contents;
			if (!ReadTextFile(aPath, contents))
			{
				return false;
			}
			try
			{
				const json parsed = json::parse(contents);
				aFetchedAt = parsed.value("fetched", static_cast<i64>(0));
				if (!parsed.contains("prices") || !parsed["prices"].is_array())
				{
					return false;
				}
				for (const auto& object : parsed["prices"])
				{
					PriceInfo price;
					price.Id = object.value("id", 0);
					price.Buy = object.value("buy", static_cast<i64>(0));
					price.Sell = object.value("sell", static_cast<i64>(0));
					price.BuyQuantity = object.value("buyQuantity", 0);
					price.SellQuantity = object.value("sellQuantity", 0);
					if (price.Id != 0)
					{
						aPrices[price.Id] = price;
					}
				}
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool SavePrices(const std::filesystem::path& aPath, const PriceDb& aPrices, i64 aFetchedAt)
		{
			json prices = json::array();
			for (const auto& pair : aPrices)
			{
				prices.push_back({
					{ "id", pair.second.Id },
					{ "buy", pair.second.Buy },
					{ "sell", pair.second.Sell },
					{ "buyQuantity", pair.second.BuyQuantity },
					{ "sellQuantity", pair.second.SellQuantity },
				});
			}
			json parsed{ { "fetched", aFetchedAt }, { "prices", prices } };
			return WriteTextFile(aPath, parsed.dump());
		}

		bool LoadCurrencies(const std::filesystem::path& aPath, CurrencyDb& aCurrencies, i64& aFetchedAt)
		{
			std::string contents;
			if (!ReadTextFile(aPath, contents))
			{
				return false;
			}
			try
			{
				const json parsed = json::parse(contents);
				aFetchedAt = parsed.value("fetched", static_cast<i64>(0));
				if (!parsed.contains("currencies") || !parsed["currencies"].is_array())
				{
					return false;
				}
				for (const auto& object : parsed["currencies"])
				{
					CurrencyInfo currency;
					currency.Id = object.value("id", 0);
					currency.Name = object.value("name", std::string{});
					currency.Icon = object.value("icon", std::string{});
					currency.Description = object.value("description", std::string{});
					if (currency.Id != 0)
					{
						aCurrencies[currency.Id] = std::move(currency);
					}
				}
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool SaveCurrencies(const std::filesystem::path& aPath, const CurrencyDb& aCurrencies, i64 aFetchedAt)
		{
			json currencies = json::array();
			for (const auto& pair : aCurrencies)
			{
				currencies.push_back({
					{ "id", pair.second.Id },
					{ "name", pair.second.Name },
					{ "icon", pair.second.Icon },
					{ "description", pair.second.Description },
				});
			}
			json parsed{ { "fetched", aFetchedAt }, { "currencies", currencies } };
			return WriteTextFile(aPath, parsed.dump());
		}

		bool LoadAchievements(const std::filesystem::path& aPath, AchievementDb& aAchievements, i64& aFetchedAt)
		{
			std::string contents;
			if (!ReadTextFile(aPath, contents))
			{
				return false;
			}
			try
			{
				const json parsed = json::parse(contents);
				aFetchedAt = parsed.value("fetched", static_cast<i64>(0));
				if (!parsed.contains("achievements") || !parsed["achievements"].is_array())
				{
					return false;
				}
				for (const auto& object : parsed["achievements"])
				{
					AchievementInfo achievement;
					achievement.Id = object.value("id", 0);
					achievement.Name = object.value("name", std::string{});
					if (achievement.Id != 0)
					{
						aAchievements[achievement.Id] = std::move(achievement);
					}
				}
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool SaveAchievements(const std::filesystem::path& aPath, const AchievementDb& aAchievements, i64 aFetchedAt)
		{
			json achievements = json::array();
			for (const auto& pair : aAchievements)
			{
				achievements.push_back({
					{ "id", pair.second.Id },
					{ "name", pair.second.Name },
				});
			}
			json parsed{ { "fetched", aFetchedAt }, { "achievements", achievements } };
			return WriteTextFile(aPath, parsed.dump());
		}

		bool LoadAccount(const std::filesystem::path& aPath, AccountData& aAccount)
		{
			std::string contents;
			if (!ReadTextFile(aPath, contents))
			{
				return false;
			}
			try
			{
				const json parsed = json::parse(contents);
				aAccount.Valid = parsed.value("valid", false);
				aAccount.Name = parsed.value("name", std::string{});
				aAccount.SyncedAt = parsed.value("syncedAt", static_cast<i64>(0));
				aAccount.Error = parsed.value("error", std::string{});

				if (parsed.contains("permissions") && parsed["permissions"].is_array())
				{
					for (const auto& value : parsed["permissions"])
					{
						if (value.is_string())
						{
							aAccount.Permissions.push_back(value.get<std::string>());
						}
					}
				}
				if (parsed.contains("items") && parsed["items"].is_object())
				{
					for (auto it = parsed["items"].begin(); it != parsed["items"].end(); ++it)
					{
						if (it.value().is_number_integer())
						{
							aAccount.Items[std::stoi(it.key())] = it.value().get<int>();
						}
					}
				}
				if (parsed.contains("currencies") && parsed["currencies"].is_object())
				{
					for (auto it = parsed["currencies"].begin(); it != parsed["currencies"].end(); ++it)
					{
						if (it.value().is_number_integer())
						{
							aAccount.Currencies[std::stoi(it.key())] = it.value().get<i64>();
						}
					}
				}
				if (parsed.contains("sources") && parsed["sources"].is_object())
				{
					for (auto it = parsed["sources"].begin(); it != parsed["sources"].end(); ++it)
					{
						if (it.value().is_number_integer())
						{
							aAccount.Sources[it.key()] = it.value().get<int>();
						}
					}
				}
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool SaveAccount(const std::filesystem::path& aPath, const AccountData& aAccount)
		{
			json parsed;
			parsed["valid"] = aAccount.Valid;
			parsed["name"] = aAccount.Name;
			parsed["syncedAt"] = aAccount.SyncedAt;
			parsed["error"] = aAccount.Error;
			parsed["permissions"] = aAccount.Permissions;

			json items = json::object();
			for (const auto& pair : aAccount.Items)
			{
				items[std::to_string(pair.first)] = pair.second;
			}
			parsed["items"] = items;

			json currencies = json::object();
			for (const auto& pair : aAccount.Currencies)
			{
				currencies[std::to_string(pair.first)] = pair.second;
			}
			parsed["currencies"] = currencies;

			json sources = json::object();
			for (const auto& pair : aAccount.Sources)
			{
				sources[pair.first] = pair.second;
			}
			parsed["sources"] = sources;

			return WriteTextFile(aPath, parsed.dump());
		}

		bool LoadCatalog(const std::filesystem::path& aPath, std::vector<int>& aIds, i64& aFetchedAt)
		{
			std::string contents;
			if (!ReadTextFile(aPath, contents))
			{
				return false;
			}
			try
			{
				const json parsed = json::parse(contents);
				aFetchedAt = parsed.value("fetched", static_cast<i64>(0));
				if (!parsed.contains("ids") || !parsed["ids"].is_array())
				{
					return false;
				}
				aIds.clear();
				for (const auto& value : parsed["ids"])
				{
					if (value.is_number_integer())
					{
						aIds.push_back(value.get<int>());
					}
				}
				return !aIds.empty();
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool SaveCatalog(const std::filesystem::path& aPath, const std::vector<int>& aIds, i64 aFetchedAt)
		{
			json parsed{ { "fetched", aFetchedAt }, { "ids", aIds } };
			return WriteTextFile(aPath, parsed.dump());
		}

		bool LoadPlanCosts(const std::filesystem::path& aPath, std::map<int, i64>& aCosts)
		{
			std::string contents;
			if (!ReadTextFile(aPath, contents))
			{
				return false;
			}
			try
			{
				const json parsed = json::parse(contents);
				if (!parsed.is_object())
				{
					return false;
				}
				for (auto it = parsed.begin(); it != parsed.end(); ++it)
				{
					if (it.value().is_number_integer())
					{
						aCosts[std::stoi(it.key())] = it.value().get<i64>();
					}
				}
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool SavePlanCosts(const std::filesystem::path& aPath, const std::map<int, i64>& aCosts)
		{
			json parsed = json::object();
			for (const auto& pair : aCosts)
			{
				parsed[std::to_string(pair.first)] = pair.second;
			}
			return WriteTextFile(aPath, parsed.dump());
		}
	}
}
