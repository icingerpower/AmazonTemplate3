#include "GsprDoneTable.h"

#include <QSettings>

namespace {
const auto SETTINGS_KEY = QStringLiteral("gspr/doneAsins");
const auto FIELD_SEP    = QStringLiteral("|");
constexpr int COL_ASIN = 0;
constexpr int COL_COUNTRY = 1;
constexpr int COL_TYPE = 2;
constexpr int COL_DATE = 3;
constexpr int COL_LONG = 4;
}

const QStringList GsprDoneTable::HEADER{
    QObject::tr("ASIN")
    , QObject::tr("Country")
    , QObject::tr("Warning type")
    , QObject::tr("Date done")
    , QObject::tr("Long to process")
};

GsprDoneTable::GsprDoneTable(QObject *parent)
    : QAbstractTableModel(parent)
{
    _load();
}

int GsprDoneTable::_find(const QString &asin, const QString &countryCode,
                         const QString &warningType) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i][COL_ASIN] == asin
                && m_rows[i][COL_COUNTRY] == countryCode
                && m_rows[i][COL_TYPE] == warningType)
            return i;
    return -1;
}

void GsprDoneTable::recordDone(const QString &asin, const QString &countryCode,
                               const QString &warningType)
{
    if (_find(asin, countryCode, warningType) >= 0)
        return; // keep the first done date
    beginInsertRows(QModelIndex{}, m_rows.size(), m_rows.size());
    m_rows << QStringList{asin, countryCode, warningType,
                          QDateTime::currentDateTime().toString(Qt::ISODate),
                          QString{}};
    endInsertRows();
    _save();
}

bool GsprDoneTable::isDone(const QString &asin, const QString &countryCode,
                           const QString &warningType) const
{
    return _find(asin, countryCode, warningType) >= 0;
}

QDateTime GsprDoneTable::dateDone(const QString &asin, const QString &countryCode,
                                  const QString &warningType) const
{
    const int i = _find(asin, countryCode, warningType);
    if (i < 0)
        return QDateTime{};
    return QDateTime::fromString(m_rows[i][COL_DATE], Qt::ISODate);
}

void GsprDoneTable::markLongToProcess(const QString &asin, const QString &countryCode,
                                      const QString &warningType)
{
    const int i = _find(asin, countryCode, warningType);
    if (i < 0 || m_rows[i][COL_LONG] == QStringLiteral("1"))
        return;
    m_rows[i][COL_LONG] = QStringLiteral("1");
    emit dataChanged(index(i, COL_LONG), index(i, COL_LONG));
    _save();
}

QStringList GsprDoneTable::asinsDone(const QString &countryCode,
                                     const QString &warningType) const
{
    QStringList asins;
    for (const QStringList &row : m_rows)
        if (row[COL_COUNTRY] == countryCode && row[COL_TYPE] == warningType)
            asins << row[COL_ASIN];
    return asins;
}

void GsprDoneTable::removeDone(const QString &asin, const QString &countryCode,
                               const QString &warningType)
{
    removeAt(_find(asin, countryCode, warningType));
}

void GsprDoneTable::removeAt(int row)
{
    if (row < 0 || row >= m_rows.size())
        return;
    beginRemoveRows(QModelIndex{}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    _save();
}

void GsprDoneTable::clear()
{
    if (m_rows.isEmpty())
        return;
    beginRemoveRows(QModelIndex{}, 0, m_rows.size() - 1);
    m_rows.clear();
    endRemoveRows();
    _save();
}

QVariant GsprDoneTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return HEADER[section];
    if (orientation == Qt::Vertical && role == Qt::DisplayRole)
        return QString::number(section + 1);
    return QVariant{};
}

int GsprDoneTable::rowCount(const QModelIndex &) const
{
    return m_rows.size();
}

int GsprDoneTable::columnCount(const QModelIndex &) const
{
    return HEADER.size();
}

QVariant GsprDoneTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == COL_LONG)
            return m_rows[index.row()][COL_LONG] == QStringLiteral("1")
                ? QObject::tr("YES — investigate") : QString{};
        return m_rows[index.row()][index.column()];
    }
    return QVariant{};
}

Qt::ItemFlags GsprDoneTable::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == COL_ASIN)
        f |= Qt::ItemIsEditable; // copy-paste only — no setData(), edits are discarded
    return f;
}

void GsprDoneTable::_load()
{
    const QStringList saved = QSettings().value(SETTINGS_KEY).toStringList();
    for (const QString &entry : saved) {
        const QStringList fields = entry.split(FIELD_SEP);
        if (fields.size() == HEADER.size())
            m_rows << fields;
    }
}

void GsprDoneTable::_save() const
{
    QStringList entries;
    for (const QStringList &row : m_rows)
        entries << row.join(FIELD_SEP);
    QSettings().setValue(SETTINGS_KEY, entries);
}
