#include "DialogEditPrompts.h"
#include "aplus/APlusWorkflow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QMessageBox>

DialogEditPrompts::DialogEditPrompts(APlusWorkflow *wf, QWidget *parent)
    : QDialog(parent)
    , m_wf(wf)
{
    setWindowTitle(tr("Edit prompts — %1").arg(wf->name()));
    resize(800, 600);

    auto *layout = new QVBoxLayout(this);

    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs);

    for (int i = 0; i < wf->stepCount(); ++i) {
        auto *tab = new QWidget(this);
        auto *tabLayout = new QVBoxLayout(tab);

        auto *vLayout = new QHBoxLayout();
        vLayout->addWidget(new QLabel(tr("Versions to generate:"), tab));
        auto *vSpin = new QSpinBox(tab);
        vSpin->setRange(1, 10);
        vSpin->setValue(wf->versionCount(i));
        vLayout->addWidget(vSpin);
        vLayout->addStretch();
        tabLayout->addLayout(vLayout);

        auto *desktopLabel = new QLabel(tr("Desktop prompt:"), tab);
        tabLayout->addWidget(desktopLabel);
        auto *desktopEdit = new QTextEdit(tab);
        desktopEdit->setPlainText(wf->defaultDesktopPrompt(i));
        tabLayout->addWidget(desktopEdit);

        auto *mobileLabel = new QLabel(tr("Mobile prompt:"), tab);
        tabLayout->addWidget(mobileLabel);
        auto *mobileEdit = new QTextEdit(tab);
        mobileEdit->setPlainText(wf->defaultMobilePrompt(i));
        tabLayout->addWidget(mobileEdit);

        m_stepWidgets.append(StepWidgets{desktopEdit, mobileEdit, vSpin});
        m_tabs->addTab(tab, wf->stepName(i));
    }

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btns, &QDialogButtonBox::accepted, this, &DialogEditPrompts::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &DialogEditPrompts::reject);
    layout->addWidget(btns);
}

void DialogEditPrompts::accept()
{
    if (_isChanged()) {
        auto res = QMessageBox::question(this, tr("Save changes?"),
            tr("Do you want to save the changed prompts for workflow \"%1\"?").arg(m_wf->name()),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (res == QMessageBox::Yes) {
            for (int i = 0; i < m_stepWidgets.size(); ++i) {
                m_wf->setVersionCount(i, m_stepWidgets[i].version->value());
                m_wf->setDefaultDesktopPrompt(i, m_stepWidgets[i].desktop->toPlainText().trimmed());
                m_wf->setDefaultMobilePrompt(i, m_stepWidgets[i].mobile->toPlainText().trimmed());
            }
            QDialog::accept();
        } else if (res == QMessageBox::No) {
            QDialog::reject();
        }
        // Cancel does nothing, keeping the dialog open.
    } else {
        QDialog::accept();
    }
}

void DialogEditPrompts::reject()
{
    if (_isChanged()) {
        auto res = QMessageBox::question(this, tr("Discard changes?"),
            tr("You have unsaved changes. Do you want to save them before closing?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (res == QMessageBox::Yes) {
            accept();
        } else if (res == QMessageBox::No) {
            QDialog::reject();
        }
    } else {
        QDialog::reject();
    }
}

bool DialogEditPrompts::_isChanged() const
{
    for (int i = 0; i < m_stepWidgets.size(); ++i) {
        if (m_stepWidgets[i].version->value() != m_wf->versionCount(i))
            return true;
        if (m_stepWidgets[i].desktop->toPlainText().trimmed() != m_wf->defaultDesktopPrompt(i))
            return true;
        if (m_stepWidgets[i].mobile->toPlainText().trimmed() != m_wf->defaultMobilePrompt(i))
            return true;
    }
    return false;
}
