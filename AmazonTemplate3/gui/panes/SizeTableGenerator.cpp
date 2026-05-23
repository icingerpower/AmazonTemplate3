#include "SizeTableGenerator.h"
#include "sizecategories/ClothingWomenCategory.h"
#include "sizecategories/ClothingMenCategory.h"
#include "sizecategories/ShoesWomenCategory.h"
#include "sizecategories/ShoesMenCategory.h"
#include "sizecategories/ClothingKidCategory.h"

QList<const AbstractSizeCategory*> SizeTableGenerator::allCategories()
{
    static const ClothingWomenCategory clothingWomen;
    static const ClothingMenCategory   clothingMen;
    static const ShoesWomenCategory    shoesWomen;
    static const ShoesMenCategory      shoesMen;
    static const ClothingKidCategory   clothingKid;
    return {&clothingWomen, &clothingMen, &shoesWomen, &shoesMen, &clothingKid};
}
