#ifndef ATTRIBUTEEQUIVALENTTABLE_H
#define ATTRIBUTEEQUIVALENTTABLE_H

#include <QAbstractTableModel>
#include <QStringList>
#include <QCoro/QCoroTask>

class Attribute;

class AttributeEquivalentTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit AttributeEquivalentTable(
            const QString &workingDirectory, QObject *parent = nullptr);
    void remove(const QModelIndex &index);

    int getPosAttr(const QString &fieldIdAmzV02, const QString &value) const;
    int getPosAttr(const QString &fieldIdAmzV02, const QSet<QString> &equivalentValues) const;
    bool hasEquivalent(const QString &fieldIdAmzV02
                       , const QString &value
                       ) const;
    bool hasEquivalent(const QString &fieldIdAmzV02
                       , const QString &value
                       , const QSet<QString> &possibleValues) const;
    void recordAttribute(const QString &fieldIdAmzV02,
                         const QSet<QString> &equivalentValues);
    const QSet<QString> &getEquivalentValues(
            const QString &fieldIdAmzV02, const QString &value) const;
    const QString &getEquivalentValue(
            const QString &fieldIdAmzV02, const QString &value, const QSet<QString> &possibleValues) const;
    QCoro::Task<void> askAiEquivalentValues(
            const QString &fieldIdAmzV02, const QString &value, const Attribute *attribute);
    QCoro::Task<void> askAiEquivalentValues(
            const QString &fieldIdAmzV02
            , const QString &value
            , const QString &langCodeFrom
            , const QString &langCodeTo
            , const QSet<QString> &possibleValues);

    QSet<QString> getEquivalentGenderWomen() const;
    QSet<QString> getEquivalentGenderMen() const;
    QSet<QString> getEquivalentGenderUnisex() const;

    QSet<QString> getEquivalentAgeAdult() const;
    QSet<QString> getEquivalentAgeKid() const;
    QSet<QString> getEquivalentAgeBaby() const;


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
    void _buildHash();
    QString m_filePath;
    QList<QStringList> m_listOfStringList;
    QHash<QString, QList<QSet<QString>>> m_fieldIdAmzV02_listOfEquivalents;
    void _loadFromFile();
    void _saveInFile();
};

#endif // ATTRIBUTEEQUIVALENTTABLE_H
