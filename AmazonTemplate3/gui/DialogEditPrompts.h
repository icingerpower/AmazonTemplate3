#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QTextEdit>
#include <QSpinBox>

class APlusWorkflow;

class DialogEditPrompts : public QDialog
{
    Q_OBJECT
public:
    DialogEditPrompts(APlusWorkflow *wf, QWidget *parent = nullptr);

    void accept() override;
    void reject() override;

private:
    void _loadPrompts();
    bool _isChanged() const;

    APlusWorkflow *m_wf;
    QTabWidget    *m_tabs;

    struct StepWidgets {
        QTextEdit *desktop;
        QTextEdit *mobile;
        QSpinBox  *version;
    };
    QList<StepWidgets> m_stepWidgets;
};
