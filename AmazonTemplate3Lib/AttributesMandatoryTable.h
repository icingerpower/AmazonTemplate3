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
    static const QString KEY_MANDATORY_ALWAYS;
    static const QStringList HEADER;

    const QString &_getFieldId(int row) const;
    QSet<QString> m_idsMandatoryAlways;
    QMap<QString, bool> m_idsMandatory;
    QMap<QString, bool> m_idsNotMandatory;
    bool m_needAiReview;
    QString m_settingsPath;
    QString m_productType;

    void _saveInSettings();
    void _clear();
    void _sort();
};

#endif // ATTRIBUTESMANDATORYTABLE_H
