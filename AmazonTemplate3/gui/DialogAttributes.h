#ifndef DIALOGATTRIBUTES_H
#define DIALOGATTRIBUTES_H

#include <QDialog>

#include <QCoro/QCoroCore>

#include "fillers/FillerSelectable.h"

namespace Ui {
class DialogAttributes;
}

class TemplateFiller;
class AbstractCli;

class DialogAttributes : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAttributes(TemplateFiller *templateFiller, QWidget *parent = nullptr);
    ~DialogAttributes();
    static QCoro::Task<bool> editAttributes(TemplateFiller *templateFiller,
                                            const QString &title,
                                            const QString &message,
                                            const FillerSelectable::MissingValueInfo &info);

    // CLI used to suggest a fix before the manual dialog (nullptr = no suggestion).
    // Set by PaneGenTemplate's CLI combo box.
    static void setSuggestionCli(AbstractCli *cli);

public slots:
    void missingPossibleAdd();
    void missingPossibleRemove();
    void replaceAdd();
    void replaceRemove();
    void equivalentRemove();
    void flagsAdd();

private:
    static AbstractCli *SUGGESTION_CLI;
    Ui::DialogAttributes *ui;
    TemplateFiller *m_templateFiller;
    void _connectSlots();
    void _setupReplacedDelegate();
};

#endif // DIALOGATTRIBUTES_H
