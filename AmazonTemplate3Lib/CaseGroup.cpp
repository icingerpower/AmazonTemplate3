#include "CaseGroup.h"

#include <algorithm>

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>

CaseGroup::CaseGroup(const QDir &workingDir, const QString &name)
    : m_workingDir(workingDir)
    , m_name(sanitizeName(name))
{
}

QString CaseGroup::sanitizeName(const QString &name)
{
    // Keep it human-readable but filesystem-safe: drop path separators and other
    // characters that are awkward across platforms, collapse whitespace.
    static const QRegularExpression kBad(QStringLiteral(R"([/\\:*?"<>|]+)"));
    QString out = name;
    out.replace(kBad, QStringLiteral(" "));
    out = out.simplified();
    return out;
}

QList<CaseGroup> CaseGroup::loadAll(const QDir &workingDir)
{
    QList<CaseGroup> groups;
    const QDir casesDir(workingDir.filePath(QStringLiteral("cases")));
    if (!casesDir.exists())
        return groups;

    for (const QString &sub : casesDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        CaseGroup g(workingDir, sub);
        g.load();
        groups.append(g);
    }
    return groups;
}

// ---------------------------------------------------------------------------
// cases
// ---------------------------------------------------------------------------

const CaseGroup::Case *CaseGroup::findCase(const QString &caseId) const
{
    for (const Case &c : m_cases)
        if (c.caseId == caseId)
            return &c;
    return nullptr;
}

CaseGroup::Case *CaseGroup::mutableCase(const QString &caseId)
{
    for (Case &c : m_cases)
        if (c.caseId == caseId)
            return &c;
    return nullptr;
}

bool CaseGroup::addCase(const QString &caseId)
{
    const QString id = caseId.trimmed();
    if (id.isEmpty() || findCase(id))
        return false;
    m_cases.append(Case{id, QString(), false});
    return true;
}

bool CaseGroup::removeCase(const QString &caseId)
{
    const auto before = m_cases.size();
    m_cases.erase(std::remove_if(m_cases.begin(), m_cases.end(),
                                 [&](const Case &c) { return c.caseId == caseId; }),
                  m_cases.end());
    return m_cases.size() != before;
}

bool CaseGroup::setCaseDone(const QString &caseId, bool done)
{
    if (Case *c = mutableCase(caseId)) {
        c->done = done;
        return true;
    }
    return false;
}

void CaseGroup::setCaseSubject(const QString &caseId, const QString &subject)
{
    if (Case *c = mutableCase(caseId))
        c->subject = subject;
}

// ---------------------------------------------------------------------------
// paths — single source of truth
// ---------------------------------------------------------------------------

QDir CaseGroup::dir() const
{
    return QDir(m_workingDir.filePath(QStringLiteral("cases/%1").arg(m_name)));
}

QString CaseGroup::claudeMdPath() const
{
    return dir().filePath(QStringLiteral("CLAUDE.md"));
}

QString CaseGroup::threadPath(const QString &caseId) const
{
    return dir().filePath(QStringLiteral("threads/%1.json").arg(caseId));
}

QStringList CaseGroup::attachmentFiles() const
{
    // Attachments are whatever files the user drops directly in the group folder,
    // minus the group's own bookkeeping files. Subfolders (threads/) are skipped
    // automatically by QDir::Files.
    static const QSet<QString> kSystemFiles = {
        QStringLiteral("group.ini"), QStringLiteral("CLAUDE.md"),
    };
    QStringList out;
    const QDir d = dir();
    if (!d.exists()) return out;
    for (const QFileInfo &fi : d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
        if (kSystemFiles.contains(fi.fileName())) continue;
        out << fi.absoluteFilePath();
    }
    return out;
}

QString CaseGroup::iniPath() const
{
    return dir().filePath(QStringLiteral("group.ini"));
}

// ---------------------------------------------------------------------------
// persistence
// ---------------------------------------------------------------------------

void CaseGroup::ensureFolder() const
{
    QDir d = dir();
    d.mkpath(QStringLiteral("."));
    d.mkpath(QStringLiteral("threads"));

    // Seed a CLAUDE.md the first time so the CLI has per-group memory/context.
    // Never overwrite: the user (and the CLI) may have edited it.
    const QString claude = claudeMdPath();
    if (!QFile::exists(claude)) {
        QFile f(claude);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            const QString body = QStringLiteral(
                "# Case group: %1\n\n"
                "Memory and context for drafting Amazon Seller Central case replies "
                "for this group. Any images or documents in this folder are context "
                "for the reply. Record durable facts (tone, policies, recurring "
                "answers) here so they carry across cases.\n")
                .arg(m_name);
            f.write(body.toUtf8());
        }
    }
}

void CaseGroup::load()
{
    m_cases.clear();
    QSettings s(iniPath(), QSettings::IniFormat);

    m_region       = s.value(QStringLiteral("group/region"), m_region).toString();
    m_account      = s.value(QStringLiteral("group/account")).toString();
    m_instructions = s.value(QStringLiteral("group/instructions")).toString();

    const int n = s.beginReadArray(QStringLiteral("cases"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        Case c;
        c.caseId  = s.value(QStringLiteral("id")).toString();
        c.subject = s.value(QStringLiteral("subject")).toString();
        c.done    = s.value(QStringLiteral("done"), false).toBool();
        if (!c.caseId.isEmpty())
            m_cases.append(c);
    }
    s.endArray();
}

void CaseGroup::save() const
{
    ensureFolder();
    QSettings s(iniPath(), QSettings::IniFormat);

    s.setValue(QStringLiteral("group/name"), m_name);
    s.setValue(QStringLiteral("group/region"), m_region);
    s.setValue(QStringLiteral("group/account"), m_account);
    s.setValue(QStringLiteral("group/instructions"), m_instructions);

    s.remove(QStringLiteral("cases")); // rewrite the array cleanly
    s.beginWriteArray(QStringLiteral("cases"));
    for (int i = 0; i < m_cases.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), m_cases[i].caseId);
        s.setValue(QStringLiteral("subject"), m_cases[i].subject);
        s.setValue(QStringLiteral("done"), m_cases[i].done);
    }
    s.endArray();
    s.sync();
}
