#include "AbstractTargetMarketplaceFactory.h"

#include "TemuTargetMarketplace.h"

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
    // Explicit registration (same pattern as AbstractFiller::ALL_FILLERS_SORTED):
    // a static library linker drops object files that are never referenced, so
    // pure static self-registration in the implementation files never runs.
    static QList<AbstractTargetMarketplaceFactory *> list = []() {
        QList<AbstractTargetMarketplaceFactory *> l;
        static TemuTargetMarketplaceFactory temu;
        l << &temu;
        // New target marketplace platforms: add their factory here.
        return l;
    }();
    return list;
}
