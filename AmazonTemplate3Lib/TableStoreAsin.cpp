#include "TableStoreAsin.h"
#include <QDate>

TableStoreAsin::TableStoreAsin(QObject *parent)
    : QAbstractTableModel(parent)
{}

void TableStoreAsin::setMarketplaces(const QStringList &mpIds, const QStringList &mpLabels)
{
    beginResetModel();
    m_mpIds    = mpIds;
    m_mpLabels = mpLabels;
    endResetModel();
}

void TableStoreAsin::setRows(const QList<Row> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

void TableStoreAsin::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void TableStoreAsin::updateImage(const QString &asin, const QPixmap &px)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).asin == asin) {
            m_rows[i].image = px;
            const QModelIndex idx = index(i, ColImage);
            emit dataChanged(idx, idx, {Qt::DecorationRole});
            return;
        }
    }
}

bool TableStoreAsin::moveRowUp(int row)
{
    if (row <= 0 || row >= m_rows.size()) return false;
    beginMoveRows({}, row, row, {}, row - 1);
    m_rows.swapItemsAt(row, row - 1);
    endMoveRows();
    return true;
}

bool TableStoreAsin::moveRowDown(int row)
{
    if (row < 0 || row >= m_rows.size() - 1) return false;
    beginMoveRows({}, row + 1, row + 1, {}, row);
    m_rows.swapItemsAt(row, row + 1);
    endMoveRows();
    return true;
}

int TableStoreAsin::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int TableStoreAsin::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColExistsStart + m_mpIds.size();
}

QVariant TableStoreAsin::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &row = m_rows.at(index.row());
    const int  col = index.column();

    if (role == Qt::SizeHintRole && col == ColImage)
        return QSize(58, 58);

    if (col == ColImage) {
        if (role == Qt::DecorationRole && !row.image.isNull())
            return row.image;
        return {};
    }

    // Centre-align existence columns
    if (role == Qt::TextAlignmentRole && col >= ColExistsStart)
        return QVariant(Qt::AlignCenter);

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
        case ColAsin:  return row.asin;
        case ColTitle: return row.title;
        case ColCreatedDate:
            return row.createdDate.isValid()
                ? row.createdDate.toString(QStringLiteral("MMM yyyy"))
                : QVariant{};
        default:
            if (col >= ColExistsStart && col < ColExistsStart + m_mpIds.size()) {
                const QString &mpId = m_mpIds.at(col - ColExistsStart);
                return row.existsInMarketplaces.contains(mpId)
                    ? QStringLiteral("✓") : QVariant{};
            }
        }
    }

    // Tooltip: full date + days ago
    if (role == Qt::ToolTipRole && col == ColCreatedDate && row.createdDate.isValid()) {
        const int days = row.createdDate.daysTo(QDate::currentDate());
        return QStringLiteral("%1  (%2 days ago)")
            .arg(row.createdDate.toString(Qt::ISODate))
            .arg(days);
    }

    return {};
}

QVariant TableStoreAsin::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case ColImage:       return {};
    case ColAsin:        return tr("ASIN");
    case ColTitle:       return tr("Title");
    case ColCreatedDate: return tr("Created");
    default:
        if (section >= ColExistsStart && section < ColExistsStart + m_mpLabels.size())
            return m_mpLabels.at(section - ColExistsStart);
    }
    return {};
}

Qt::ItemFlags TableStoreAsin::flags(const QModelIndex &index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}
