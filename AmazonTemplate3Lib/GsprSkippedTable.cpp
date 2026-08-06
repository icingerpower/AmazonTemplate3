#include "GsprSkippedTable.h"

#include <QDateTime>
#include <QSettings>

namespace {
const auto SETTINGS_KEY = QStringLiteral("gspr/skippedAsins");
const auto FIELD_SEP    = QStringLiteral("|");
}

const QStringList GsprSkippedTable::HEADER{
    QObject::tr("ASIN")
    , QObject::tr("SKU")
    , QObject::tr("Date skipped")
};

GsprSkippedTable::GsprSkippedTable(QObject *parent)
    : QAbstractTableModel(parent)
{
    _load();
}

void GsprSkippedTable::recordSkip(const QString &asin, const QString &sku)
{
    if (contains(asin))
        return;
    beginInsertRows(QModelIndex{}, m_rows.size(), m_rows.size());
    m_rows << QStringList{asin, sku,
                          QDateTime::currentDateTime().toString(Qt::ISODate)};
    endInsertRows();
    _save();
}

void GsprSkippedTable::removeAt(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    beginRemoveRows(QModelIndex{}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    _save();
}

bool GsprSkippedTable::contains(const QString &asin) const
{
    for (const QStringList &row : m_rows)
        if (row[0] == asin)
            return true;
    return false;
}

QStringList GsprSkippedTable::asins() const
{
    QStringList out;
    for (const QStringList &row : m_rows)
        out << row[0];
    return out;
}

QVariant GsprSkippedTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return HEADER[section];
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return QString::number(section + 1);
    return QVariant{};
}

int GsprSkippedTable::rowCount(const QModelIndex &) const
{
    return m_rows.size();
}

int GsprSkippedTable::columnCount(const QModelIndex &) const
{
    return HEADER.size();
}

QVariant GsprSkippedTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return m_rows[index.row()][index.column()];
    return QVariant{};
}

Qt::ItemFlags GsprSkippedTable::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 0)
        f |= Qt::ItemIsEditable; // copy-paste only — no setData(), edits are discarded
    return f;
}

void GsprSkippedTable::_load()
{
    const QStringList saved = QSettings().value(SETTINGS_KEY).toStringList();
    for (const QString &entry : saved) {
        const QStringList fields = entry.split(FIELD_SEP);
        if (fields.size() == HEADER.size())
            m_rows << fields;
    }
}

void GsprSkippedTable::_save() const
{
    QStringList entries;
    for (const QStringList &row : m_rows)
        entries << row.join(FIELD_SEP);
    QSettings().setValue(SETTINGS_KEY, entries);
}
