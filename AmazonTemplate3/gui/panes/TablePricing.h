#ifndef TABLEPRICING_H
#define TABLEPRICING_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>

// Descriptor for one dynamic country column group.
struct PricingCountry {
    QString marketplaceId;
    QString country;   // e.g. "UK", "US"
    QString currency;  // e.g. "GBP", "USD"
    double  rate;      // EUR → this currency (1.0 for EUR countries)
};

class TablePricing : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit TablePricing(const QList<PricingCountry> &countries,
                          QObject *parent = nullptr);

    // Set base data for a SKU (always from amazon.de).
    // euQty       : total EU FBA quantity (-1 = unknown).
    // eurPrice    : listing price in EUR (-1.0 = not found or not listed).
    // productType : SP-API product type (needed for price PATCH later).
    void setBaseData(const QString &sku, int euQty, double eurPrice,
                     const QString &productType);

    // Set per-country data after fetching from the target marketplace.
    // countryIndex: index into the countries list passed to the constructor.
    // currentPrice: current listing price in that country's currency (-1.0 = unknown).
    // exists      : true if the SKU has an active listing there.
    void setCountryData(const QString &sku, int countryIndex,
                        double currentPrice, bool exists);

    int      rowCount   (const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData    (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData (int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;

    // Fixed column indices
    enum FixedCol { ColSku = 0, ColEuQty = 1, ColEurPrice = 2, k_fixedCols = 3 };
    // Within each dynamic country group (3 columns per country)
    enum ColOffset { OffCurrent = 0, OffNew = 1, OffListed = 2, k_colSpan = 3 };

    struct CountryData {
        double currentPrice = -1.0;
        bool   exists       = false;
    };

    struct Row {
        QString            sku;
        QString            productType;  // SP-API product type (from amazon.de summaries)
        int                euQty    = -1;
        double             eurPrice = -1.0;
        QList<CountryData> countries;   // one entry per m_countries, same order
    };

    // Access rows/countries for read-only use by PanePricing (e.g. Update workflow)
    const QList<Row>            &rows()      const { return m_rows; }
    const QList<PricingCountry> &countries() const { return m_countries; }

private:

    QList<PricingCountry> m_countries;
    QList<Row>            m_rows;

    int _rowForSku(const QString &sku) const;
};

#endif // TABLEPRICING_H
