#include "TableGpsrManufacturers.h"

#include <QColor>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
constexpr auto kFileName = "gpsr_manufacturers.json";
} // namespace

TableGpsrManufacturers::TableGpsrManufacturers(QObject *parent)
    : QAbstractTableModel(parent)
{}

void TableGpsrManufacturers::load(const QDir &workingDir)
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
        row.asin        = asin;
        row.companyName = obj.value(QStringLiteral("companyName")).toString();
        row.countryCode = obj.value(QStringLiteral("countryCode")).toString();
        row.email       = obj.value(QStringLiteral("email")).toString();
        row.phone       = obj.value(QStringLiteral("phone")).toString();
        row.title       = obj.value(QStringLiteral("title")).toString();
        m_asinIndex.insert(asin, m_rows.size());
        m_rows.append(row);
    }
    endResetModel();
}

void TableGpsrManufacturers::save() const
{
    if (!m_workingDir.exists()) return;

    QJsonArray arr;
    for (const Row &row : m_rows) {
        QJsonObject obj;
        obj[QStringLiteral("asin")]        = row.asin;
        obj[QStringLiteral("companyName")] = row.companyName;
        obj[QStringLiteral("countryCode")] = row.countryCode;
        obj[QStringLiteral("email")]       = row.email;
        obj[QStringLiteral("phone")]       = row.phone;
        obj[QStringLiteral("title")]       = row.title;
        arr.append(obj);
    }

    QSaveFile f(m_workingDir.filePath(QString::fromLatin1(kFileName)));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.commit();
}

bool TableGpsrManufacturers::addOrUpdate(const QString &asin, const QString &title)
{
    auto it = m_asinIndex.find(asin);
    if (it != m_asinIndex.end()) {
        const int ri = it.value();
        if (m_rows[ri].title.isEmpty() && !title.isEmpty()) {
            m_rows[ri].title = title;
            emit dataChanged(index(ri, ColTitle), index(ri, ColTitle));
        }
        return false; // already present
    }

    const int ri = m_rows.size();
    beginInsertRows({}, ri, ri);
    Row row;
    row.asin  = asin;
    row.title = title;
    m_rows.append(row);
    m_asinIndex.insert(asin, ri);
    endInsertRows();
    return true;
}

QJsonArray TableGpsrManufacturers::buildAttributeJson(const QString &asin,
                                                      const QString &marketplaceId) const
{
    auto it = m_asinIndex.find(asin);
    if (it == m_asinIndex.end()) return {};
    const Row &row = m_rows[it.value()];
    if (row.companyName.isEmpty()) return {};

    QJsonObject obj;
    obj.insert(QStringLiteral("marketplace_id"), marketplaceId);
    obj.insert(QStringLiteral("company_name"),   row.companyName);
    obj.insert(QStringLiteral("country_code"),   row.countryCode);
    if (!row.email.isEmpty()) obj.insert(QStringLiteral("email"),        row.email);
    if (!row.phone.isEmpty()) obj.insert(QStringLiteral("phone_number"), row.phone);

    return QJsonArray{obj};
}

// ---------------------------------------------------------------------------
// QAbstractTableModel
// ---------------------------------------------------------------------------

int TableGpsrManufacturers::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int TableGpsrManufacturers::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant TableGpsrManufacturers::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_rows.size())
        return {};
    const Row &row = m_rows[idx.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (idx.column()) {
        case ColAsin:        return row.asin;
        case ColCompanyName: return row.companyName;
        case ColCountryCode: return row.countryCode;
        case ColEmail:       return row.email;
        case ColPhone:       return row.phone;
        case ColTitle:       return row.title;
        default:             return {};
        }
    }

    if (row.companyName.isEmpty()) {
        if (role == Qt::BackgroundRole) return QColor(160, 60, 0);  // dark orange
        if (role == Qt::ForegroundRole) return QColor(Qt::white);
    }

    return {};
}

QVariant TableGpsrManufacturers::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
    switch (section) {
    case ColAsin:        return tr("ASIN");
    case ColCompanyName: return tr("Company name");
    case ColCountryCode: return tr("Country");
    case ColEmail:       return tr("Email");
    case ColPhone:       return tr("Phone");
    case ColTitle:       return tr("Title");
    default:             return {};
    }
}

Qt::ItemFlags TableGpsrManufacturers::flags(const QModelIndex &idx) const
{
    if (!idx.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool TableGpsrManufacturers::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    if (role != Qt::EditRole) return false;
    if (!idx.isValid()) return false;
    if (idx.row() < 0 || idx.row() >= m_rows.size()) return false;

    Row &row = m_rows[idx.row()];
    switch (idx.column()) {
    case ColCompanyName: row.companyName = value.toString().trimmed(); break;
    case ColCountryCode: row.countryCode = value.toString().trimmed(); break;
    case ColEmail:       row.email       = value.toString().trimmed(); break;
    case ColPhone:       row.phone       = value.toString().trimmed(); break;
    default: return false; // ASIN and Title are copy-only
    }

    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole, Qt::BackgroundRole,
                                Qt::ForegroundRole});
    save();
    return true;
}
