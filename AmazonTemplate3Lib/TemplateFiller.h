#ifndef TEMPLATEFILLER_H
#define TEMPLATEFILLER_H

#include <QString>


class TemplateFiller
{
public:
    static TemplateFiller *instance();
    void setTemplates(const QString &templateFromPath
                      , const QStringList &templateToPaths);
    void checkParentSkus(); // TODO raise exeption
    void checkKeywords(); // TODO raise exeption
    void checkPreviewImages(); // TODO raise exeption
    QStringList getImagePreviowFileNames() const;
    QStringList suggestAttributesChildOnly(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesMandatory(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValues(
            const QStringList &previousTemplatePaths) const;
    QStringList suggestAttributesSameValueChild(
            const QStringList &previousTemplatePaths) const;

private:
    TemplateFiller();
};

#endif // TEMPLATEFILLER_H
