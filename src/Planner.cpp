				const bool vendorNode = KindForRecipe(aNode) == SourceKind::Vendor;
				const bool promotion = KindForRecipe(aNode) == SourceKind::MysticForge;
				const Preference preference = GetPreference(aContext, aNode.Id);
				const i64 buyCost = node.Tradable ? unitPrice * node.Remaining : 0;
				const bool strictlyCheaperToBuy = buyCost < craftCost;
				bool buy = false;
				if (node.Tradable)
				{
					if (preference == Preference::Buy)
					{
						buy = true;
					}
					else if (preference == Preference::Auto)
					{
						// A material promotion is only a cheaper buy when it is strictly
						// less than the Trading Post listing. If the two are equal, the
						// route stays on the craft side rather than being auto-swapped to TP,
						// matching the MaterialUpgrades interpretation that the route wins
						// only on a real cost advantage.
						buy = vendorNode
							? buyCost <= craftCost
							: ((aContext.Options.PreferBuyingTradable || promotion) && strictlyCheaperToBuy);
					}
				}
