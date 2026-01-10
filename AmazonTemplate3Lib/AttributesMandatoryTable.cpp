#include <QSettings>
#include <QColor>
#include <QDir>

#include "AttributesMandatoryTable.h"
#include "AttributesMandatoryAiTable.h"

const QString AttributesMandatoryTable::KEY_MANDATORY{"attrMandatory"};

AttributesMandatoryTable::AttributesMandatoryTable(
        const QString &workingDir,
        const QString &productType,
        const QSet<QString> &curTemplateFieldIdsMandatory,
        const QSet<QString> &previousTemplateFieldIdsMandatory,
        const QHash<QString, int> &curTemplateFieldIds,
        QObject *parent)
    : QAbstractTableModel(parent)
{

    m_needAiReview = false;
    m_settingsPath = QDir{workingDir}.absoluteFilePath("mandatoryFieldIds.ini");
    m_productType = productType.toLower();
    QSettings settings{m_settingsPath, QSettings::IniFormat};

    // First we check if it exits in the settings
    settings.beginGroup(m_productType);
    if (settings.contains(KEY_MANDATORY))
    {
        m_idsMandatory = settings.value(KEY_MANDATORY).value<QMap<QString, bool>>();
    }
    settings.endGroup();

    m_idsNotMandatory.clear();

        // Second we check if it exits in previous template filled
    if (m_idsMandatory.isEmpty())
    {
        for (const auto &previousFieldId : previousTemplateFieldIdsMandatory)
        {
            if (curTemplateFieldIds.contains(previousFieldId))
            {
                m_idsMandatory.insert(previousFieldId, true);
            }
        }
    }

    // Last we load the Excel file values
    if (m_idsMandatory.isEmpty())
    {
        m_needAiReview = true;
        for (const auto &curFieldId : curTemplateFieldIdsMandatory)
        {
            m_idsMandatory[curFieldId] = true;
        }
    }
    const auto &allFieldIds = curTemplateFieldIds.keys();
    for (const auto &fieldId : allFieldIds)
    {
        if (!m_idsMandatory.contains(fieldId))
        {
            m_idsNotMandatory[fieldId] = false;
        }
    }
    auto allTemplateFieldIdsMandatory = curTemplateFieldIdsMandatory;
    allTemplateFieldIdsMandatory.unite(previousTemplateFieldIdsMandatory);
    for (const auto &fieldId : allTemplateFieldIdsMandatory)
    {
        if (!m_idsMandatory.contains(fieldId)
                && !m_idsNotMandatory.contains(fieldId))
        {
            m_idsMandatory[fieldId] = true;
        }
    }
    _saveInSettings();
}

void AttributesMandatoryTable::update(
        const QSet<QString> &attributesMandatory
        , const QSet<QString> &attributesNotMandatory)
{
    beginResetModel();

    for (const auto &id : attributesMandatory)
    {
        if (m_idsNotMandatory.contains(id))
        {
            m_idsNotMandatory.remove(id);
            m_idsMandatory.insert(id, true);
        }
    }

    for (const auto &id : attributesNotMandatory)
    {
        if (m_idsMandatory.contains(id))
        {
            m_idsMandatory.remove(id);
            m_idsNotMandatory.insert(id, false);
        }
    }

    _saveInSettings();
    endResetModel();
}

void AttributesMandatoryTable::_saveInSettings()
{
    QSettings settings{m_settingsPath, QSettings::IniFormat};
    const QString productTypeLower = m_productType.toLower();

    settings.beginGroup(productTypeLower);
    settings.setValue(KEY_MANDATORY, QVariant::fromValue(m_idsMandatory));

    settings.endGroup();
    settings.sync();
}

QSet<QString> AttributesMandatoryTable::getMandatoryIds() const
{
    QSet<QString> ids;
    for (auto it = m_idsMandatory.begin();
         it != m_idsMandatory.end(); ++it)
    {
        ids.insert(it.key());
    }
    return ids;
}

int AttributesMandatoryTable::rowCount(const QModelIndex &parent) const
{
    return m_idsMandatory.size() + m_idsNotMandatory.size();
}

int AttributesMandatoryTable::columnCount(const QModelIndex &parent) const
{
    return 2;
}

QVariant AttributesMandatoryTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section) {
        case 0: return tr("Attribute ID");
        case 1: return tr("Is Mandatory");
        default: return QVariant();
        }
    }
    return QVariant();
}

Qt::ItemFlags AttributesMandatoryTable::flags(const QModelIndex &index) const
{
    return Qt::ItemIsEnabled | Qt::ItemIsEditable;
}

bool AttributesMandatoryTable::needAiReview() const
{
    return m_needAiReview;
}

QVariant AttributesMandatoryTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        if (index.column() == 1)
        {
            return index.row() < m_idsMandatory.size();
        }
        if (index.row() < m_idsMandatory.size())
        {
            return std::next(m_idsMandatory.begin(), index.row()).key();
        }
        else
        {
            return std::next(m_idsNotMandatory.begin(), index.row() - m_idsMandatory.size()).key();
        }
    }
    return QVariant();
}

bool AttributesMandatoryTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && index.column() == 1 && role == Qt::EditRole && value != data(index))
    {
        if (index.row() < m_idsMandatory.size())
        {
            const auto &fieldId = std::next(m_idsMandatory.begin(), index.row()).key();
            int row = index.row();
            m_idsNotMandatory[fieldId] = false;
            m_idsMandatory.remove(fieldId);
            emit dataChanged(this->index(row, 0)
                             , this->index(rowCount()-1, 1));
        }
        else
        {
            const auto &fieldId = std::next(m_idsNotMandatory.begin(), index.row() - m_idsMandatory.size()).key();
            m_idsMandatory[fieldId] = true;
            m_idsNotMandatory.remove(fieldId);
            emit dataChanged(this->index(0, 0)
                             , this->index(rowCount()-1, 1));
        }
        _saveInSettings();
        return true;
    }
    return false;
}
