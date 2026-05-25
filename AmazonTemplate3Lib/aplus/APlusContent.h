#ifndef APLUSCONTENT_H
#define APLUSCONTENT_H

#include <QObject>
#include <QDir>
#include <QList>
#include <QString>
#include <QDateTime>

enum class APlusElementType { SizeChart, Image, Faq };

struct APlusVersion {
    QDateTime generated;
    QString   desktopFile;
    QString   mobileFile;
};

struct APlusElement {
    APlusElementType    type = APlusElementType::Image;
    QString             id;
    QString             displayName;
    QList<APlusVersion> versions;

    const APlusVersion *current() const;
};

class APlusContent : public QObject
{
    Q_OBJECT
public:
    explicit APlusContent(QObject *parent = nullptr);

    void setDir(const QDir &aplusDir);
    QDir dir() const;

    bool load();
    void save() const;

    const QList<APlusElement> &elements() const;
    QList<APlusElement>       &elements();

    APlusElement       *findElement(const QString &id);
    const APlusElement *findElement(const QString &id) const;
    int                 indexOf(const QString &id) const;

    // Prepends a new version (keeps history) — use for AI-generated content.
    void pushVersion(const QString &id, APlusElementType type,
                     const QString &displayName, const APlusVersion &ver);
    // Replaces all versions with exactly one — use for deterministic content
    // (e.g. size chart rendered from table data) that has no meaningful history.
    void setSingleVersion(const QString &id, APlusElementType type,
                          const QString &displayName, const APlusVersion &ver);
    void deleteVersion(const QString &id, int versionIndex);
    void promoteVersion(const QString &id, int versionIndex);
    void ensureImageElement(int index);

signals:
    void elementChanged(const QString &id);
    void layoutChanged();

private:
    QDir                m_dir;
    QList<APlusElement> m_elements;
};

#endif // APLUSCONTENT_H
