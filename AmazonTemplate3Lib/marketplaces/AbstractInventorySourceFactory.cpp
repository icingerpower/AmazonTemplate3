#include "AbstractInventorySourceFactory.h"

#include "AmazonFbaInventorySource.h"

AbstractInventorySourceFactory::Recorder::Recorder(AbstractInventorySourceFactory *factory)
{
    getFactories().append(factory);
}

const QList<AbstractInventorySourceFactory *> &AbstractInventorySourceFactory::ALL_SOURCE_FACTORIES()
{
    return getFactories();
}

QList<AbstractInventorySource *> AbstractInventorySourceFactory::buildAllInstances(QSettings *settings)
{
    QList<AbstractInventorySource *> all;
    for (auto *factory : getFactories())
        all << factory->createInstances(settings);
    return all;
}

QList<AbstractInventorySourceFactory *> &AbstractInventorySourceFactory::getFactories()
{
    // Explicit registration (same pattern as AbstractFiller::ALL_FILLERS_SORTED):
    // a static library linker drops object files that are never referenced, so
    // pure static self-registration in the implementation files never runs.
    static QList<AbstractInventorySourceFactory *> list = []() {
        QList<AbstractInventorySourceFactory *> l;
        static AmazonFbaInventorySourceFactory amazonFba;
        l << &amazonFba;
        // Octopia fulfillment: add its factory here.
        return l;
    }();
    return list;
}
