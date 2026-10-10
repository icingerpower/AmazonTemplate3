#pragma once

#include "AbstractTargetMarketplaceFactory.h"

// Amazon participates in sales reporting without becoming an inventory-sync
// target. Its FBA fulfillment source remains registered independently.
class AmazonSalesChannelFactory : public AbstractTargetMarketplaceFactory
{
public:
    QString platformId() const override { return QStringLiteral("amazon"); }
    QString platformDisplayName() const override { return QStringLiteral("Amazon"); }
    QList<AbstractTargetMarketplace *> createInstances(QSettings *) const override { return {}; }
    QList<AbstractSalesChannel *> createSalesChannels(QSettings *settings) const override;
};
