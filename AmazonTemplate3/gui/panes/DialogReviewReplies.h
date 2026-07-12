#ifndef DIALOGREVIEWREPLIES_H
#define DIALOGREVIEWREPLIES_H

#include <QDialog>
#include <QList>

#include "CaseDraft.h"

class QCheckBox;
class QTextEdit;

// Review dialog shown after a run: one card per drafted case with Amazon's last
// reply, an editable draft, and a checkbox (checked by default). "Send checked"
// accepts the dialog; approvedDrafts() then returns the checked cases with their
// (possibly edited) reply text, which PaneCases sends via the worker.
//
// Cases that failed to scrape/draft are listed read-only (no checkbox) so the
// user can see what went wrong.
class DialogReviewReplies : public QDialog
{
    Q_OBJECT
public:
    explicit DialogReviewReplies(const QList<CaseDraft> &drafts, QWidget *parent = nullptr);

    // Checked cases, each with draft text replaced by the edited content.
    QList<CaseDraft> approvedDrafts() const;

    // True if the user opted to click Send themselves in the browser (the worker
    // fills + attaches but does not auto-submit).
    bool letUserSend() const;

private:
    QCheckBox *m_letUserSend = nullptr;

    struct Row {
        CaseDraft   draft;
        QCheckBox  *check = nullptr;
        QTextEdit  *edit  = nullptr;
    };
    QList<Row> m_rows;
};

#endif // DIALOGREVIEWREPLIES_H
