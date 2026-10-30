	// The same promotion with a cheap enough listing is bought instead, even
	// though the plan prefers to craft.
	PriceDb cheapPrices = prices;
	cheapPrices[2].Sell = 100;
	const Plan bought = BuildPlan(Recipe(100, 1, 1, { promoted }, "Weaponsmith"), 1, items, cheapPrices, {}, {}, options, {});
	Check(bought.Root.Children.size() == 1 && bought.Root.Children[0].Source == SourceKind::TradingPost, "a promotion dearer than the Trading Post is bought");
	Check(bought.RemainingCost == 2000, "the cheaper listing is what the plan charges");

	// Equal-price promotions stay on the forge route unless the user explicitly
	// requests a buy, which matches the "cheaper route wins" interpretation.
	PriceDb equalPrices = prices;
	equalPrices[2].Sell = 5000;
	const Plan equal = BuildPlan(Recipe(100, 1, 1, { promoted }, "Weaponsmith"), 1, items, equalPrices, {}, {}, options, {});
	Check(equal.Root.Children.size() == 1 && equal.Root.Children[0].Source == SourceKind::MysticForge, "an equal-price promotion stays on the forge route");
	Check(equal.RemainingCost == 5000, "the equal-price route is not auto-swapped to the Trading Post");
}
