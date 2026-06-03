#include "TableProductWarnings.h"

const QStringList TableProductWarnings::HEADER = {
    QStringLiteral("ASIN"),
    QStringLiteral("SKU"),
    QStringLiteral("Title"),
    QStringLiteral("Attribute"),
    QStringLiteral("Error"),
    QStringLiteral("Value"),
    QStringLiteral("AI Value")
};

TableProductWarnings::TableProductWarnings(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void TableProductWarnings::addRow(const WarningRow &row)
{
    const int newRowIndex = m_rows.size();
    beginInsertRows(QModelIndex(), newRowIndex, newRowIndex);
    m_rows.append(row);
    endInsertRows();
}

void TableProductWarnings::clear()
{
    if (m_rows.isEmpty())
        return;
    beginRemoveRows(QModelIndex(), 0, m_rows.size() - 1);
    m_rows.clear();
    endRemoveRows();
}

int TableProductWarnings::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

int TableProductWarnings::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant TableProductWarnings::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    if (index.column() < 0 || index.column() >= ColCount)
        return {};

    if (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole)
        return {};

    const WarningRow &row = m_rows.at(index.row());
    switch (index.column()) {
    case ColAsin:        return row.asin;
    case ColSku:         return row.sku;
    case ColTitle:       return row.title;
    case ColAttributeId: return row.attributeId;
    case ColError:       return row.issueMessage;
    case ColValue:       return row.value;
    case ColAiValue:     return row.aiValue;
    default:             return {};
    }
}

QVariant TableProductWarnings::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};

    if (orientation == Qt::Horizontal) {
        if (section < 0 || section >= HEADER.size())
            return {};
        return HEADER.at(section);
    }

    // Vertical: 1-based row number
    if (section < 0 || section >= m_rows.size())
        return {};
    return section + 1;
}

bool TableProductWarnings::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;
    if (role != Qt::EditRole)
        return false;
    if (index.row() < 0 || index.row() >= m_rows.size())
        return false;
    if (index.column() != ColAiValue)
        return false;

    const QString newValue = value.toString();
    WarningRow &row = m_rows[index.row()];
    if (row.aiValue == newValue)
        return true;

    row.aiValue = newValue;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

Qt::ItemFlags TableProductWarnings::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    // All columns open an inline editor on double-click for selection/copy-paste.
    // Only ColAiValue actually persists changes (setData enforces this).
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
}
