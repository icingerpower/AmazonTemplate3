#ifndef MANDATORYATTRIBUTESMANAGER_H
#define MANDATORYATTRIBUTESMANAGER_H

#include <QSet>
#include <QString>
#include <QCoro/QCoroTask>
#include <QCoro/QCoroSignal>
#include <QCoro/QCoroCore>

class MandatoryAttributesManager
{
public:
    static MandatoryAttributesManager *instance();
    QCoro::Task<void> load(
            const QString &settingPath
            , const QString &productType
            , const QSet<QString> &curTemplateFieldIds
            , const QSet<QString> &curTemplateFieldIdsMandatory);
    QSet<QString> getMandatoryIds() const;

    bool hasIdsFromPreviousTemplates() const;

    void setMandatoryIdsCurTemplates(
            const QSet<QString> &newMandatoryIdsCurTemplates);

    void setMandatoryIdsFileRemovedAi(
            const QSet<QString> &newMandatoryIdsFileRemovedAi);

    void setMandatoryIdsPreviousTemplates(
            const QSet<QString> &newMandatoryIdsPreviousTemplates);

    void setIdsNonMandatoryAddedByAi(
            const QSet<QString> &newIdsNonMandatoryAddedByAi);

    void setIdsAddedManually(
            const QSet<QString> &newIdsAddedManually);

    void setIdsRemovedManually(
            const QSet<QString> &newIdsRemovedManually);

private:
    MandatoryAttributesManager();
    QSet<QString> m_curTemplateAllIds;
    QSet<QString> m_mandatoryIdsCurTemplates;
    QSet<QString> m_mandatoryIdsFileRemovedAi;
    QSet<QString> m_mandatoryIdsPreviousTemplates;
    QSet<QString> m_idsNonMandatoryAddedByAi;
    QSet<QString> m_idsAddedManually;
    QSet<QString> m_idsRemovedManually;
    QHash<QString, QSet<QString> *> m_key_ids;
    QString m_settingsPath;
    QString m_productType;
    void _clear();
    void _saveInSettings();
};

#endif // MANDATORYATTRIBUTESMANAGER_H
