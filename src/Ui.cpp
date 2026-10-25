#include "Ui.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include "Planner.h"
#include "imgui.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace mc
{
	namespace
	{
		struct UiSnapshot
		{
			std::vector<CatalogEntry> Catalog;
			std::map<int, i64> Costs;
			std::shared_ptr<const Plan> PlanPtr;
			std::shared_ptr<const Plan> TrackedPlan;
			AccountData Account;
			CurrencyDb Currencies;
			std::string Status;
			std::string Error;
		};

		std::map<std::string, Texture_t*> g_TextureCache;

		std::string TrimTrailingSlash(const std::string& aValue)
		{
			std::string result = aValue;
			while (!result.empty() && result.back() == '/')
			{
				result.pop_back();
			}
			return result;
		}

		UiSnapshot TakeSnapshot(App& aApp)
		{
			UiSnapshot snapshot;
			std::lock_guard<std::mutex> lock(aApp.Mutex);
			snapshot.Catalog = aApp.Catalog;
			snapshot.Costs = aApp.KnownCosts;
			snapshot.PlanPtr = aApp.CurrentPlan;
			snapshot.TrackedPlan = aApp.TrackedPlan;
			snapshot.Account = aApp.Account;
			snapshot.Currencies = aApp.Currencies;
			snapshot.Status = aApp.Status;
			snapshot.Error = aApp.LastError;
			return snapshot;
		}

		bool IsTracked(const App& aApp, int aItemId)
		{
			return std::find(aApp.Settings.Tracked.begin(), aApp.Settings.Tracked.end(), aItemId) != aApp.Settings.Tracked.end();
		}

		void SetTracked(App& aApp, int aItemId, bool aTracked)
		{
			auto& tracked = aApp.Settings.Tracked;
			auto it = std::find(tracked.begin(), tracked.end(), aItemId);
			if (aTracked && it == tracked.end())
			{
				tracked.push_back(aItemId);
			}
			else if (!aTracked && it != tracked.end())
			{
				tracked.erase(it);
			}
			aApp.SaveSettingsNow();
			aApp.RefreshTrackedShopping();
		}

		void CopyToClipboard(const std::string& aText)
		{
#ifdef _WIN32
			const int length = MultiByteToWideChar(CP_UTF8, 0, aText.c_str(), -1, nullptr, 0);
			if (length <= 0)
			{
				return;
			}
			std::wstring wide(static_cast<std::size_t>(length), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, aText.c_str(), -1, wide.data(), length);
			wide.resize(static_cast<std::size_t>(length - 1));

			if (!OpenClipboard(nullptr))
			{
				return;
			}
			EmptyClipboard();
			const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
			HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
			if (memory)
			{
				void* target = GlobalLock(memory);
				if (target)
				{
					std::memcpy(target, wide.c_str(), bytes);
					GlobalUnlock(memory);
					SetClipboardData(CF_UNICODETEXT, memory);
				}
			}
			CloseClipboard();
#endif
		}

		ImVec4 RarityColor(const std::string& aRarity)
		{
			if (aRarity == "Junk") return ImVec4(0.67f, 0.67f, 0.67f, 1.0f);
			if (aRarity == "Fine") return ImVec4(0.38f, 0.64f, 0.85f, 1.0f);
			if (aRarity == "Masterwork") return ImVec4(0.10f, 0.58f, 0.02f, 1.0f);
			if (aRarity == "Rare") return ImVec4(0.99f, 0.82f, 0.04f, 1.0f);
			if (aRarity == "Exotic") return ImVec4(1.00f, 0.64f, 0.02f, 1.0f);
			if (aRarity == "Ascended") return ImVec4(0.98f, 0.24f, 0.55f, 1.0f);
			if (aRarity == "Legendary") return ImVec4(0.62f, 0.27f, 0.93f, 1.0f);
			return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		Texture_t* GetTexture(App& aApp, const std::string& aUrl, const std::string& aKey)
		{
			if (aUrl.empty())
			{
				return nullptr;
			}
			auto cached = g_TextureCache.find(aKey);
			if (cached != g_TextureCache.end())
			{
				return cached->second;
			}

			const std::size_t scheme = aUrl.find("://");
			const std::size_t slash = scheme == std::string::npos ? std::string::npos : aUrl.find('/', scheme + 3);
			if (slash == std::string::npos)
			{
				return nullptr;
			}

			const std::string host = aUrl.substr(0, slash);
			const std::string path = aUrl.substr(slash);
			const std::string identifier = "MC_" + aKey;
			Texture_t* texture = aApp.Api->Textures_GetOrCreateFromURL(identifier.c_str(), host.c_str(), path.c_str());
			if (texture)
			{
				g_TextureCache[aKey] = texture;
			}
			return texture;
		}

		void DrawIcon(App& aApp, const std::string& aUrl, const std::string& aKey, float aSize)
		{
			Texture_t* texture = GetTexture(aApp, aUrl, aKey);
			if (texture)
			{
				ImGui::Image(reinterpret_cast<ImTextureID>(texture->Resource), ImVec2(aSize, aSize));
			}
			else
			{
				ImGui::Dummy(ImVec2(aSize, aSize));
			}
		}

		std::string CurrencyText(const CurrencyDb& aCurrencies, int aId, i64 aAmount)
		{
			if (aId == 1)
			{
				return FormatCoins(aAmount);
			}
			return FormatNumber(aAmount);
		}

		std::string CurrencyName(const CurrencyDb& aCurrencies, int aId)
		{
			auto it = aCurrencies.find(aId);
			if (it != aCurrencies.end() && !it->second.Name.empty())
			{
				return it->second.Name;
			}
			return "Currency #" + std::to_string(aId);
		}

		ImVec4 SourceColor(SourceKind aKind)
		{
			switch (aKind)
			{
			case SourceKind::TradingPost: return ImVec4(0.55f, 0.80f, 0.95f, 1.0f);
			case SourceKind::MysticForge: return ImVec4(0.78f, 0.52f, 0.98f, 1.0f);
			case SourceKind::Vendor: return ImVec4(0.95f, 0.82f, 0.45f, 1.0f);
			case SourceKind::Achievement: return ImVec4(0.50f, 0.88f, 0.55f, 1.0f);
			case SourceKind::Container: return ImVec4(0.65f, 0.85f, 0.85f, 1.0f);
			case SourceKind::Currency: return ImVec4(0.90f, 0.75f, 0.40f, 1.0f);
			case SourceKind::Craft: return ImVec4(0.85f, 0.85f, 0.90f, 1.0f);
			default: return ImVec4(0.62f, 0.62f, 0.66f, 1.0f);
			}
		}

		std::string TimeAgo(i64 aTimestamp)
		{
			if (aTimestamp == 0)
			{
				return "never";
			}
			i64 delta = NowUnix() - aTimestamp;
			if (delta < 0) delta = 0;
			if (delta < 60) return "just now";
			if (delta < 3600) return std::to_string(delta / 60) + "m ago";
			if (delta < 86400) return std::to_string(delta / 3600) + "h ago";
			return std::to_string(delta / 86400) + "d ago";
		}

		void ShowItemTooltip(App& aApp, int aItemId, bool aIsCurrency)
		{
			ItemInfo item;
			PriceInfo price;
			CurrencyInfo currency;
			bool hasItem = false;
			bool hasPrice = false;
			bool hasCurrency = false;

			{
				std::lock_guard<std::mutex> lock(aApp.Mutex);
				auto itemIt = aApp.Items.find(aItemId);
				if (itemIt != aApp.Items.end())
				{
					item = itemIt->second;
					hasItem = true;
				}
				auto priceIt = aApp.Prices.find(aItemId);
				if (priceIt != aApp.Prices.end())
				{
					price = priceIt->second;
					hasPrice = true;
				}
				auto currencyIt = aApp.Currencies.find(aItemId);
				if (currencyIt != aApp.Currencies.end())
				{
					currency = currencyIt->second;
					hasCurrency = true;
				}
			}

			if (aIsCurrency && !hasCurrency)
			{
				return;
			}
			if (!aIsCurrency && !hasItem)
			{
				return;
			}

			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);

			if (hasItem)
			{
				DrawIcon(aApp, item.Icon, "item_" + std::to_string(aItemId), 40.0f);
				ImGui::SameLine();
				ImGui::BeginGroup();
				ImGui::TextColored(RarityColor(item.Rarity), "%s", item.Name.c_str());
				if (!item.Subtype.empty() || !item.Type.empty())
				{
					ImGui::TextDisabled("%s%s%s", ItemTypeLabel(item).c_str(), item.Level > 0 ? " - Level " : "", item.Level > 0 ? std::to_string(item.Level).c_str() : "");
				}
				ImGui::EndGroup();

				if (!item.Description.empty())
				{
					ImGui::Separator();
					ImGui::TextUnformatted(item.Description.c_str());
				}
			}
			else
			{
				DrawIcon(aApp, currency.Icon, "currency_" + std::to_string(aItemId), 40.0f);
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.45f, 1.0f), "%s", currency.Name.c_str());
			}

			if (hasPrice)
			{
				ImGui::Separator();
				ImGui::Text("Instant buy: %s", FormatCoins(price.Sell).c_str());
				ImGui::Text("Buy order: %s", FormatCoins(price.Buy).c_str());
				ImGui::TextDisabled("Supply: %s listings / %s orders", FormatNumber(price.SellQuantity).c_str(), FormatNumber(price.BuyQuantity).c_str());
			}
			else if (hasItem)
			{
				ImGui::Separator();
				ImGui::TextDisabled("Not available on the Trading Post");
			}

			if (hasItem && item.VendorValue > 0)
			{
				ImGui::TextDisabled("Vendor value: %s", FormatCoins(item.VendorValue).c_str());
			}

			if (hasItem && !item.ChatLink.empty())
			{
				ImGui::Separator();
				ImGui::TextDisabled("%s", item.ChatLink.c_str());
				ImGui::TextDisabled("Right-click the item in the tree to copy its chat link.");
			}

			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		std::string BuildShoppingText(const Plan& aPlan, const CurrencyDb& aCurrencies)
		{
			std::ostringstream stream;
			stream << "Mystic Crafting - " << aPlan.GoalName << "\r\n";
			if (aPlan.GoalCount > 1)
			{
				stream << "Combined tracked plans: " << aPlan.GoalCount << "\r\n";
			}
			stream << "Remaining cost: " << FormatCoins(aPlan.RemainingCost) << "\r\n\r\n";				for (const auto& entry : aPlan.Shopping)
				{
					stream << entry.Quantity << " x " << entry.Name << " (" << FormatCoins(entry.UnitCost) << " each) = " << FormatCoins(entry.TotalCost);
					stream << "\r\n";
				}

			if (!aPlan.CurrencyNeeded.empty())
			{
				stream << "\r\nCurrencies:\r\n";
				for (const auto& pair : aPlan.CurrencyNeeded)
				{
					if (pair.second <= 0)
					{
						continue;
					}
					stream << pair.second << " " << CurrencyName(aCurrencies, pair.first) << "\r\n";
				}
			}

			return stream.str();
		}

		bool CatalogRowMatches(const App& aApp, const CatalogEntry& aEntry)
		{
			if (!aApp.Search.empty())
			{
				if (ToLower(aEntry.Name).find(aApp.Search) == std::string::npos)
				{
					return false;
				}
			}
			if (aApp.StatusFilter == 1 && !aEntry.Owned)
			{
				return false;
			}
			if (aApp.StatusFilter == 2 && aEntry.Owned)
			{
				return false;
			}
			if (aApp.OnlyTracked && !IsTracked(aApp, aEntry.Id))
			{
				return false;
			}
			return true;
		}

		void DrawCatalog(App& aApp, const UiSnapshot& aSnapshot)
		{
			static char searchBuffer[128] = {};
			static std::string lastSearch;
			if (lastSearch != aApp.Search)
			{
				std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", aApp.Search.c_str());
				lastSearch = aApp.Search;
			}

			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputTextWithHint("##mc_search", "Search legendaries...", searchBuffer, sizeof(searchBuffer)))
			{
				aApp.Search = ToLower(Trim(searchBuffer));
				lastSearch = aApp.Search;
			}

			std::set<std::string> typeSet;
			for (const auto& entry : aSnapshot.Catalog)
			{
				if (!entry.TypeLabel.empty())
				{
					typeSet.insert(entry.TypeLabel);
				}
			}
			std::vector<std::string> types(typeSet.begin(), typeSet.end());
			if (aApp.TypeFilter < 0 || aApp.TypeFilter > static_cast<int>(types.size()))
			{
				aApp.TypeFilter = 0;
			}

			ImGui::SetNextItemWidth(140.0f);
			const char* typePreview = aApp.TypeFilter == 0 ? "All types" : types[static_cast<std::size_t>(aApp.TypeFilter - 1)].c_str();
			if (ImGui::BeginCombo("##mc_type", typePreview))
			{
				if (ImGui::Selectable("All types", aApp.TypeFilter == 0))
				{
					aApp.TypeFilter = 0;
				}
				for (std::size_t i = 0; i < types.size(); ++i)
				{
					if (ImGui::Selectable(types[i].c_str(), aApp.TypeFilter == static_cast<int>(i) + 1))
					{
						aApp.TypeFilter = static_cast<int>(i) + 1;
					}
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(110.0f);
			const char* statuses[] = { "All", "Owned", "Not owned" };
			if (ImGui::BeginCombo("##mc_status", statuses[aApp.StatusFilter]))
			{
				for (int i = 0; i < 3; ++i)
				{
					if (ImGui::Selectable(statuses[i], aApp.StatusFilter == i))
					{
						aApp.StatusFilter = i;
					}
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			ImGui::Checkbox("Tracked only", &aApp.OnlyTracked);

			static char itemIdBuffer[16] = {};
			ImGui::SetNextItemWidth(110.0f);
			ImGui::InputTextWithHint("##mc_item_id", "Item ID", itemIdBuffer, sizeof(itemIdBuffer), ImGuiInputTextFlags_CharsDecimal);
			ImGui::SameLine();
			if (ImGui::Button("Open item"))
			{
				const int itemId = std::atoi(itemIdBuffer);
				if (itemId > 0)
				{
					aApp.SelectedItem = itemId;
					aApp.Settings.SelectedItem = itemId;
					aApp.LoadPlan(itemId, true);
				}
			}

			std::vector<const CatalogEntry*> rows;
			for (const auto& entry : aSnapshot.Catalog)
			{
				if (!CatalogRowMatches(aApp, entry))
				{
					continue;
				}
				if (aApp.TypeFilter > 0 && entry.TypeLabel != types[static_cast<std::size_t>(aApp.TypeFilter - 1)])
				{
					continue;
				}
				rows.push_back(&entry);
			}

			std::sort(rows.begin(), rows.end(), [&aApp, &aSnapshot](const CatalogEntry* a, const CatalogEntry* b) {
				int result = 0;
				switch (aApp.SortKey)
				{
				case 1:
					result = a->TypeLabel.compare(b->TypeLabel);
					break;
				case 2:
				{
					const i64 costA = aSnapshot.Costs.count(a->Id) ? aSnapshot.Costs.at(a->Id) : (std::numeric_limits<i64>::max)();
					const i64 costB = aSnapshot.Costs.count(b->Id) ? aSnapshot.Costs.at(b->Id) : (std::numeric_limits<i64>::max)();
					result = costA < costB ? -1 : (costA > costB ? 1 : 0);
					break;
				}
				case 3:
					result = static_cast<int>(a->Owned) - static_cast<int>(b->Owned);
					break;
				default:
					result = a->Name.compare(b->Name);
					break;
				}
				if (result == 0)
				{
					result = a->Name.compare(b->Name);
				}
				return aApp.SortAscending ? result < 0 : result > 0;
			});

			const float rowHeight = aApp.Settings.CompactDensity ? 22.0f : 28.0f;
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("##mc_catalog", 3, flags, ImVec2(0, ImGui::GetContentRegionAvail().y)))
			{
				ImGui::TableSetupColumn("Legendary", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0, 0);
				ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.0f, 1);
				ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, 95.0f, 2);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
				{
					if (specs->SpecsDirty && specs->SpecsCount > 0)
					{
						aApp.SortKey = static_cast<int>(specs->Specs[0].ColumnUserID);
						aApp.SortAscending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
						specs->SpecsDirty = false;
					}
				}

				for (const CatalogEntry* entry : rows)
				{
					ImGui::TableNextRow(0, rowHeight);
					ImGui::TableSetColumnIndex(0);
					ImGui::PushID(entry->Id);

					const ImVec2 rowStart = ImGui::GetCursorScreenPos();
					const bool selected = aApp.SelectedItem == entry->Id;
					if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap, ImVec2(0, rowHeight)))
					{
						aApp.SelectedItem = entry->Id;
						aApp.Settings.SelectedItem = entry->Id;
						aApp.LoadPlan(entry->Id, false);
					}

					const bool hovered = ImGui::IsItemHovered();
					if (ImGui::BeginPopupContextItem("##rowctx"))
					{
						if (ImGui::MenuItem(IsTracked(aApp, entry->Id) ? "Untrack" : "Track"))
						{
							SetTracked(aApp, entry->Id, !IsTracked(aApp, entry->Id));
						}
						if (ImGui::MenuItem("Load plan"))
						{
							aApp.SelectedItem = entry->Id;
							aApp.Settings.SelectedItem = entry->Id;
							aApp.LoadPlan(entry->Id, false);
						}
						ImGui::EndPopup();
					}

					ImGui::SetCursorScreenPos(rowStart);
					DrawIcon(aApp, entry->Icon, "item_" + std::to_string(entry->Id), rowHeight - 6.0f);
					ImGui::SameLine(0, 6.0f);
					if (IsTracked(aApp, entry->Id))
					{
						ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.45f, 1.0f), "[T]");
						ImGui::SameLine();
					}
					if (entry->Owned)
					{
						ImGui::TextColored(ImVec4(0.50f, 0.88f, 0.55f, 1.0f), "%s", entry->Name.c_str());
					}
					else
					{
						ImGui::TextUnformatted(entry->Name.c_str());
					}

					ImGui::TableSetColumnIndex(1);
					ImGui::TextDisabled("%s", entry->TypeLabel.c_str());

					ImGui::TableSetColumnIndex(2);
					auto cost = aSnapshot.Costs.find(entry->Id);
					if (cost != aSnapshot.Costs.end())
					{
						ImGui::Text("%s", FormatCoins(cost->second).c_str());
					}
					else
					{
						ImGui::TextDisabled("-");
					}

					if (hovered && !ImGui::IsPopupOpen("##rowctx"))
					{
						ShowItemTooltip(aApp, entry->Id, false);
					}
					ImGui::PopID();
				}

				ImGui::EndTable();
			}
		}

		void DrawPlanNode(App& aApp, const UiSnapshot& aSnapshot, const PlanNode& aNode, std::vector<std::pair<int, int>>& aPreferenceUpdates)
		{
			const bool hasChildren = !aNode.Children.empty();
			ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_OpenOnArrow;
			if (!hasChildren)
			{
				treeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			}
			if (hasChildren && aNode.Depth < 2)
			{
				treeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(&aNode);

			const bool open = ImGui::TreeNodeEx("##node", treeFlags, "");
			const bool hovered = ImGui::IsItemHovered();

			if (ImGui::BeginPopupContextItem("##nodectx"))
			{
				const Preference current = [&]() {
					auto it = aApp.Settings.Preferences.find(aNode.Id);
					return it == aApp.Settings.Preferences.end() ? Preference::Auto : static_cast<Preference>(it->second);
				}();

				if (ImGui::MenuItem("Source: automatic (cheapest)", nullptr, current == Preference::Auto))
				{
					aPreferenceUpdates.emplace_back(aNode.Id, static_cast<int>(Preference::Auto));
				}
				if (ImGui::MenuItem("Source: always craft", nullptr, current == Preference::Craft))
				{
					aPreferenceUpdates.emplace_back(aNode.Id, static_cast<int>(Preference::Craft));
				}
				if (ImGui::MenuItem("Source: always buy", nullptr, current == Preference::Buy, aNode.Tradable))
				{
					aPreferenceUpdates.emplace_back(aNode.Id, static_cast<int>(Preference::Buy));
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Copy name"))
				{
					CopyToClipboard(aNode.Name);
					aApp.Notify("Item name copied.");
				}
				if (ImGui::MenuItem("Copy chat link", nullptr, false, !aNode.ChatLink.empty()))
				{
					CopyToClipboard(aNode.ChatLink);
					aApp.Notify("Chat link copied.");
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine(0, 4.0f);
			DrawIcon(aApp, aNode.Icon, (aNode.IsCurrency ? "currency_" : "item_") + std::to_string(aNode.Id), 18.0f);
			ImGui::SameLine(0, 5.0f);

			if (aNode.Complete)
			{
				ImGui::TextDisabled("%s", aNode.Name.c_str());
			}
			else
			{
				ImGui::TextColored(RarityColor(aNode.Rarity), "%s", aNode.Name.c_str());
			}

			if (aNode.Crafts > 1)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(%dx)", aNode.Crafts);
			}

			if (ImGui::IsItemHovered())
			{
				ShowItemTooltip(aApp, aNode.Id, aNode.IsCurrency);
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", aNode.Required);

			ImGui::TableSetColumnIndex(2);
			if (aNode.Owned > 0)
			{
				ImGui::TextColored(ImVec4(0.50f, 0.88f, 0.55f, 1.0f), "%d", aNode.Owned);
			}
			else
			{
				ImGui::TextDisabled("0");
			}

			ImGui::TableSetColumnIndex(3);
			if (aNode.Remaining > 0)
			{
				ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%d", aNode.Remaining);
			}
			else
			{
				ImGui::TextDisabled("-");
			}

			ImGui::TableSetColumnIndex(4);
			ImGui::TextColored(SourceColor(aNode.Source), "%s", SourceKindLabel(aNode.Source));
			if (ImGui::IsItemHovered() && !aNode.SourceDetail.empty())
			{
				ImGui::SetTooltip("%s", aNode.SourceDetail.c_str());
			}

			ImGui::TableSetColumnIndex(5);
			if (aNode.TotalCost > 0)
			{
				ImGui::Text("%s", FormatCoins(aNode.TotalCost).c_str());
			}
			else
			{
				ImGui::TextDisabled("-");
			}

			if (hovered)
			{
				ShowItemTooltip(aApp, aNode.Id, aNode.IsCurrency);
			}

			if (open && hasChildren)
			{
				for (const auto& child : aNode.Children)
				{
					if (!aApp.Settings.ShowCompleted && child.Complete)
					{
						continue;
					}
					DrawPlanNode(aApp, aSnapshot, child, aPreferenceUpdates);
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}

		void DrawPlan(App& aApp, const UiSnapshot& aSnapshot)
		{
			const std::shared_ptr<const Plan>& plan = aSnapshot.PlanPtr;
			if (!plan || plan->GoalId != aApp.SelectedItem)
			{
				ImGui::Dummy(ImVec2(0, 12));
				if (aApp.Busy)
				{
					ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.45f, 1.0f), "Loading acquisition data...");
				}
				else
				{
					ImGui::TextWrapped("Select a legendary on the left, or search for one, to build a crafting plan.");
				}
				if (!aSnapshot.Error.empty())
				{
					ImGui::Spacing();
					ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", aSnapshot.Error.c_str());
				}
				return;
			}

			ImGui::PushID(plan->GoalId);
			DrawIcon(aApp, plan->GoalIcon, "item_" + std::to_string(plan->GoalId), 42.0f);
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::TextColored(RarityColor(plan->GoalRarity), "%s", plan->GoalName.c_str());
			ImGui::TextDisabled("%s", plan->GoalType.c_str());
			ImGui::EndGroup();

			ImGui::SameLine();
			const bool tracked = IsTracked(aApp, plan->GoalId);
			if (ImGui::Button(tracked ? "Untrack" : "Track"))
			{
				SetTracked(aApp, plan->GoalId, !tracked);
			}
			ImGui::SameLine();
			if (ImGui::Button("Refresh"))
			{
				aApp.ForcePriceRefresh = true;
				aApp.LoadPlan(plan->GoalId, true);
			}
			ImGui::SameLine();
			if (ImGui::Button("Copy list"))
			{
				CopyToClipboard(BuildShoppingText(*plan, aSnapshot.Currencies));
				aApp.Notify("Shopping list copied to the clipboard.");
			}

			char progressText[128];
			std::snprintf(progressText, sizeof(progressText), "%.0f%% - %s remaining", plan->Progress() * 100.0f, FormatCoins(plan->RemainingCost).c_str());
			ImGui::ProgressBar(plan->Progress(), ImVec2(-1, 0), progressText);

			ImGui::Text("Remaining: %s", FormatCoins(plan->RemainingCost).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("| From scratch: %s", FormatCoins(plan->FullCost).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("| Nodes: %d", plan->NodeCount);
			if (plan->Complete)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.50f, 0.88f, 0.55f, 1.0f), "| Already owned");
			}

			ImGui::Spacing();
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("##mc_plan", 6, flags, ImVec2(0, ImGui::GetContentRegionAvail().y)))
			{
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Need", ImGuiTableColumnFlags_WidthFixed, 55.0f);
				ImGui::TableSetupColumn("Owned", ImGuiTableColumnFlags_WidthFixed, 55.0f);
				ImGui::TableSetupColumn("Missing", ImGuiTableColumnFlags_WidthFixed, 62.0f);
				ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 120.0f);
				ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, 95.0f);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				std::vector<std::pair<int, int>> preferenceUpdates;
				DrawPlanNode(aApp, aSnapshot, plan->Root, preferenceUpdates);
				ImGui::EndTable();

				if (!preferenceUpdates.empty())
				{
					for (const auto& update : preferenceUpdates)
					{
						aApp.Settings.Preferences[update.first] = update.second;
					}
					aApp.SaveSettingsNow();
					aApp.LoadPlan(plan->GoalId, false);
				}
			}

			ImGui::PopID();
		}

		void DrawShoppingList(App& aApp, const UiSnapshot& aSnapshot)
		{
			const int trackedCount = static_cast<int>(aApp.Settings.Tracked.size());
			if (trackedCount > 0)
			{
				bool combine = aApp.Settings.CombineTracked;
				if (ImGui::Checkbox("Combine all tracked plans", &combine))
				{
					aApp.Settings.CombineTracked = combine;
					aApp.SaveSettingsNow();
					aApp.RefreshTrackedShopping();
				}
				ImGui::SameLine();
				ImGui::TextDisabled("(%d tracked %s)", trackedCount, trackedCount == 1 ? "plan" : "plans");
			}

			const bool useCombined = aApp.Settings.CombineTracked && trackedCount > 0;
			const std::shared_ptr<const Plan>& plan = useCombined ? aSnapshot.TrackedPlan : aSnapshot.PlanPtr;
			if (!plan)
			{
				if (useCombined)
				{
					if (aApp.Busy)
					{
						ImGui::TextDisabled("Building the combined shopping list...");
					}
					else
					{
						ImGui::TextWrapped("The combined shopping list is not ready. Use Refresh on a tracked plan, or untrack a plan and open it again.");
					}
				}
				if (!aSnapshot.PlanPtr)
				{
					ImGui::TextWrapped("No plan loaded yet. Select a legendary on the Planner tab.");
				}
				return;
			}

			if (plan->GoalCount > 1)
			{
				ImGui::Text("Shopping list for %d tracked plans", plan->GoalCount);
			}
			else
			{
				ImGui::Text("Shopping list for %s", plan->GoalName.c_str());
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%s remaining)", FormatCoins(plan->RemainingCost).c_str());
			if (plan->GoalCount > 1)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("| %s from scratch", FormatCoins(plan->FullCost).c_str());
			}
			if (ImGui::Button("Copy list"))
			{
				CopyToClipboard(BuildShoppingText(*plan, aSnapshot.Currencies));
				aApp.Notify("Shopping list copied to the clipboard.");
			}

			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

			const float tableHeight = ImGui::GetContentRegionAvail().y * 0.58f;
			if (ImGui::BeginTable("##mc_shopping", 4, flags, ImVec2(0, tableHeight)))
			{
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_WidthFixed, 70.0f);
				ImGui::TableSetupColumn("Unit price", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableHeadersRow();

				for (const auto& entry : plan->Shopping)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					DrawIcon(aApp, entry.Icon, "item_" + std::to_string(entry.Id), 16.0f);
					ImGui::SameLine(0, 4.0f);
					ImGui::TextUnformatted(entry.Name.c_str());
					if (ImGui::IsItemHovered())
					{
						ShowItemTooltip(aApp, entry.Id, entry.Currency);
					}
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%d", entry.Quantity);
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%s", FormatCoins(entry.UnitCost).c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%s", FormatCoins(entry.TotalCost).c_str());
				}

				ImGui::EndTable();
			}

			i64 tradingPostTotal = 0;
			for (const auto& entry : plan->Shopping)
			{
				if (entry.Source == SourceKind::TradingPost)
				{
					tradingPostTotal += entry.TotalCost;
				}
			}
			ImGui::Text("Trading Post total: %s", FormatCoins(tradingPostTotal).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("| Other gold costs: %s", FormatCoins(std::max<i64>(0, plan->RemainingCost - tradingPostTotal)).c_str());

			ImGui::Text("Currencies and wallet costs");
			ImGui::SameLine();
			ImGui::TextDisabled("(required / owned / missing)");
			if (ImGui::BeginTable("##mc_currency", 4, flags, ImVec2(0, ImGui::GetContentRegionAvail().y)))
			{
				ImGui::TableSetupColumn("Currency", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Required", ImGuiTableColumnFlags_WidthFixed, 110.0f);
				ImGui::TableSetupColumn("Owned", ImGuiTableColumnFlags_WidthFixed, 110.0f);
				ImGui::TableSetupColumn("Missing", ImGuiTableColumnFlags_WidthFixed, 110.0f);
				ImGui::TableHeadersRow();

				std::map<int, i64> rows = plan->CurrencyRequired;
				for (const auto& pair : plan->CurrencyNeeded)
				{
					if (pair.second > rows[pair.first])
					{
						rows[pair.first] = pair.second;
					}
				}
				for (const auto& pair : rows)
				{
					if (pair.second <= 0)
					{
						continue;
					}
					i64 missing = 0;
					auto missingIt = plan->CurrencyNeeded.find(pair.first);
					if (missingIt != plan->CurrencyNeeded.end())
					{
						missing = missingIt->second;
					}
					i64 owned = 0;
					auto ownedIt = plan->CurrencyOwned.find(pair.first);
					if (ownedIt != plan->CurrencyOwned.end())
					{
						owned = ownedIt->second;
					}
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(CurrencyName(aSnapshot.Currencies, pair.first).c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(CurrencyText(aSnapshot.Currencies, pair.first, pair.second).c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(CurrencyText(aSnapshot.Currencies, pair.first, owned).c_str());
					ImGui::TableSetColumnIndex(3);
					if (missing > 0)
					{
						ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "%s", CurrencyText(aSnapshot.Currencies, pair.first, missing).c_str());
					}
					else
					{
						ImGui::TextColored(ImVec4(0.50f, 0.88f, 0.55f, 1.0f), "covered");
					}
				}

				ImGui::EndTable();
			}
		}

		const char* StepVerb(CraftAction aAction)
		{
			switch (aAction)
			{
			case CraftAction::MysticForge: return "Forge";
			case CraftAction::Buy: return "Buy";
			case CraftAction::Vendor: return "Buy";
			case CraftAction::Gather: return "Gather";
			case CraftAction::Achievement: return "Unlock";
			case CraftAction::Obtain: return "Obtain";
			case CraftAction::Craft:
			default: return "Craft";
			}
		}

		std::string StepQuantity(const CraftStep& aStep)
		{
			const bool crafts = aStep.Action == CraftAction::Craft || aStep.Action == CraftAction::MysticForge;
			if (crafts)
			{
				return std::to_string(aStep.Quantity) + (aStep.Quantity == 1 ? " craft" : " crafts");
			}
			if (aStep.Action == CraftAction::Vendor)
			{
				return "x" + FormatNumber(aStep.Quantity);
			}
			if (aStep.Currency)
			{
				return FormatNumber(aStep.Remaining);
			}
			return "x" + FormatNumber(aStep.Remaining);
		}

		std::string BuildStepsText(const Plan& aPlan, const std::vector<CraftStep>& aSteps)
		{
			std::ostringstream stream;
			stream << "Mystic Crafting - " << aPlan.GoalName << " - crafting steps\r\n";
			stream << "Remaining cost: " << FormatCoins(aPlan.RemainingCost) << "\r\n\r\n";

			int number = 1;
			for (const auto& step : aSteps)
			{
				stream << number++ << ". " << StepVerb(step.Action) << " " << step.Name << " (" << StepQuantity(step) << ")";
				if (!step.Detail.empty())
				{
					stream << " - " << step.Detail;
				}
				if (step.Cost > 0)
				{
					stream << " = " << FormatCoins(step.Cost);
				}
				stream << "\r\n";
			}
			return stream.str();
		}

		void DrawCraftingSteps(App& aApp, const UiSnapshot& aSnapshot)
		{
			const std::shared_ptr<const Plan>& plan = aSnapshot.PlanPtr;
			if (!plan)
			{
				ImGui::TextWrapped("No plan loaded yet. Select a legendary on the Planner tab.");
				return;
			}

			const std::vector<CraftStep> steps = BuildCraftingSteps(*plan);
			ImGui::Text("Crafting steps for %s", plan->GoalName.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("(%d steps, %s remaining)", static_cast<int>(steps.size()), FormatCoins(plan->RemainingCost).c_str());
			if (ImGui::Button("Copy steps"))
			{
				CopyToClipboard(BuildStepsText(*plan, steps));
				aApp.Notify("Crafting steps copied to the clipboard.");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("Steps are ordered so every ingredient is made or bought before it is used.");

			if (steps.empty())
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.50f, 0.88f, 0.55f, 1.0f), "Nothing left to do - everything for this plan is already owned.");
				return;
			}

			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("##mc_steps", 4, flags, ImVec2(0, ImGui::GetContentRegionAvail().y)))
			{
				ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
				ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Amount", ImGuiTableColumnFlags_WidthFixed, 100.0f);
				ImGui::TableSetupColumn("Cost", ImGuiTableColumnFlags_WidthFixed, 110.0f);
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				int number = 1;
				for (const auto& step : steps)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextDisabled("%d", number++);

					ImGui::TableSetColumnIndex(1);
					DrawIcon(aApp, step.Icon, (step.Currency ? "currency_" : "item_") + std::to_string(step.Id), 18.0f);
					ImGui::SameLine(0, 5.0f);
					ImGui::BeginGroup();
					ImGui::TextUnformatted((std::string(StepVerb(step.Action)) + " " + step.Name).c_str());
					if (!step.Detail.empty())
					{
						ImGui::TextDisabled("%s", step.Detail.c_str());
					}
					ImGui::EndGroup();
					if (ImGui::IsItemHovered())
					{
						ShowItemTooltip(aApp, step.Id, step.Currency);
					}

					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(StepQuantity(step).c_str());

					ImGui::TableSetColumnIndex(3);
					if (step.Cost > 0)
					{
						ImGui::TextUnformatted(FormatCoins(step.Cost).c_str());
					}
					else
					{
						ImGui::TextDisabled("-");
					}
				}

				ImGui::EndTable();
			}
		}

		void DrawAccountTab(App& aApp, const UiSnapshot& aSnapshot)
		{
			static char keyBuffer[256] = {};
			static std::string lastKey;
			if (lastKey != aApp.KeyInputBuffer)
			{
				std::snprintf(keyBuffer, sizeof(keyBuffer), "%s", aApp.KeyInputBuffer.c_str());
				lastKey = aApp.KeyInputBuffer;
			}

			ImGui::TextUnformatted("Guild Wars 2 API key");
			ImGui::SetNextItemWidth(520.0f);
			if (ImGui::InputText("##mc_api_key", keyBuffer, sizeof(keyBuffer), ImGuiInputTextFlags_Password))
			{
				aApp.KeyInputBuffer = keyBuffer;
				lastKey = aApp.KeyInputBuffer;
			}
			ImGui::SameLine();
			if (ImGui::Button("Save and synchronize"))
			{
				aApp.Settings.ApiKey = Trim(aApp.KeyInputBuffer);
				aApp.SaveSettingsNow();
				aApp.SyncAccount();
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear key"))
			{
				aApp.Settings.ApiKey.clear();
				aApp.KeyInputBuffer.clear();
				keyBuffer[0] = '\0';
				lastKey.clear();
				aApp.SaveSettingsNow();
			}

			ImGui::TextDisabled("Create a key with the account, inventories, characters, wallet and unlocks permissions.");
			ImGui::TextDisabled("The key is encrypted with Windows DPAPI before it is written to settings.json.");

			ImGui::Spacing();
			if (aSnapshot.Account.Valid)
			{
				ImGui::TextColored(ImVec4(0.50f, 0.88f, 0.55f, 1.0f), "Connected as %s", aSnapshot.Account.Name.empty() ? "(unknown account)" : aSnapshot.Account.Name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("| synced %s", TimeAgo(aSnapshot.Account.SyncedAt).c_str());
				if (!aSnapshot.Account.Permissions.empty())
				{
					std::ostringstream permissions;
					for (std::size_t i = 0; i < aSnapshot.Account.Permissions.size(); ++i)
					{
						if (i > 0) permissions << ", ";
						permissions << aSnapshot.Account.Permissions[i];
					}
					ImGui::TextDisabled("Permissions: %s", permissions.str().c_str());
				}
				if (!aSnapshot.Account.Error.empty())
				{
					ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.45f, 1.0f), "%s", aSnapshot.Account.Error.c_str());
				}
			}
			else
			{
				ImGui::TextDisabled("No account connected. Prices and recipes still work; ownership tracking needs a key.");
			}

			ImGui::Spacing();
			if (ImGui::Button("Synchronize now"))
			{
				aApp.SyncAccount();
			}
			ImGui::SameLine();
			if (ImGui::Button("Refresh prices"))
			{
				aApp.ForcePriceRefresh = true;
				if (aApp.SelectedItem != 0)
				{
					aApp.LoadPlan(aApp.SelectedItem, false);
				}
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Data sources");
			bool changed = false;
			changed |= ImGui::Checkbox("Material storage", &aApp.Settings.IncludeMaterials);
			ImGui::SameLine();
			changed |= ImGui::Checkbox("Bank", &aApp.Settings.IncludeBank);
			ImGui::SameLine();
			changed |= ImGui::Checkbox("Shared inventory", &aApp.Settings.IncludeSharedInventory);
			changed |= ImGui::Checkbox("Character bags", &aApp.Settings.IncludeCharacters);
			ImGui::SameLine();
			changed |= ImGui::Checkbox("Legendary armory", &aApp.Settings.IncludeArmory);
			if (changed)
			{
				aApp.SaveSettingsNow();
				aApp.SyncAccount();
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Wallet");
			if (aSnapshot.Account.Currencies.empty())
			{
				ImGui::TextDisabled("No wallet data synchronized yet.");
			}
			else
			{
				int shown = 0;
				for (const auto& pair : aSnapshot.Account.Currencies)
				{
					if (shown++ >= 10)
					{
						break;
					}
					ImGui::Text("  %s: %s", CurrencyName(aSnapshot.Currencies, pair.first).c_str(), CurrencyText(aSnapshot.Currencies, pair.first, pair.second).c_str());
				}
			}
		}

		void DrawSettingsTab(App& aApp)
		{
			ImGui::TextUnformatted("Planning");
			bool changed = false;
			changed |= ImGui::Checkbox("Prefer buying tradable components when cheaper", &aApp.Settings.PreferBuyingTradable);
			changed |= ImGui::Checkbox("Use buy order prices instead of instant buy prices", &aApp.Settings.BuyOrderPrices);
			changed |= ImGui::Checkbox("Subtract owned materials from the plan", &aApp.Settings.IncludeAccountItems);
			changed |= ImGui::Checkbox("Show completed entries in the tree", &aApp.Settings.ShowCompleted);
			changed |= ImGui::Checkbox("Compact display density", &aApp.Settings.CompactDensity);
			if (changed)
			{
				aApp.SaveSettingsNow();
				if (aApp.SelectedItem != 0)
				{
					aApp.LoadPlan(aApp.SelectedItem, false);
				}
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Acquisition data provider");
			ImGui::TextDisabled("Mystic Forge, merchant and achievement sources come from the public gw2efficiency recipe API.");
			static char providerBuffer[256] = {};
			static std::string lastProvider;
			if (lastProvider != aApp.Settings.RecipeBase)
			{
				std::snprintf(providerBuffer, sizeof(providerBuffer), "%s", aApp.Settings.RecipeBase.c_str());
				lastProvider = aApp.Settings.RecipeBase;
			}
			ImGui::SetNextItemWidth(520.0f);
			if (ImGui::InputText("##mc_provider", providerBuffer, sizeof(providerBuffer)))
			{
				lastProvider = providerBuffer;
			}
			ImGui::SameLine();
			if (ImGui::Button("Apply"))
			{
				aApp.Settings.RecipeBase = Trim(providerBuffer);
				aApp.RecipeBase = TrimTrailingSlash(aApp.Settings.RecipeBase);
				aApp.SaveSettingsNow();
				if (aApp.SelectedItem != 0)
				{
					aApp.LoadPlan(aApp.SelectedItem, true);
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset"))
			{
				aApp.Settings.RecipeBase = "https://api.gw2efficiency.com";
				aApp.RecipeBase = aApp.Settings.RecipeBase;
				std::snprintf(providerBuffer, sizeof(providerBuffer), "%s", aApp.Settings.RecipeBase.c_str());
				lastProvider = aApp.Settings.RecipeBase;
				aApp.SaveSettingsNow();
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Tracked plans");
			if (aApp.Settings.Tracked.empty())
			{
				ImGui::TextDisabled("Nothing tracked yet. Use Track on a plan or the row context menu.");
			}
			else
			{
				for (std::size_t i = 0; i < aApp.Settings.Tracked.size(); )
				{
					const int id = aApp.Settings.Tracked[i];
					std::string trackedName = "Item #" + std::to_string(id);
					{
						std::lock_guard<std::mutex> lock(aApp.Mutex);
						auto itemIt = aApp.Items.find(id);
						if (itemIt != aApp.Items.end() && !itemIt->second.Name.empty())
						{
							trackedName = itemIt->second.Name;
						}
					}
					ImGui::PushID(id);
					ImGui::TextUnformatted(trackedName.c_str());
					ImGui::SameLine();
					if (ImGui::SmallButton("Open"))
					{
						aApp.SelectedItem = id;
						aApp.Settings.SelectedItem = id;
						aApp.LoadPlan(id, false);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove"))
					{
						aApp.Settings.Tracked.erase(aApp.Settings.Tracked.begin() + static_cast<std::ptrdiff_t>(i));
						aApp.SaveSettingsNow();
						aApp.RefreshTrackedShopping();
						ImGui::PopID();
						continue;
					}
					ImGui::PopID();
					++i;
				}
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Cache");
			ImGui::TextDisabled("Recipe graphs, item metadata, prices and account data are cached under addons/MysticCrafting/cache.");
			if (ImGui::Button("Clear cache and reload"))
			{
				aApp.ClearCache();
			}
			ImGui::SameLine();
			if (ImGui::Button("Refresh catalog"))
			{
				aApp.ReloadCatalog(true);
			}
		}

		void DrawAboutTab(App& aApp)
		{
			ImGui::TextUnformatted("Mystic Crafting - Nexus port");
			ImGui::TextDisabled("Version 1.2.0 | API revision %d", NEXUS_API_VERSION);
			ImGui::Separator();
			ImGui::TextWrapped(
				"A clean-room port of the Blish HUD Mystic Crafting module. It builds a complete acquisition tree for "
				"legendary and other items, synchronizes your account, prices every purchase and tracks your progress.");
			ImGui::Spacing();
			ImGui::TextUnformatted("Features");
			ImGui::BulletText("Legendary catalog with search, type and ownership filters, sorting and compact density");
			ImGui::BulletText("Full requirement tree with recipes, Mystic Forge, vendors, achievements and containers");
			ImGui::BulletText("Account synchronization: materials, bank, shared slots, character bags, wallet and armory");
			ImGui::BulletText("Live Trading Post prices, buy-or-craft comparison and per-item source overrides");
			ImGui::BulletText("Dependency-ordered crafting steps with amounts, sources, costs and clipboard export");
			ImGui::BulletText("Aggregated shopping list for one plan or all tracked plans, with shared ownership and clipboard export");
			ImGui::BulletText("Item tooltips with rarity, description, prices and chat links");

			ImGui::Spacing();
			ImGui::TextUnformatted("Data sources");
			ImGui::BulletText("ArenaNet Guild Wars 2 API: items, recipes, prices, account data");
			ImGui::BulletText("gw2efficiency public recipe API: Mystic Forge, merchant and achievement sources");
			ImGui::TextDisabled("Guild Wars 2 and all related assets are trademarks of ArenaNet, LLC. This addon is unofficial and not affiliated with ArenaNet.");

			ImGui::Spacing();
			ImGui::TextUnformatted("Disclaimer");
			ImGui::TextWrapped(
				"The original Blish HUD module is closed-source, so this is an independent implementation of its feature set, "
				"not a copy of its code or data. Costs are estimates based on the selected price basis and the acquisition graph; "
				"verify expensive purchases in game.");
		}

		void DrawTopBar(App& aApp, const UiSnapshot& aSnapshot)
		{
			if (aApp.Busy)
			{
				ImGui::TextColored(ImVec4(0.95f, 0.82f, 0.45f, 1.0f), "Working...");
			}
			else
			{
				ImGui::TextColored(ImVec4(0.50f, 0.88f, 0.55f, 1.0f), "Ready");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("| %s", aSnapshot.Status.c_str());

			if (!aSnapshot.Error.empty())
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "(i)");
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", aSnapshot.Error.c_str());
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Catalog"))
			{
				aApp.ReloadCatalog(true);
			}
			ImGui::SameLine();
			if (ImGui::Button("Account"))
			{
				aApp.SyncAccount();
			}
			ImGui::SameLine();
			if (ImGui::Button("Prices"))
			{
				aApp.ForcePriceRefresh = true;
				if (aApp.SelectedItem != 0)
				{
					aApp.LoadPlan(aApp.SelectedItem, false);
				}
			}
			ImGui::Separator();
		}
	}

	void RenderMain(App& aApp)
	{
		const UiSnapshot snapshot = TakeSnapshot(aApp);

		ImGui::SetNextWindowSize(ImVec2(1220.0f, 780.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Mystic Crafting", &aApp.Visible, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			return;
		}

		aApp.Settings.WindowVisible = aApp.Visible;
		DrawTopBar(aApp, snapshot);

		const ImGuiTabItemFlags selectPlanner = aApp.TabRequest && aApp.ActiveTab == 0 ? ImGuiTabItemFlags_SetSelected : 0;
		const ImGuiTabItemFlags selectSteps = aApp.TabRequest && aApp.ActiveTab == 1 ? ImGuiTabItemFlags_SetSelected : 0;
		const ImGuiTabItemFlags selectShopping = aApp.TabRequest && aApp.ActiveTab == 2 ? ImGuiTabItemFlags_SetSelected : 0;
		const ImGuiTabItemFlags selectAccount = aApp.TabRequest && aApp.ActiveTab == 3 ? ImGuiTabItemFlags_SetSelected : 0;
		const ImGuiTabItemFlags selectSettings = aApp.TabRequest && aApp.ActiveTab == 4 ? ImGuiTabItemFlags_SetSelected : 0;
		const ImGuiTabItemFlags selectAbout = aApp.TabRequest && aApp.ActiveTab == 5 ? ImGuiTabItemFlags_SetSelected : 0;
		aApp.TabRequest = false;

		if (ImGui::BeginTabBar("##mc_tabs", ImGuiTabBarFlags_None))
		{
			if (ImGui::BeginTabItem("Planner", nullptr, selectPlanner))
			{
				const ImVec2 available = ImGui::GetContentRegionAvail();
				const float leftWidth = available.x * 0.34f;
				ImGui::BeginChild("##mc_catalog_pane", ImVec2(leftWidth, 0), true);
				DrawCatalog(aApp, snapshot);
				ImGui::EndChild();
				ImGui::SameLine();
				ImGui::BeginChild("##mc_plan_pane", ImVec2(0, 0), true);
				DrawPlan(aApp, snapshot);
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Crafting Steps", nullptr, selectSteps))
			{
				DrawCraftingSteps(aApp, snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Shopping List", nullptr, selectShopping))
			{
				DrawShoppingList(aApp, snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Account", nullptr, selectAccount))
			{
				DrawAccountTab(aApp, snapshot);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Settings", nullptr, selectSettings))
			{
				DrawSettingsTab(aApp);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("About", nullptr, selectAbout))
			{
				DrawAboutTab(aApp);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void RenderOptions(App& aApp)
	{
		ImGui::TextUnformatted("Mystic Crafting");
		if (ImGui::Button("Open planner"))
		{
			aApp.Visible = true;
			aApp.Settings.WindowVisible = true;
		}
		ImGui::Separator();
		bool changed = false;
		changed |= ImGui::Checkbox("Prefer buying tradable components", &aApp.Settings.PreferBuyingTradable);
		changed |= ImGui::Checkbox("Subtract owned materials", &aApp.Settings.IncludeAccountItems);
		changed |= ImGui::Checkbox("Compact display density", &aApp.Settings.CompactDensity);
		if (changed)
		{
			aApp.SaveSettingsNow();
			if (aApp.SelectedItem != 0)
			{
				aApp.LoadPlan(aApp.SelectedItem, false);
			}
		}
		ImGui::TextDisabled("The API key and account sources are configured in the Account tab.");
	}
}
