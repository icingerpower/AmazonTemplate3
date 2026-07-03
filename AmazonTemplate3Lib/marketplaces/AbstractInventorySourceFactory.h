#ifndef ABSTRACTINVENTORYSOURCEFACTORY_H
#define ABSTRACTINVENTORYSOURCEFACTORY_H

#include <QList>
#include <QString>

#include "AbstractInventorySource.h"

class QSettings;

// One static instance per inventory platform (e.g. "Amazon FBA EU").
// Registered at startup via DECLARE_INVENTORY_SOURCE_FACTORY().
// Produces N configured AbstractInventorySource instances from QSettings.
class AbstractInventorySourceFactory
{
public:
    virtual ~AbstractInventorySourceFactory() = default;

    virtual QString platformId()          const = 0;
    virtual QString platformDisplayName() const = 0;

    // Create one AbstractInventorySource per configured account.
    // Caller takes ownership of returned objects.
    virtual QList<AbstractInventorySource *> createInstances(QSettings *settings) const = 0;

    // Convenience: iterate all registered factories and collect every instance.
    static QList<AbstractInventorySource *> buildAllInstances(QSettings *settings);

    static const QList<AbstractInventorySourceFactory *> &ALL_SOURCE_FACTORIES();

    class Recorder
    {
    public:
        explicit Recorder(AbstractInventorySourceFactory *factory);
    };

private:
    static QList<AbstractInventorySourceFactory *> &getFactories();
};

#define DECLARE_INVENTORY_SOURCE_FACTORY(ClassName)                                  \
    static ClassName instance##ClassName;                                            \
    static AbstractInventorySourceFactory::Recorder recorder##ClassName{&instance##ClassName};

#endif // ABSTRACTINVENTORYSOURCEFACTORY_H
