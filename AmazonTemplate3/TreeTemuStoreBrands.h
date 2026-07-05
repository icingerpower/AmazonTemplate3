#ifndef TREETEMUSTOREBRANDS_H
#define TREETEMUSTOREBRANDS_H

#include <QAbstractItemModel>
#include <QList>
#include <QString>

#include "TemuStoreModel.h"
#include "apis/TemuInventoryApi.h"

// Two-level tree: Temu stores (from PaneSettings) → brands sold in that store.
// Each brand line carries one manufacturer and one GSPR (EU responsible person)
// representative, selected among the entities registered in the Temu seller
// account. A brand may appear in at most one store per country.
//
// Persisted in the working directory settings under "TemuStoreBrands/", keyed
// by {country}|{label} so a store temporarily removed from PaneSettings keeps
// its brand data and finds it again when re-added or reordered.
class TreeTemuStoreBrands : public QAbstractItemModel
{
    Q_OBJECT

public:
    static const int COL_BRAND        = 0;
    static const int COL_MANUFACTURER = 1;
    static const int COL_GSPR         = 2;

    // Combo choices (QStringList of entity names) for the manufacturer /
    // GSPR columns of a brand row — used by the edit delegate.
    static const int RoleChoices = Qt::UserRole + 1;

    explicit TreeTemuStoreBrands(const QList<TemuStore> &stores, QObject *parent = nullptr);

    // Brands seen on loaded ASINs, cached in the working directory settings.
    // cacheBrand() is called whenever ASIN data carrying a brand is fetched;
    // knownBrands() feeds the brand selection combo.
    static void        cacheBrand(const QString &brand);
    static QStringList knownBrands();

    // Known brands not yet used by a store of the same country (per-country
    // uniqueness rule) — candidates for "Add brand" on that store.
    QStringList availableBrandsForStore(int storeRow) const;

    // Entities fetched from the store's Temu account (dialog injects them
    // once the API replies). Refreshes the combo choices of the store's rows.
    void setEntityChoices(int storeRow,
                          const QList<TemuInventoryApi::RepEntity> &manufacturers,
                          const QList<TemuInventoryApi::RepEntity> &gsprReps);

    QModelIndex addBrand(int storeRow, const QString &brand = {});
    void removeBrand(const QModelIndex &brandIndex);

    bool isStoreIndex(const QModelIndex &index) const;
    int  storeRowOf(const QModelIndex &index) const; // -1 if none

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

signals:
    void errorOccurred(const QString &message);

private:
    struct Brand {
        QString brand;
        qint64  manufacturerId = 0;
        QString manufacturerName;
        qint64  gsprRepId = 0;
        QString gsprRepName;
    };
    struct StoreNode {
        QString country;
        QString label;
        QList<Brand> brands;
        QList<TemuInventoryApi::RepEntity> manufacturers;
        QList<TemuInventoryApi::RepEntity> gsprReps;
    };
    QList<StoreNode> m_stores;

    static QString _storeKey(const StoreNode &store);
    bool _brandExistsInCountry(const QString &brand, const QString &country,
                               int exceptStore, int exceptBrand) const;
    void _load();
    void _save() const;
};

#endif // TREETEMUSTOREBRANDS_H
