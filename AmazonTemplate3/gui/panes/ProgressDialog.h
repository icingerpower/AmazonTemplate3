#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QPointer>
#include <QString>

class QDialog;
class QWidget;
class QProgressBar;
class QTextEdit;
class QLabel;
class QPushButton;

// Handles into a progress dialog built by makeProgressDlg(). QPointer so callers
// can safely null-check after the dialog is closed (WA_DeleteOnClose).
struct ProgressDlgHandles {
    QPointer<QProgressBar> bar;
    QPointer<QTextEdit>    log;
    QPointer<QLabel>       status;
    QPointer<QPushButton>  closeBtn;
};

// Build a modal-less progress dialog with a status line, indeterminate bar,
// a scrolling log, a "Copy log" button and a (initially disabled) Close button.
// The dialog is WA_DeleteOnClose; *h is filled with widget handles.
QDialog *makeProgressDlg(QWidget *parent, const QString &title, ProgressDlgHandles *h);

#endif // PROGRESSDIALOG_H
