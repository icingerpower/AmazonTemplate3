#ifndef ABSTRACTTARGETMARKETPLACEFACTORY_H
#define ABSTRACTTARGETMARKETPLACEFACTORY_H

#include <QList>
#include <QString>

#include "AbstractTargetMarketplace.h"

class QSettings;

// One static instance per target marketplace platform (e.g. "Temu EU").
// Registered at startup via DECLARE_TARGET_MARKETPLACE_FACTORY().
// Produces N configured AbstractTargetMarketplace instances from QSettings.
class AbstractTargetMarketplaceFactory
{
public:
    virtual ~AbstractTargetMarketplaceFactory() = default;

    virtual QString platformId()          const = 0;
    virtual QString platformDisplayName() const = 0;

    // Create one AbstractTargetMarketplace per configured store.
    // Caller takes ownership of returned objects.
    virtual QList<AbstractTargetMarketplace *> createInstances(QSettings *settings) const = 0;

    // Convenience: iterate all registered factories and collect every instance.
    static QList<AbstractTargetMarketplace *> buildAllInstances(QSettings *settings);

    static const QList<AbstractTargetMarketplaceFactory *> &ALL_MARKETPLACE_FACTORIES();

    class Recorder
    {
    public:
        explicit Recorder(AbstractTargetMarketplaceFactory *marketplace);
    };

private:
    static QList<AbstractTargetMarketplaceFactory *> &getFactories();
};

#define DECLARE_TARGET_MARKETPLACE_FACTORY(ClassName)                                    \
    static ClassName instance##ClassName;                                                \
    static AbstractTargetMarketplaceFactory::Recorder recorder##ClassName{&instance##ClassName};

#endif // ABSTRACTTARGETMARKETPLACEFACTORY_H
