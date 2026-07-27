#ifndef DIALOGSUGGESTEQUIVALENT_H
#define DIALOGSUGGESTEQUIVALENT_H

#include <QDialog>

#include "fillers/FillerSelectable.h"

class AbstractCli;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// Shown when a "no equivalent value" error occurs during template filling.
// First asks the selected AI CLI (which runs in the working directory, so it
// can grep the attributeEquivalent.csv / attributeReplacement.csv knowledge
// base) to diagnose the problem and suggest the correct equivalent value.
// The user can then validate the suggestion, pick/type another allowed value,
// or fall back to the full manual attributes dialog.
class DialogSuggestEquivalent : public QDialog
{
    Q_OBJECT

public:
    DialogSuggestEquivalent(AbstractCli *cli,
                            const QString &title,
                            const QString &message,
                            const FillerSelectable::MissingValueInfo &info,
                            QWidget *parent = nullptr);

    // Exact allowed value chosen by the user (empty if none).
    QString selectedValue() const;

    // True when the user clicked "Edit manually…" to open DialogAttributes.
    bool editManuallyRequested() const { return m_editManually; }

public slots:
    void accept() override;

private:
    void _askCli(const QString &message);
    void _onCliReply(const QString &output);

    AbstractCli *m_cli;
    FillerSelectable::MissingValueInfo m_info;
    bool m_editManually = false;

    QLabel *m_statusLabel;
    QPlainTextEdit *m_diagnosisEdit;
    QComboBox *m_comboValue;
    QPushButton *m_validateButton;
};

#endif // DIALOGSUGGESTEQUIVALENT_H
