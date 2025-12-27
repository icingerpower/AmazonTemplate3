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
    MandatoryAttributesManager();
    QCoro::Task<void> load(
            const QString &templateFileNameFrom,
            const QString &settingPath
            , const QString &productType
            , const QHash<QString, int> &curTemplateFieldIds
            , const QSet<QString> &curTemplateFieldIdsMandatory
            , bool restart = false);
    QSet<QString> getMandatoryIds() const;

    bool hasIdsFromPreviousTemplates() const;

    void setMandatoryIdsCurTemplates(
            const QSet<QString> &newMandatoryIdsCurTemplates);

    void setMandatoryIdsPreviousTemplates(
            const QSet<QString> &newMandatoryIdsPreviousTemplates);

    void setIdsChangedManually(
            const QSet<QString> &newIdsAddedManually
            , const QSet<QString> &newIdsRemovedManually);

    const QSet<QString> &idsNonMandatoryAddedByAi() const;

    const QSet<QString> &mandatoryIdsFileRemovedAi() const;

private:
    static const QString SETTINGS_KEYS_TEMPLATES_DONE;
    QHash<QString, int> m_curTemplateAllIds;
    QSet<QString> m_doneTemplatePaths;
    QHash<QString, QSet<QString>> m_productType_reviewedAttributes;
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
