#include "CaseWorkerRunner.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>

#ifndef CASE_WORKER_DIR
#define CASE_WORKER_DIR ""
#endif

// ---------------------------------------------------------------------------
// CaseWorkerThread
// ---------------------------------------------------------------------------

QString CaseWorkerThread::lastAmazonReply() const
{
    for (auto it = messages.crbegin(); it != messages.crend(); ++it)
        if (it->from == QLatin1String("amazon"))
            return it->text;
    return {};
}

bool CaseWorkerThread::isAmazonTurn() const
{
    return !messages.isEmpty() && messages.constLast().from == QLatin1String("amazon");
}

CaseWorkerThread CaseWorkerThread::parse(const QJsonObject &o)
{
    CaseWorkerThread t;
    t.caseId     = o.value(QStringLiteral("caseId")).toString();
    t.region     = o.value(QStringLiteral("region")).toString();
    t.subject    = o.value(QStringLiteral("subject")).toString();
    t.status     = o.value(QStringLiteral("status")).toString();
    t.threadHash = o.value(QStringLiteral("threadHash")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject m = v.toObject();
        t.messages.append(CaseWorkerThreadMessage{
            m.value(QStringLiteral("from")).toString(),
            m.value(QStringLiteral("author")).toString(),
            m.value(QStringLiteral("text")).toString(),
            m.value(QStringLiteral("ts")).toString(),
        });
    }
    return t;
}

// ---------------------------------------------------------------------------
// CaseWorkerRunner
// ---------------------------------------------------------------------------

CaseWorkerRunner::CaseWorkerRunner(QObject *parent)
    : QObject(parent)
{
    const QString override = QSettings().value(QStringLiteral("cases/workerDir")).toString();
    m_workerDir = override.isEmpty() ? QString::fromUtf8(CASE_WORKER_DIR) : override;
}

void CaseWorkerRunner::setWorkerDir(const QString &dir) { m_workerDir = dir; }

QString CaseWorkerRunner::_tsxPath() const
{
    return QDir(m_workerDir).filePath(QStringLiteral("node_modules/.bin/tsx"));
}

QString CaseWorkerRunner::_scriptPath() const
{
    return QDir(m_workerDir).filePath(QStringLiteral("src/oneshot.ts"));
}

bool CaseWorkerRunner::isConfigured(QString *why) const
{
    if (m_workerDir.isEmpty()) {
        if (why) *why = tr("Worker directory is not set (cases/workerDir).");
        return false;
    }
    if (!QFileInfo::exists(_scriptPath())) {
        if (why) *why = tr("Worker script not found at %1.").arg(_scriptPath());
        return false;
    }
    if (!QFileInfo::exists(_tsxPath())) {
        if (why) *why = tr("Dependencies missing — run `npm install` in %1.").arg(m_workerDir);
        return false;
    }
    return true;
}

// Augment PATH with the usual nvm/user-local locations so the tsx shim's
// `#!/usr/bin/env node` resolves even when launched from a GUI session.
static QProcessEnvironment childEnv()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString home = QDir::homePath();
    QStringList extra{home + QStringLiteral("/.local/bin")};
    const QDir nvmNodeDir(home + QStringLiteral("/.nvm/versions/node"));
    for (const QString &ver : nvmNodeDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        extra << nvmNodeDir.filePath(ver + QStringLiteral("/bin"));

    const QString sep(QLatin1Char(':'));
    env.insert(QStringLiteral("PATH"), extra.join(sep) + sep + env.value(QStringLiteral("PATH")));
    return env;
}

void CaseWorkerRunner::_run(const QJsonObject &job, QObject *context,
                            std::function<void(QJsonObject, QString)> onDone)
{
    QString why;
    if (!isConfigured(&why)) {
        onDone({}, why);
        return;
    }

    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(m_workerDir);
    proc->setProcessEnvironment(childEnv());

    QPointer<QObject> ctx(context);
    QObject::connect(proc, &QProcess::errorOccurred, this,
                     [this, proc](QProcess::ProcessError) {
        emit logMessage(tr("worker process error: %1").arg(proc->errorString()));
    });

    // Forward stderr diagnostics line-by-line as log messages.
    QObject::connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
        const QByteArray err = proc->readAllStandardError();
        for (const QByteArray &line : err.split('\n'))
            if (!line.trimmed().isEmpty())
                emit logMessage(QString::fromUtf8(line));
    });

    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [proc, ctx, onDone](int, QProcess::ExitStatus) {
        proc->deleteLater();
        if (!ctx) return; // caller gone
        const QByteArray out = proc->readAllStandardOutput();
        // The result is the last non-empty JSON line on stdout.
        QJsonObject result;
        QString error;
        const QList<QByteArray> lines = out.split('\n');
        QByteArray jsonLine;
        for (const QByteArray &l : lines)
            if (!l.trimmed().isEmpty()) jsonLine = l.trimmed();
        if (jsonLine.isEmpty()) {
            error = QObject::tr("worker produced no output");
        } else {
            QJsonParseError perr;
            const QJsonDocument doc = QJsonDocument::fromJson(jsonLine, &perr);
            if (perr.error != QJsonParseError::NoError)
                error = QObject::tr("bad worker output: %1").arg(perr.errorString());
            else {
                result = doc.object();
                if (result.contains(QStringLiteral("error")))
                    error = result.value(QStringLiteral("error")).toString();
            }
        }
        onDone(result, error);
    });

    proc->start(_tsxPath(), {_scriptPath()});
    proc->write(QJsonDocument(job).toJson(QJsonDocument::Compact));
    proc->closeWriteChannel();
}

void CaseWorkerRunner::scrape(const QList<WorkerCase> &casesIn,
                              QObject *context, std::function<void(QList<ScrapeResult>)> callback)
{
    QJsonArray cases;
    for (const WorkerCase &c : casesIn)
        cases.append(QJsonObject{{QStringLiteral("region"), c.region},
                                 {QStringLiteral("caseId"), c.caseId},
                                 {QStringLiteral("account"), c.account}});
    const QJsonObject job{{QStringLiteral("action"), QStringLiteral("scrape")},
                          {QStringLiteral("cases"), cases}};

    _run(job, context, [callback](QJsonObject result, QString error) {
        QList<ScrapeResult> out;
        if (!error.isEmpty()) { callback(out); return; }
        for (const QJsonValue &v : result.value(QStringLiteral("results")).toArray()) {
            const QJsonObject o = v.toObject();
            ScrapeResult r;
            r.region  = o.value(QStringLiteral("region")).toString();
            r.caseId  = o.value(QStringLiteral("caseId")).toString();
            r.ok      = o.value(QStringLiteral("ok")).toBool();
            r.error   = o.value(QStringLiteral("error")).toString();
            r.sessionExpired = o.value(QStringLiteral("sessionExpired")).toBool();
            r.raw     = o.value(QStringLiteral("thread")).toObject();
            r.thread  = CaseWorkerThread::parse(r.raw);
            out.append(r);
        }
        callback(out);
    });
}

void CaseWorkerRunner::reply(const QList<ReplyJob> &jobs, bool manualSend,
                             QObject *context, std::function<void(QList<ReplyResult>)> callback)
{
    QJsonArray cases;
    for (const ReplyJob &j : jobs) {
        QJsonArray files;
        for (const QString &f : j.files) files.append(f);
        cases.append(QJsonObject{{QStringLiteral("region"), j.region},
                                 {QStringLiteral("caseId"), j.caseId},
                                 {QStringLiteral("text"), j.text},
                                 {QStringLiteral("account"), j.account},
                                 {QStringLiteral("files"), files}});
    }
    const QJsonObject job{{QStringLiteral("action"), QStringLiteral("reply")},
                          {QStringLiteral("cases"), cases},
                          {QStringLiteral("manualSend"), manualSend}};

    _run(job, context, [callback](QJsonObject result, QString error) {
        QList<ReplyResult> out;
        if (!error.isEmpty()) { callback(out); return; }
        for (const QJsonValue &v : result.value(QStringLiteral("results")).toArray()) {
            const QJsonObject o = v.toObject();
            ReplyResult r;
            r.region = o.value(QStringLiteral("region")).toString();
            r.caseId = o.value(QStringLiteral("caseId")).toString();
            r.ok     = o.value(QStringLiteral("ok")).toBool();
            r.error  = o.value(QStringLiteral("error")).toString();
            r.sessionExpired = o.value(QStringLiteral("sessionExpired")).toBool();
            r.attached = o.value(QStringLiteral("attached")).toInt();
            r.sent     = o.value(QStringLiteral("sent")).toBool();
            out.append(r);
        }
        callback(out);
    });
}

void CaseWorkerRunner::login(const QString &region,
                             QObject *context, std::function<void(bool, QString)> callback)
{
    const QJsonObject job{{QStringLiteral("action"), QStringLiteral("login")},
                          {QStringLiteral("region"), region}};
    _run(job, context, [callback](QJsonObject result, QString error) {
        if (!error.isEmpty()) { callback(false, error); return; }
        callback(result.value(QStringLiteral("ok")).toBool(), {});
    });
}
