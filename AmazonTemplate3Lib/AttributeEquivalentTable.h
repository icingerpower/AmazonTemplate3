#ifndef ATTRIBUTEEQUIVALENTTABLE_H
#define ATTRIBUTEEQUIVALENTTABLE_H

#include <QAbstractTableModel>
#include <QStringList>

class AttributeEquivalentTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit AttributeEquivalentTable(
            const QString &workingDirectory, QObject *parent = nullptr);

    bool hasEquivalent(const QString &attrId, const QString &value) const;
    int getPosAttr(const QString &attrId) const;
    void recordAttribute(const QString &attrId,
                         const QStringList &equivalentValues);

    // Header:
    QVariant headerData(
            int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    static const QStringList HEADERS;
    QString m_filePath;
    QList<QStringList> m_listOfStringList;
    void _loadFromFile();
    void _saveInFile();
};

#endif // ATTRIBUTEEQUIVALENTTABLE_H
