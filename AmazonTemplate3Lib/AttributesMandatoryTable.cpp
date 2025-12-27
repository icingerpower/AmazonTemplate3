#include "AttributesMandatoryTable.h"
#include "AttributesMandatoryAiTable.h"
#include <QSettings>
#include <QColor>

const QString AttributesMandatoryTable::KEY_MANDATORY{"attrMandatory"};

AttributesMandatoryTable::AttributesMandatoryTable(QObject *parent)
    : QAbstractTableModel(parent)
{
    //m_key_ids["m_idsAddedManually"] = &m_idsAddedManually;
    //m_key_ids["m_idsRemovedManually"] = &m_idsRemovedManually;
    //m_key_ids["m_mandatoryIdsPreviousTemplates"] = &m_mandatoryIdsPreviousTemplates;
    m_needAiReview = false;
}

void AttributesMandatoryTable::load(
        const QString &settingPath,
        const QString &productType,
        const QSet<QString> &curTemplateFieldIdsMandatory,
        const QSet<QString> &previousTemplateFieldIdsMandatory,
        const QHash<QString, int> &curTemplateFieldIds)
{
    beginResetModel();
    
    m_settingsPath = settingPath;
    m_productType = productType;
    const QString &productTypeLower = m_productType.toLower();
    m_idsMandatory.clear();
    m_idsNotMandatory.clear();

    QSettings settings{m_settingsPath, QSettings::IniFormat};

    // First we check if it exits in the settings
    settings.beginGroup(productTypeLower);
    if (settings.contains(KEY_MANDATORY))
    {
        m_idsMandatory = settings.value(KEY_MANDATORY).value<QMap<QString, bool>>();
    }
    settings.endGroup();

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
        _saveInSettings();
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
    endResetModel();
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
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == 1)
    {
        f |= Qt::ItemIsUserCheckable | Qt::ItemIsEditable;
    }
    return f;
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
    if (index.isValid() && index.column() == 1 && role == Qt::CheckStateRole && value != data(index))
    {
        if (index.row() < m_idsMandatory.size())
        {
            const auto &fieldId = std::next(m_idsMandatory.begin(), index.row()).key();
            m_idsNotMandatory[fieldId] = false;
            m_idsMandatory.remove(fieldId);
            emit dataChanged(this->index(index.row(), 0)
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
