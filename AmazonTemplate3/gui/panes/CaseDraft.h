#ifndef CASEDRAFT_H
#define CASEDRAFT_H

#include <QString>
#include <QStringList>

// A drafted reply for one case, produced by a run and reviewed before sending.
struct CaseDraft {
    QString     groupName;
    QString     region;
    QString     account;         // marketplace to select on the switcher when sending
    QString     caseId;
    QString     subject;
    QString     amazonLastReply; // last Amazon message in the thread
    QString     draft;           // CLI output (empty when error set)
    QString     error;           // non-empty when scrape/draft failed
    QStringList attachments;     // abs paths of files to attach to this reply
};

#endif // CASEDRAFT_H
