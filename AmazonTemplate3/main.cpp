#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "../../common/workingdirectory/DialogOpenConfig.h"
#include "../../common/types/types.h"

#include "gui/MainWindow.h"

#include <QApplication>
#include <QSet>

int main(int argc, char *argv[])
{
    qRegisterMetaType<QSet<QString>>();
    qRegisterMetaType<QHash<QString, QSet<QString>>>();

    QCoreApplication::setOrganizationName("Icinger Power");
    QCoreApplication::setOrganizationDomain("ecomelitepro.com");
    QCoreApplication::setApplicationName("Amazon Template 3");

    QApplication a(argc, argv);
    WorkingDirectoryManager::instance()->installDarkOrangePalette();
    DialogOpenConfig dialog;
    dialog.exec();
    if (dialog.wasRejected())
    {
        return 0;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
