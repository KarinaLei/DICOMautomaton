//Metadata.h.

#pragma once

#include <string>
#include <map>
#include <optional>
#include <list>
#include <initializer_list>
#include <functional>
#include <regex>

#include "YgorString.h"
#include "YgorMath.h"

#include "Structs.h"


using metadata_map_t = std::map<std::string,std::string>;

//Generic helper functions.
std::string Generate_Random_UID(long int len);

std::string Generate_Random_Int_Str(long int low, long int high);


// Retrieve the metadata value corresponding to a given key, but only if present and it can be converted to type T.
template <class T>
std::optional<T>
get_as(const metadata_map_t &map,
       const std::string &key);


// Interpret the metadata value corresponding to a given key as a numeric type T, if present and convertible apply the
// given function, and then replace the existing value. The updated value (if a replacement is performed) is
// returned.
//
// This function will not add a new metadata key. It will only update an existing key when it can be convertible to T.
template <class T>
std::optional<T>
apply_as(metadata_map_t &map,
         const std::string &key,
         const std::function<T(T)> &f);


// Sets of DICOM metadata key-value elements that provide reasonable defaults or which draw from the provided reference
// map, if available.
//
// Note that these roughly correspond to DICOM modules/macros.
metadata_map_t coalesce_metadata_sop_common(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_patient(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_general_study(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_general_series(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_patient_study(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_frame_of_reference(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_general_equipment(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_general_image(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_image_plane(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_image_pixel(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_multi_frame(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_voi_lut(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_modality_lut(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_rt_dose(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_ct_image(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_rt_image(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_rt_plan(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_mr_image(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_mr_diffusion(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_mr_spectroscopy(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_mr_private_siemens_diffusion(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_misc(const metadata_map_t &ref);


// Modality-specific wrappers.
metadata_map_t coalesce_metadata_for_lsamp(const metadata_map_t &ref);
metadata_map_t coalesce_metadata_for_rtdose(const metadata_map_t &ref);

