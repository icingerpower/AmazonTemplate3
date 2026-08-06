#ifndef CASEWORKERRUNNER_H
#define CASEWORKERRUNNER_H

#include <functional>

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QProcess;

// One message in a scraped case thread (mirrors the Node worker's ThreadMessage).
struct CaseWorkerThreadMessage {
    QString from;   // "amazon" | "seller" | "unknown"
    QString author;
    QString text;
    QString ts;
};

// A scraped case thread (mirrors the Node worker's CaseThread).
struct CaseWorkerThread {
    QString caseId;
    QString region;
    QString subject;
    QString status;
    QString threadHash;
    QList<CaseWorkerThreadMessage> messages;

    QString lastAmazonReply() const;                 // last Amazon-authored message
    bool    isAmazonTurn() const;                    // latest message is from Amazon
    static CaseWorkerThread parse(const QJsonObject &o);
};

struct ScrapeResult {
    QString region;
    QString caseId;
    bool    ok = false;
    QString error;
    bool    sessionExpired = false;
    CaseWorkerThread thread;
    QJsonObject raw;                                 // raw thread JSON, for saving to disk
};

// A case to scrape (region + id + the marketplace to select on the switcher).
struct WorkerCase {
    QString region;
    QString caseId;
    QString account;
};

struct ReplyJob {
    QString     region;
    QString     caseId;
    QString     text;
    QString     account;
    QStringList files;      // absolute paths to attach
};

// One EU marketplace to process on the GSPR compliance page.
struct GsprTarget {
    QString countryCode;      // "DE"
    QString countryName;      // account-switcher label, e.g. "Germany"
    QStringList skipAsins;    // warning/safety info done earlier — never retried
    QStringList skipAsinsRp;  // Responsible Person done earlier — never retried
    QStringList skipAsinsMfr; // manufacturer contact done earlier — never retried
};

// Outcome of one per-ASIN GSPR warning row. status:
// "submitted" (saved by this run) | "failed" | "pending" (not actionable on
// the page — submitted earlier or by a human) | "skipped" (in skipAsins).
struct GsprWarningOutcome {
    QString asin;
    bool    ok = false;
    QString status;
    QString reason;        // why it failed (e.g. no "Safety attestation" option)
    QString statusText;    // row's next-steps text (pending diagnosis)
    QString type;          // "psi" (warning/safety info) | "rp" (Responsible Person)
};

struct GsprResult {
    QString countryCode;
    bool    ok = false;
    QString url;           // final page URL
    QString dumpDir;       // where the worker dumped the page (snapshot runs)
    QString error;
    bool    sessionExpired = false;
    QList<GsprWarningOutcome> warnings; // per-ASIN outcomes ("safety" runs)
};

struct ReplyResult {
    QString region;
    QString caseId;
    bool    ok = false;         // reply submitted with no error
    QString error;
    bool    sessionExpired = false;
    int     attached = 0;       // files attached
    bool    sent = false;       // reply actually submitted
};

// Launches the one-shot Node worker (case-worker/src/oneshot.ts) via QProcess —
// same fire-and-forget, context-guarded pattern as AbstractCli::runPromptAsync.
// No server, no port: each call spawns a process, feeds it a JSON job on stdin,
// reads one JSON result on stdout, and the process exits.
class CaseWorkerRunner : public QObject
{
    Q_OBJECT
public:
    explicit CaseWorkerRunner(QObject *parent = nullptr);

    // Directory of the case-worker project (defaults to the CASE_WORKER_DIR
    // compile define, overridable via QSettings "cases/workerDir").
    void setWorkerDir(const QString &dir);
    QString workerDir() const { return m_workerDir; }

    // True when node/tsx and the script are present. On false, *why explains.
    bool isConfigured(QString *why = nullptr) const;

    void scrape(const QList<WorkerCase> &cases,
                QObject *context, std::function<void(QList<ScrapeResult>)> callback);
    // manualSend: fill + attach but leave the browser open for the user to click
    // Send (don't auto-submit).
    void reply(const QList<ReplyJob> &jobs, bool manualSend,
               QObject *context, std::function<void(QList<ReplyResult>)> callback);
    void login(const QString &region,
               QObject *context, std::function<void(bool ok, QString error)> callback);
    // GSPR compliance-page run (EU session). Marketplaces are processed one at
    // a time in one browser window; the worker dumps each page under dumpDir.
    void gspr(const QString &subaction, const QList<GsprTarget> &targets,
              const QString &dumpDir, const QString &ecRepPattern,
              const QJsonArray &manufacturers, const QStringList &userSkipAsins,
              bool manualManufacturerSave,
              QObject *context, std::function<void(QList<GsprResult>)> callback);

    // Answer a worker "@@gspr-ask" pause: writes one JSON line to the stdin of
    // the (single) live worker process. Returns false when none is running.
    bool sendLineToWorker(const QByteArray &line);

    // Gracefully stop every worker process this runner started (SIGTERM, then
    // SIGKILL after 5 s). Playwright closes its browser on SIGTERM, releasing
    // the profile lock. Each job's callback still fires (with whatever output
    // the worker managed to produce).
    void stopAll();

signals:
    void logMessage(const QString &msg); // forwards the worker's stderr lines

private:
    // Runs one job; onDone gets the parsed result object (or an error string).
    // keepStdinOpen leaves the worker's stdin writable for interactive replies
    // (gspr pauses); other jobs close it after the job line.
    void _run(const QJsonObject &job, QObject *context,
              std::function<void(QJsonObject result, QString error)> onDone,
              bool keepStdinOpen = false);
    QString _tsxPath() const;
    QString _scriptPath() const;

    QString m_workerDir;
    QList<QPointer<QProcess>> m_procs; // live worker processes (for stopAll)
};

#endif // CASEWORKERRUNNER_H
