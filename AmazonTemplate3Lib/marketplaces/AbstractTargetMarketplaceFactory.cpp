#include "AbstractTargetMarketplaceFactory.h"

AbstractTargetMarketplaceFactory::Recorder::Recorder(AbstractTargetMarketplaceFactory *factory)
{
    getFactories().append(factory);
}

const QList<AbstractTargetMarketplaceFactory *> &AbstractTargetMarketplaceFactory::ALL_MARKETPLACE_FACTORIES()
{
    return getFactories();
}

QList<AbstractTargetMarketplace *> AbstractTargetMarketplaceFactory::buildAllInstances(QSettings *settings)
{
    QList<AbstractTargetMarketplace *> all;
    for (auto *factory : getFactories())
        all << factory->createInstances(settings);
    return all;
}

QList<AbstractTargetMarketplaceFactory *> &AbstractTargetMarketplaceFactory::getFactories()
{
    static QList<AbstractTargetMarketplaceFactory *> list;
    return list;
}
