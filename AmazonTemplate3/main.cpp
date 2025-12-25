#include "../../common/types/types.h"

#include "gui/MainWindow.h"

#include <QApplication>
#include <QSet>

int main(int argc, char *argv[])
{
    qRegisterMetaType<QSet<QString>>();

    QCoreApplication::setOrganizationName("Icinger Power");
    QCoreApplication::setOrganizationDomain("ecomelitepro.com");
    QCoreApplication::setApplicationName("Amazon Template 2");

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
