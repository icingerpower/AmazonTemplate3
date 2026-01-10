#include <QSettings>
#include <QColor>
#include <QDir>

#include "AttributesMandatoryTable.h"

const QString AttributesMandatoryTable::KEY_MANDATORY{"attrMandatory"};
const QString AttributesMandatoryTable::KEY_MANDATORY_ALWAYS{"attrMandatoryAlways"};
const QStringList AttributesMandatoryTable::HEADER{
    tr("Attribute ID"), tr("Is Mandatory"), tr("Is Always Mandatory")};

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
    if (settings.contains(KEY_MANDATORY_ALWAYS))
    {
        m_idsMandatoryAlways = settings.value(KEY_MANDATORY_ALWAYS).value<QSet<QString>>();
    }

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
    for (const auto &alwaysId : std::as_const(m_idsMandatoryAlways))
    {
        m_idsMandatory[alwaysId] = true;
        m_idsNotMandatory.remove(alwaysId);
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

    settings.setValue(KEY_MANDATORY_ALWAYS, QVariant::fromValue(m_idsMandatoryAlways));

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
    return HEADER.size();
}

QVariant AttributesMandatoryTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        return HEADER[section];
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

const QString &AttributesMandatoryTable::_getFieldId(int row) const
{
    const auto &id = row < m_idsMandatory.size() ?
                std::next(m_idsMandatory.begin(), row).key()
              : std::next(m_idsNotMandatory.begin(), row - m_idsMandatory.size()).key();
    return id;
}

QVariant AttributesMandatoryTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        if (index.column() == 1)
        {
            return index.row() < m_idsMandatory.size();
        }
        const auto &id = _getFieldId(index.row());
        if (index.column() == 0)
        {
            return id;
        }
        else
        {
            return m_idsMandatoryAlways.contains(id);
        }
    }
    return QVariant();
}

bool AttributesMandatoryTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (index.isValid() && index.column() > 0)
    {
        if (role == Qt::EditRole && value != data(index))
        {
            if (index.column() == 1)
            {
                if (index.row() < m_idsMandatory.size())
                {
                    const auto &fieldId = std::next(m_idsMandatory.begin(), index.row()).key();
                    int row = index.row();
                    m_idsNotMandatory[fieldId] = false;
                    m_idsMandatory.remove(fieldId);
                    m_idsMandatoryAlways.remove(fieldId);
                    emit dataChanged(this->index(row, 0)
                                     , this->index(rowCount()-1, columnCount()-1));
                }
                else
                {
                    const auto &fieldId = std::next(m_idsNotMandatory.begin(), index.row() - m_idsMandatory.size()).key();
                    m_idsMandatory[fieldId] = true;
                    m_idsNotMandatory.remove(fieldId);
                    emit dataChanged(this->index(0, 0)
                                     , this->index(rowCount()-1, columnCount()-1));
                }
                _saveInSettings();
                return true;
            }
            else if (index.column() == 2)
            {
                const auto &id = _getFieldId(index.row());
                if (value.toBool())
                {
                    m_idsMandatoryAlways.insert(id);
                }
                else
                {
                    m_idsMandatoryAlways.remove(id);
                }
            }
        }
    }
    return false;
}

