#include "DialogReviewReplies.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

DialogReviewReplies::DialogReviewReplies(const QList<CaseDraft> &drafts, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Review replies"));
    resize(900, 700);

    auto *outer = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Review each drafted reply. Uncheck any you don't want to send, edit the "
           "text as needed, then click \"Send checked\". Nothing is sent until you do."),
        this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    m_letUserSend = new QCheckBox(
        tr("Let me click Send myself in the browser (fill + attach only, don't auto-send)"), this);
    m_letUserSend->setChecked(true); // manual send by default — user confirms in the browser
    outer->addWidget(m_letUserSend);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *container = new QWidget(scroll);
    auto *vbox = new QVBoxLayout(container);

    int reviewable = 0;
    for (const CaseDraft &d : drafts) {
        auto *box = new QGroupBox(container);
        auto *bl = new QVBoxLayout(box);

        const QString head = d.subject.isEmpty()
            ? tr("Case %1  [%2/%3]").arg(d.caseId, d.region.toUpper(), d.account)
            : tr("Case %1 — %2  [%3/%4]").arg(d.caseId, d.subject, d.region.toUpper(), d.account);

        if (!d.error.isEmpty()) {
            // Failed case: read-only, not sendable.
            box->setTitle(tr("⚠ %1").arg(head));
            auto *err = new QLabel(tr("Not drafted: %1").arg(d.error), box);
            err->setWordWrap(true);
            bl->addWidget(err);
            vbox->addWidget(box);
            continue;
        }

        ++reviewable;
        auto *check = new QCheckBox(head, box);
        check->setChecked(true);
        QFont bold = check->font();
        bold.setBold(true);
        check->setFont(bold);
        bl->addWidget(check);

        if (!d.amazonLastReply.isEmpty()) {
            bl->addWidget(new QLabel(tr("Amazon's last message:"), box));
            auto *amazon = new QTextEdit(box);
            amazon->setReadOnly(true);
            amazon->setPlainText(d.amazonLastReply);
            amazon->setMaximumHeight(140);
            amazon->setStyleSheet(QStringLiteral("background:#f4f4f4;"));
            bl->addWidget(amazon);
        }

        if (!d.attachments.isEmpty()) {
            QStringList names;
            for (const QString &p : d.attachments)
                names << p.section(QLatin1Char('/'), -1);
            auto *att = new QLabel(tr("📎 Will attach %n file(s): %1", nullptr, d.attachments.size())
                                       .arg(names.join(QStringLiteral(", "))), box);
            att->setWordWrap(true);
            bl->addWidget(att);
        }

        bl->addWidget(new QLabel(tr("Your reply (editable):"), box));
        auto *edit = new QTextEdit(box);
        edit->setPlainText(d.draft);
        edit->setMinimumHeight(120);
        bl->addWidget(edit);

        // Enable/disable the editor with the checkbox for a clear visual cue.
        QObject::connect(check, &QCheckBox::toggled, edit, &QWidget::setEnabled);

        m_rows.append(Row{d, check, edit});
        vbox->addWidget(box);
    }

    if (drafts.isEmpty())
        vbox->addWidget(new QLabel(tr("No drafts to review."), container));

    vbox->addStretch();
    scroll->setWidget(container);
    outer->addWidget(scroll, 1);

    auto *buttons = new QDialogButtonBox(this);
    auto *sendBtn = buttons->addButton(tr("Send checked"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    sendBtn->setEnabled(reviewable > 0);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
}

bool DialogReviewReplies::letUserSend() const
{
    return m_letUserSend && m_letUserSend->isChecked();
}

QList<CaseDraft> DialogReviewReplies::approvedDrafts() const
{
    QList<CaseDraft> out;
    for (const Row &r : m_rows) {
        if (!r.check || !r.check->isChecked()) continue;
        CaseDraft d = r.draft;
        if (r.edit) d.draft = r.edit->toPlainText().trimmed();
        if (!d.draft.isEmpty()) out.append(d);
    }
    return out;
}
