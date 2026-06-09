#include "TreeProductWarnings.h"

#include <QBrush>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

const QStringList TreeProductWarnings::HEADER = {
    QStringLiteral("ASIN"),
    QStringLiteral("SKU"),
    QStringLiteral("Title"),
    QStringLiteral("Ask AI"),
    QStringLiteral("All countries"),
    QStringLiteral("All siblings"),
    QStringLiteral("Attribute"),
    QStringLiteral("Error")
};

static bool _isBulletPointAttrId(const QString &attrId)
{
    return attrId.compare(QStringLiteral("bullet_point"), Qt::CaseInsensitive) == 0;
}

TreeProductWarnings::TreeProductWarnings(QObject *parent)
    : QAbstractItemModel(parent)
{
}

TreeProductWarnings::~TreeProductWarnings()
{
    qDeleteAll(m_violations);
    m_violations.clear();
}

void TreeProductWarnings::addRow(const WarningRow &row)
{
    auto *vn = new ViolationNode;
    vn->row = row;
    const QString attrIdLower = row.attributeId.toLower();
    vn->askAi        = !m_excludedAttrIds.contains(attrIdLower);
    vn->allCountries = m_allCountriesAttrIds.contains(attrIdLower);
    vn->allSiblings  = m_allSiblingsAttrIds.contains(attrIdLower);

    // Always: child 0 is the "Current value" read-only child.
    vn->children.append(ChildNode{true, -1, QStringLiteral("Current value"), QString{}});

    if (_isBulletPointAttrId(row.attributeId)) {
        for (int i = 0; i < 5; ++i) {
            vn->children.append(ChildNode{
                false,
                i,
                QStringLiteral("Bullet %1").arg(i + 1),
                QString{}
            });
        }
    } else {
        vn->children.append(ChildNode{
            false, -1, QStringLiteral("AI Value"), QString{}
        });
    }

    const int newIdx = m_violations.size();
    beginInsertRows(QModelIndex(), newIdx, newIdx);
    m_violations.append(vn);
    endInsertRows();
}

void TreeProductWarnings::clear()
{
    beginResetModel();
    qDeleteAll(m_violations);
    m_violations.clear();
    endResetModel();
}

int TreeProductWarnings::violationCount() const
{
    return m_violations.size();
}

TreeProductWarnings::ViolationNode *TreeProductWarnings::violationAt(int i)
{
    if (i < 0 || i >= m_violations.size()) return nullptr;
    return m_violations[i];
}

const TreeProductWarnings::ViolationNode *TreeProductWarnings::violationAt(int i) const
{
    if (i < 0 || i >= m_violations.size()) return nullptr;
    return m_violations[i];
}

void TreeProductWarnings::setAiValue(int violIdx, int aiChildIndex, const QString &value)
{
    if (violIdx < 0 || violIdx >= m_violations.size()) return;
    ViolationNode *vn = m_violations[violIdx];
    if (!vn) return;

    int aiCounter = 0;
    for (int i = 0; i < vn->children.size(); ++i) {
        ChildNode &c = vn->children[i];
        if (c.isCurrentValue) continue;
        if (aiCounter == aiChildIndex) {
            if (c.aiValue == value) return;
            c.aiValue = value;
            const QModelIndex parentIdx = createIndex(violIdx, 0, nullptr);
            const QModelIndex childIdx  = index(i, ColError, parentIdx);
            emit dataChanged(childIdx, childIdx, {Qt::DisplayRole, Qt::EditRole});
            return;
        }
        ++aiCounter;
    }
}

void TreeProductWarnings::setWorkingDir(const QString &path)
{
    m_workingDir = path;
    _loadExclusions();
    _loadUploadOptions();
    // Re-apply prefs to any already-loaded rows
    for (int i = 0; i < m_violations.size(); ++i) {
        const QString attrIdLower = m_violations[i]->row.attributeId.toLower();
        m_violations[i]->askAi        = !m_excludedAttrIds.contains(attrIdLower);
        m_violations[i]->allCountries = m_allCountriesAttrIds.contains(attrIdLower);
        m_violations[i]->allSiblings  = m_allSiblingsAttrIds.contains(attrIdLower);
    }
    if (!m_violations.isEmpty()) {
        emit dataChanged(createIndex(0, ColAskAi, nullptr),
                         createIndex(m_violations.size() - 1, ColAllSiblings, nullptr),
                         {Qt::CheckStateRole});
    }
}

bool TreeProductWarnings::isAskAi(int violIdx) const
{
    if (violIdx < 0 || violIdx >= m_violations.size()) return true;
    const ViolationNode *vn = m_violations[violIdx];
    return vn ? vn->askAi : true;
}

bool TreeProductWarnings::isAllCountries(int violIdx) const
{
    if (violIdx < 0 || violIdx >= m_violations.size()) return false;
    const ViolationNode *vn = m_violations[violIdx];
    return vn ? vn->allCountries : false;
}

bool TreeProductWarnings::isAllSiblings(int violIdx) const
{
    if (violIdx < 0 || violIdx >= m_violations.size()) return false;
    const ViolationNode *vn = m_violations[violIdx];
    return vn ? vn->allSiblings : false;
}

void TreeProductWarnings::_loadExclusions()
{
    m_excludedAttrIds.clear();
    if (m_workingDir.isEmpty()) return;

    const QString filePath = QDir(m_workingDir)
                                 .filePath(QStringLiteral("warnings_ask_ai_exclusions.json"));
    QFile f(filePath);
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) return;

    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonValue v = doc.object().value(QStringLiteral("excluded"));
    if (!v.isArray()) return;

    const QJsonArray arr = v.toArray();
    for (const QJsonValue &item : arr) {
        const QString s = item.toString().trimmed().toLower();
        if (!s.isEmpty()) m_excludedAttrIds.insert(s);
    }
}

void TreeProductWarnings::_saveExclusions() const
{
    if (m_workingDir.isEmpty()) return;

    QJsonArray arr;
    for (const QString &id : m_excludedAttrIds) arr.append(id);
    QJsonObject obj;
    obj.insert(QStringLiteral("excluded"), arr);
    const QJsonDocument doc(obj);

    const QString filePath = QDir(m_workingDir)
                                 .filePath(QStringLiteral("warnings_ask_ai_exclusions.json"));
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(doc.toJson(QJsonDocument::Indented));
    f.commit();
}

void TreeProductWarnings::_loadUploadOptions()
{
    m_allCountriesAttrIds.clear();
    m_allSiblingsAttrIds.clear();
    if (m_workingDir.isEmpty()) return;

    const QString filePath = QDir(m_workingDir)
                                 .filePath(QStringLiteral("warnings_upload_options.json"));
    QFile f(filePath);
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) return;

    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QJsonObject obj = doc.object();

    const QJsonValue ac = obj.value(QStringLiteral("allCountries"));
    if (ac.isArray()) {
        for (const QJsonValue &item : ac.toArray()) {
            const QString s = item.toString().trimmed().toLower();
            if (!s.isEmpty()) m_allCountriesAttrIds.insert(s);
        }
    }

    const QJsonValue as = obj.value(QStringLiteral("allSiblings"));
    if (as.isArray()) {
        for (const QJsonValue &item : as.toArray()) {
            const QString s = item.toString().trimmed().toLower();
            if (!s.isEmpty()) m_allSiblingsAttrIds.insert(s);
        }
    }
}

void TreeProductWarnings::_saveUploadOptions() const
{
    if (m_workingDir.isEmpty()) return;

    QJsonArray acArr;
    for (const QString &id : m_allCountriesAttrIds) acArr.append(id);
    QJsonArray asArr;
    for (const QString &id : m_allSiblingsAttrIds) asArr.append(id);

    QJsonObject obj;
    obj.insert(QStringLiteral("allCountries"), acArr);
    obj.insert(QStringLiteral("allSiblings"),  asArr);
    const QJsonDocument doc(obj);

    const QString filePath = QDir(m_workingDir)
                                 .filePath(QStringLiteral("warnings_upload_options.json"));
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(doc.toJson(QJsonDocument::Indented));
    f.commit();
}

QModelIndex TreeProductWarnings::index(int row, int col, const QModelIndex &parent) const
{
    if (row < 0 || col < 0 || col >= ColCount) return {};

    if (!parent.isValid()) {
        if (row >= m_violations.size()) return {};
        return createIndex(row, col, static_cast<void *>(nullptr));
    }

    // Parent must be a top-level violation row.
    if (parent.internalPointer() != nullptr) return {};
    if (parent.row() < 0 || parent.row() >= m_violations.size()) return {};

    ViolationNode *vn = m_violations[parent.row()];
    if (!vn || row >= vn->children.size()) return {};
    return createIndex(row, col, static_cast<void *>(vn));
}

QModelIndex TreeProductWarnings::parent(const QModelIndex &index) const
{
    if (!index.isValid()) return {};
    void *ptr = index.internalPointer();
    if (ptr == nullptr) return {}; // top-level row

    auto *vn = static_cast<ViolationNode *>(ptr);
    const int pos = m_violations.indexOf(vn);
    if (pos < 0) return {};
    return createIndex(pos, 0, static_cast<void *>(nullptr));
}

int TreeProductWarnings::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) return m_violations.size();
    if (parent.internalPointer() == nullptr) {
        // Top-level violation row → return number of children.
        if (parent.row() < 0 || parent.row() >= m_violations.size()) return 0;
        const ViolationNode *vn = m_violations[parent.row()];
        return vn ? vn->children.size() : 0;
    }
    return 0; // children themselves have no descendants
}

int TreeProductWarnings::columnCount(const QModelIndex & /*parent*/) const
{
    return ColCount;
}

QVariant TreeProductWarnings::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};

    void *ptr = index.internalPointer();
    if (ptr == nullptr) {
        // Top-level violation row.
        if (index.row() < 0 || index.row() >= m_violations.size()) return {};
        const ViolationNode *vn = m_violations[index.row()];
        if (!vn) return {};
        const WarningRow &row = vn->row;

        // Checkbox columns — handled separately because they use
        // Qt::CheckStateRole rather than Qt::DisplayRole.
        if (index.column() == ColAskAi) {
            if (role == Qt::CheckStateRole)
                return vn->askAi ? Qt::Checked : Qt::Unchecked;
            return {};
        }
        if (index.column() == ColAllCountries) {
            if (role == Qt::CheckStateRole)
                return vn->allCountries ? Qt::Checked : Qt::Unchecked;
            return {};
        }
        if (index.column() == ColAllSiblings) {
            if (role == Qt::CheckStateRole)
                return vn->allSiblings ? Qt::Checked : Qt::Unchecked;
            return {};
        }

        if (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole)
            return {};

        switch (index.column()) {
        case ColAsin:      return row.asin;
        case ColSku:       return row.sku;
        case ColTitle:     return row.title;
        case ColAttribute: return row.attributeId;
        case ColError:     return row.issueMessage;
        default:           return {};
        }
    }

    // Child row — checkbox columns never show anything for children.
    if (index.column() == ColAskAi
        || index.column() == ColAllCountries
        || index.column() == ColAllSiblings) return {};

    auto *vn = static_cast<ViolationNode *>(ptr);
    if (!vn) return {};
    if (index.row() < 0 || index.row() >= vn->children.size()) return {};
    const ChildNode &child = vn->children[index.row()];
    const WarningRow &row  = vn->row;

    // Dark-red background on AI value cells that are still empty.
    if (role == Qt::BackgroundRole
            && index.column() == ColError
            && !child.isCurrentValue
            && child.aiValue.isEmpty())
        return QBrush(QColor(120, 20, 20));

    if (role != Qt::DisplayRole && role != Qt::EditRole && role != Qt::ToolTipRole)
        return {};

    switch (index.column()) {
    case ColAttribute:
        return child.label;
    case ColError:
        // For child rows, ColError (adjacent to the label) carries the value so
        // it is always visible without horizontal scrolling.
        if (child.isCurrentValue) {
            if (_isBulletPointAttrId(row.attributeId) && !row.bulletPoints.isEmpty())
                return row.bulletPoints.join(QLatin1Char('\n'));
            return row.value;
        }
        return child.aiValue;
    default:
        return {};
    }
}

QVariant TreeProductWarnings::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (o != Qt::Horizontal) return {};
    if (section < 0 || section >= HEADER.size()) return {};
    return HEADER.at(section);
}

bool TreeProductWarnings::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid()) return false;

    void *ptr = index.internalPointer();

    // ---- Parent row: Ask AI checkbox toggling ----
    if (ptr == nullptr) {
        if (index.column() == ColAskAi && role == Qt::CheckStateRole) {
            if (index.row() < 0 || index.row() >= m_violations.size()) return false;
            ViolationNode *vn = m_violations[index.row()];
            if (!vn) return false;

            const bool checked   = (value.toInt() == Qt::Checked);
            const QString attrId = vn->row.attributeId.toLower();

            // Toggle ALL rows with the same attributeId so the per-attribute
            // exclusion is applied consistently.
            for (int i = 0; i < m_violations.size(); ++i) {
                if (m_violations[i]->row.attributeId.toLower() == attrId) {
                    m_violations[i]->askAi = checked;
                    const QModelIndex idx = createIndex(i, ColAskAi, nullptr);
                    emit dataChanged(idx, idx, {Qt::CheckStateRole});
                }
            }

            if (checked) m_excludedAttrIds.remove(attrId);
            else         m_excludedAttrIds.insert(attrId);
            _saveExclusions();
            return true;
        }

        if (index.column() == ColAllCountries && role == Qt::CheckStateRole) {
            if (index.row() < 0 || index.row() >= m_violations.size()) return false;
            ViolationNode *vn = m_violations[index.row()];
            if (!vn) return false;

            const bool checked   = (value.toInt() == Qt::Checked);
            const QString attrId = vn->row.attributeId.toLower();

            for (int i = 0; i < m_violations.size(); ++i) {
                if (m_violations[i]->row.attributeId.toLower() == attrId) {
                    m_violations[i]->allCountries = checked;
                    const QModelIndex idx = createIndex(i, ColAllCountries, nullptr);
                    emit dataChanged(idx, idx, {Qt::CheckStateRole});
                }
            }

            if (checked) m_allCountriesAttrIds.insert(attrId);
            else         m_allCountriesAttrIds.remove(attrId);
            _saveUploadOptions();
            return true;
        }

        if (index.column() == ColAllSiblings && role == Qt::CheckStateRole) {
            if (index.row() < 0 || index.row() >= m_violations.size()) return false;
            ViolationNode *vn = m_violations[index.row()];
            if (!vn) return false;

            const bool checked   = (value.toInt() == Qt::Checked);
            const QString attrId = vn->row.attributeId.toLower();

            for (int i = 0; i < m_violations.size(); ++i) {
                if (m_violations[i]->row.attributeId.toLower() == attrId) {
                    m_violations[i]->allSiblings = checked;
                    const QModelIndex idx = createIndex(i, ColAllSiblings, nullptr);
                    emit dataChanged(idx, idx, {Qt::CheckStateRole});
                }
            }

            if (checked) m_allSiblingsAttrIds.insert(attrId);
            else         m_allSiblingsAttrIds.remove(attrId);
            _saveUploadOptions();
            return true;
        }

        // Top-level rows are otherwise read-only.
        return false;
    }

    // ---- Child row: existing AI value editing via ColError ----
    if (role != Qt::EditRole) return false;
    if (index.column() != ColError) return false;

    auto *vn = static_cast<ViolationNode *>(ptr);
    if (!vn) return false;
    if (index.row() < 0 || index.row() >= vn->children.size()) return false;

    ChildNode &child = vn->children[index.row()];
    if (child.isCurrentValue) return false;

    const QString newValue = value.toString();
    if (child.aiValue == newValue) return true;

    child.aiValue = newValue;
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

Qt::ItemFlags TreeProductWarnings::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;

    if (index.internalPointer() == nullptr) {
        // Parent row
        if (index.column() == ColAskAi
            || index.column() == ColAllCountries
            || index.column() == ColAllSiblings)
            return Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
        return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    }
    // Child row
    if (index.column() == ColAskAi
        || index.column() == ColAllCountries
        || index.column() == ColAllSiblings)
        return Qt::ItemIsEnabled;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
}
