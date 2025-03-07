
#pragma once

#include <exception>
#include <functional>
#include <optional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <string>    
#include <vector>

#include <cstdlib>            //Needed for exit() calls.
#include <utility>            //Needed for std::pair.

#include "YgorMath.h"         //Needed for samples_1D.


// A copy of this structure will be passed to the algorithm. It should be used to set parameters, if there are any, that affect
// how the algorithm is performed. It generally should not be used to pass information back to the caller.
struct AlignViaABCParams {

    // A placeholder parameter.
    double xyz = 1.0;

};

// The aim of the algorithm is to extract a transformation. Since we might want to apply this transformation to
// other objects (e.g., surface meshes, other images) we need to somehow return this transformation as a function
// that can be evaluated and passed around. A good way to do this is to split the transformation into a set of
// numbers and an algorithm that can make sense of the numbers. For example, a polynomial can be split into a set of
// coefficients and a generic algorithm that can be evaluated for any set of coefficients. Another example is a
// matrix, say an Affine matrix, which we can write to a file as a set of coefficients that can be applied to the
// positions of each point.
//
// However, actually extracting the transformation is an implementation detail. You should focus first on getting the
// deformable registration algorithm working first before worrying about how to extract the transformation.
struct AlignViaABCTransform {

    // A placeholder parameter.
    double xyz = 1.0;

};

// This is the function the performs the registration algorithm. If there is no transformation, or the algorithm fails,
// the result is un-set. If it succeeds, the result holds the transformation.
std::optional<AlignViaABCTransform>
AlignViaABC(AlignViaABCParams & params,
            const planar_image_collection<float, double> & moving,
            const planar_image_collection<float, double> & stationary );

