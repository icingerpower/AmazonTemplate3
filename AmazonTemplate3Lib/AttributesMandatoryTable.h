#ifndef ATTRIBUTESMANDATORYTABLE_H
#define ATTRIBUTESMANDATORYTABLE_H

#include <QAbstractTableModel>
#include <QSet>
#include <QHash>
#include <QString>
#include <QSharedPointer>

class AttributesMandatoryAiTable;

class AttributesMandatoryTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit AttributesMandatoryTable(
            const QString &settingPath
            , const QString &productType
            , const QSet<QString> &curTemplateFieldIdsMandatory
            , const QSet<QString> &previousTemplateFieldIdsMandatory
            , const QHash<QString, int> &curTemplateFieldIds
            , QObject *parent = nullptr);

    QSet<QString> getMandatoryIds() const;
    bool hasIdsFromPreviousTemplates() const;

    void setIdsChangedManually(
            const QSet<QString> &newIdsAddedManually
            , const QSet<QString> &newIdsRemovedManually);

    void update(const QSet<QString> &attributesMandatory
                , const QSet<QString> &attributesNotMandatory);

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    bool needAiReview() const;

private:
    static const QString KEY_MANDATORY;
    
    QMap<QString, bool> m_idsMandatory;
    QMap<QString, bool> m_idsNotMandatory;
    bool m_needAiReview;
    //QSet<QString> m_mandatoryIdsCurTemplates; // Initial mandatory from file
    //QSet<QString> m_idsAddedManually;
    //QSet<QString> m_idsRemovedManually;
    
    //QList<QString> m_orderedFieldIds;
    
    // Previous "history" or fallback set
    //QSet<QString> m_mandatoryIdsPreviousTemplates;
    
    // Internal helper for manual overrides persistence
    QString m_settingsPath;
    QString m_productType;

    void _saveInSettings();
    void _clear();
    void _sort();
};

#endif // ATTRIBUTESMANDATORYTABLE_H
