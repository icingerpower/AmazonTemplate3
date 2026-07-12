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

signals:
    void logMessage(const QString &msg); // forwards the worker's stderr lines

private:
    // Runs one job; onDone gets the parsed result object (or an error string).
    void _run(const QJsonObject &job, QObject *context,
              std::function<void(QJsonObject result, QString error)> onDone);
    QString _tsxPath() const;
    QString _scriptPath() const;

    QString m_workerDir;
};

#endif // CASEWORKERRUNNER_H
