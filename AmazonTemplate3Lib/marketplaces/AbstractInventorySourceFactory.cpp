#include "AbstractInventorySourceFactory.h"

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
    static QList<AbstractInventorySourceFactory *> list;
    return list;
}
