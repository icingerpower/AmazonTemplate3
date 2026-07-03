#include "TablePricing.h"

TablePricing::TablePricing(const QList<PricingCountry> &countries, QObject *parent)
    : QAbstractTableModel(parent)
    , m_countries(countries)
{
}

// ---------------------------------------------------------------------------
// Data population
// ---------------------------------------------------------------------------

int TablePricing::_rowForSku(const QString &sku) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].sku == sku) return i;
    }
    return -1;
}

void TablePricing::setBaseData(const QString &sku, int euQty, double eurPrice,
                               const QString &productType)
{
    int row = _rowForSku(sku);
    if (row < 0) {
        beginInsertRows({}, m_rows.size(), m_rows.size());
        Row r;
        r.sku         = sku;
        r.productType = productType;
        r.euQty       = euQty;
        r.eurPrice    = eurPrice;
        r.countries.resize(m_countries.size());
        m_rows.append(r);
        endInsertRows();
    } else {
        m_rows[row].productType = productType;
        m_rows[row].euQty       = euQty;
        m_rows[row].eurPrice    = eurPrice;
        emit dataChanged(index(row, ColEuQty), index(row, ColEurPrice));
    }
}

void TablePricing::setCountryData(const QString &sku, int countryIndex,
                                  double currentPrice, bool exists)
{
    const int row = _rowForSku(sku);
    if (row < 0 || countryIndex < 0 || countryIndex >= m_countries.size())
        return;

    m_rows[row].countries[countryIndex].currentPrice = currentPrice;
    m_rows[row].countries[countryIndex].exists       = exists;

    const int firstCol = k_fixedCols + countryIndex * k_colSpan;
    emit dataChanged(index(row, firstCol),
                     index(row, firstCol + k_colSpan - 1));
}

// ---------------------------------------------------------------------------
// QAbstractTableModel
// ---------------------------------------------------------------------------

int TablePricing::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int TablePricing::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : k_fixedCols + m_countries.size() * k_colSpan;
}

static QString fmtPrice(double price)
{
    return price >= 0.0 ? QString::number(price, 'f', 2) : QStringLiteral("–");
}

QVariant TablePricing::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows[index.row()];
    const int  col = index.column();

    // --- Fixed columns ---
    if (col < k_fixedCols) {
        switch (role) {
        case Qt::EditRole:
            if (col == ColSku) return row.sku;
            break;
        case Qt::DisplayRole:
            switch (col) {
            case ColSku:      return row.sku;
            case ColEuQty:    return row.euQty >= 0 ? QVariant(row.euQty) : QVariant(QStringLiteral("–"));
            case ColEurPrice: return fmtPrice(row.eurPrice);
            }
            break;
        case Qt::TextAlignmentRole:
            if (col == ColEuQty || col == ColEurPrice)
                return int(Qt::AlignRight | Qt::AlignVCenter);
            break;
        }
        return {};
    }

    // --- Dynamic country columns ---
    const int dynCol      = col - k_fixedCols;
    const int countryIdx  = dynCol / k_colSpan;
    const int offsetInGrp = dynCol % k_colSpan;

    if (countryIdx >= m_countries.size())
        return {};

    const PricingCountry &country     = m_countries[countryIdx];
    const CountryData    &countryData = row.countries[countryIdx];

    switch (role) {
    case Qt::DisplayRole:
        switch (offsetInGrp) {
        case OffCurrent:
            return fmtPrice(countryData.currentPrice);
        case OffNew: {
            if (row.eurPrice < 0.0) return QStringLiteral("–");
            const double newPrice = row.eurPrice * country.rate;
            return QString::number(newPrice, 'f', 2);
        }
        case OffListed:
            return {};  // shown via CheckStateRole
        }
        break;

    case Qt::CheckStateRole:
        if (offsetInGrp == OffListed)
            return countryData.exists ? Qt::Checked : Qt::Unchecked;
        break;

    case Qt::TextAlignmentRole:
        if (offsetInGrp == OffCurrent || offsetInGrp == OffNew)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        if (offsetInGrp == OffListed)
            return int(Qt::AlignHCenter | Qt::AlignVCenter);
        break;
    }
    return {};
}

QVariant TablePricing::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    if (section < k_fixedCols) {
        switch (section) {
        case ColSku:      return tr("SKU");
        case ColEuQty:    return tr("EU Qty");
        case ColEurPrice: return tr("EUR Price");
        }
    }

    const int dynCol      = section - k_fixedCols;
    const int countryIdx  = dynCol / k_colSpan;
    const int offsetInGrp = dynCol % k_colSpan;

    if (countryIdx >= m_countries.size())
        return {};

    const QString &code = m_countries[countryIdx].country;
    switch (offsetInGrp) {
    case OffCurrent: return QStringLiteral("%1 Current").arg(code);
    case OffNew:     return QStringLiteral("%1 New").arg(code);
    case OffListed:  return QStringLiteral("%1 Listed").arg(code);
    }
    return {};
}

Qt::ItemFlags TablePricing::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled;
    if (index.column() == ColSku)
        f |= Qt::ItemIsSelectable | Qt::ItemIsEditable;
    return f;
}

bool TablePricing::setData(const QModelIndex &, const QVariant &, int)
{
    return false;
}
