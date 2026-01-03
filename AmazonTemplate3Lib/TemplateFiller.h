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
#include "fillers/AbstractFiller.h"

class AttributesMandatoryAiTable;
class AttributesMandatoryTable;
class AttributeEquivalentTable;
class AttributeFlagsTable;
class AttributePossibleMissingTable;
class AttributeValueReplacedTable;
class AiFailureTable;

class TemplateFiller
{
public:
    static const QSet<QString> VALUES_MANDATORY;
    static const QHash<QString, QSet<QString>> SHEETS_MANDATORY;
    TemplateFiller(const QString &workingDirCommon
                   , const QString &templateFromPath
                   , const QStringList &templateToPaths
                   , const QStringList &templateSourcePaths
                   , const QMap<QString, QString> &skuPattern_customInstruction);
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
                      , const QStringList &templateToPaths
                      , const QStringList &templateSourcePaths
                      , const QMap<QString, QString> &skuPattern_customInstructions);
    void checkParentSkus();
    void checkKeywords();
    QHash<QString, QString> checkPreviewImages();
    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QHash<QString, QSet<QString>>>>>> checkPossibleValues();
    void buildAttributes();
    void checkColumnsFilled();
    QCoro::Task<void> fillValues();

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

    const QString &marketplaceFrom() const;

    const QHash<QString, QString> &sku_imagePreviewFilePath() const;
    void saveAiValue(const QString &settingsFileName, const QString &id, const QString &value) const;
    void saveAiValue(const QString &settingsFileName, const QHash<QString, QString> &id_values) const;
    bool hasAiValue(const QString &settingsFileName, const QString &id) const;
    QString getAiReply(const QString &settingsFileName, const QString &id) const;
    QSharedPointer<QSettings> settingsCommon() const; // Settings of current working directory
    QSharedPointer<QSettings> settingsProducts() const; // Settings of current working directory

    AiFailureTable *aiFailureTable() const;

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
    AiFailureTable *m_aiFailureTable;
    void _clearAttributeManagers();
    QString m_productType;
    AbstractFiller::Age m_age;
    AbstractFiller::Gender m_gender;
    QString m_marketplaceFrom;
    QDir m_workingDirCommon;
    QDir m_workingDir;
    QDir m_workingDirImage;
    QString m_templateFromPath;
    QStringList m_templateToPaths;
    QStringList m_templateSourcePaths;
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
    QHash<QString, QSet<QString> > _readKeywords();
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
    QHash<QString, QHash<QString, QSet<QString>>> _get_parentSku_variation_skus(QXlsx::Document &doc) const;
    void _formatFieldId(QString &fieldId) const;
    int _getIndCol(const QHash<QString, int> &fieldId_index
                   , const QStringList &possibleValues) const;
    int _getIndColSku(const QHash<QString, int> &fieldId_index) const;
    int _getIndColSkuParent(const QHash<QString, int> &fieldId_index) const;
    int _getIndColColorName(const QHash<QString, int> &fieldId_index) const;
    int _getIndColProductType(const QHash<QString, int> &fieldId_index) const;
    int _getIndColAge(const QHash<QString, int> &fieldId_index) const;
    int _getIndColGender(const QHash<QString, int> &fieldId_index) const;
    QCoro::Task<void> _readAgeGender();
    QString _get_productType(QXlsx::Document &doc) const;
    QString _get_productType(const QString &filePath) const;
    QHash<QString, QHash<QString, QSharedPointer<Attribute>>> m_marketplace_attributeId_attributeInfos;
    QHash<QString, QHash<QString, QString>> _get_sku_fieldId_fromValues(
            const QString &templatePath
            , const QSet<QString> &fieldIdsWhiteList = QSet<QString>{}) const;

    QHash<QString, QMap<QString, QString>> m_sku_attribute_valuesForAi;
    QHash<QString, QHash<QString, QString>> m_sku_fieldId_fromValues;
    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QString>>>> m_countryCode_langCode_sku_fieldId_sourceValues;
    QHash<QString, QHash<QString, QHash<QString, QString>>> m_langCode_sku_fieldId_toValues;
    QHash<QString, QHash<QString, QHash<QString, QHash<QString, QString>>>> m_countryCode_langCode_sku_fieldId_toValues;
    void _fillValuesSources();
    void _saveTemplates();
    QHash<QString, QString> m_sku_imagePreviewFilePath;
    QMap<QString, QString> m_skuPattern_customInstructions;
};

#endif // TEMPLATEFILLER_H
