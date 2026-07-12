#include "ProgressDialog.h"

#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

QDialog *makeProgressDlg(QWidget *parent, const QString &title, ProgressDlgHandles *h)
{
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(title);
    dlg->resize(600, 440);

    auto *vLayout = new QVBoxLayout(dlg);

    auto *statusLabel = new QLabel(QObject::tr("Starting…"), dlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    vLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(dlg);
    progressBar->setRange(0, 0); // indeterminate spinner
    vLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(dlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    vLayout->addWidget(logEdit);

    auto *hLayout  = new QHBoxLayout();
    auto *copyBtn  = new QPushButton(QObject::tr("Copy log"), dlg);
    hLayout->addWidget(copyBtn);
    hLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    hLayout->addWidget(closeBtns);
    vLayout->addLayout(hLayout);

    QObject::connect(copyBtn, &QPushButton::clicked, dlg, [logEdit]() {
        QGuiApplication::clipboard()->setText(logEdit->toPlainText());
    });
    QObject::connect(closeBtns, &QDialogButtonBox::rejected, dlg, &QDialog::close);

    h->bar      = progressBar;
    h->log      = logEdit;
    h->status   = statusLabel;
    h->closeBtn = closeBtn;

    return dlg;
}
