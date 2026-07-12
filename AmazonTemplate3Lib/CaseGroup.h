#ifndef CASEGROUP_H
#define CASEGROUP_H

#include <QDir>
#include <QList>
#include <QString>
#include <QStringList>

// A "group" of Amazon Seller Central support cases handled together.
//
// Each group owns one folder under {workingDir}/cases/{name}/ that is BOTH the
// folder the user opens (to drop in images/documents) AND the working directory
// the AI CLI is run in — so anything dropped in is visible to the CLI at draft
// time, alongside the group's CLAUDE.md memory. dir() is the single source of
// truth for that path; never build it anywhere else.
//
// Folder contents:
//   group.ini            region + instructions + the case list (persisted here)
//   CLAUDE.md            per-group memory the CLI reads every run
//   threads/{id}.json    last scraped thread for a case (written by PaneCases)
//   <user files>         images / documents dropped in via "Open folder"
//
// Persistence mirrors ClassificationTypeMap: plain QSettings(IniFormat).
class CaseGroup
{
public:
    // One case tracked within a group. subject is cached from the last scrape.
    struct Case {
        QString caseId;
        QString subject;
        bool    done = false;
    };

    CaseGroup() = default;
    CaseGroup(const QDir &workingDir, const QString &name);

    // Scan {workingDir}/cases/ and load every group found (each loaded from its
    // own group.ini). Returns them sorted by name.
    static QList<CaseGroup> loadAll(const QDir &workingDir);

    // Turn a user-typed group name into a filesystem-safe folder name.
    static QString sanitizeName(const QString &name);

    bool isValid() const { return !m_name.isEmpty(); }

    // -- identity / fields -------------------------------------------------
    QString name() const { return m_name; }

    QString region() const { return m_region; }          // "eu" | "na" | "jp"
    void    setRegion(const QString &region) { m_region = region; }

    // Amazon marketplace/country to select on the account-switcher (e.g. "United
    // Kingdom"). Empty until the user picks one; Run is blocked while empty.
    QString account() const { return m_account; }
    void    setAccount(const QString &account) { m_account = account; }

    QString instructions() const { return m_instructions; }
    void    setInstructions(const QString &text) { m_instructions = text; }

    // -- cases -------------------------------------------------------------
    const QList<Case> &cases() const { return m_cases; }
    bool addCase(const QString &caseId);                 // false if already present
    bool removeCase(const QString &caseId);
    bool setCaseDone(const QString &caseId, bool done);
    void setCaseSubject(const QString &caseId, const QString &subject);
    const Case *findCase(const QString &caseId) const;

    // -- paths (single source of truth) ------------------------------------
    QDir    dir() const;                                 // {workingDir}/cases/{name}
    QString claudeMdPath() const;                        // dir()/CLAUDE.md
    QString threadPath(const QString &caseId) const;     // dir()/threads/{caseId}.json
    QStringList attachmentFiles() const;                 // files dropped in the group folder

    // -- persistence -------------------------------------------------------
    void ensureFolder() const;   // mkpath dir()+threads/, seed CLAUDE.md if absent
    void load();                 // read group.ini
    void save() const;           // ensureFolder() + write group.ini

private:
    QString iniPath() const;     // dir()/group.ini
    Case   *mutableCase(const QString &caseId);

    QDir        m_workingDir;
    QString     m_name;                 // == folder name (already sanitized)
    QString     m_region = QStringLiteral("eu");
    QString     m_account;              // marketplace/country for the switcher
    QString     m_instructions;
    QList<Case> m_cases;
};

#endif // CASEGROUP_H
