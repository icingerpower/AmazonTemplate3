#ifndef TABLECURRENCYRATES_H
#define TABLECURRENCYRATES_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>

class TableCurrencyRates : public QAbstractTableModel
{
    Q_OBJECT
public:
    struct Entry {
        QString marketplaceId;
        QString country;   // e.g. "DE", "US"
        QString currency;  // e.g. "EUR", "USD"
        double  rate;      // EUR → this currency
        bool    checked;
    };

    enum Column { ColCountry = 0, ColCurrency = 1, ColRate = 2 };

    explicit TableCurrencyRates(QObject *parent = nullptr);

    int      rowCount   (const QModelIndex &parent = {}) const override;
    int      columnCount(const QModelIndex &parent = {}) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData    (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;

    const QList<Entry> &entries() const { return m_entries; }

private:
    QList<Entry> m_entries;
};

#endif // TABLECURRENCYRATES_H
