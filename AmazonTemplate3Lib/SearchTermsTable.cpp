#include "SearchTermsTable.h"

#include <QColor>

SearchTermsTable::SearchTermsTable(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void SearchTermsTable::setCountries(const QList<QPair<QString, QString>> &countries)
{
    beginResetModel();
    m_rows.clear();
    for (const auto &c : countries) {
        Row r;
        r.countryCode   = c.first;
        r.marketplaceId = c.second;
        m_rows.append(r);
    }
    endResetModel();
}

void SearchTermsTable::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void SearchTermsTable::setRowResult(const QString &countryCode, const QString &asin,
                                    const QString &sku, const QString &productType,
                                    const QString &title, const QString &searchTerms,
                                    bool found)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].countryCode.compare(countryCode, Qt::CaseInsensitive) != 0)
            continue;
        m_rows[i].asin        = asin;
        m_rows[i].sku         = sku;
        m_rows[i].productType = productType;
        m_rows[i].title       = title;
        m_rows[i].searchTerms = searchTerms;
        m_rows[i].found       = found;
        emit dataChanged(index(i, 0), index(i, ColumnCount - 1));
        return;
    }
}

void SearchTermsTable::setSearchTerms(int row, const QString &value)
{
    if (row < 0 || row >= m_rows.size())
        return;
    m_rows[row].searchTerms = value;
    emit dataChanged(index(row, ColSearchTerms), index(row, ColBytes),
                      {Qt::DisplayRole, Qt::EditRole});
}

int SearchTermsTable::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int SearchTermsTable::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant SearchTermsTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case ColCountry:     return r.countryCode;
        case ColTitle:       return r.title;
        case ColSearchTerms: return r.searchTerms;
        case ColBytes:       return r.searchTerms.toUtf8().size();
        default: return {};
        }
    }
    if (role == Qt::ForegroundRole && !r.found)
        return QColor(Qt::gray); // not listed in this country — nothing to retrieve/upload
    return {};
}

QVariant SearchTermsTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case ColCountry:     return tr("Country");
    case ColTitle:       return tr("Title");
    case ColSearchTerms: return tr("Search terms");
    case ColBytes:       return tr("Bytes");
    default: return {};
    }
}

Qt::ItemFlags SearchTermsTable::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColSearchTerms)
        f |= Qt::ItemIsEditable;
    return f;
}

bool SearchTermsTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.column() != ColSearchTerms)
        return false;
    if (index.row() < 0 || index.row() >= m_rows.size())
        return false;
    const QString s = value.toString();
    if (m_rows[index.row()].searchTerms == s)
        return false;
    m_rows[index.row()].searchTerms = s;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    const QModelIndex bytesIdx = this->index(index.row(), ColBytes);
    emit dataChanged(bytesIdx, bytesIdx);
    return true;
}
