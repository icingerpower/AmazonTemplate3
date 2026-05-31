#include "SizingTableTemplateModel.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace {
constexpr auto kFileName = "sizing_templates.json";

QJsonObject measurementsToJson(const QMap<QString, MeasurementInput> &m)
{
    QJsonObject obj;
    for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("ref"),   it.value().refValue);
        entry.insert(QStringLiteral("step"),  it.value().step);
        entry.insert(QStringLiteral("range"), it.value().rangeVal);
        obj.insert(it.key(), entry);
    }
    return obj;
}

QMap<QString, MeasurementInput> measurementsFromJson(const QJsonObject &obj)
{
    QMap<QString, MeasurementInput> out;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        MeasurementInput mi;
        mi.refValue = entry.value(QStringLiteral("ref")).toDouble();
        mi.step     = entry.value(QStringLiteral("step")).toDouble();
        mi.rangeVal = entry.value(QStringLiteral("range")).toDouble();
        out.insert(it.key(), mi);
    }
    return out;
}

QJsonObject templateToJson(const SizingTemplate &t)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"),           t.id);
    obj.insert(QStringLiteral("name"),         t.name);
    obj.insert(QStringLiteral("category"),     t.category);
    obj.insert(QStringLiteral("mode"),         t.mode);
    obj.insert(QStringLiteral("from"),         t.fromVal);
    obj.insert(QStringLiteral("to"),           t.toVal);
    obj.insert(QStringLiteral("brandMode"),     t.brandMode);
    obj.insert(QStringLiteral("brandFrom"),     t.brandFromVal);
    obj.insert(QStringLiteral("brandTo"),       t.brandToVal);
    obj.insert(QStringLiteral("measurements"),  measurementsToJson(t.measurements));
    return obj;
}

SizingTemplate templateFromJson(const QJsonObject &obj)
{
    SizingTemplate t;
    t.id           = obj.value(QStringLiteral("id")).toString();
    t.name         = obj.value(QStringLiteral("name")).toString();
    t.category     = obj.value(QStringLiteral("category")).toString();
    t.mode         = obj.value(QStringLiteral("mode")).toString();
    t.fromVal      = obj.value(QStringLiteral("from")).toString();
    t.toVal        = obj.value(QStringLiteral("to")).toString();
    t.brandMode    = obj.value(QStringLiteral("brandMode")).toString();
    t.brandFromVal = obj.value(QStringLiteral("brandFrom")).toString();
    t.brandToVal   = obj.value(QStringLiteral("brandTo")).toString();
    t.measurements = measurementsFromJson(obj.value(QStringLiteral("measurements")).toObject());
    if (t.id.isEmpty())
        t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return t;
}
} // namespace

SizingTableTemplateModel::SizingTableTemplateModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void SizingTableTemplateModel::setWorkingDir(const QDir &dir)
{
    m_dir = dir;
    load();
}

QString SizingTableTemplateModel::_filePath() const
{
    return m_dir.filePath(QString::fromLatin1(kFileName));
}

int SizingTableTemplateModel::addTemplate(const QString &name)
{
    SizingTemplate t;
    t.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    t.name = name;

    const int row = m_templates.size();
    beginInsertRows(QModelIndex(), row, row);
    m_templates.append(t);
    endInsertRows();
    save();
    return row;
}

void SizingTableTemplateModel::removeTemplateAt(int row)
{
    if (row < 0 || row >= m_templates.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_templates.removeAt(row);
    endRemoveRows();
    save();
}

QString SizingTableTemplateModel::idForRow(int row) const
{
    if (row < 0 || row >= m_templates.size())
        return {};
    return m_templates.at(row).id;
}

int SizingTableTemplateModel::rowForId(const QString &id) const
{
    for (int i = 0; i < m_templates.size(); ++i)
        if (m_templates.at(i).id == id)
            return i;
    return -1;
}

const SizingTemplate &SizingTableTemplateModel::templateAt(int row) const
{
    return m_templates.at(row);
}

void SizingTableTemplateModel::updateTemplate(int row, const SizingTemplate &data)
{
    if (row < 0 || row >= m_templates.size())
        return;
    m_templates[row] = data;
    const QModelIndex tl = index(row, 0);
    const QModelIndex br = index(row, ColumnCount - 1);
    emit dataChanged(tl, br);
    save();
}

void SizingTableTemplateModel::load()
{
    beginResetModel();
    m_templates.clear();

    if (m_dir.exists()) {
        QFile f(_filePath());
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isArray()) {
                const QJsonArray arr = doc.array();
                m_templates.reserve(arr.size());
                for (const QJsonValue &v : arr) {
                    if (v.isObject())
                        m_templates.append(templateFromJson(v.toObject()));
                }
            }
        }
    }
    endResetModel();
}

void SizingTableTemplateModel::save() const
{
    if (!m_dir.exists())
        return;

    QJsonArray arr;
    for (const SizingTemplate &t : m_templates)
        arr.append(templateToJson(t));

    QSaveFile f(_filePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.commit();
}

int SizingTableTemplateModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_templates.size();
}

int SizingTableTemplateModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant SizingTableTemplateModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_templates.size())
        return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    const SizingTemplate &t = m_templates.at(index.row());
    switch (index.column()) {
    case ColName:     return t.name;
    case ColCategory: return t.category;
    default:          return {};
    }
}

QVariant SizingTableTemplateModel::headerData(int section, Qt::Orientation orientation,
                                              int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ColName:     return tr("Name");
        case ColCategory: return tr("Category");
        default:          return {};
        }
    }
    return section + 1;
}

Qt::ItemFlags SizingTableTemplateModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColName)
        f |= Qt::ItemIsEditable;
    return f;
}

bool SizingTableTemplateModel::setData(const QModelIndex &index, const QVariant &value,
                                       int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.column() != ColName)
        return false;
    if (index.row() < 0 || index.row() >= m_templates.size())
        return false;
    const QString s = value.toString();
    if (m_templates[index.row()].name == s)
        return false;
    m_templates[index.row()].name = s;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    save();
    return true;
}
