

#include <list>
#include <functional>
#include <limits>
#include <map>
#include <cmath>

#include <experimental/any>

#include "YgorMisc.h"
#include "YgorMath.h"
#include "YgorImages.h"


bool DCEMRIS0MapV2(planar_image_collection<float,double>::images_list_it_t  local_img_it,
                   std::list<std::reference_wrapper<planar_image_collection<float,double>>> external_imgs,
                   std::list<std::reference_wrapper<contour_collection<double>>>, 
                   std::experimental::any );



