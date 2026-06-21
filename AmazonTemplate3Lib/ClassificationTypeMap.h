#ifndef CLASSIFICATIONTYPEMAP_H
#define CLASSIFICATIONTYPEMAP_H

#include <QHash>
#include <QString>
#include <QStringList>

// Persists a mapping of browseClassification.classificationId → Amazon productType
// to {workingDir}/classification_types.ini so the user only needs to assign each
// classification once. productType values are reused across sessions.
class ClassificationTypeMap
{
public:
    ClassificationTypeMap() = default;

    void load(const QString &workingDir);
    void save() const;

    // Returns the assigned productType, or empty string if unknown.
    QString productType(const QString &classificationId) const;

    // Set (and persist on next save()) the productType for a classificationId.
    void setProductType(const QString &classificationId, const QString &productType);

    // All productTypes ever assigned, sorted, deduplicated — for combo box suggestions.
    QStringList knownProductTypes() const;

private:
    QHash<QString, QString> m_map; // classificationId → productType
    QString m_filePath;
};

#endif
