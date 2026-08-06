#include "GsprFailedTable.h"

#include <QDateTime>
#include <QSettings>

namespace {
const auto SETTINGS_KEY = QStringLiteral("gspr/failedAsins");
const auto FIELD_SEP    = QStringLiteral("|");
}

const QStringList GsprFailedTable::HEADER{
    QObject::tr("ASIN")
    , QObject::tr("Country")
    , QObject::tr("Date tried")
    , QObject::tr("Reason")
};

GsprFailedTable::GsprFailedTable(QObject *parent)
    : QAbstractTableModel(parent)
{
    _load();
}

void GsprFailedTable::recordFailure(const QString &asin, const QString &countryCode,
                                    const QString &reason)
{
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i][0] == asin && m_rows[i][1] == countryCode) {
            m_rows[i][2] = now;
            m_rows[i][3] = reason;
            emit dataChanged(index(i, 2), index(i, 3));
            _save();
            return;
        }
    }
    beginInsertRows(QModelIndex{}, m_rows.size(), m_rows.size());
    m_rows << QStringList{asin, countryCode, now, reason};
    endInsertRows();
    _save();
}

void GsprFailedTable::removeAt(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    beginRemoveRows(QModelIndex{}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    _save();
}

void GsprFailedTable::removeFailure(const QString &asin, const QString &countryCode)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i][0] == asin && m_rows[i][1] == countryCode) {
            removeAt(i);
            return;
        }
    }
}

void GsprFailedTable::clear()
{
    if (m_rows.isEmpty())
        return;
    beginRemoveRows(QModelIndex{}, 0, m_rows.size() - 1);
    m_rows.clear();
    endRemoveRows();
    _save();
}

QVariant GsprFailedTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return HEADER[section];
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return QString::number(section + 1);
    return QVariant{};
}

int GsprFailedTable::rowCount(const QModelIndex &) const
{
    return m_rows.size();
}

int GsprFailedTable::columnCount(const QModelIndex &) const
{
    return HEADER.size();
}

QVariant GsprFailedTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return m_rows[index.row()][index.column()];
    return QVariant{};
}

Qt::ItemFlags GsprFailedTable::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 0)
        f |= Qt::ItemIsEditable; // copy-paste only — no setData(), edits are discarded
    return f;
}

void GsprFailedTable::_load()
{
    const QStringList saved = QSettings().value(SETTINGS_KEY).toStringList();
    for (const QString &entry : saved) {
        QStringList fields = entry.split(FIELD_SEP);
        while (fields.size() < HEADER.size())
            fields << QString{}; // entries saved before the Reason column
        if (fields.size() == HEADER.size())
            m_rows << fields;
    }
}

void GsprFailedTable::_save() const
{
    QStringList entries;
    for (const QStringList &row : m_rows)
        entries << row.join(FIELD_SEP);
    QSettings().setValue(SETTINGS_KEY, entries);
}
