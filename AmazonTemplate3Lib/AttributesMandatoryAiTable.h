#ifndef ATTRIBUTESMANDATORYAITABLE_H
#define ATTRIBUTESMANDATORYAITABLE_H

#include <QObject>
#include <QSet>
#include <QHash>
#include <QVariant>
#include <QCoro/QCoroTask>

class AttributesMandatoryAiTable : public QObject
{
    Q_OBJECT
public:
    explicit AttributesMandatoryAiTable(QObject *parent = nullptr);

    QCoro::Task<void> load(
            const QString &productType,
            const QSet<QString> &curTemplateFieldIdsMandatory,
            const QHash<QString, int> &curTemplateFieldIds);

    // Accessors for AI-determined sets
    QSet<QString> fieldIdsAiAdded() const;
    QSet<QString> fieldIdsAiRemoved() const;

    // Check if an attribute has been reviewed for the current product type
    bool isAttributeReviewed(const QString &attrId) const;
    void save();

private:
    static const QString SETTINGS_KEYS_GROUP;

    QHash<QString, QSet<QString>> m_productType_reviewedAttributes;
    
    // AI decisions specific to current product type context
    // Though persistence might suggest these are global/per-product in settings?
    // The previous implementation loaded them per product-type.
    QSet<QString> m_curTemplateFieldIdsMandatory;
    QSet<QString> m_fieldIdsAiAdded;
    QSet<QString> m_fieldIdsAiRemoved;

    QString m_settingsPath;
    QString m_productType;
    
    void _saveInSettings();
    void _clear();
};

#endif // ATTRIBUTESMANDATORYAITABLE_H
