#include "TableUnresolvedAsins.h"

#include <QColor>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
constexpr auto kFileName = "unresolved_asins.json";
} // namespace

TableUnresolvedAsins::TableUnresolvedAsins(QObject *parent)
    : QAbstractTableModel(parent)
{}

void TableUnresolvedAsins::load(const QDir &workingDir)
{
    m_workingDir = workingDir;

    QFile f(workingDir.filePath(QString::fromLatin1(kFileName)));
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();

    beginResetModel();
    m_rows.clear();
    m_asinIndex.clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        const QString asin = obj.value(QStringLiteral("asin")).toString();
        if (asin.isEmpty()) continue;
        Row row;
        row.asin  = asin;
        row.sku   = obj.value(QStringLiteral("sku")).toString();
        row.title = obj.value(QStringLiteral("title")).toString();
        m_asinIndex.insert(asin, m_rows.size());
        m_rows.append(row);
    }
    endResetModel();
}

void TableUnresolvedAsins::save() const
{
    if (!m_workingDir.exists()) return;

    QJsonArray arr;
    for (const Row &row : m_rows) {
        QJsonObject obj;
        obj[QStringLiteral("asin")]  = row.asin;
        obj[QStringLiteral("sku")]   = row.sku;
        obj[QStringLiteral("title")] = row.title;
        arr.append(obj);
    }

    QSaveFile f(m_workingDir.filePath(QString::fromLatin1(kFileName)));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.commit();
}

bool TableUnresolvedAsins::addOrUpdate(const QString &asin, const QString &title)
{
    auto it = m_asinIndex.find(asin);
    if (it != m_asinIndex.end()) {
        // Update title if the stored one is blank and we now have one.
        const int ri = it.value();
        if (m_rows[ri].title.isEmpty() && !title.isEmpty()) {
            m_rows[ri].title = title;
            emit dataChanged(index(ri, ColTitle), index(ri, ColTitle));
        }
        return false; // already present
    }

    const int ri = m_rows.size();
    beginInsertRows({}, ri, ri);
    m_rows.append({asin, {}, title});
    m_asinIndex.insert(asin, ri);
    endInsertRows();
    return true;
}

QString TableUnresolvedAsins::skuForAsin(const QString &asin) const
{
    auto it = m_asinIndex.find(asin);
    if (it == m_asinIndex.end()) return {};
    return m_rows[it.value()].sku;
}

QHash<QString, QString> TableUnresolvedAsins::buildSkuMap() const
{
    QHash<QString, QString> map;
    for (const Row &row : m_rows)
        if (!row.sku.isEmpty())
            map.insert(row.asin, row.sku);
    return map;
}

// ---------------------------------------------------------------------------
// QAbstractTableModel
// ---------------------------------------------------------------------------

int TableUnresolvedAsins::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int TableUnresolvedAsins::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant TableUnresolvedAsins::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_rows.size())
        return {};
    const Row &row = m_rows[idx.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (idx.column()) {
        case ColAsin:  return row.asin;
        case ColSku:   return row.sku;
        case ColTitle: return row.title;
        default:       return {};
        }
    }

    // Highlight rows still missing a SKU
    if (role == Qt::BackgroundRole && row.sku.isEmpty())
        return QColor(255, 235, 200); // light orange

    return {};
}

QVariant TableUnresolvedAsins::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
    switch (section) {
    case ColAsin:  return tr("ASIN");
    case ColSku:   return tr("SKU");
    case ColTitle: return tr("Title");
    default:       return {};
    }
}

Qt::ItemFlags TableUnresolvedAsins::flags(const QModelIndex &idx) const
{
    if (!idx.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (idx.column() == ColSku)
        f |= Qt::ItemIsEditable;
    return f;
}

bool TableUnresolvedAsins::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    if (role != Qt::EditRole) return false;
    if (!idx.isValid() || idx.column() != ColSku) return false;
    if (idx.row() < 0 || idx.row() >= m_rows.size()) return false;

    m_rows[idx.row()].sku = value.toString().trimmed();
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole});
    save();
    return true;
}
