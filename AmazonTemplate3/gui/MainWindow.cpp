#include "MainWindow.h"
#include "./ui_MainWindow.h"
#include "panes/PaneGenTemplate.h"
#include "panes/PaneSizing.h"
#include "panes/PaneSettings.h"
#include "AbstractCli.h"

#include <QDir>
#include <QStandardPaths>
#include "../../common/workingdirectory/WorkingDirectoryManager.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Extra directories to search beyond the inherited PATH.
    // Covers nvm-managed Node binaries (~/.nvm/versions/node/*/bin)
    // and user-local installs (~/.local/bin).
    QStringList extraPaths;
    const QString home = QDir::homePath();
    extraPaths << home + QStringLiteral("/.local/bin");
    const QDir nvmNodeDir(home + QStringLiteral("/.nvm/versions/node"));
    for (const QString &ver : nvmNodeDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        extraPaths << nvmNodeDir.filePath(ver + QStringLiteral("/bin"));

    // Synchronous PATH-based CLI availability check. This replaces the previous
    // QCoro-based AvailableCliTable, which crashed at runtime due to a GCC 13
    // coroutine codegen bug in libQCoro6Core.a built at -O2/-O3.
    for (AbstractCli *cli : AbstractCli::ALL_CLIS()) {
        if (!QStandardPaths::findExecutable(cli->getExecutable()).isEmpty()
                || !QStandardPaths::findExecutable(cli->getExecutable(), extraPaths).isEmpty())
            m_availableClis.append(cli);
    }

    ui->tabWidget->addTab(new PaneGenTemplate(this), tr("Gen Template"));

    auto *paneSizing = new PaneSizing(this);
    paneSizing->setWorkingDir(WorkingDirectoryManager::instance()->workingDir());
    paneSizing->setAvailableClis(m_availableClis);
    ui->tabWidget->addTab(paneSizing, tr("Sizing"));

    auto *paneSettings = new PaneSettings(this);
    paneSettings->setAvailableClis(m_availableClis);
    ui->tabWidget->addTab(paneSettings, tr("Settings"));
}

MainWindow::~MainWindow()
{
    delete ui;
}
