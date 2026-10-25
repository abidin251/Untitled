#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Core.h"
#include "Http.h"
#include "Nexus.h"
#include "Store.h"

namespace mc
{
	struct App
	{
		AddonAPI_t* Api = nullptr;
		std::filesystem::path Directory;
		std::filesystem::path CacheDirectory;
		UserSettings Settings;

		std::string Gw2Base = "https://api.guildwars2.com";
		std::string RecipeBase = "https://api.gw2efficiency.com";

		mutable std::mutex Mutex;
		ItemDb Items;
		PriceDb Prices;
		CurrencyDb Currencies;
		AccountData Account;
		std::vector<CatalogEntry> Catalog;
		std::shared_ptr<const Plan> CurrentPlan;
		std::shared_ptr<const Plan> TrackedPlan;
		std::map<int, i64> KnownCosts;
		std::map<int, bool> KnownOwned;

		i64 ItemsFetchedAt = 0;
		i64 PricesFetchedAt = 0;
		i64 CurrenciesFetchedAt = 0;
		i64 CatalogFetchedAt = 0;

		std::string Status = "Starting up...";
		std::string LastError;
		std::atomic<bool> Busy{ false };
		std::atomic<bool> ToggleRequested{ false };
		std::atomic<bool> ForcePriceRefresh{ false };
		std::vector<std::string> PendingAlerts;

		// Render-thread UI state (not shared with the worker).
		bool Visible = false;
		int SelectedItem = 0;
		int ActiveTab = 0;
		bool TabRequest = false;
		std::string Search;
		int TypeFilter = 0;
		int StatusFilter = 0;
		int SortKey = 0;
		bool SortAscending = true;
		bool OnlyTracked = false;
		bool ShowDebugData = false;

		std::string KeyInputBuffer;

		void Start();
		void Stop();
		void SaveSettingsNow();

		void ReloadCatalog(bool aForce);
		void LoadPlan(int aItemId, bool aForce);
		void RefreshTrackedShopping();
		void SyncAccount();
		void ClearCache();

		void Queue(std::function<void()> aJob);
		void SetStatus(const std::string& aStatus, const std::string& aError = {});
		void Notify(const std::string& aMessage);

		// Worker-side helpers.
		HttpResponse GetGw2(const std::string& aPath, const std::string& aApiKey);
		HttpResponse GetRecipe(int aItemId, const std::string& aBase);
		bool EnsureItems(const std::vector<int>& aIds, std::string& aError);
		bool EnsurePrices(const std::vector<int>& aIds, std::string& aError);
		bool EnsureCurrencies(std::string& aError);
		void RebuildCatalogFromIds(const std::vector<int>& aIds);
		void RebuildKnownOwnership();

		std::vector<int> CollectRecipeItemIds(const RecipeNode& aRoot) const;

		std::filesystem::path RecipeCachePath(int aItemId) const;

	private:
		void ReloadCatalogJob();
		bool LoadRecipeTree(int aItemId, bool aForce, const UserSettings& aSettings, RecipeNode& aRoot, std::string& aError, std::string& aWarning);
		void LoadPlanJob(int aItemId, bool aForce, const UserSettings& aSettings);
		void BuildTrackedPlanJob(const UserSettings& aSettings);
		void SyncAccountJob(const UserSettings& aSettings, int aGoalId);

		void StartWorker();
		void StopWorker();
		void WorkerLoop();

		std::thread Worker;
		std::mutex QueueMutex;
		std::condition_variable QueueSignal;
		std::deque<std::function<void()>> JobQueue;
		bool Stopping = false;
		int PendingJobs = 0;
	};

	extern App* GApp;
}
