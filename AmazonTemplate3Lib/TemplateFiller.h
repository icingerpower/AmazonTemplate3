#ifndef TEMPLATEFILLER_H
#define TEMPLATEFILLER_H

#include <QString>
#include <QStringList>
#include <QDir>

#include <xlsxdocument.h>
#include <QCoro/QCoroTask>
class MandatoryAttributesManager;
class AttributeEquivalentTable;
class AttributeFlagsTable;
class AttributePossibleMissingTable;
class AttributeValueReplacedTable;

class TemplateFiller
{
public:
    static const QSet<QString> VALUES_MANDATORY;
    static const QHash<QString, QString> SHEETS_MANDATORY;
    TemplateFiller(const QString &templateFromPath
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
    void setTemplates(const QString &templateFromPath
                      , const QStringList &templateToPaths);
    void checkParentSkus(); // TODO raise exeption
    void checkKeywords(); // TODO raise exeption
    void checkPreviewImages(); // TODO raise exeption
    QStringList getImagePreviewFileNames() const;
    QString checkSkus() const; // check child / parent are correct without duplicates

     // Return all field with values or ask AI after reading field ids
    QStringList findPreviousTemplatePath() const;
    QCoro::Task<AttributesToValidate> findAttributesMandatoryToValidateManually(
            const QStringList &previousTemplatePaths) const;
    void validateMandatory(const QSet<QString> &attributesMandatory,
                           const QSet<QString> &attributesNotMandatory);

    QStringList suggestAttributesChildOnly(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValues(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValueChild(
            const QStringList &previousTemplatePaths) const;

private:
    QHash<QString, QHash<QString, QString>> m_countryCode_langCode_keywords;
    QHash<QString, QHash<QString, QHash<QString, QString>>> m_skuPattern_countryCode_langCode_keywords;
    MandatoryAttributesManager *m_mandatoryAttributesManager;
    AttributeEquivalentTable *m_attributeEquivalentTable;
    AttributeFlagsTable *m_attributeFlagsTable;
    AttributePossibleMissingTable *m_attributePossibleMissingTable;
    AttributeValueReplacedTable *m_attributeValueReplacedTable;
    void _clearAttributeManagers();
    QString m_productType;
    QDir m_workingDir;
    QDir m_workingDirImage;
    QString m_templateFromPath;
    QStringList m_templateToPaths;
    QString m_langCodeFrom;
    QString m_countryCodeFrom;
    QString _getCountryCode(const QString &templateFilePath) const;
    QString _getLangCode(const QString &templateFilePath) const;
    QString _getLangCodeFromText(const QString &langInfos) const;
    void _selectTemplateSheet(QXlsx::Document &doc) const;
    void _selectValidValuesSheet(QXlsx::Document &doc) const;
    void _selectMandatorySheet(QXlsx::Document &doc) const;
    QHash<QString, QSet<QString> > _readKeywords(const QStringList &filePaths);
    TemplateFiller::VersionAmz _getDocumentVersion(QXlsx::Document &document) const;
    int _getRowFieldId(VersionAmz version) const;
    QHash<QString, int> _get_fieldId_index(QXlsx::Document &doc) const;
    QSet<QString> _get_fieldIdMandatory(QXlsx::Document &doc) const;
    QHash<QString, QSet<QString>> _get_fieldId_possibleValues(QXlsx::Document &doc) const;
    QHash<QString, QSet<QString>> _get_parentSku_skus(QXlsx::Document &doc) const;
    void _formatFieldId(QString &fieldId) const;
    int _getIndCol(const QHash<QString, int> &fieldId_index
                   , const QStringList &possibleValues) const;
    int _getIndColSku(const QHash<QString, int> &fieldId_index) const;
    int _getIndColSkuParent(const QHash<QString, int> &fieldId_index) const;
    int _getIndColColorName(const QHash<QString, int> &fieldId_index) const;
    int _getIndColProductType(const QHash<QString, int> &fieldId_index) const;
    QString _readProductType(const QString &filePath) const;
};

#endif // TEMPLATEFILLER_H
