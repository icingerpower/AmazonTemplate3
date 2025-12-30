#include <QDir>
#include <algorithm>
#include <QColor>
#include <QBrush>

#include "AttributeFlagsTable.h"

AttributeFlagsTable::AttributeFlagsTable(
        const QString &workingDirectory, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = QDir{workingDirectory}.absoluteFilePath("attributeFlags.csv");
    for (const auto &markeplace : Attribute::MARKETPLACES)
    {
        m_colNames << markeplace;
    }
    m_indFirstFlag = m_colNames.size();
    const auto &colFlags = Attribute::STRING_FLAG.keys();
    for (const auto &flagName : colFlags)
    {
        m_colNames << flagName;
    }
    _loadFromFile();
}

QString AttributeFlagsTable::getFieldId(
        const QString &marketplaceFrom
        , const QString &fieldIdFrom
        , const QString &marketplaceTo) const
{
    if (marketplaceFrom == marketplaceTo)
    {
        return fieldIdFrom;
    }
    
    if (m_marketplace_fieldId_indRow.contains(marketplaceFrom))
    {
        const auto &fieldId_indRow = m_marketplace_fieldId_indRow[marketplaceFrom];
        if (fieldId_indRow.contains(fieldIdFrom))
        {
            int rowIndex = fieldId_indRow[fieldIdFrom];
            int colIndex = m_colNames.indexOf(marketplaceTo);
            
            if (colIndex != -1 && rowIndex >= 0 && rowIndex < m_listOfVariantList.size())
            {
                // Ensure colIndex is within bounds of the specific row (though rows should be full size)
                if (colIndex < m_listOfVariantList[rowIndex].size())
                {
                    return m_listOfVariantList[rowIndex][colIndex].toString();
                }
            }
        }
    }
    return QString{};
}

QSet<QString> AttributeFlagsTable::getUnrecordedFieldIds(
        const QString &marketplace, const QSet<QString> &fieldIds) const
{
    int colIdx = m_colNames.indexOf(marketplace);
    if (colIdx == -1)
    {
        return fieldIds;
    }

    QSet<QString> unrecordedIds = fieldIds;
    for (const auto &variantList : std::as_const(m_listOfVariantList))
    {
        const QString &val = variantList[colIdx].toString();
        if (!val.isEmpty())
        {
            unrecordedIds.remove(val);
            if (unrecordedIds.isEmpty())
            {
                break;
            }
        }
    }
    return unrecordedIds;
}

QHash<QString, QString> AttributeFlagsTable::get_marketplace_id(
        const QString &marketplace, const QString &fieldId) const
{
    QHash<QString, QString> marketplace_id;
    int rowIndex = m_marketplace_fieldId_indRow[marketplace][fieldId];
    for (int i=0; i<m_indFirstFlag; ++i)
    {
        const auto &curMarketplace = m_colNames[i];
        const auto &fieldId = m_listOfVariantList[rowIndex][i].toString();
        if (!fieldId.isEmpty())
        {
            marketplace_id[curMarketplace] = fieldId;
        }
    }
    return marketplace_id;
}

Attribute::Flag AttributeFlagsTable::getFlags(
        const QString &marketplace, const QString &fieldId) const
{
    Attribute::Flag flags = Attribute::NoFlag;
    if (m_marketplace_fieldId_indRow.contains(marketplace) &&
        m_marketplace_fieldId_indRow[marketplace].contains(fieldId))
    {
        int rowIndex = m_marketplace_fieldId_indRow[marketplace][fieldId];
        for (int i = m_indFirstFlag; i < m_colNames.size(); ++i)
        {
            if (m_listOfVariantList[rowIndex][i].toBool())
            {
                const QString &flagName = m_colNames[i];
                Attribute::Flag curFlag = Attribute::STRING_FLAG.value(flagName);
                flags = static_cast<Attribute::Flag>(
                            static_cast<int>(flags) | static_cast<int>(curFlag));
            }
        }
    }
    return flags;
}

bool AttributeFlagsTable::hasFlag(
        const QString &marketplace, const QString &fieldId, Attribute::Flag flag) const
{
    Attribute::Flag flags = getFlags(marketplace, fieldId);
    return (flags & flag) == flag;
}

void AttributeFlagsTable::recordAttributeNotRecordedYet(
        const QString &marketplaceId, const QSet<QString> &fieldIds)
{
    bool added = false;
    for (const auto &fieldId : fieldIds)
    {
        QHash<QString, QString> marketplace_ids{{marketplaceId, fieldId}};
        int pos = getPosAttr(marketplace_ids);
        if (pos < 0)
        {
            QVariantList variantList;
            for (int i = 0; i < m_indFirstFlag; ++i)
            {
                const auto &colName = m_colNames[i];
                if (marketplace_ids.contains(colName))
                {
                    variantList << marketplace_ids[colName];
                }
                else
                {
                    variantList << QString{};
                }
            }
            // Append false for each flag column.
            for (int i = m_indFirstFlag; i < m_colNames.size(); ++i)
            {
                variantList << false;
            }
            beginInsertRows(QModelIndex{}, 0, 0);
            m_listOfVariantList.insert(0, variantList);
            endInsertRows();
            added = true;
        }
    }
    if (added)
    {
        _sort();
        _saveInFile();
    }
}

void AttributeFlagsTable::recordAttribute(const QHash<QString, QString> &marketplace_ids)
{
    // Record attribute values and set all flag columns to false (no flags provided).
    int pos = getPosAttr(marketplace_ids);
    if (pos >= 0)
    {
        // Update existing row: fill attribute values if empty.
        for (int i = 0; i < m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            if (marketplace_ids.contains(colName))
            {
                QModelIndex idx = index(pos, i);
                if (data(idx).toString().isEmpty())
                {
                    setData(idx, marketplace_ids[colName]);
                }
            }
        }
        _sort();
    }
    else
    {
        // Create a new row with attribute values and all flags false.
        QVariantList variantList;
        for (int i = 0; i < m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            if (marketplace_ids.contains(colName))
                variantList << marketplace_ids[colName];
            else
                variantList << QString{};
        }
        // Append false for each flag column.
        for (int i = m_indFirstFlag; i < m_colNames.size(); ++i)
        {
            variantList << false;
        }
        beginInsertRows(QModelIndex{}, 0, 0);
        m_listOfVariantList.insert(0, variantList);
        _sort();
        _saveInFile();
        endInsertRows();
    }
}

int AttributeFlagsTable::getPosAttr(const QHash<QString, QString> &marketplace_ids) const
{
    int rowIdx = 0;
    for (const auto &variantList : std::as_const(m_listOfVariantList))
    {
        bool compatible = true;
        bool hasAtLeastOneMatch = false;

        for (int i=0; i<m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            const QString &val = variantList[i].toString();

            if (marketplace_ids.contains(colName))
            {
                if (!val.isEmpty())
                {
                    if (val != marketplace_ids[colName])
                    {
                        compatible = false;
                        break;
                    }
                    else
                    {
                        hasAtLeastOneMatch = true;
                    }
                }
            }
        }

        if (compatible && hasAtLeastOneMatch)
        {
            return rowIdx;
        }
        rowIdx++;
    }
    return -1;
}

void AttributeFlagsTable::recordAttribute(
        const QHash<QString, QString> &marketplace_ids, Attribute::Flag flag)
{
    int pos = getPosAttr(marketplace_ids);
    if (pos >= 0)
    {
        for (int i=0; i<m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            if (marketplace_ids.contains(colName))
            {
                QModelIndex idx = index(pos, i);
                if (data(idx).toString().isEmpty())
                {
                    setData(idx, marketplace_ids[colName]);
                }
            }
        }
        _sort();

        /* // We don't update flags for now
        for (int i=m_indFirstFlag; i<m_colNames.size(); ++i)
        {
             QString flagName = m_colNames[i];
             Attribute::Flag curFlag = Attribute::STRING_FLAG.value(flagName);
             if ((flag & curFlag) == curFlag)
             {
                 QModelIndex idx = index(pos, i);
                 if (!data(idx).toBool())
                 {
                    setData(idx, true);
                 }
             }
        }
        //*/
    }
    else
    {
        QVariantList variantList;
        for (int i=0; i<m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            if (marketplace_ids.contains(colName))
            {
                variantList << marketplace_ids[colName];
            }
            else
            {
                variantList << QString{};
            }
        }
        for (int i=m_indFirstFlag; i<m_colNames.size(); ++i)
        {
            QString flagName = m_colNames[i];
            Attribute::Flag curFlag = Attribute::STRING_FLAG.value(flagName);
            bool contains = (flag & curFlag) == curFlag;
            variantList << contains;
        }
        beginInsertRows(QModelIndex{}, 0, 0);
        m_listOfVariantList.insert(0, variantList);
        _sort();
        _saveInFile();
        endInsertRows();
    }
}

QVariant AttributeFlagsTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        return m_colNames[section];
    }
    return QVariant{};
}

int AttributeFlagsTable::rowCount(const QModelIndex &) const
{
    return m_listOfVariantList.size();
}

int AttributeFlagsTable::columnCount(const QModelIndex &) const
{
    return m_colNames.size();
}

QVariant AttributeFlagsTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::EditRole || role == Qt::DisplayRole)
    {
        return m_listOfVariantList[index.row()][index.column()];
    }
    if (role == Qt::BackgroundRole)
    {
        // Light pink background when all flag columns are false
        bool allFalse = true;
        for (int col = m_indFirstFlag; col < m_colNames.size(); ++col)
        {
            if (m_listOfVariantList[index.row()][col].toBool())
            {
                allFalse = false;
                break;
            }
        }
        if (allFalse)
        {
            return QBrush(QColor(139, 0, 0));
        }
    }
    return QVariant{};
}

bool AttributeFlagsTable::setData(
        const QModelIndex &index, const QVariant &value, int role)
{
    if (data(index, role) != value)
    {
        m_listOfVariantList[index.row()][index.column()] = value;
        _saveInFile();
        emit dataChanged(index, index, {role});
        return true;
    }
    return false;
}

Qt::ItemFlags AttributeFlagsTable::flags(const QModelIndex &index) const
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void AttributeFlagsTable::_loadFromFile()
{
    QFile file{m_filePath};
    if (file.open(QFile::ReadOnly))
    {
        QTextStream stream{&file};
        const auto &lines = stream.readAll().split("\n");
        if (lines.isEmpty())
        {
            file.close();
            return;
        }

        const auto &oldColNames = lines[0].split(COL_SEP);
        QHash<QString, int> oldColName_index;
        int i = 0;
        for (const auto &colName : oldColNames)
        {
            oldColName_index[colName] = i;
            ++i;
        }
        for (int i=1; i<lines.size(); ++i)
        {
            if (!lines[i].trimmed().isEmpty())
            {
                QVariantList newElements;
                const auto &oldElements = lines[i].split(COL_SEP);
                int j=0;
                for (const auto &newColName : m_colNames)
                {
                    bool valueFound = false;
                    if (oldColName_index.contains(newColName))
                    {
                        int oldColIndex = oldColName_index[newColName];
                        if (oldColIndex < oldElements.size())
                        {
                            if (j < m_indFirstFlag)
                            {
                                newElements << oldElements[oldColIndex];
                            }
                            else
                            {
                                newElements << (oldElements[oldColIndex] == STR_TRUE);
                            }
                            valueFound = true;
                        }
                    }

                    if (!valueFound)
                    {
                        if (j < m_indFirstFlag)
                        {
                            newElements << QString{};
                        }
                        else
                        {
                            // If a flags didn't have a value saved, the default value will be false.
                            newElements << false;
                        }
                    }
                    ++j;
                }
                m_listOfVariantList << newElements;
            }
        }
        file.close();
        _sort();
    }
}

void AttributeFlagsTable::_saveInFile()
{
    QFile file{m_filePath};
    if (file.open(QFile::WriteOnly))
    {
        QTextStream stream{&file};
        stream << m_colNames.join(COL_SEP);
        for (const auto &variantList : m_listOfVariantList)
        {
            QStringList elements;
            for (int i=0; i<m_indFirstFlag; ++i)
            {
                elements << variantList[i].toString();
            }
            for (int i=m_indFirstFlag; i<m_colNames.size(); ++i)
            {
                if (variantList[i].toBool())
                {
                    elements << STR_TRUE;
                }
                else
                {
                    elements << STR_FALSE;
                }
            }
            stream << "\n" + elements.join(COL_SEP);
        }
        file.close();
    }
}

void AttributeFlagsTable::_updateMap()
{
    m_marketplace_fieldId_indRow.clear();
    for (int i=0; i<m_listOfVariantList.size(); ++i)
    {
        for (int j=0; j<m_indFirstFlag; ++j)
        {
            const auto &marketplace = m_colNames[j];
            const auto &fieldId = m_listOfVariantList[i][j].toString();
            m_marketplace_fieldId_indRow[marketplace][fieldId] = i;
        }
    }
}

void AttributeFlagsTable::_sort()
{
    // Determine column indices for sorting priority
    int idxV02 = m_colNames.indexOf(Attribute::AMAZON_V02);
    int idxV01 = m_colNames.indexOf(Attribute::AMAZON_V01);
    int idxTemu = m_colNames.indexOf(Attribute::TEMU_EN);
    if (idxV02 == -1 || idxV01 == -1 || idxTemu == -1)
        return; // required columns not present

    // Stable sort to maintain relative order where values are equal
    std::stable_sort(m_listOfVariantList.begin(), m_listOfVariantList.end(),
        [&](const QVariantList &a, const QVariantList &b) {
            const QString aV02 = a[idxV02].toString();
            const QString bV02 = b[idxV02].toString();
            if (aV02 != bV02)
                return aV02 < bV02;
            const QString aV01 = a[idxV01].toString();
            const QString bV01 = b[idxV01].toString();
            if (aV01 != bV01)
                return aV01 < bV01;
            const QString aTemu = a[idxTemu].toString();
            const QString bTemu = b[idxTemu].toString();
            return aTemu < bTemu;
        });
    // Notify views that the layout changed
    _updateMap();
    emit layoutChanged();
}



