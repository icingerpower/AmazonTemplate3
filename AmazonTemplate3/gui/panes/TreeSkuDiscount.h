#ifndef TREESKUDISCOUNT_H
#define TREESKUDISCOUNT_H

#include <QAbstractItemModel>
#include <QList>
#include <QString>

// Two-level tree feeding PaneDiscount's treeViewProducts.
//
//   Parent row  = product (SKU / ASIN / preferred-language title, aggregate units)
//   Child rows  = one per checked country (localized title, units, prices)
//
// New price is expressed in the listing's ORIGINAL currency (that is what the
// Amazon discount PATCH must carry). The EUR columns are display-only, for
// cross-country comparison. Eligible child rows render their New price cell in
// dark red.
class TreeSkuDiscount : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column {
        ColSku = 0,
        ColAsin,
        ColTitle,
        ColUnits,
        ColAgedUnits,     // units stored >= the min-months threshold
        ColSales90,       // units sold last 90 days, across marketplaces
        ColUnitPriceEur,
        ColCurrencyEur,   // literal "EUR" tag for the normalized column
        ColOrigAmount,
        ColOrigCurrency,
        ColNewPrice,
        ColEraseMinMax,   // checkbox: min/max will be erased on Apply
        ColumnCount
    };

    struct CountryRow {
        QString marketplaceId;
        QString countryCode;    // "FR", "DE", "GB"…
        QString productType;    // SP-API product type on THIS marketplace (for PATCH)
        QString title;          // country-localized title
        int     units        = 0;
        double  unitPriceEur  = 0.0;   // orig amount converted to EUR (0 = unknown)
        double  origAmount    = 0.0;   // listing price in origCurrency (0 = unknown)
        QString origCurrency;          // "EUR", "GBP"…
        double  newPrice      = 0.0;   // discounted price, in origCurrency
        double  minPrice      = 0.0;   // existing minimum_seller_allowed_price (0 = none)
        double  maxPrice      = 0.0;   // existing maximum_seller_allowed_price (0 = none)
        bool    listed        = false; // SKU has a listing on this marketplace
        bool    eligible      = false; // listed AND has a usable price to discount

        // True when applying the discount will erase a min or max price floor/
        // ceiling on this marketplace (only eligible rows are ever PATCHed).
        bool eraseMinMax() const { return eligible && (minPrice > 0.0 || maxPrice > 0.0); }
    };

    struct Product {
        QString sku;
        QString asin;
        QString title;                 // preferred-language title (FR→EN)
        QString productType;           // SP-API product type (needed for PATCH)
        bool    checked     = true;    // when false, Apply skips this product
        int     available   = 0;       // shared pan-EU FBA pool (parent Units)
        int     agedUnits   = 0;       // units stored >= the min-months threshold
        int     unitsSold90 = -1;      // units sold last 90d across marketplaces (-1 = unknown)
        int     minMonths   = 0;       // the threshold used, for the tooltip
        QList<CountryRow> countries;   // each child repeats the shared pool figure
    };

    explicit TreeSkuDiscount(QObject *parent = nullptr);

    void setProducts(const QList<Product> &products);
    const QList<Product> &products() const { return m_products; }
    void clear();

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    // Only handles toggling a product (parent) row's check state; all other
    // edits are rejected (returns false) so cell text stays read-only.
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    // Editable so a double-click opens an editor whose text can be selected and
    // copied (e.g. to look up a SKU). Edits are not persisted — setData is not
    // implemented, so the base-class no-op discards any change.
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    // internalId convention: 0 => top-level product row; (parentRow + 1) => child.
    static constexpr quintptr kTopLevel = 0;

    bool isProduct(const QModelIndex &idx) const { return idx.internalId() == kTopLevel; }

    QList<Product> m_products;
};

#endif // TREESKUDISCOUNT_H
