#pragma once
#include <QList>
class AbstractSizeCategory;

class SizeTableGenerator {
public:
    static QList<const AbstractSizeCategory*> allCategories();
};
