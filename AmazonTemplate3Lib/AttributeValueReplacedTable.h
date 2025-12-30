#ifndef ATTRIBUTEVALUEREPLACEDTABLE_H
#define ATTRIBUTEVALUEREPLACEDTABLE_H

#include <QAbstractTableModel>
#include <QStringList>

class AttributeValueReplacedTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit AttributeValueReplacedTable(
            const QString &workingDirectory, QObject *parent = nullptr);
    void remove(const QModelIndex &index);
    bool contains(const QString &marketplace,
                  const QString &countryCode,
                  const QString &langCode,
                  const QString &fieldId,
                  const QString &valueFrom) const;
    bool replaceIfContains(const QString &marketplace,
                           const QString &countryCode,
                           const QString &langCode,
                           const QString &fieldId,
                           QString &valueToUpdate) const;
    void replaceIfContains(
            const QString &marketplace,
            const QString &countryCode,
            const QString &langCode,
            const QString &fieldId,
            QSet<QString> &possibleValues) const;

    void recordAttribute(const QString &marketplace,
                         const QString &countryCode,
                         const QString &langCode,
                         const QString &fieldId,
                         const QString &valueFrom,
                         const QString &valueTo);

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

#endif // ATTRIBUTEVALUEREPLACEDTABLE_H
