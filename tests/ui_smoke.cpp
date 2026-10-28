#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "App.h"
#include "Ui.h"
#include "imgui.h"

namespace mc
{
	// The UI smoke test links only the renderer, so provide the application
	// methods it calls. They are intentionally inert here.
	void App::LoadPlan(int, bool) {}
	void App::SaveSettingsNow() {}
	void App::Notify(const std::string&) {}
	void App::ClearCache() {}
	void App::ReloadCatalog(bool) {}
	void App::SyncAccount() {}
	void App::RefreshTrackedShopping() {}
}


namespace
{
	struct PreviewImage
	{
		int Width = 0;
		int Height = 0;
		std::vector<unsigned char> Pixels;
	};

	void BlendPixel(PreviewImage& aImage, int aX, int aY, const unsigned char* aTexture, int aTextureWidth, int aTextureHeight, float aU, float aV, float aR, float aG, float aB, float aA)
	{
		if (aX < 0 || aY < 0 || aX >= aImage.Width || aY >= aImage.Height || aA <= 0.0f)
		{
			return;
		}
		const int tx = std::clamp(static_cast<int>(aU * aTextureWidth), 0, aTextureWidth - 1);
		const int ty = std::clamp(static_cast<int>(aV * aTextureHeight), 0, aTextureHeight - 1);
		const unsigned char* texel = aTexture + (ty * aTextureWidth + tx) * 4;
		const float alpha = aA * (texel[3] / 255.0f);
		const float srcR = aR * (texel[0] / 255.0f);
		const float srcG = aG * (texel[1] / 255.0f);
		const float srcB = aB * (texel[2] / 255.0f);
		unsigned char* dst = aImage.Pixels.data() + (aY * aImage.Width + aX) * 3;
		dst[0] = static_cast<unsigned char>(srcR * alpha * 255.0f + dst[0] * (1.0f - alpha));
		dst[1] = static_cast<unsigned char>(srcG * alpha * 255.0f + dst[1] * (1.0f - alpha));
		dst[2] = static_cast<unsigned char>(srcB * alpha * 255.0f + dst[2] * (1.0f - alpha));
	}

	void RasterizeDrawData(PreviewImage& aImage, const ImDrawData* aDrawData, const unsigned char* aTexture, int aTextureWidth, int aTextureHeight)
	{
		for (int listIndex = 0; listIndex < aDrawData->CmdListsCount; ++listIndex)
		{
			const ImDrawList* list = aDrawData->CmdLists[listIndex];
			for (const ImDrawCmd& command : list->CmdBuffer)
			{
				if (command.UserCallback)
				{
					continue;
				}
				const int clipX0 = std::max(0, static_cast<int>(command.ClipRect.x));
				const int clipY0 = std::max(0, static_cast<int>(command.ClipRect.y));
				const int clipX1 = std::min(aImage.Width, static_cast<int>(command.ClipRect.z));
				const int clipY1 = std::min(aImage.Height, static_cast<int>(command.ClipRect.w));
				if (clipX1 <= clipX0 || clipY1 <= clipY0)
				{
					continue;
				}

				const ImDrawIdx* indices = list->IdxBuffer.Data + command.IdxOffset;
				const ImDrawVert* vertices = list->VtxBuffer.Data + command.VtxOffset;
				for (unsigned int i = 0; i + 2 < command.ElemCount; i += 3)
				{
					const ImDrawVert& v0 = vertices[indices[i]];
					const ImDrawVert& v1 = vertices[indices[i + 1]];
					const ImDrawVert& v2 = vertices[indices[i + 2]];
					const ImVec2 origin = aDrawData->DisplayPos;
					const ImVec2 p0(v0.pos.x + origin.x, v0.pos.y + origin.y);
					const ImVec2 p1(v1.pos.x + origin.x, v1.pos.y + origin.y);
					const ImVec2 p2(v2.pos.x + origin.x, v2.pos.y + origin.y);

					const float denominator = (p1.y - p2.y) * (p0.x - p2.x) + (p2.x - p1.x) * (p0.y - p2.y);
					if (std::fabs(denominator) < 1e-6f)
					{
						continue;
					}

					const int minX = std::max(clipX0, static_cast<int>(std::floor(std::min({ p0.x, p1.x, p2.x }))));
					const int maxX = std::min(clipX1, static_cast<int>(std::ceil(std::max({ p0.x, p1.x, p2.x }))));
					const int minY = std::max(clipY0, static_cast<int>(std::floor(std::min({ p0.y, p1.y, p2.y }))));
					const int maxY = std::min(clipY1, static_cast<int>(std::ceil(std::max({ p0.y, p1.y, p2.y }))));

					const float a0 = ((v0.col >> 24) & 0xFF) / 255.0f;
					const float a1 = ((v1.col >> 24) & 0xFF) / 255.0f;
					const float a2 = ((v2.col >> 24) & 0xFF) / 255.0f;
					const float r0 = (v0.col & 0xFF) / 255.0f;
					const float r1 = (v1.col & 0xFF) / 255.0f;
					const float r2 = (v2.col & 0xFF) / 255.0f;
					const float g0 = ((v0.col >> 8) & 0xFF) / 255.0f;
					const float g1 = ((v1.col >> 8) & 0xFF) / 255.0f;
					const float g2 = ((v2.col >> 8) & 0xFF) / 255.0f;
					const float b0 = ((v0.col >> 16) & 0xFF) / 255.0f;
					const float b1 = ((v1.col >> 16) & 0xFF) / 255.0f;
					const float b2 = ((v2.col >> 16) & 0xFF) / 255.0f;

					for (int y = minY; y < maxY; ++y)
					{
						for (int x = minX; x < maxX; ++x)
						{
							const float px = x + 0.5f;
							const float py = y + 0.5f;
							const float w0 = ((p1.y - p2.y) * (px - p2.x) + (p2.x - p1.x) * (py - p2.y)) / denominator;
							const float w1 = ((p2.y - p0.y) * (px - p2.x) + (p0.x - p2.x) * (py - p2.y)) / denominator;
							const float w2 = 1.0f - w0 - w1;
							if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
							{
								continue;
							}
							const float u = w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x;
							const float v = w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y;
							BlendPixel(aImage, x, y, aTexture, aTextureWidth, aTextureHeight, u, v,
								w0 * r0 + w1 * r1 + w2 * r2,
								w0 * g0 + w1 * g1 + w2 * g2,
								w0 * b0 + w1 * b1 + w2 * b2,
								w0 * a0 + w1 * a1 + w2 * a2);
						}
					}
				}
			}
		}
	}

	void SavePpm(const std::string& aPath, const PreviewImage& aImage)
	{
		std::ofstream stream(aPath, std::ios::binary);
		stream << "P6\n" << aImage.Width << " " << aImage.Height << "\n255\n";
		stream.write(reinterpret_cast<const char*>(aImage.Pixels.data()), static_cast<std::streamsize>(aImage.Pixels.size()));
	}
}

namespace
{
	Texture_t* NoTexture(const char*, const char*, const char*)
	{
		return nullptr;
	}

	mc::PlanNode MakeNode(int id, const char* name, int required, int owned, mc::SourceKind source, int64_t cost)
	{
		mc::PlanNode node;
		node.Id = id;
		node.Name = name;
		node.Required = required;
		node.Owned = owned;
		node.Remaining = required - owned;
		node.Complete = node.Remaining == 0;
		node.Source = source;
		node.SourceDetail = "Test source";
		node.TotalCost = cost;
		return node;
	}

	std::shared_ptr<mc::Plan> MakePlan()
	{
		auto plan = std::make_shared<mc::Plan>();
		plan->GoalId = 100;
		plan->GoalName = "Test Legendary";
		plan->GoalType = "Greatsword";
		plan->GoalRarity = "Legendary";
		plan->RemainingCost = 12345;
		plan->FullCost = 23456;
		plan->NodeCount = 4;

		mc::PlanNode root = MakeNode(100, "Test Legendary", 1, 0, mc::SourceKind::MysticForge, 12345);
		root.MultipleRecipes = 2;
		mc::PlanNode gift = MakeNode(200, "Test Gift", 1, 0, mc::SourceKind::MysticForge, 10000);
		gift.Crafts = 2;
		gift.ChatLink = "[&AgHcdwAA]";
		gift.Rarity = "Exotic";
		mc::PlanNode ore = MakeNode(201, "Test Ore", 250, 100, mc::SourceKind::TradingPost, 10000);
		ore.UnitCost = 40;
		mc::PlanNode currency = MakeNode(1, "Coin", 500000, 100000, mc::SourceKind::Currency, 400000);
		currency.IsCurrency = true;
		currency.Rarity = "";
		gift.Children.push_back(ore);
		gift.Children.push_back(currency);

		mc::PlanNode owned = MakeNode(202, "Already Owned", 1, 1, mc::SourceKind::Unknown, 0);

		// Merchant, achievement and forge steps, so the grouped steps tab shows
		// one block per place to go.
		mc::PlanNode runes = MakeNode(203, "Icy Runestone", 2, 0, mc::SourceKind::Vendor, 20000);
		runes.SourceDetail = "Rojan the Penitent (Earthshake Basin, Frostgorge Sound)";
		runes.Crafts = 2;
		mc::PlanNode trophy = MakeNode(204, "Test Trophy", 1, 0, mc::SourceKind::Achievement, 0);
		trophy.SourceDetail = "Achievement #2438";
		mc::PlanNode forge = MakeNode(205, "Forged Gift", 1, 0, mc::SourceKind::MysticForge, 5000);
		forge.Crafts = 1;
		forge.Discipline = "Mystic Forge";
		forge.SourceDetail = "Mystic Forge";
		forge.Children.push_back(runes);
		forge.Children.push_back(trophy);

		root.Children.push_back(gift);
		root.Children.push_back(owned);
		root.Children.push_back(forge);
		plan->Root = root;

		mc::ShoppingItem item;
		item.Id = 201;
		item.Name = "Test Ore";
		item.Quantity = 150;
		item.UnitCost = 40;
		item.TotalCost = 6000;
		item.Source = mc::SourceKind::TradingPost;
		item.FromTradingPost = true;
		plan->Shopping.push_back(item);

		mc::ShoppingItem vendor;
		vendor.Id = 203;
		vendor.Name = "Icy Runestone";
		vendor.Quantity = 2;
		vendor.UnitCost = 10000;
		vendor.TotalCost = 20000;
		vendor.Source = mc::SourceKind::Vendor;
		vendor.SourceDetail = "Rojan the Penitent (Earthshake Basin, Frostgorge Sound)";
		vendor.FromVendor = true;
		plan->Shopping.push_back(vendor);

		mc::ShoppingItem karma;
		karma.Id = 19925;
		karma.Name = "Obsidian Shard";
		karma.Quantity = 5;
		karma.Source = mc::SourceKind::Vendor;
		karma.SourceDetail = "Tactician Deathstrider (Straits of Devastation)";
		karma.PriceText = "2,100 Karma";
		karma.TotalPriceText = "10,500 Karma";
		karma.FromVendor = true;
		plan->Shopping.push_back(karma);

		mc::ShoppingItem achievement;
		achievement.Id = 204;
		achievement.Name = "Test Trophy";
		achievement.Quantity = 1;
		achievement.Source = mc::SourceKind::Achievement;
		achievement.SourceDetail = "Achievement #2438";
		plan->Shopping.push_back(achievement);

		mc::ShoppingItem notes;
		notes.Id = mc::kResearchNoteCurrency;
		notes.Name = "Research Note";
		notes.Quantity = 250;
		notes.UnitCost = 100;
		notes.TotalCost = 25000;
		notes.Source = mc::SourceKind::Currency;
		notes.Currency = true;
		notes.SourceDetail = "Crafted and salvaged with a research kit";
		plan->Shopping.push_back(notes);

		mc::ShoppingItem noteIngredient;
		noteIngredient.Id = 208;
		noteIngredient.Name = "Cured Leather";
		noteIngredient.Quantity = 500;
		noteIngredient.UnitCost = 100;
		noteIngredient.TotalCost = 50000;
		noteIngredient.Source = mc::SourceKind::TradingPost;
		noteIngredient.SourceDetail = "For research notes - Buy from Trading Post";
		noteIngredient.FromTradingPost = true;
		noteIngredient.ResearchNotes = true;
		plan->Shopping.push_back(noteIngredient);

		plan->ResearchNotesRequired = 250;

		mc::AchievementNeed need;
		need.ItemId = 204;
		need.ItemName = "Test Trophy";
		need.Quantity = 1;
		need.AchievementId = 2438;
		need.Detail = "Achievement #2438";
		plan->AchievementNeeds.push_back(need);
		plan->ResearchNotesNeeded = 250;
		plan->ResearchNoteCost = 50000;
		plan->ResearchNoteSourceName = "Rabid Draconic Gauntlets";
		plan->ResearchNoteYield = 5;
		plan->ResearchNoteCrafts = 50;

		plan->CurrencyNeeded[1] = 400000;
		plan->CurrencyNeeded[23] = 200;
		plan->CurrencyOwned[1] = 100000;
		return plan;
	}
}

int main()
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1280.0f, 800.0f);
	io.DeltaTime = 1.0f / 60.0f;
	io.Fonts->AddFontDefault();
	unsigned char* pixels = nullptr;
	int width = 0;
	int height = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(1));

	AddonAPI_t api{};
	api.Textures_GetOrCreateFromURL = NoTexture;

	mc::App app;
	app.Api = &api;
	app.Visible = true;

	mc::CatalogEntry entry;
	entry.Id = 100;
	entry.Name = "Test Legendary";
	entry.TypeLabel = "Greatsword";
	entry.Rarity = "Legendary";
	app.Catalog.push_back(entry);
	entry.Id = 101;
	entry.Name = "Another Legendary";
	entry.TypeLabel = "Staff";
	entry.Owned = true;
	app.Catalog.push_back(entry);
	app.SelectedItem = 100;
	app.CurrentPlan = MakePlan();
	app.KnownCosts[100] = 12345;

	mc::ItemInfo info;
	info.Id = 100;
	info.Name = "Test Legendary";
	info.Rarity = "Legendary";
	info.Type = "Weapon";
	info.Subtype = "Greatsword";
	info.Description = "A test item used by the automated UI smoke test.";
	info.Level = 80;
	info.ChatLink = "[&AgHcdwAA]";
	app.Items[100] = info;

	app.Currencies[1].Id = 1;
	app.Currencies[1].Name = "Coin";
	app.Currencies[23].Id = 23;
	app.Currencies[23].Name = "Spirit Shard";

	app.Status = "Loaded Test Legendary";
	app.Settings.Tracked.push_back(100);
	app.Settings.Tracked.push_back(101);
	app.Settings.CombineTracked = true;
	app.Settings.IncludeResearchNotes = true;

	{
		auto combinedPlan = MakePlan();
		combinedPlan->GoalCount = 2;
		combinedPlan->GoalName = "2 tracked plans";
		combinedPlan->RemainingCost *= 2;
		combinedPlan->FullCost *= 2;
		combinedPlan->CurrencyRequired[1] = 500000;
		if (!combinedPlan->Shopping.empty())
		{
			combinedPlan->Shopping[0].Quantity *= 2;
			combinedPlan->Shopping[0].TotalCost *= 2;
		}

		app.TrackedPlan = combinedPlan;
	}

	const bool onlyMain = std::getenv("MC_UI_ONLY_MAIN") != nullptr;
	for (int tab = 0; tab < 6; ++tab)
	{
		app.ActiveTab = tab;
		app.TabRequest = true;
		// Tab selection is applied at the end of the frame, so the first rendered
		// frame of a tab initializes table layout. Dump the settled third frame.
		for (int frame = 0; frame < 3; ++frame)
		{
			ImGui::NewFrame();
			mc::RenderMain(app);
			if (!onlyMain)
			{
				ImGui::Begin("Nexus options");
				mc::RenderOptions(app);
				ImGui::End();
			}
			ImGui::Render();

			if (const char* dumpDir = std::getenv("MC_UI_DUMP_DIR"))
			{
				if (frame == 2)
				{
					PreviewImage image;
					image.Width = static_cast<int>(io.DisplaySize.x);
					image.Height = static_cast<int>(io.DisplaySize.y);
					image.Pixels.assign(static_cast<std::size_t>(image.Width * image.Height * 3), 18);
					RasterizeDrawData(image, ImGui::GetDrawData(), pixels, width, height);
					SavePpm(std::string(dumpDir) + "/tab" + std::to_string(tab) + ".ppm", image);
				}
			}
		}
	}

	// The shopping list and the crafting steps must also render in single-plan mode.
	app.Settings.CombineTracked = false;
	app.ActiveTab = 1;
	app.TabRequest = true;
	for (int frame = 0; frame < 3; ++frame)
	{
		ImGui::NewFrame();
		mc::RenderMain(app);
		ImGui::Render();
	}

	ImGui::DestroyContext();
	std::printf("UI smoke test passed (all tabs rendered, tracked crafting steps, vendor, achievement and research note shopping lines).\n");
	return 0;
}
