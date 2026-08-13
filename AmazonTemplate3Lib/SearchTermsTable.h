#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QPair>
#include <QString>

// One row per country: the current search terms (Amazon's "generic_keyword"
// listing attribute) for the representative child ASIN found to exist there,
// plus an auto-computed UTF-8 byte count (Amazon enforces a per-marketplace
// byte limit on this field). Only the Search terms column is editable.
class SearchTermsTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    struct Row {
        QString countryCode;    // e.g. "FR"
        QString marketplaceId;
        QString asin;           // representative child ASIN found in this country
        QString sku;            // its seller SKU (needed to PATCH generic_keyword)
        QString productType;    // cached productType for the PATCH
        QString title;
        QString searchTerms;
        bool    found = false;  // true once a child was confirmed to exist here
    };

    enum Column { ColCountry = 0, ColTitle, ColSearchTerms, ColBytes, ColumnCount };

    explicit SearchTermsTable(QObject *parent = nullptr);

    // Resets the table to one row per (countryCode, marketplaceId), all empty.
    void setCountries(const QList<QPair<QString, QString>> &countries);
    void clear();

    // Updates one row's retrieved data (matched by country code, case-insensitive).
    void setRowResult(const QString &countryCode, const QString &asin, const QString &sku,
                      const QString &productType, const QString &title,
                      const QString &searchTerms, bool found);

    const QList<Row> &rows() const { return m_rows; }
    Row rowAt(int i) const { return m_rows.value(i); }
    // Directly sets the search-terms text for a row (used by "Copy search
    // terms" normalization). Emits dataChanged for the affected columns.
    void setSearchTerms(int row, const QString &value);

    int           rowCount(const QModelIndex &parent = {}) const override;
    int           columnCount(const QModelIndex &parent = {}) const override;
    QVariant      data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                             int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool          setData(const QModelIndex &index, const QVariant &value,
                          int role = Qt::EditRole) override;

private:
    QList<Row> m_rows;
};
