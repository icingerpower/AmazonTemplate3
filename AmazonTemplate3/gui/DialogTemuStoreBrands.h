#ifndef DIALOGTEMUSTOREBRANDS_H
#define DIALOGTEMUSTOREBRANDS_H

#include <QDialog>
#include <QCoro/QCoroTask>

class QTreeView;
class QLabel;
class TreeTemuStoreBrands;

// View/edit the store → brands mapping (TreeTemuStoreBrands). The
// manufacturer / GSPR representative combo choices are fetched from each
// store's Temu account when the dialog opens.
class DialogTemuStoreBrands : public QDialog
{
    Q_OBJECT

public:
    explicit DialogTemuStoreBrands(QWidget *parent = nullptr);

private:
    QCoro::Task<void> _fetchEntityChoices();
    void _addBrand();
    void _removeBrand();

    TreeTemuStoreBrands *m_model  = nullptr;
    QTreeView           *m_tree   = nullptr;
    QLabel              *m_status = nullptr;
};

#endif // DIALOGTEMUSTOREBRANDS_H
