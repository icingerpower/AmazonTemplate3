#include "TableWarningsManufacturer.h"

TableWarningsManufacturer::TableWarningsManufacturer(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void TableWarningsManufacturer::addRow(const ManufacturerWarningRow &row)
{
    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    m_rows.append(row);
    endInsertRows();
}

void TableWarningsManufacturer::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

int TableWarningsManufacturer::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

int TableWarningsManufacturer::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant TableWarningsManufacturer::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};

    const ManufacturerWarningRow &row = m_rows.at(index.row());
    switch (index.column()) {
    case ColSku:          return row.sku;
    case ColAsin:         return row.asin;
    case ColTitle:        return row.title;
    case ColCountry:      return row.countryCode;
    case ColAttributeId:  return row.attributeId;
    case ColDescription:  return row.issueMessage;
    case ColManufacturer: return row.manufacturerName;
    default:              return {};
    }
}

QVariant TableWarningsManufacturer::headerData(int section, Qt::Orientation orientation,
                                               int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};

    switch (section) {
    case ColSku:          return QStringLiteral("SKU");
    case ColAsin:         return QStringLiteral("ASIN");
    case ColTitle:        return QStringLiteral("Title");
    case ColCountry:      return QStringLiteral("Country");
    case ColAttributeId:  return QStringLiteral("Attribute ID");
    case ColDescription:  return QStringLiteral("Description");
    case ColManufacturer: return QStringLiteral("Manufacturer");
    default:              return {};
    }
}

Qt::ItemFlags TableWarningsManufacturer::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    if (index.column() == ColManufacturer)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool TableWarningsManufacturer::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole)
        return false;
    if (index.row() < 0 || index.row() >= m_rows.size())
        return false;
    if (index.column() != ColManufacturer)
        return false;

    m_rows[index.row()].manufacturerName = value.toString();
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}
