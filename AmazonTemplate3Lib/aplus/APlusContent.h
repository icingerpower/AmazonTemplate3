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
    APlusVersion       *current();
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
    // Clears only the desktop or mobile file of a version (deletes it from disk).
    // If both sides end up empty the whole version is removed.
    void clearVersionFile(const QString &id, int versionIndex, bool desktop);
    void promoteVersion(const QString &id, int versionIndex);
    void ensureImageElement(int index);

    // Deletes image files left in each element's directory that are NOT part of
    // that element's version history (orphans from failed/over-agentic CLI runs,
    // e.g. the plain desktop.png/mobile.png or superseded v_* files). The version
    // history referenced by index.json is preserved. Returns the count removed.
    int pruneOrphanFiles() const;

signals:
    void elementChanged(const QString &id);
    void layoutChanged();

private:
    QDir                m_dir;
    QList<APlusElement> m_elements;
};

#endif // APLUSCONTENT_H
