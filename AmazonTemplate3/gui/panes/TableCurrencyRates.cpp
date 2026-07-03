#include "TableCurrencyRates.h"
#include <QSettings>

static const QList<TableCurrencyRates::Entry> k_defaultEntries = {
    // Europe
    { QStringLiteral("A1F83G8C2ARO7P"), QStringLiteral("UK"), QStringLiteral("GBP"), 0.8650, false },
    { QStringLiteral("A1PA6795UKMFR9"), QStringLiteral("DE"), QStringLiteral("EUR"), 1.0000, false },
    { QStringLiteral("A13V1IB3VIYZZH"), QStringLiteral("FR"), QStringLiteral("EUR"), 1.0000, false },
    { QStringLiteral("APJ6JRA9NG5V4"),  QStringLiteral("IT"), QStringLiteral("EUR"), 1.0000, false },
    { QStringLiteral("A1RKKUPIHCS9HS"), QStringLiteral("ES"), QStringLiteral("EUR"), 1.0000, false },
    { QStringLiteral("A1805IZSGTT6HW"), QStringLiteral("NL"), QStringLiteral("EUR"), 1.0000, false },
    { QStringLiteral("A2NODRKZP88ZB9"), QStringLiteral("SE"), QStringLiteral("SEK"), 11.450, false },
    { QStringLiteral("A1C3SOZRARQ6R3"), QStringLiteral("PL"), QStringLiteral("PLN"), 4.2500, false },
    { QStringLiteral("ARBP9OOSHTCHU"),  QStringLiteral("BE"), QStringLiteral("EUR"), 1.0000, false },
    // Americas
    { QStringLiteral("ATVPDKIKX0ER"),   QStringLiteral("US"), QStringLiteral("USD"), 1.0800, false },
    { QStringLiteral("A2EUQ1WTGCTBG2"), QStringLiteral("CA"), QStringLiteral("CAD"), 1.4700, false },
    { QStringLiteral("A1AM78C64UM0Y8"), QStringLiteral("MX"), QStringLiteral("MXN"), 20.500, false },
    { QStringLiteral("A2Q3Y263D00KWC"), QStringLiteral("BR"), QStringLiteral("BRL"), 5.8000, false },
};

TableCurrencyRates::TableCurrencyRates(QObject *parent)
    : QAbstractTableModel(parent)
    , m_entries(k_defaultEntries)
{
    QSettings s;
    for (Entry &e : m_entries) {
        const QString base = QStringLiteral("pricing/rates/%1").arg(e.marketplaceId);
        if (s.contains(base + QStringLiteral("/checked")))
            e.checked = s.value(base + QStringLiteral("/checked")).toBool();
        if (s.contains(base + QStringLiteral("/rate")))
            e.rate = s.value(base + QStringLiteral("/rate")).toDouble();
    }
}

int TableCurrencyRates::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_entries.size();
}

int TableCurrencyRates::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return 3;
}

QVariant TableCurrencyRates::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const Entry &e = m_entries[index.row()];

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColCountry:  return e.country;
        case ColCurrency: return e.currency;
        case ColRate:     return QString::number(e.rate, 'f', 4);
        }
        break;
    case Qt::EditRole:
        if (index.column() == ColRate)
            return e.rate;
        break;
    case Qt::CheckStateRole:
        if (index.column() == ColCountry)
            return e.checked ? Qt::Checked : Qt::Unchecked;
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == ColRate)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    }
    return {};
}

bool TableCurrencyRates::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return false;

    Entry &e = m_entries[index.row()];

    if (role == Qt::CheckStateRole && index.column() == ColCountry) {
        e.checked = (value.toInt() == Qt::Checked);
        QSettings().setValue(
            QStringLiteral("pricing/rates/%1/checked").arg(e.marketplaceId), e.checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    if (role == Qt::EditRole && index.column() == ColRate) {
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (!ok || v <= 0.0) return false;
        e.rate = v;
        QSettings().setValue(
            QStringLiteral("pricing/rates/%1/rate").arg(e.marketplaceId), e.rate);
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }

    return false;
}

QVariant TableCurrencyRates::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColCountry:  return tr("Country");
    case ColCurrency: return tr("Currency");
    case ColRate:     return tr("Rate (EUR →)");
    }
    return {};
}

Qt::ItemFlags TableCurrencyRates::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled;
    if (index.column() == ColCountry)
        f |= Qt::ItemIsUserCheckable;
    if (index.column() == ColRate)
        f |= Qt::ItemIsEditable;
    return f;
}
