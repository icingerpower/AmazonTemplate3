#include "MainWindow.h"
#include "./ui_MainWindow.h"
#include "panes/PaneGenTemplate.h"
#include "panes/PaneSettings.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->tabWidget->addTab(new PaneGenTemplate(this), tr("Gen Template"));
    ui->tabWidget->addTab(new PaneSettings(this), tr("Settings"));
}

MainWindow::~MainWindow()
{
    delete ui;
}
