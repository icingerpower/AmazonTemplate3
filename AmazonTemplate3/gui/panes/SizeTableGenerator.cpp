#include "SizeTableGenerator.h"
#include "sizecategories/ClothingWomenCategory.h"
#include "sizecategories/ClothingMenCategory.h"
#include "sizecategories/ShoesWomenCategory.h"
#include "sizecategories/ShoesMenCategory.h"
#include "sizecategories/ClothingKidCategory.h"
#include "sizecategories/TopWomenCategory.h"
#include "sizecategories/TopMenCategory.h"
#include "sizecategories/RectangleCategory.h"

QList<const AbstractSizeCategory*> SizeTableGenerator::allCategories()
{
    static const ClothingWomenCategory clothingWomen;
    static const ClothingMenCategory   clothingMen;
    static const TopWomenCategory      topWomen;
    static const TopMenCategory        topMen;
    static const ShoesWomenCategory    shoesWomen;
    static const ShoesMenCategory      shoesMen;
    static const ClothingKidCategory   clothingKid;
    static const RectangleCategory     rectangle;
    return {&clothingWomen, &clothingMen, &topWomen, &topMen,
            &shoesWomen, &shoesMen, &clothingKid, &rectangle};
}
