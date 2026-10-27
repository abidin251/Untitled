#include <windows.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "App.h"
#include "Nexus.h"
#include "Parse.h"
#include "Planner.h"
#include "Ui.h"
#include "imgui.h"

namespace mc
{
	App* GApp = nullptr;
}

namespace
{
	constexpr std::uint32_t kSignature = 0x4D435231; // "MCR1"
	constexpr mc::i64 kRecipeCacheSeconds = 7 * 24 * 60 * 60;
	constexpr mc::i64 kPriceCacheSeconds = 5 * 60;
	constexpr mc::i64 kCatalogCacheSeconds = 24 * 60 * 60;
	constexpr mc::i64 kCurrencyCacheSeconds = 30 * 24 * 60 * 60;

	AddonDefinition_t g_Definition{};

	std::string TrimTrailingSlash(const std::string& aValue)
	{
		std::string result = aValue;
		while (!result.empty() && result.back() == '/')
		{
			result.pop_back();
		}
		return result;
	}

	std::string ChunkIds(const std::vector<int>& aIds, std::size_t aBegin, std::size_t aCount)
	{
		std::string result;
		for (std::size_t i = 0; i < aCount && aBegin + i < aIds.size(); ++i)
		{
			if (i > 0)
			{
				result.push_back(',');
			}
			result += std::to_string(aIds[aBegin + i]);
		}
		return result;
	}

	std::vector<int> UniqueIds(const std::vector<int>& aIds)
	{
		std::vector<int> result = aIds;
		std::sort(result.begin(), result.end());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		result.erase(std::remove(result.begin(), result.end(), 0), result.end());
		return result;
	}
}

namespace mc
{
	void App::Start()
	{
		const char* addonDirectory = Api->Paths_GetAddonDirectory("MysticCrafting");
		Directory = addonDirectory ? std::filesystem::u8path(addonDirectory) : std::filesystem::u8path("addons/MysticCrafting");
		CacheDirectory = Directory / "cache";
		store::EnsureDirectory(Directory);
		store::EnsureDirectory(CacheDirectory);
		store::EnsureDirectory(CacheDirectory / "recipes");

		std::string settingsError;
		store::LoadSettings(Directory / "settings.json", Settings, settingsError);
		if (!settingsError.empty())
		{
			Api->Log(LOGL_WARNING, "MysticCrafting", settingsError.c_str());
		}

		if (!Settings.RecipeBase.empty())
		{
			RecipeBase = TrimTrailingSlash(Settings.RecipeBase);
		}
		Settings.RecipeBase = RecipeBase;

		KeyInputBuffer = Settings.ApiKey;
		Visible = Settings.WindowVisible;

		store::LoadItems(CacheDirectory / "items.json", Items, ItemsFetchedAt);
		store::LoadPrices(CacheDirectory / "prices.json", Prices, PricesFetchedAt);
		store::LoadCurrencies(CacheDirectory / "currencies.json", Currencies, CurrenciesFetchedAt);
		store::LoadAccount(CacheDirectory / "account.json", Account);
		store::LoadPlanCosts(CacheDirectory / "plan-costs.json", KnownCosts);

		std::vector<int> catalogIds;
		store::LoadCatalog(CacheDirectory / "catalog.json", catalogIds, CatalogFetchedAt);
		if (!catalogIds.empty())
		{
			RebuildCatalogFromIds(catalogIds);
		}

		SelectedItem = Settings.SelectedItem;
		if (SelectedItem == 0 && !Catalog.empty())
		{
			SelectedItem = Catalog.front().Id;
		}

		StartWorker();

		const i64 now = NowUnix();
		if (Catalog.empty() || now - CatalogFetchedAt > kCatalogCacheSeconds)
		{
			ReloadCatalog(false);
		}

		if (!Settings.ApiKey.empty() && (Account.SyncedAt == 0 || now - Account.SyncedAt > 300))
		{
			SyncAccount();
		}

		if (SelectedItem != 0)
		{
			LoadPlan(SelectedItem, false);
		}
		else if (!Settings.Tracked.empty())
		{
			RefreshTrackedShopping();
		}

		Api->Log(LOGL_INFO, "MysticCrafting", "Mystic Crafting started.");
	}

	void App::Stop()
	{
		Settings.SelectedItem = SelectedItem;
		Settings.WindowVisible = Visible;
		SaveSettingsNow();
		StopWorker();
		Api->Log(LOGL_INFO, "MysticCrafting", "Mystic Crafting stopped.");
	}

	void App::SaveSettingsNow()
	{
		Settings.RecipeBase = RecipeBase;
		store::SaveSettings(Directory / "settings.json", Settings);
	}

	void App::SetStatus(const std::string& aStatus, const std::string& aError)
	{
		std::lock_guard<std::mutex> lock(Mutex);
		Status = aStatus;
		LastError = aError;
		if (!aError.empty() && Api)
		{
			Api->Log(LOGL_WARNING, "MysticCrafting", aError.c_str());
		}
	}

	void App::Notify(const std::string& aMessage)
	{
		std::lock_guard<std::mutex> lock(Mutex);
		PendingAlerts.push_back(aMessage);
	}

	void App::StartWorker()
	{
		Stopping = false;
		Worker = std::thread(&App::WorkerLoop, this);
	}

	void App::StopWorker()
	{
		{
			std::lock_guard<std::mutex> lock(QueueMutex);
			Stopping = true;
			JobQueue.clear();
		}
		QueueSignal.notify_all();
		if (Worker.joinable())
		{
			Worker.join();
		}
	}

	void App::WorkerLoop()
	{
		while (true)
		{
			std::function<void()> job;
			{
				std::unique_lock<std::mutex> lock(QueueMutex);
				QueueSignal.wait(lock, [this]() { return Stopping || !JobQueue.empty(); });
				if (Stopping)
				{
					return;
				}
				job = std::move(JobQueue.front());
				JobQueue.pop_front();
			}

			try
			{
				job();
			}
			catch (const std::exception& ex)
			{
				SetStatus("Unexpected error", ex.what());
			}
			catch (...)
			{
				SetStatus("Unexpected error", "An unknown error occurred.");
			}

			{
				std::lock_guard<std::mutex> lock(QueueMutex);
				--PendingJobs;
				Busy = PendingJobs > 0;
			}
		}
	}

	void App::Queue(std::function<void()> aJob)
	{
		{
			std::lock_guard<std::mutex> lock(QueueMutex);
			JobQueue.push_back(std::move(aJob));
			++PendingJobs;
			Busy = true;
		}
		QueueSignal.notify_one();
	}

	HttpResponse App::GetGw2(const std::string& aPath, const std::string& aApiKey)
	{
		return HttpGet(Gw2Base + aPath, aApiKey);
	}

	HttpResponse App::GetRecipe(int aItemId, const std::string& aBase)
	{
		std::string base = aBase.empty() ? RecipeBase : aBase;
		base = TrimTrailingSlash(base);
		return HttpGet(base + "/recipes?ids=" + std::to_string(aItemId));
	}

	std::filesystem::path App::RecipeCachePath(int aItemId) const
	{
		return CacheDirectory / "recipes" / (std::to_string(aItemId) + ".json");
	}

	bool App::EnsureItems(const std::vector<int>& aIds, std::string& aError)
	{
		std::vector<int> missing;
		{
			std::lock_guard<std::mutex> lock(Mutex);
			for (int id : UniqueIds(aIds))
			{
				if (Items.find(id) == Items.end())
				{
					missing.push_back(id);
				}
			}
		}

		if (missing.empty())
		{
			return true;
		}

		bool success = true;
		for (std::size_t offset = 0; offset < missing.size(); offset += 200)
		{
			const std::size_t count = std::min<std::size_t>(200, missing.size() - offset);
			const std::string path = "/v2/items?ids=" + ChunkIds(missing, offset, count);
			const HttpResponse response = GetGw2(path, {});
			if (!response.Ok())
			{
				aError += response.Error + "; ";
				success = false;
				continue;
			}

			const auto parsed = ParseItems(response.Body);
			{
				std::lock_guard<std::mutex> lock(Mutex);
				for (const auto& item : parsed)
				{
					Items[item.Id] = item;
				}
			}
		}

		{
			std::lock_guard<std::mutex> lock(Mutex);
			ItemsFetchedAt = NowUnix();
			store::SaveItems(CacheDirectory / "items.json", Items, ItemsFetchedAt);
		}
		return success;
	}

	bool App::EnsurePrices(const std::vector<int>& aIds, std::string& aError)
	{
		const bool forced = ForcePriceRefresh.exchange(false);
		const std::vector<int> ids = UniqueIds(aIds);
		if (ids.empty())
		{
			return true;
		}

		// A global timestamp is not enough: a plan can reference items that were not
		// part of the previous plan, so always fill gaps even while the cache is fresh.
		std::vector<int> missing;
		{
			std::lock_guard<std::mutex> lock(Mutex);
			for (int id : ids)
			{
				if (Prices.find(id) == Prices.end())
				{
					missing.push_back(id);
				}
			}
		}

		const bool cacheFresh = PricesFetchedAt != 0 && NowUnix() - PricesFetchedAt < kPriceCacheSeconds;
		if (!forced && cacheFresh && missing.empty())
		{
			return true;
		}

		const std::vector<int> fetchIds = forced ? ids : (cacheFresh ? missing : ids);
		if (fetchIds.empty())
		{
			return true;
		}

		bool success = true;
		for (std::size_t offset = 0; offset < fetchIds.size(); offset += 200)
		{
			const std::size_t count = std::min<std::size_t>(200, fetchIds.size() - offset);
			const std::string path = "/v2/commerce/prices?ids=" + ChunkIds(fetchIds, offset, count);
			const HttpResponse response = GetGw2(path, {});
			if (!response.Ok())
			{
				aError += response.Error + "; ";
				success = false;
				continue;
			}

			const auto parsed = ParsePrices(response.Body);
			{
				std::lock_guard<std::mutex> lock(Mutex);
				for (const auto& price : parsed)
				{
					Prices[price.Id] = price;
				}
			}
		}

		{
			std::lock_guard<std::mutex> lock(Mutex);
			PricesFetchedAt = NowUnix();
			store::SavePrices(CacheDirectory / "prices.json", Prices, PricesFetchedAt);
		}
		return success;
	}

	bool App::EnsureCurrencies(std::string& aError)
	{
		if (!Currencies.empty() && CurrenciesFetchedAt != 0 && NowUnix() - CurrenciesFetchedAt < kCurrencyCacheSeconds)
		{
			return true;
		}

		const HttpResponse response = GetGw2("/v2/currencies?ids=all", {});
		if (!response.Ok())
		{
			aError += response.Error + "; ";
			return false;
		}

		const auto parsed = ParseCurrencies(response.Body);
		{
			std::lock_guard<std::mutex> lock(Mutex);
			for (const auto& currency : parsed)
			{
				Currencies[currency.Id] = currency;
			}
			CurrenciesFetchedAt = NowUnix();
			store::SaveCurrencies(CacheDirectory / "currencies.json", Currencies, CurrenciesFetchedAt);
		}
		return true;
	}

	std::vector<int> App::CollectRecipeItemIds(const RecipeNode& aRoot) const
	{
		std::vector<int> ids;
		std::set<int> seen;

		std::function<void(const RecipeNode&)> walk = [&](const RecipeNode& aNode) {
			if (!aNode.IsCurrency() && aNode.Id != 0 && seen.insert(aNode.Id).second)
			{
				ids.push_back(aNode.Id);
			}
			for (const auto& child : aNode.Components)
			{
				walk(child);
			}
		};
		walk(aRoot);
		return ids;
	}

	void App::RebuildCatalogFromIds(const std::vector<int>& aIds)
	{
		std::vector<CatalogEntry> catalog;
		{
			std::lock_guard<std::mutex> lock(Mutex);
			for (int id : aIds)
			{
				auto it = Items.find(id);
				if (it == Items.end())
				{
					continue;
				}
				CatalogEntry entry;
				entry.Id = id;
				entry.Name = it->second.Name.empty() ? "Item #" + std::to_string(id) : it->second.Name;
				entry.Icon = it->second.Icon;
				entry.TypeLabel = ItemTypeLabel(it->second);
				entry.Rarity = it->second.Rarity;
				entry.Owned = Account.Items.find(id) != Account.Items.end() && Account.Items[id] > 0;
				catalog.push_back(std::move(entry));
			}
		}

		std::sort(catalog.begin(), catalog.end(), [](const CatalogEntry& a, const CatalogEntry& b) {
			if (a.TypeLabel != b.TypeLabel)
			{
				return a.TypeLabel < b.TypeLabel;
			}
			return a.Name < b.Name;
		});

		std::lock_guard<std::mutex> lock(Mutex);
		Catalog = std::move(catalog);
	}

	void App::RebuildKnownOwnership()
	{
		std::lock_guard<std::mutex> lock(Mutex);
		for (auto& entry : Catalog)
		{
			entry.Owned = Account.Items.find(entry.Id) != Account.Items.end() && Account.Items[entry.Id] > 0;
			KnownOwned[entry.Id] = entry.Owned;
		}
	}

	void App::ReloadCatalog(bool aForce)
	{
		Queue([this]() { ReloadCatalogJob(); });
		(void)aForce;
	}

	void App::ReloadCatalogJob()
	{
		SetStatus("Loading the legendary catalog...");
		std::string error;

		HttpResponse response = GetGw2("/v2/legendaryarmory", {});
		std::vector<int> ids;
		if (response.Ok())
		{
			ids = ParseIntArray(response.Body);
		}
		else
		{
			error = response.Error;
			i64 ignored = 0;
			store::LoadCatalog(CacheDirectory / "catalog.json", ids, ignored);
		}

		if (ids.empty())
		{
			SetStatus("Could not load the legendary catalog", error);
			return;
		}

		EnsureItems(ids, error);
		if (response.Ok())
		{
			std::lock_guard<std::mutex> lock(Mutex);
			CatalogFetchedAt = NowUnix();
			store::SaveCatalog(CacheDirectory / "catalog.json", ids, CatalogFetchedAt);
		}

		RebuildCatalogFromIds(ids);
		RebuildKnownOwnership();

		std::size_t count = 0;
		{
			std::lock_guard<std::mutex> lock(Mutex);
			count = Catalog.size();
		}
		SetStatus("Catalog ready: " + std::to_string(count) + " legendaries", error);
	}

	void App::LoadPlan(int aItemId, bool aForce)
	{
		if (aItemId == 0)
		{
			return;
		}
		const UserSettings snapshot = Settings;
		Queue([this, aItemId, aForce, snapshot]() { LoadPlanJob(aItemId, aForce, snapshot); });
	}

	bool App::LoadRecipeTree(int aItemId, bool aForce, const UserSettings& aSettings, RecipeNode& aRoot, std::string& aError, std::string& aWarning)
	{
		const auto cachePath = RecipeCachePath(aItemId);
		std::string cachedBody;
		const bool hasCache = store::ReadTextFile(cachePath, cachedBody) && !cachedBody.empty();
		const bool freshCache = hasCache && NowUnix() - store::FileTimestamp(cachePath) < kRecipeCacheSeconds;
		std::string parseError;

		if (!aForce && freshCache && ParseRecipeTree(cachedBody, aRoot, parseError))
		{
			aRoot.Id = aRoot.Id == 0 ? aItemId : aRoot.Id;
			return true;
		}

		const HttpResponse response = GetRecipe(aItemId, aSettings.RecipeBase);
		if (response.Ok() && ParseRecipeTree(response.Body, aRoot, parseError))
		{
			store::WriteTextFile(cachePath, response.Body);
			aRoot.Id = aRoot.Id == 0 ? aItemId : aRoot.Id;
			return true;
		}

		if (hasCache && ParseRecipeTree(cachedBody, aRoot, parseError))
		{
			aRoot.Id = aRoot.Id == 0 ? aItemId : aRoot.Id;
			if (!response.Ok())
			{
				aWarning = response.Error + " - showing the last cached data.";
			}
			return true;
		}

		aError = response.Error.empty() ? parseError : response.Error;
		return false;
	}

	// Research notes are produced by crafting an item at a station and letting
	// the craft convert into notes, so the plan prices them from the ingredients
	// of that craft. Every cheap candidate is loaded so the planner can keep
	// whichever turns out to be the cheapest per note.
	bool App::LoadResearchNoteSources(const UserSettings& aSettings, std::vector<int>& aItemIds, std::vector<RecipeNode>& aOut)
	{
		if (!aSettings.IncludeResearchNotes)
		{
			return false;
		}

		for (int id : ResearchNoteSourceCandidates())
		{
			std::string error;
			std::string warning;
			RecipeNode node;
			if (!LoadRecipeTree(id, false, aSettings, node, error, warning) || !node.IsRecipe())
			{
				// A candidate without a usable recipe simply cannot be a source.
				continue;
			}

			const std::vector<int> ids = CollectRecipeItemIds(node);
			aItemIds.insert(aItemIds.end(), ids.begin(), ids.end());
			aOut.push_back(std::move(node));
		}

		return !aOut.empty();
	}

	void App::RefreshTrackedShopping()
	{
		const UserSettings snapshot = Settings;
		Queue([this, snapshot]() { BuildTrackedPlanJob(snapshot); });
	}

	void App::BuildTrackedPlanJob(const UserSettings& aSettings)
	{
		if (!aSettings.CombineTracked || aSettings.Tracked.empty())
		{
			std::lock_guard<std::mutex> lock(Mutex);
			TrackedPlan.reset();
			return;
		}

		std::vector<int> tracked = UniqueIds(aSettings.Tracked);
		std::vector<RecipeNode> roots;
		roots.reserve(tracked.size());
		std::string warning;
		for (int id : tracked)
		{
			RecipeNode root;
			std::string error;
			if (LoadRecipeTree(id, false, aSettings, root, error, warning))
			{
				roots.push_back(std::move(root));
			}
		}

		if (roots.empty())
		{
			std::lock_guard<std::mutex> lock(Mutex);
			TrackedPlan.reset();
			return;
		}

		std::vector<int> itemIds;
		for (const auto& root : roots)
		{
			const std::vector<int> ids = CollectRecipeItemIds(root);
			itemIds.insert(itemIds.end(), ids.begin(), ids.end());
		}

		std::vector<RecipeNode> researchSources;
		const bool hasResearchSources = LoadResearchNoteSources(aSettings, itemIds, researchSources);

		std::string dependencyError;
		EnsureCurrencies(dependencyError);
		EnsureItems(itemIds, dependencyError);
		EnsurePrices(itemIds, dependencyError);

		ItemDb items;
		PriceDb prices;
		CurrencyDb currencies;
		AccountData account;
		{
			std::lock_guard<std::mutex> lock(Mutex);
			items = Items;
			prices = Prices;
			currencies = Currencies;
			if (aSettings.IncludeAccountItems && Account.Valid)
			{
				account = Account;
			}
		}

		PlanOptions options;
		options.PreferBuyingTradable = aSettings.PreferBuyingTradable;
		options.BuyOrderPrices = aSettings.BuyOrderPrices;
		options.IncludeAccountItems = aSettings.IncludeAccountItems;
		options.IncludeResearchNotes = aSettings.IncludeResearchNotes;
		if (hasResearchSources)
		{
			options.ResearchNoteSources = &researchSources;
		}

		std::map<int, Preference> preferences;
		for (const auto& pair : aSettings.Preferences)
		{
			preferences[pair.first] = static_cast<Preference>(pair.second);
		}

		Plan combined = BuildCombinedPlan(roots, items, prices, currencies, account, options, preferences);
		auto pointer = std::make_shared<Plan>(std::move(combined));
		{
			std::lock_guard<std::mutex> lock(Mutex);
			TrackedPlan = pointer;
		}
	}

	void App::LoadPlanJob(int aItemId, bool aForce, const UserSettings& aSettings)
	{
		if (aItemId == 0)
		{
			return;
		}

		SetStatus("Loading acquisition data...");
		RecipeNode root;
		std::string error;
		std::string warning;
		if (!LoadRecipeTree(aItemId, aForce, aSettings, root, error, warning))
		{
			SetStatus("Could not load acquisition data", error);
			return;
		}

		std::string dependencyError;
		std::vector<int> itemIds = CollectRecipeItemIds(root);
		std::vector<RecipeNode> researchSources;
		const bool hasResearchSources = LoadResearchNoteSources(aSettings, itemIds, researchSources);
		EnsureCurrencies(dependencyError);
		EnsureItems(itemIds, dependencyError);
		EnsurePrices(itemIds, dependencyError);

		ItemDb items;
		PriceDb prices;
		CurrencyDb currencies;
		AccountData account;
		{
			std::lock_guard<std::mutex> lock(Mutex);
			items = Items;
			prices = Prices;
			currencies = Currencies;
			if (aSettings.IncludeAccountItems && Account.Valid)
			{
				account = Account;
			}
		}

		PlanOptions options;
		options.PreferBuyingTradable = aSettings.PreferBuyingTradable;
		options.BuyOrderPrices = aSettings.BuyOrderPrices;
		options.IncludeAccountItems = aSettings.IncludeAccountItems;
		options.IncludeResearchNotes = aSettings.IncludeResearchNotes;
		if (hasResearchSources)
		{
			options.ResearchNoteSources = &researchSources;
		}

		std::map<int, Preference> preferences;
		for (const auto& pair : aSettings.Preferences)
		{
			preferences[pair.first] = static_cast<Preference>(pair.second);
		}

		Plan plan = BuildPlan(root, 1, items, prices, currencies, account, options, preferences);
		auto pointer = std::make_shared<Plan>(std::move(plan));

		std::map<int, i64> knownCosts;
		{
			std::lock_guard<std::mutex> lock(Mutex);
			CurrentPlan = pointer;
			KnownCosts[pointer->GoalId] = pointer->RemainingCost;
			KnownOwned[pointer->GoalId] = pointer->Complete;
			knownCosts = KnownCosts;
		}
		store::SavePlanCosts(CacheDirectory / "plan-costs.json", knownCosts);

		std::string finalError = dependencyError;
		if (finalError.empty())
		{
			finalError = warning;
		}
		SetStatus("Loaded " + pointer->GoalName + " (" + FormatCoins(pointer->RemainingCost) + " remaining)", finalError);

		if (aSettings.CombineTracked && !aSettings.Tracked.empty())
		{
			BuildTrackedPlanJob(aSettings);
		}
		else
		{
			std::lock_guard<std::mutex> lock(Mutex);
			TrackedPlan.reset();
		}
	}

	void App::SyncAccount()
	{
		const UserSettings snapshot = Settings;
		const int goalId = SelectedItem;
		Queue([this, snapshot, goalId]() { SyncAccountJob(snapshot, goalId); });
	}

	void App::SyncAccountJob(const UserSettings& aSettings, int aGoalId)
	{
		if (aSettings.ApiKey.empty())
		{
			SetStatus("No API key configured", "Add an API key on the Account tab to synchronize your materials.");
			return;
		}

		SetStatus("Synchronizing account data...");
		const HttpResponse tokenResponse = GetGw2("/v2/tokeninfo", aSettings.ApiKey);
		if (!tokenResponse.Ok())
		{
			SetStatus("API key rejected", tokenResponse.Error);
			return;
		}

		AccountData account;
		try
		{
			const auto token = nlohmann::json::parse(tokenResponse.Body);
			account.Name = token.value("name", std::string{});
			if (token.contains("permissions") && token["permissions"].is_array())
			{
				for (const auto& permission : token["permissions"])
				{
					if (permission.is_string())
					{
						account.Permissions.push_back(permission.get<std::string>());
					}
				}
			}
		}
		catch (const std::exception& ex)
		{
			SetStatus("Could not read account data", ex.what());
			return;
		}

		account.Valid = true;
		std::string warnings;

		auto addItemArray = [&account](const nlohmann::json& aArray, const std::string& aSource) {
			if (!aArray.is_array())
			{
				return;
			}
			for (const auto& object : aArray)
			{
				if (!object.is_object())
				{
					continue;
				}
				const int id = object.value("id", 0);
				const int count = object.value("count", 1);
				if (id > 0 && count > 0)
				{
					account.Items[id] += count;
					account.Sources[aSource] += count;
				}
			}
		};

		auto fetchArray = [this, &aSettings, &warnings](const char* aPath, nlohmann::json& aOut) {
			const HttpResponse response = GetGw2(aPath, aSettings.ApiKey);
			if (!response.Ok())
			{
				warnings += std::string(aPath) + ": " + response.Error + "; ";
				return false;
			}
			try
			{
				aOut = nlohmann::json::parse(response.Body);
				return true;
			}
			catch (const std::exception&)
			{
				warnings += std::string(aPath) + ": invalid response; ";
				return false;
			}
		};

		const bool canInventory = account.HasPermission("account") && account.HasPermission("inventories");
		const bool canWallet = account.HasPermission("account") && account.HasPermission("wallet");
		const bool canCharacters = account.HasPermission("characters");

		nlohmann::json data;

		if (aSettings.IncludeMaterials && canInventory && fetchArray("/v2/account/materials", data))
		{
			addItemArray(data, "Material Storage");
		}
		if (aSettings.IncludeBank && canInventory && fetchArray("/v2/account/bank", data))
		{
			addItemArray(data, "Bank");
		}
		if (aSettings.IncludeSharedInventory && canInventory && fetchArray("/v2/account/inventory", data))
		{
			addItemArray(data, "Shared Inventory");
		}
		if (aSettings.IncludeArmory && account.HasPermission("unlocks") && fetchArray("/v2/account/legendaryarmory", data))
		{
			addItemArray(data, "Legendary Armory");
		}
		if (canWallet && fetchArray("/v2/account/wallet", data) && data.is_array())
		{
			for (const auto& entry : data)
			{
				if (entry.is_object())
				{
					account.Currencies[entry.value("id", 0)] += entry.value("value", static_cast<i64>(0));
				}
			}
		}
		else if (!canWallet)
		{
			warnings += "wallet permission missing; ";
		}
		if (!canInventory)
		{
			warnings += "inventories permission missing; ";
		}

		if (aSettings.IncludeCharacters && canCharacters && fetchArray("/v2/characters?ids=all", data) && data.is_array())
		{
			for (const auto& character : data)
			{
				if (!character.is_object())
				{
					continue;
				}
				const std::string name = character.value("name", std::string{});
				if (name.empty())
				{
					continue;
				}
				const HttpResponse inventory = GetGw2("/v2/characters/" + UrlEncode(name) + "/inventory", aSettings.ApiKey);
				if (!inventory.Ok())
				{
					continue;
				}
				try
				{
					const auto bags = nlohmann::json::parse(inventory.Body);
					if (bags.contains("bags") && bags["bags"].is_array())
					{
						for (const auto& bag : bags["bags"])
						{
							if (bag.is_object() && bag.contains("inventory"))
							{
								addItemArray(bag["inventory"], "Character Bags");
							}
						}
					}
				}
				catch (const std::exception&)
				{
				}
			}
		}
		else if (aSettings.IncludeCharacters && !canCharacters)
		{
			warnings += "characters permission missing; ";
		}

		account.SyncedAt = NowUnix();
		account.Error = warnings;

		{
			std::lock_guard<std::mutex> lock(Mutex);
			Account = account;
			store::SaveAccount(CacheDirectory / "account.json", Account);
		}

		RebuildKnownOwnership();

		std::size_t itemKinds = account.Items.size();
		SetStatus("Account synchronized" + (account.Name.empty() ? "" : " for " + account.Name), warnings);
		Notify("Mystic Crafting: account synchronized (" + std::to_string(itemKinds) + " item stacks).");

		if (aGoalId != 0)
		{
			LoadPlanJob(aGoalId, false, aSettings);
		}
	}

	void App::ClearCache()
	{
		const int goalId = SelectedItem;
		const UserSettings snapshot = Settings;
		Queue([this, goalId, snapshot]() {
			std::error_code error;
			std::filesystem::remove_all(CacheDirectory, error);
			store::EnsureDirectory(CacheDirectory);
			store::EnsureDirectory(CacheDirectory / "recipes");
			{
				std::lock_guard<std::mutex> lock(Mutex);
				Items.clear();
				Prices.clear();
				Currencies.clear();
				Catalog.clear();
				Account = AccountData{};
				CurrentPlan.reset();
				TrackedPlan.reset();
				KnownCosts.clear();
				KnownOwned.clear();
				ItemsFetchedAt = 0;
				PricesFetchedAt = 0;
				CurrenciesFetchedAt = 0;
				CatalogFetchedAt = 0;
			}
			SetStatus("Cache cleared.");
			Notify("Mystic Crafting cache cleared.");
			ReloadCatalogJob();
			if (goalId != 0)
			{
				LoadPlanJob(goalId, true, snapshot);
			}
		});
	}
}

// -----------------------------------------------------------------------------
// Nexus entry points
// -----------------------------------------------------------------------------

namespace
{
	void OnKeybind(const char*, bool aIsRelease)
	{
		if (!aIsRelease && mc::GApp)
		{
			mc::GApp->ToggleRequested = true;
		}
	}

	void RenderCallback()
	{
		mc::App* app = mc::GApp;
		if (!app)
		{
			return;
		}

		if (app->ToggleRequested.exchange(false))
		{
			app->Visible = !app->Visible;
			app->Settings.WindowVisible = app->Visible;
		}

		std::vector<std::string> alerts;
		{
			std::lock_guard<std::mutex> lock(app->Mutex);
			alerts.swap(app->PendingAlerts);
		}
		for (const auto& alert : alerts)
		{
			app->Api->GUI_SendAlert(alert.c_str());
		}

		if (app->Visible)
		{
			mc::RenderMain(*app);
		}
	}

	void OptionsCallback()
	{
		if (mc::GApp)
		{
			mc::RenderOptions(*mc::GApp);
		}
	}

	void AddonLoad(AddonAPI_t* aApi)
	{
		ImGui::SetCurrentContext(static_cast<ImGuiContext*>(aApi->ImguiContext));
		ImGui::SetAllocatorFunctions(
			reinterpret_cast<void* (*)(size_t, void*)>(aApi->ImguiMalloc),
			reinterpret_cast<void (*)(void*, void*)>(aApi->ImguiFree));

		auto* app = new mc::App();
		app->Api = aApi;
		mc::GApp = app;
		app->Start();

		aApi->GUI_Register(RT_Render, RenderCallback);
		aApi->GUI_Register(RT_OptionsRender, OptionsCallback);
		aApi->GUI_RegisterCloseOnEscape("Mystic Crafting", &app->Visible);
		aApi->InputBinds_RegisterWithString("KB_MYSTICCRAFTING_TOGGLE", OnKeybind, "(null)");

		HMODULE module = nullptr;
		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&AddonLoad),
			&module);
		aApi->Textures_LoadFromResource("ICON_MYSTICCRAFTING", 101, module, nullptr);
		aApi->Textures_LoadFromResource("ICON_MYSTICCRAFTING_HOVER", 102, module, nullptr);
		aApi->QuickAccess_Add(
			"SC_MYSTICCRAFTING",
			"ICON_MYSTICCRAFTING",
			"ICON_MYSTICCRAFTING_HOVER",
			"KB_MYSTICCRAFTING_TOGGLE",
			"Mystic Crafting");
	}

	void AddonUnload()
	{
		mc::App* app = mc::GApp;
		if (!app)
		{
			return;
		}

		app->Api->QuickAccess_Remove("SC_MYSTICCRAFTING");
		app->Api->InputBinds_Deregister("KB_MYSTICCRAFTING_TOGGLE");
		app->Api->GUI_DeregisterCloseOnEscape("Mystic Crafting");
		app->Api->GUI_Deregister(RenderCallback);
		app->Api->GUI_Deregister(OptionsCallback);

		app->Stop();
		delete app;
		mc::GApp = nullptr;
	}
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
	g_Definition.Signature = kSignature;
	g_Definition.APIVersion = NEXUS_API_VERSION;
	g_Definition.Name = "Mystic Crafting";
	g_Definition.Version = { 1, 7, 0, 0 };
	g_Definition.Author = "Mystic Crafting Nexus port";
	g_Definition.Description = "Legendary and item crafting planner with live prices and account synchronization.";
	g_Definition.Load = AddonLoad;
	g_Definition.Unload = AddonUnload;
	g_Definition.Flags = AF_None;
	g_Definition.Provider = UP_None;
	g_Definition.UpdateLink = "";
	return &g_Definition;
}
