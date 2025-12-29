#ifndef TEMPLATEFILLER_H
#define TEMPLATEFILLER_H

#include <QString>
#include <QStringList>
#include <QDir>
#include <QSettings>
#include <QSharedPointer>

#include <xlsxdocument.h>
#include <QCoro/QCoroTask>

#include "Attribute.h"

class AttributesMandatoryAiTable;
class AttributesMandatoryTable;
class AttributeEquivalentTable;
class AttributeFlagsTable;
class AttributePossibleMissingTable;
class AttributeValueReplacedTable;

class TemplateFiller
{
public:
    static const QSet<QString> VALUES_MANDATORY;
    static const QHash<QString, QSet<QString>> SHEETS_MANDATORY;
    TemplateFiller(const QString &workingDirCommon, const QString &templateFromPath
                      , const QStringList &templateToPaths);
    ~TemplateFiller();
    struct AttributesToValidate{
        QSet<QString> addedAi;
        QSet<QString> removedAi;
    };
    enum VersionAmz{
        V01
        , V02
    };
    void setTemplates(const QString &commonSettingsDir
                      , const QString &templateFromPath
                      , const QStringList &templateToPaths);
    void checkParentSkus();
    void checkKeywords();
    void checkPreviewImages();
    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString> > > > > checkPossibleValues();
    void buildAttributes();
    void checkColumnsFilled();
    QStringList getImagePreviewFileNames() const;

     // Return all field with values or ask AI after reading field ids
    QStringList findPreviousTemplatePath() const;
    QCoro::Task<AttributesToValidate> findAttributesMandatoryToValidateManually() const;
    void validateMandatory(const QSet<QString> &attributesMandatory,
                           const QSet<QString> &attributesNotMandatory);

    QStringList suggestAttributesChildOnly(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValues(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValueChild(
            const QStringList &previousTemplatePaths) const;

    AttributesMandatoryTable *mandatoryAttributesTable() const;
    AttributesMandatoryAiTable *mandatoryAttributesAiTable() const;

    AttributeEquivalentTable *attributeEquivalentTable() const;

    AttributeFlagsTable *attributeFlagsTable() const;

    AttributePossibleMissingTable *attributePossibleMissingTable() const;

    AttributeValueReplacedTable *attributeValueReplacedTable() const;

private:
    QHash<QString, QHash<QString, QString>> m_countryCode_langCode_keywords;
    QHash<QString, QHash<QString, QHash<QString, QString>>> m_skuPattern_countryCode_langCode_keywords;
    AttributesMandatoryTable *m_mandatoryAttributesTable;
    AttributesMandatoryAiTable *m_mandatoryAttributesAiTable;
    AttributeEquivalentTable *m_attributeEquivalentTable;
    AttributeFlagsTable *m_attributeFlagsTable;
    QMetaObject::Connection m_connectionFlagsTable;
    AttributePossibleMissingTable *m_attributePossibleMissingTable;
    AttributeValueReplacedTable *m_attributeValueReplacedTable;
    void _clearAttributeManagers();
    QString m_productType;
    QString m_marketplaceFrom;
    QDir m_workingDirCommon;
    QDir m_workingDir;
    QDir m_workingDirImage;
    QString m_templateFromPath;
    QStringList m_templateToPaths;
    QStringList _get_allTemplatePaths() const;
    QString _get_cellVal(QXlsx::Document &doc, int row, int col) const;
    QString m_langCodeFrom;
    QString m_countryCodeFrom;
    QString _get_countryCode(const QString &templateFilePath) const;
    QString _get_langCode(const QString &templateFilePath) const;
    QString _getLangCodeFromText(const QString &langInfos) const;
    void _selectTemplateSheet(QXlsx::Document &doc) const;
    void _selectValidValuesSheet(QXlsx::Document &doc) const;
    void _selectMandatorySheet(QXlsx::Document &doc) const;
    QHash<QString, QSet<QString> > _readKeywords(const QStringList &filePaths);
    TemplateFiller::VersionAmz _getDocumentVersion(QXlsx::Document &document) const;
    int _getRowFieldId(VersionAmz version) const;
    QHash<QString, int> _get_fieldId_index(QXlsx::Document &doc) const;
    QString _get_marketplaceFrom() const;
    QString _get_marketplace(QXlsx::Document &doc) const;
    QSet<QString> _get_fieldIdMandatory(QXlsx::Document &doc) const;
    QSet<QString> _get_fieldIdMandatoryAll() const;
    QSet<QString> _get_fieldIdMandatoryPrevious() const;
    QHash<QString, QSet<QString>> _get_fieldId_possibleValues(QXlsx::Document &doc) const;
    QHash<QString, QSet<QString>> _get_parentSku_skus(QXlsx::Document &doc) const;
    void _formatFieldId(QString &fieldId) const;
    int _getIndCol(const QHash<QString, int> &fieldId_index
                   , const QStringList &possibleValues) const;
    int _getIndColSku(const QHash<QString, int> &fieldId_index) const;
    int _getIndColSkuParent(const QHash<QString, int> &fieldId_index) const;
    int _getIndColColorName(const QHash<QString, int> &fieldId_index) const;
    int _getIndColProductType(const QHash<QString, int> &fieldId_index) const;
    QString _get_productType(QXlsx::Document &doc) const;
    QString _get_productType(const QString &filePath) const;
    QSharedPointer<QSettings> settingsWorkingDir() const; // Settings of current working directory
    QHash<QString, QHash<QString, QSharedPointer<Attribute>>> marketplaceId_attributeId_attributeInfos;
};

#endif // TEMPLATEFILLER_H
