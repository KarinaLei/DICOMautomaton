//Imebra_Shim.cc - DICOMautomaton 2012-2013. Written by hal clark.
//
//This file is supposed to be a kind of 'shim' or 'wrapper' around the Imebra library.
//
//Why is it needed? **Strictly for convenice.** Compilation of the Imembra library requires
// a lot of time, and the library must be compiled without linking parts of code which 
// use Imebra stuff. This is not a design flaw, but it is an inconvenience when one 
// wants to (write) (compile) (test), (write) (compile) (test), etc.. because it becomes
// more like (write) (       c   o   m   p   i   l   e       ) (test), etc..
//
//NOTE: that we do not properly handle unicode. Everything is stuffed into a std::string.
//

#include <YgorImages.h>
#include <algorithm>      //Needed for std::sort.
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <exception>
#include <experimental/optional>
#include <functional>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>         //Needed for std::unique_ptr.
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>        //Needed for std::pair.
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-parameter"
#include "imebra.h"

#pragma GCC diagnostic pop

#include "Imebra_Shim.h"
#include "Structs.h"
#include "YgorContainers.h" //Needed for 'bimap' class.
#include "YgorMath.h"       //Needed for 'vec3' class.
#include "YgorMisc.h"       //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorString.h"     //Needed for Canonicalize_String2().

//------------------ General ----------------------
//This is used to grab the contents of a single DICOM tag. It can be used for whatever. Some routines
// use it to grab specific things. Each invocation involves disk access and file parsing.
//
//NOTE: On error, the output will be an empty string.
std::string get_tag_as_string(const std::string &filename, size_t U, size_t L){
    using namespace puntoexe;
    ptr<puntoexe::stream> readStream(new puntoexe::stream);
    readStream->openFile(filename.c_str(), std::ios::in);
    if(readStream == nullptr) return std::string("");

    ptr<puntoexe::streamReader> reader(new puntoexe::streamReader(readStream));
    ptr<imebra::dataSet> TopDataSet = imebra::codecs::codecFactory::getCodecFactory()->load(reader);
    if(TopDataSet == nullptr) return std::string("");
    return TopDataSet->getString(U, 0, L, 0);
}

std::string get_modality(const std::string &filename){
    //Should exist in each DICOM file.
    return get_tag_as_string(filename,0x0008,0x0060);
}

std::string get_patient_ID(const std::string &filename){
    //Should exist in each DICOM file.
    return get_tag_as_string(filename,0x0010,0x0020);
}

//------------------ General ----------------------
//Mass top-level tag enumeration, for ingress into database.
//
//NOTE: May not be complete. Add additional tags as needed!
std::map<std::string,std::string> get_metadata_top_level_tags(const std::string &filename){
    std::map<std::string,std::string> out;
    const auto ctrim = CANONICALIZE::TRIM_ENDS;

    //Attempt to parse the DICOM file and harvest the elements of interest. We are only interested in
    // top-level elements specifying metadata (i.e., not pixel data) and will not need to recurse into 
    // any DICOM sequences.
    puntoexe::ptr<puntoexe::stream> readStream(new puntoexe::stream);
    readStream->openFile(filename.c_str(), std::ios::in);
    if(readStream == nullptr){
        FUNCWARN("Could not parse file '" << filename << "'. Is it valid DICOM? Cannot continue");
        return out;
    }

    puntoexe::ptr<puntoexe::streamReader> reader(new puntoexe::streamReader(readStream));
    puntoexe::ptr<puntoexe::imebra::dataSet> tds = puntoexe::imebra::codecs::codecFactory::getCodecFactory()->load(reader);

    //We pull out all the data we need as strings. For single element strings, the SQL engine can directly perform
    // the type casting. The benefit of this is twofold: (1) the SQL engine hides the checking code, simplifying
    // this implementation, and (2) the SQL engine can appropriately handle SQL NULLs without extra conditionals
    // and workarounds here.
    //
    // Multi-element data are a little trickier, especially with Imebra apparently unable to get the whole element
    // in the raw DICOM representation as string. We break these items into individual elements and then delimit 
    // such that they appear just as they would if read from the DICOM file (with '\'s separating) as needed.
    //
    // To ensure that there are no duplicated tags (at this single level) we exclusively use 'emplace'.
    //
    // TODO: Properly handle missing strings. In many cases, Imebra will feel obligated to return a NON-EMPTY string
    //       if a tag is missing. For example, the date '0000-00-00' is returned instead of an empty string for date
    //       fields. Instead of handling these issues specifically, use the Imebra member function
    //           ptr< data > puntoexe::imebra::dataSet::getTag(imbxUint16 groupId, imbxUint16 order,
    //                                                         imbxUint16 tagId, bool bCreate = false)
    //       to check if the tag is present before asking for a string. Then, of course, we need to check later if
    //       the item is present. If using a map, I think the default empty string will suffice to communicate this
    //       info.
    //
    // TODO: It would be better to handle some VR's directly rather than converting to string and back. For example
    //       doubles. There is currently a lot of unnecessary loss of precision, and I doubt NaN's and Inf's are handled
    //       correctly, since they are locale-dependent in several ways.
    //
    
    auto insert_as_string_if_nonempty = [&out,&ctrim,&tds](uint16_t group, uint16_t tag,
                                                           std::string name ) -> void {
        const uint32_t first_order = 0; // Always zero for modern DICOM files.
        const uint32_t first_element = 0; // Usually zero, but several tags can store multiple elements.

        //Check if the tag has already been found.
        if(out.count(name) != 0) return;

        //Check if the tag is present in the file.
        const bool create_if_not_found = false;
        const auto ptr = tds->getTag(group, first_order, tag, create_if_not_found);
        if(ptr == nullptr) return;

        //Add the first element.
        const auto str = tds->getString(group, first_order, tag, first_element);
        const auto trimmed = Canonicalize_String2(str, ctrim);
        if(!trimmed.empty()) out.emplace(name, trimmed);

        //Check if there are additional elements.
        try{
            const uint32_t buffer_id = 0;
            auto dh = tds->getDataHandler(group, first_order, tag, buffer_id, create_if_not_found);
            for(uint32_t i = 1 ; i < dh->getSize(); ++i){
                const auto str = tds->getString(group, first_order, tag, first_element + i);
                const auto trimmed = Canonicalize_String2(str, ctrim);
                if(!trimmed.empty()){
                    out[name] += R"***(\)***"_s + trimmed;
                }else{
                    return;
                }
            }
        }catch(const std::exception &){ }

        return;
    };

    auto insert_seq_tag_as_string_if_nonempty = [&out,&ctrim,&tds](uint16_t seq_group, uint16_t seq_tag, std::string seq_name,
                                                                   uint16_t tag_group, uint16_t tag_tag, std::string tag_name) -> void {
        const uint32_t first_order = 0; // Always zero for modern DICOM files.
        const uint32_t first_element = 0; // Usually zero, but several tags can store multiple elements.
        const auto full_name = seq_name + R"***(/)***"_s + tag_name;

        //Check if the tag has already been found.
        if(out.count(full_name) != 0) return;

        //Check if the sequence can be found.
        const auto seq_ptr = tds->getSequenceItem(seq_group, first_order, seq_tag, first_element);
        if(seq_ptr == nullptr) return;

        //Check if the tag is present in the sequence.
        const bool create_if_not_found = false;
        const auto tag_ptr = seq_ptr->getTag(tag_group, first_order, tag_tag, create_if_not_found);
        if(tag_ptr == nullptr) return;

        //Add the first element.
        const auto str = seq_ptr->getString(tag_group, first_order, tag_tag, first_element);
        const auto trimmed = Canonicalize_String2(str, ctrim);
        if(!trimmed.empty()) out.emplace(full_name, trimmed);

        //Check if there are additional elements.
        try{
            const uint32_t buffer_id = 0;
            auto dh = seq_ptr->getDataHandler(tag_group, first_order, tag_tag, buffer_id, create_if_not_found);
            for(uint32_t i = 1 ; i < dh->getSize(); ++i){
                const auto str = seq_ptr->getString(tag_group, first_order, tag_tag, first_element + i);
                const auto trimmed = Canonicalize_String2(str, ctrim);
                if(!trimmed.empty()){
                    out[full_name] += R"***(\)***"_s + trimmed;
                }else{
                    return;
                }
            }
        }catch(const std::exception &){ }

        return;
    };

    // seq_group,seq_tag,seq_name or tag_group,tag_tag,tag_name.
    struct path_node {
        uint16_t group   = 0; // The first number in common DICOM tag parlance.
        uint16_t tag     = 0; // The second number in common DICOM tag parlance.

        std::string name = "";

        uint32_t order   = 0; // Rarely used in modern DICOM. Almost always going to be zero.
        uint32_t element = 0; // Used to enumerate items in lists.

    };

    std::function< void (std::deque<path_node>,
                         puntoexe::ptr<puntoexe::imebra::dataSet>,
                         std::string) > insert_seq_vec_tag_as_string_if_nonempty_proto;

    insert_seq_vec_tag_as_string_if_nonempty_proto = [&out,&ctrim,&tds,&insert_seq_vec_tag_as_string_if_nonempty_proto](
            std::deque<path_node> apath, //Remaining path elements (relative to base_node).
            puntoexe::ptr<puntoexe::imebra::dataSet> base_node = nullptr, // The current spot in the DICOM path hierarchy.
            std::string base_fullpath = "" // The current full path name for the base node.
         ) -> void {

        if(base_node == nullptr) base_node = tds; // Default to DICOM file top level.

        // This routine can access tags with deeply-nested sequences in their path, including paths with multiple items.

        if(apath.empty()) throw std::logic_error("Reached DICOM path terminus node.");

        // Extract info about the current node.
        const auto this_node = apath.front();
        apath.pop_front();

        //Extract the full path name of the tag.
        if(!base_fullpath.empty()) base_fullpath += R"***(/)***"_s;
        base_fullpath += this_node.name;

        //If this is a sequence, jump to the sequence node as the new base and recurse.
        if(!apath.empty()){

            //Check if there is a second element.
            auto next_seq_ptr = base_node->getSequenceItem(this_node.group, this_node.order,
                                                           this_node.tag,   this_node.element + 1);
            if(next_seq_ptr != nullptr){

                //Cycle through all elements, suffixing with the element number.
                for(uint32_t i = 0; i < 10000 ; ++i){
                    const auto this_fullpath = base_fullpath + "#" + std::to_string(i);
                    auto seq_ptr = base_node->getSequenceItem(this_node.group, this_node.order,
                                                              this_node.tag,   this_node.element + i);
                    if(seq_ptr != nullptr){
                        insert_seq_vec_tag_as_string_if_nonempty_proto(apath, seq_ptr, this_fullpath);
                    }else{
                        break;
                    }
                }
            }else{
                //This sequence does not appear to be part of a list. So just jump to it.
                auto seq_ptr = base_node->getSequenceItem(this_node.group, this_node.order,
                                                          this_node.tag,   this_node.element);
                insert_seq_vec_tag_as_string_if_nonempty_proto(apath, seq_ptr, base_fullpath);
            }

        //Otherwise, this is a leaf node.
        }else{
            //Check if the tag has already been found.
            if(out.count(base_fullpath) != 0) return;
            
            //Check if the tag is present in the sequence.
            const bool create_if_not_found = false;
            const auto tag_ptr = base_node->getTag(this_node.group, this_node.order,
                                                   this_node.tag,
                                                   create_if_not_found);
            if(tag_ptr == nullptr) return;

            //Add the first element.
            const auto str = base_node->getString(this_node.group, this_node.order,
                                                  this_node.tag,   this_node.element);
            const auto trimmed = Canonicalize_String2(str, ctrim);
            if(!trimmed.empty()) out.emplace(base_fullpath, trimmed);

            //Check if there are additional elements.
            for(uint32_t i = 1 ; i < 10000 ; ++i){
                try{
                    const auto str = base_node->getString(this_node.group, this_node.order,
                                                          this_node.tag,   this_node.element + i);
                    const auto trimmed = Canonicalize_String2(str, ctrim);
                    if(!trimmed.empty()){
                        out[base_fullpath] += R"***(\)***"_s + trimmed;
                    }else{
                        return;
                    }
                }catch(const std::exception &){
                    return;
                }
            }
        }
        return;
    };

    auto insert_seq_vec_tag_as_string_if_nonempty = std::bind(insert_seq_vec_tag_as_string_if_nonempty_proto,
                                                              std::placeholders::_1,
                                                              tds, "");
    
    //SOP Common Module.
    insert_as_string_if_nonempty(0x0008, 0x0016, "SOPClassUID");
    insert_as_string_if_nonempty(0x0008, 0x0018, "SOPInstanceUID");
    insert_as_string_if_nonempty(0x0008, 0x0005, "SpecificCharacterSet");
    insert_as_string_if_nonempty(0x0008, 0x0012, "InstanceCreationDate");
    insert_as_string_if_nonempty(0x0008, 0x0013, "InstanceCreationTime");
    insert_as_string_if_nonempty(0x0008, 0x0014, "InstanceCreatorUID");
    insert_as_string_if_nonempty(0x0008, 0x0114, "CodingSchemeExternalUID");
    insert_as_string_if_nonempty(0x0020, 0x0013, "InstanceNumber");

    //Patient Module.
    insert_as_string_if_nonempty(0x0010, 0x0010, "PatientsName");
    insert_as_string_if_nonempty(0x0010, 0x0020, "PatientID");
    insert_as_string_if_nonempty(0x0010, 0x0030, "PatientsBirthDate");
    insert_as_string_if_nonempty(0x0010, 0x0040, "PatientsGender");

    //General Study Module.
    insert_as_string_if_nonempty(0x0020, 0x000D, "StudyInstanceUID");
    insert_as_string_if_nonempty(0x0008, 0x0020, "StudyDate");
    insert_as_string_if_nonempty(0x0008, 0x0030, "StudyTime");
    insert_as_string_if_nonempty(0x0008, 0x0090, "ReferringPhysiciansName");
    insert_as_string_if_nonempty(0x0020, 0x0010, "StudyID");
    insert_as_string_if_nonempty(0x0008, 0x0050, "AccessionNumber");
    insert_as_string_if_nonempty(0x0008, 0x1030, "StudyDescription");


    //General Series Module.
    insert_as_string_if_nonempty(0x0008, 0x0060, "Modality");
    insert_as_string_if_nonempty(0x0020, 0x000E, "SeriesInstanceUID");
    insert_as_string_if_nonempty(0x0020, 0x0011, "SeriesNumber");
    insert_as_string_if_nonempty(0x0008, 0x0021, "SeriesDate");
    insert_as_string_if_nonempty(0x0008, 0x0031, "SeriesTime");
    insert_as_string_if_nonempty(0x0008, 0x103E, "SeriesDescription");
    insert_as_string_if_nonempty(0x0018, 0x0015, "BodyPartExamined");
    insert_as_string_if_nonempty(0x0018, 0x5100, "PatientPosition");
    insert_as_string_if_nonempty(0x0040, 0x1001, "RequestedProcedureID");
    insert_as_string_if_nonempty(0x0040, 0x0009, "ScheduledProcedureStepID");
    insert_as_string_if_nonempty(0x0008, 0x1070, "OperatorsName");

    //Patient Study Module.
    insert_as_string_if_nonempty(0x0010, 0x1030, "PatientsMass");

    //Frame of Reference Module.
    insert_as_string_if_nonempty(0x0020, 0x0052, "FrameofReferenceUID");
    insert_as_string_if_nonempty(0x0020, 0x1040, "PositionReferenceIndicator");

    //General Equipment Module.
    insert_as_string_if_nonempty(0x0008, 0x0070, "Manufacturer");
    insert_as_string_if_nonempty(0x0008, 0x0080, "InstitutionName");
    insert_as_string_if_nonempty(0x0008, 0x1010, "StationName");
    insert_as_string_if_nonempty(0x0008, 0x1040, "InstitutionalDepartmentName");
    insert_as_string_if_nonempty(0x0008, 0x1090, "ManufacturersModelName");
    insert_as_string_if_nonempty(0x0018, 0x1020, "SoftwareVersions");

    //General Image Module.
    insert_as_string_if_nonempty(0x0020, 0x0013, "InstanceNumber");
    insert_as_string_if_nonempty(0x0020, 0x0020, "PatientOrientation");
    insert_as_string_if_nonempty(0x0008, 0x0023, "ContentDate");
    insert_as_string_if_nonempty(0x0008, 0x0033, "ContentTime");
    insert_as_string_if_nonempty(0x0008, 0x0008, "ImageType");
    insert_as_string_if_nonempty(0x0020, 0x0012, "AcquisitionNumber");
    insert_as_string_if_nonempty(0x0008, 0x0022, "AcquisitionDate");
    insert_as_string_if_nonempty(0x0008, 0x0032, "AcquisitionTime");
    insert_as_string_if_nonempty(0x0008, 0x2111, "DerivationDescription");
    //insert_as_string_if_nonempty(0x0008, 0x9215, "DerivationCodeSequence");
    insert_as_string_if_nonempty(0x0020, 0x1002, "ImagesInAcquisition");
    insert_as_string_if_nonempty(0x0020, 0x4000, "ImageComments");
    insert_as_string_if_nonempty(0x0028, 0x0300, "QualityControlImage");

    //Image Plane Module.
    insert_as_string_if_nonempty(0x0028, 0x0030, "PixelSpacing");
    insert_as_string_if_nonempty(0x0020, 0x0037, "ImageOrientationPatient");
    insert_as_string_if_nonempty(0x0020, 0x0032, "ImagePositionPatient");
    insert_as_string_if_nonempty(0x0018, 0x0050, "SliceThickness");
    insert_as_string_if_nonempty(0x0020, 0x1041, "SliceLocation");

    //Image Pixel Module.
    insert_as_string_if_nonempty(0x0028, 0x0002, "SamplesPerPixel");
    insert_as_string_if_nonempty(0x0028, 0x0004, "PhotometricInterpretation");
    insert_as_string_if_nonempty(0x0028, 0x0010, "Rows");
    insert_as_string_if_nonempty(0x0028, 0x0011, "Columns");
    insert_as_string_if_nonempty(0x0028, 0x0100, "BitsAllocated");
    insert_as_string_if_nonempty(0x0028, 0x0101, "BitsStored");
    insert_as_string_if_nonempty(0x0028, 0x0102, "HighBit");
    insert_as_string_if_nonempty(0x0028, 0x0103, "PixelRepresentation");
    //insert_as_string_if_nonempty(0x7FE0, 0x0010, "PixelData"); //Raw pixel bytes.
    insert_as_string_if_nonempty(0x0028, 0x0006, "PlanarConfiguration");
    insert_as_string_if_nonempty(0x0028, 0x0034, "PixelAspectRatio");

    //Multi-Frame Module.
    insert_as_string_if_nonempty(0x0028, 0x0008, "NumberOfFrames");
    insert_as_string_if_nonempty(0x0028, 0x0009, "FrameIncrementPointer");

    //Modality LUT Module.
    //insert_as_string_if_nonempty(0x0028, 0x3000, "ModalityLUTSequence");
    insert_as_string_if_nonempty(0x0028, 0x3002, "LUTDescriptor");
    insert_as_string_if_nonempty(0x0028, 0x3004, "ModalityLUTType");
    insert_as_string_if_nonempty(0x0028, 0x3006, "LUTData");
    insert_as_string_if_nonempty(0x0028, 0x1052, "RescaleIntercept");
    insert_as_string_if_nonempty(0x0028, 0x1053, "RescaleSlope");
    insert_as_string_if_nonempty(0x0028, 0x1054, "RescaleType");

    //RT Dose Module.
    insert_as_string_if_nonempty(0x0028, 0x0002, "SamplesPerPixel");
    insert_as_string_if_nonempty(0x0028, 0x0004, "PhotometricInterpretation");
    insert_as_string_if_nonempty(0x0028, 0x0100, "BitsAllocated");
    insert_as_string_if_nonempty(0x0028, 0x0101, "BitsStored");
    insert_as_string_if_nonempty(0x0028, 0x0102, "HighBit");
    insert_as_string_if_nonempty(0x0028, 0x0103, "PixelRepresentation");
    insert_as_string_if_nonempty(0x3004, 0x0002, "DoseUnits");
    insert_as_string_if_nonempty(0x3004, 0x0004, "DoseType");
    insert_as_string_if_nonempty(0x3004, 0x000a, "DoseSummationType");
    insert_as_string_if_nonempty(0x3004, 0x000e, "DoseGridScaling");

    insert_seq_tag_as_string_if_nonempty( 0x300C, 0x0002, "ReferencedRTPlanSequence",
                                          0x0008, 0x1150, "ReferencedSOPClassUID");
    insert_seq_tag_as_string_if_nonempty( 0x300C, 0x0002, "ReferencedRTPlanSequence",
                                          0x0008, 0x1155, "ReferencedSOPInstanceUID");
    insert_seq_tag_as_string_if_nonempty( 0x300C, 0x0020, "ReferencedFractionGroupSequence",
                                          0x300C, 0x0022, "ReferencedFractionGroupNumber");
    insert_seq_tag_as_string_if_nonempty( 0x300C, 0x0004, "ReferencedBeamSequence",
                                          0x300C, 0x0006, "ReferencedBeamNumber");

    //insert_as_string_if_nonempty(0x300C, 0x0002, "ReferencedRTPlanSequence");
    //insert_as_string_if_nonempty(0x0008, 0x1150, "ReferencedSOPClassUID");
    //insert_as_string_if_nonempty(0x0008, 0x1155, "ReferencedSOPInstanceUID");
    //insert_as_string_if_nonempty(0x300C, 0x0020, "ReferencedFractionGroupSequence");
    //insert_as_string_if_nonempty(0x300C, 0x0022, "ReferencedFractionGroupNumber");
    //insert_as_string_if_nonempty(0x300C, 0x0004, "ReferencedBeamSequence");
    //insert_as_string_if_nonempty(0x300C, 0x0006, "ReferencedBeamNumber");
    
    insert_seq_vec_tag_as_string_if_nonempty( std::deque<path_node>(
                                              { { 0x300C, 0x0002, "ReferencedRTPlanSequence" },
                                                { 0x300C, 0x0020, "ReferencedFractionGroupSequence" },
                                                { 0x300C, 0x0004, "ReferencedBeamSequence" },
                                                { 0x300C, 0x0006, "ReferencedBeamNumber" } }) );

    //RT Image Module.
    insert_as_string_if_nonempty(0x3002, 0x0002, "RTImageLabel");
    insert_as_string_if_nonempty(0x3002, 0x0004, "RTImageDescription");
    insert_as_string_if_nonempty(0x3002, 0x000a, "ReportedValuesOrigin");
    insert_as_string_if_nonempty(0x3002, 0x000c, "RTImagePlane");
    insert_as_string_if_nonempty(0x3002, 0x000d, "XRayImageReceptorTranslation");
    insert_as_string_if_nonempty(0x3002, 0x000e, "XRayImageReceptorAngle");
    insert_as_string_if_nonempty(0x3002, 0x0010, "RTImageOrientation");
    insert_as_string_if_nonempty(0x3002, 0x0011, "ImagePlanePixelSpacing");
    insert_as_string_if_nonempty(0x3002, 0x0012, "RTImagePosition");
    insert_as_string_if_nonempty(0x3002, 0x0020, "RadiationMachineName");
    insert_as_string_if_nonempty(0x3002, 0x0022, "RadiationMachineSAD");
    insert_as_string_if_nonempty(0x3002, 0x0026, "RTImageSID");
    insert_as_string_if_nonempty(0x3002, 0x0029, "FractionNumber");

    insert_as_string_if_nonempty(0x300a, 0x00b3, "PrimaryDosimeterUnit");
    insert_as_string_if_nonempty(0x300a, 0x011e, "GantryAngle");
    insert_as_string_if_nonempty(0x300a, 0x0120, "BeamLimitingDeviceAngle");
    insert_as_string_if_nonempty(0x300a, 0x0122, "PatientSupportAngle");
    insert_as_string_if_nonempty(0x300a, 0x0128, "TableTopVerticalPosition");
    insert_as_string_if_nonempty(0x300a, 0x0129, "TableTopLongitudinalPosition");
    insert_as_string_if_nonempty(0x300a, 0x012a, "TableTopLateralPosition");
    insert_as_string_if_nonempty(0x300a, 0x012c, "IsocenterPosition");

    insert_as_string_if_nonempty(0x300c, 0x0006, "ReferencedBeamNumber");
    insert_as_string_if_nonempty(0x300c, 0x0008, "StartCumulativeMetersetWeight");
    insert_as_string_if_nonempty(0x300c, 0x0009, "EndCumulativeMetersetWeight");
    insert_as_string_if_nonempty(0x300c, 0x0022, "ReferencedFractionGroupNumber");

    insert_seq_tag_as_string_if_nonempty( 0x3002, 0x0030, "ExposureSequence",
                                          0x0018, 0x0060, "KVP");
    insert_seq_tag_as_string_if_nonempty( 0x3002, 0x0030, "ExposureSequence",
                                          0x0018, 0x1150, "ExposureTime");
    insert_seq_tag_as_string_if_nonempty( 0x3002, 0x0030, "ExposureSequence",
                                          0x3002, 0x0032, "MetersetExposure");
    insert_seq_vec_tag_as_string_if_nonempty( std::deque<path_node>(
                                              { { 0x3002, 0x0030, "ExposureSequence" },
                                                { 0x300A, 0x00B6, "BeamLimitingDeviceSequence" },
                                                { 0x300A, 0x00B8, "RTBeamLimitingDeviceType" } }) );
    insert_seq_vec_tag_as_string_if_nonempty( std::deque<path_node>(
                                              { { 0x3002, 0x0030, "ExposureSequence" },
                                                { 0x300A, 0x00B6, "BeamLimitingDeviceSequence" },
                                                { 0x300A, 0x00BC, "NumberOfLeafJawPairs" } }) );
    insert_seq_vec_tag_as_string_if_nonempty( std::deque<path_node>(
                                              { { 0x3002, 0x0030, "ExposureSequence" },
                                                { 0x300A, 0x00B6, "BeamLimitingDeviceSequence" },
                                                { 0x300A, 0x011C, "LeafJawPositions" } }) );


    //Unclassified others...
    insert_as_string_if_nonempty(0x0018, 0x0020, "ScanningSequence");
    insert_as_string_if_nonempty(0x0018, 0x0021, "SequenceVariant");
    insert_as_string_if_nonempty(0x0018, 0x0022, "ScanOptions");
    insert_as_string_if_nonempty(0x0018, 0x0023, "MRAcquisitionType");

    insert_as_string_if_nonempty(0x2001, 0x100a, "SliceNumber");
    insert_as_string_if_nonempty(0x0054, 0x1330, "ImageIndex");
    insert_as_string_if_nonempty(0x0018, 0x0088, "SpacingBetweenSlices");

    insert_as_string_if_nonempty(0x0028, 0x0010, "Rows");
    insert_as_string_if_nonempty(0x0028, 0x0011, "Columns");
    insert_as_string_if_nonempty(0x3004, 0x000C, "GridFrameOffsetVector");

    insert_as_string_if_nonempty(0x0020, 0x0100, "TemporalPositionIdentifier");
    insert_as_string_if_nonempty(0x0020, 0x9128, "TemporalPositionIndex");
    //insert_seq_vec_tag_as_string_if_nonempty(std::deque<path_node>(
    //                                         { { 0x0020, 0x9128, "TemporalPositionIndex" } }) );
    insert_as_string_if_nonempty(0x0020, 0x0105, "NumberofTemporalPositions");

    insert_as_string_if_nonempty(0x0020, 0x0110, "TemporalResolution");
    insert_as_string_if_nonempty(0x0054, 0x1300, "FrameReferenceTime");
    insert_as_string_if_nonempty(0x0018, 0x1063, "FrameTime");
    insert_as_string_if_nonempty(0x0018, 0x1060, "TriggerTime");
    insert_as_string_if_nonempty(0x0018, 0x1069, "TriggerTimeOffset");

    insert_as_string_if_nonempty(0x0040, 0x0244, "PerformedProcedureStepStartDate");
    insert_as_string_if_nonempty(0x0040, 0x0245, "PerformedProcedureStepStartTime");
    insert_as_string_if_nonempty(0x0040, 0x0250, "PerformedProcedureStepEndDate");
    insert_as_string_if_nonempty(0x0040, 0x0251, "PerformedProcedureStepEndTime");

    insert_as_string_if_nonempty(0x0018, 0x1152, "Exposure");
    insert_as_string_if_nonempty(0x0018, 0x1150, "ExposureTime");
    insert_as_string_if_nonempty(0x0018, 0x1153, "ExposureInMicroAmpereSeconds");
    insert_as_string_if_nonempty(0x0018, 0x1151, "XRayTubeCurrent");


    insert_as_string_if_nonempty(0x0018, 0x0080, "RepetitionTime");
    insert_as_string_if_nonempty(0x0018, 0x0081, "EchoTime");
    insert_as_string_if_nonempty(0x0018, 0x0083, "NumberofAverages");
    insert_as_string_if_nonempty(0x0018, 0x0084, "ImagingFrequency");
    insert_as_string_if_nonempty(0x0018, 0x0085, "ImagedNucleus");
    insert_as_string_if_nonempty(0x0018, 0x0086, "EchoNumbers");
    insert_as_string_if_nonempty(0x0018, 0x0087, "MagneticFieldStrength");
    insert_as_string_if_nonempty(0x0018, 0x0089, "NumberofPhaseEncodingSteps");
    insert_as_string_if_nonempty(0x0018, 0x0091, "EchoTrainLength");
    insert_as_string_if_nonempty(0x0018, 0x0093, "PercentSampling");
    insert_as_string_if_nonempty(0x0018, 0x0094, "PercentPhaseFieldofView");
    insert_as_string_if_nonempty(0x0018, 0x0095, "PixelBandwidth");
    insert_as_string_if_nonempty(0x0018, 0x1000, "DeviceSerialNumber");

    insert_as_string_if_nonempty(0x0018, 0x1030, "ProtocolName");

    insert_as_string_if_nonempty(0x0018, 0x1250, "ReceiveCoilName");
    insert_as_string_if_nonempty(0x0018, 0x1251, "TransmitCoilName");
    insert_as_string_if_nonempty(0x0018, 0x1312, "InplanePhaseEncodingDirection");
    insert_as_string_if_nonempty(0x0018, 0x1314, "FlipAngle");
    insert_as_string_if_nonempty(0x0018, 0x1316, "SAR");
    insert_as_string_if_nonempty(0x0018, 0x1318, "dB_dt");
    insert_as_string_if_nonempty(0x0018, 0x9073, "AcquisitionDuration");
    insert_as_string_if_nonempty(0x0018, 0x9087, "Diffusion_bValue");
    insert_as_string_if_nonempty(0x0018, 0x9089, "DiffusionGradientOrientation");

    insert_as_string_if_nonempty(0x2001, 0x1004, "DiffusionDirection");

    insert_as_string_if_nonempty(0x0028, 0x1050, "WindowCenter");
    insert_as_string_if_nonempty(0x0028, 0x1051, "WindowWidth");

    insert_as_string_if_nonempty(0x300a, 0x0002, "RTPlanLabel");
    insert_as_string_if_nonempty(0x300a, 0x0003, "RTPlanName");
    insert_as_string_if_nonempty(0x300a, 0x0004, "RTPlanDescription");
    insert_as_string_if_nonempty(0x300a, 0x0006, "RTPlanDate");
    insert_as_string_if_nonempty(0x300a, 0x0007, "RTPlanTime");
    insert_as_string_if_nonempty(0x300a, 0x000c, "RTPlanGeometry");

    insert_as_string_if_nonempty(0x0008, 0x0090, "ReferringPhysicianName");


    return out;
}



//------------------ Contours ---------------------

//Returns a bimap with the (raw) ROI tags and their corresponding ROI numbers. The ROI numbers are
// arbitrary identifiers used within the DICOM file to identify contours more conveniently.
bimap<std::string,long int> get_ROI_tags_and_numbers(const std::string &FilenameIn){
    using namespace puntoexe;
    ptr<puntoexe::stream> readStream(new puntoexe::stream);
    readStream->openFile(FilenameIn.c_str(), std::ios::in);

    ptr<puntoexe::streamReader> reader(new puntoexe::streamReader(readStream));
    ptr<imebra::dataSet> TopDataSet = imebra::codecs::codecFactory::getCodecFactory()->load(reader);
    ptr<imebra::dataSet> SecondDataSet;

    size_t i=0, j;
    std::string ROI_name;
    long int ROI_number;
    bimap<std::string,long int> the_pairs;
 
    do{
         //See gdcmdump output of an RS file  OR  Dicom documentation for these
         // numbers. 0x3006,0x0020 defines the top-level ROI sequence. 0x3006,0x0026
         // defines the name for each item (of which there might be many with the same number..)
         SecondDataSet = TopDataSet->getSequenceItem(0x3006, 0, 0x0020, i);
         if(SecondDataSet != nullptr){
            //Loop over all items within this data set. There should not be more than one, but I am 
            // suspect of 'data from the wild.'
            j=0;
            do{
                ROI_name   = SecondDataSet->getString(0x3006, 0, 0x0026, j);
                ROI_number = static_cast<long int>(SecondDataSet->getSignedLong(0x3006, 0, 0x0022, j));
                if(ROI_name.size()){
                    the_pairs[ROI_number] = ROI_name;
                }
                ++j;
            }while((ROI_name.size() != 0));
        }

        ++i;
    }while(SecondDataSet != nullptr);
    return the_pairs;
}


//Returns contour data from a DICOM RS file sorted into organ-specific collections.
std::unique_ptr<Contour_Data> get_Contour_Data(const std::string &filename){
    std::unique_ptr<Contour_Data> output (new Contour_Data());
    bimap<std::string,long int> tags_names_and_numbers = get_ROI_tags_and_numbers(filename);

    auto FileMetadata = get_metadata_top_level_tags(filename);


    using namespace puntoexe;
    ptr<puntoexe::stream> readStream(new puntoexe::stream);
    readStream->openFile(filename.c_str(), std::ios::in);
    ptr<puntoexe::streamReader> reader(new puntoexe::streamReader(readStream));
    ptr<imebra::dataSet> TopDataSet = imebra::codecs::codecFactory::getCodecFactory()->load(reader);
    ptr<imebra::dataSet> SecondDataSet, ThirdDataSet;

    //Collect the data into a container of contours with meta info. It may be unordered (within the file).
    std::map<std::tuple<std::string,long int>, contour_collection<double>> mapcache;
    for(size_t i=0; (SecondDataSet = TopDataSet->getSequenceItem(0x3006, 0, 0x0039, i)) != nullptr; ++i){
        long int Last_ROI_Numb = 0;
        for(size_t j=0; (ThirdDataSet = SecondDataSet->getSequenceItem(0x3006, 0, 0x0040, j)) != nullptr; ++j){
            long int ROI_number = static_cast<long int>(SecondDataSet->getSignedLong(0x3006, 0, 0x0084, j));
            if(ROI_number == 0){
                ROI_number = Last_ROI_Numb;
            }else{
                Last_ROI_Numb = ROI_number;
            }

            if((ThirdDataSet = SecondDataSet->getSequenceItem(0x3006, 0, 0x0040, j)) == nullptr){
                continue;
            }

            ptr<puntoexe::imebra::handlers::dataHandler> the_data_handler;
            for(size_t k=0; (the_data_handler = ThirdDataSet->getDataHandler(0x3006, 0, 0x0050, k, false)) != nullptr; ++k){
                contour_of_points<double> shtl;
                shtl.closed = true;

                //This is the number of coordinates we will get (ie. the number of doubles).
                const long int numb_of_coordinates = the_data_handler->getSize();
                for(long int N = 0; N < numb_of_coordinates; N += 3){
                    const double x = the_data_handler->getDouble(N + 0);
                    const double y = the_data_handler->getDouble(N + 1);
                    const double z = the_data_handler->getDouble(N + 2);
                    shtl.points.emplace_back(x,y,z);
                }
                shtl.Reorient_Counter_Clockwise();
                shtl.metadata = FileMetadata;

                auto ROIName = tags_names_and_numbers[ROI_number];
                shtl.metadata["ROINumber"] = std::to_string(ROI_number);
                shtl.metadata["ROIName"] = ROIName;
                
                const auto key = std::make_tuple(ROIName, ROI_number);
                mapcache[key].contours.push_back(std::move(shtl));
            }
        }
    }


    //Now sort the contours into contour_with_metas. We sort based on ROI number.
    for(auto & m_it : mapcache){
        output->ccs.emplace_back( ); //std::move(m_it->second) ) );
        output->ccs.back() = m_it.second; 

        output->ccs.back().Raw_ROI_name       = std::get<0>(m_it.first);
        output->ccs.back().ROI_number         = std::get<1>(m_it.first);
        output->ccs.back().Minimum_Separation = -1.0; //min_spacing;
        //output->ccs.back().Segmentation_History = ...empty...;
    }

    //Find the minimum separation between contours (which isn't zero).
    double min_spacing = 1E30;
    for(auto & cc : output->ccs){ 
        if(cc.contours.size() < 2) continue;

        for(auto c1_it = cc.contours.begin(); c1_it != --(cc.contours.end()); ++c1_it){
            auto c2_it = c1_it;
            ++c2_it;

            const double height1 = c1_it->Average_Point().Dot(vec3<double>(0.0,0.0,1.0));
            const double height2 = c2_it->Average_Point().Dot(vec3<double>(0.0,0.0,1.0));
            const double spacing = YGORABS(height2-height1);

            if((spacing < min_spacing) && (spacing > 1E-3)) min_spacing = spacing;
        }
    }
    //FUNCINFO("The minimum spacing found was " << min_spacing);
    for(auto & cc_it : output->ccs){
        cc_it.Minimum_Separation = min_spacing;
        for(auto & cc : cc_it.contours) cc.metadata["MinimumSeparation"] = std::to_string(min_spacing);
//        output->ccs.back().metadata["MinimumSeparation"] = std::to_string(min_spacing);
    }

    return output;
}


//-------------------- Images ----------------------
//This routine will often result in an array with only a single image. So collate output as needed.
//
// NOTE: I believe this routine is only valid for single frame images, like common CT and MR images.
//       PT and US have not been tested. RTDOSE files should use the Load_Dose_Array code, which 
//       handles multi-frame images (and thus might be adaptable for other non-RTDOSE multi-frame 
//       images).
std::unique_ptr<Image_Array> Load_Image_Array(const std::string &FilenameIn){
    std::unique_ptr<Image_Array> out(new Image_Array());

    out->filename   = FilenameIn;

    using namespace puntoexe;
    ptr<puntoexe::stream> readStream(new puntoexe::stream);
    readStream->openFile(FilenameIn.c_str(), std::ios::in);

    ptr<puntoexe::streamReader> reader(new puntoexe::streamReader(readStream));
    ptr<imebra::dataSet> TopDataSet = imebra::codecs::codecFactory::getCodecFactory()->load(reader);

    //Helper routines that do not create tags when they are missing.
    //
    // Note: Issuing the following:
    //    const auto image_pos_x = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0032, 0)); 
    //       will result in a new tag with a default value being created if it did not previously exist!
    auto retrieve_as_string = [&TopDataSet](uint16_t group, 
                                            uint16_t tag, 
                                            uint32_t element = 0) 
                                            -> std::experimental::optional<std::string> {
        const uint32_t first_order = 0; // Always zero for modern DICOM files.

        //Check if the tag is present in the file. If not, bail.
        const bool create_if_not_found = false;
        const auto ptr = TopDataSet->getTag(group, first_order, tag, create_if_not_found);
        if(ptr == nullptr) return std::experimental::optional<std::string>();

        //Retrieve the element.
        const auto str = TopDataSet->getString(group, first_order, tag, element);
        return str;
    };
    auto retrieve_as_long_int = [&TopDataSet,&retrieve_as_string](uint16_t group, 
                                              uint16_t tag, 
                                              uint32_t element = 0) 
                                              -> std::experimental::optional<double> {
        auto o = retrieve_as_string(group,tag,element);
        if(!o) return std::experimental::nullopt;
        return std::stol(o.value());
    };                                            
    auto retrieve_as_double = [&TopDataSet,&retrieve_as_string](uint16_t group, 
                                            uint16_t tag, 
                                            uint32_t element = 0) 
                                            -> std::experimental::optional<double> {
        auto o = retrieve_as_string(group,tag,element);
        if(!o) return std::experimental::nullopt;
        return std::stod(o.value());
    };                                            
    auto retrieve_coalesce_as_string = [&TopDataSet,&retrieve_as_string](std::list<std::array<uint32_t,3>> qs) 
                                                    -> std::experimental::optional<std::string> {
        for(const auto &q : qs){
            const auto group = static_cast<uint16_t>(q[0]);
            const auto tag = static_cast<uint16_t>(q[1]);
            const auto element = q[2];
            auto o = retrieve_as_string(group,tag,element);
            if(o) return o;
        }

        //None were available.
        return std::experimental::nullopt;
    };
    auto retrieve_coalesce_as_long_int = [&TopDataSet,&retrieve_coalesce_as_string](std::list<std::array<uint32_t,3>> qs)
                                                       -> std::experimental::optional<double> {
        auto o = retrieve_coalesce_as_string(qs);
        if(!o) return std::experimental::nullopt;
        return std::stol(o.value());
    };                                            
    auto retrieve_coalesce_as_double = [&TopDataSet,&retrieve_coalesce_as_string](std::list<std::array<uint32_t,3>> qs)
                                                     -> std::experimental::optional<double> {
        auto o = retrieve_coalesce_as_string(qs);
        if(!o) return std::experimental::nullopt;
        return std::stod(o.value());
    };                                            

    // ------------------------------------------- General --------------------------------------------------
    const auto modality = retrieve_as_string(0x0008, 0x0060).value();


    // ---------------------------------------- Image Metadata ----------------------------------------------

    //These should exist in all files. They appear to be the same for CT and DS files of the same set. Not sure
    // if this is *always* the case.
    const auto image_pos_x = retrieve_coalesce_as_double({ {0x0020, 0x0032, 0}, //"ImagePositionPatient".
                                                           {0x3002, 0x0012, 0}  //"RTImagePosition".
                                                         }).value_or(0.0);
    const auto image_pos_y = retrieve_coalesce_as_double({ {0x0020, 0x0032, 1}, //"ImagePositionPatient".
                                                           {0x3002, 0x0012, 1}  //"RTImagePosition".
                                                         }).value_or(0.0);

    auto image_pos_z = retrieve_coalesce_as_double({ {0x0020, 0x0032, 2}, //"ImagePositionPatient".
                                                     {0x3002, 0x000d, 2}  //"XRayImageReceptorTranslation".
                                                   }).value_or(std::numeric_limits<double>::quiet_NaN());
    if(!std::isfinite(image_pos_z)){ // Try derived values.
        // This is useful for RTIMAGES.
        const auto RTImageSID = retrieve_coalesce_as_double({ {0x3002, 0x0026, 0} }).value_or(1000.0);
        const auto RadMchnSAD = retrieve_coalesce_as_double({ {0x3002, 0x0022, 0} }).value_or(1000.0);
        image_pos_z = (RTImageSID - RadMchnSAD);
    }
    const vec3<double> image_pos(image_pos_x,image_pos_y,image_pos_z); //Only for first image!


    const auto image_orien_c_x = retrieve_coalesce_as_double({ {0x0020, 0x0037, 0}, //"ImagePositionPatient".
                                                               {0x3002, 0x0010, 0}  //"RTImageOrientation".
                                                             }).value_or(1.0);
    const auto image_orien_c_y = retrieve_coalesce_as_double({ {0x0020, 0x0037, 1}, //"ImagePositionPatient".
                                                               {0x3002, 0x0010, 1}  //"RTImageOrientation".
                                                             }).value_or(0.0);
    const auto image_orien_c_z = retrieve_coalesce_as_double({ {0x0020, 0x0037, 2}, //"ImagePositionPatient".
                                                               {0x3002, 0x0010, 2}  //"RTImageOrientation".
                                                             }).value_or(0.0);
    const vec3<double> image_orien_c = vec3<double>(image_orien_c_x,image_orien_c_y,image_orien_c_z).unit();


    const auto image_orien_r_x = retrieve_coalesce_as_double({ {0x0020, 0x0037, 3}, //"ImageOrientationPatient".
                                                               {0x3002, 0x0010, 3}  //"RTImageOrientation".
                                                             }).value_or(0.0);
    const auto image_orien_r_y = retrieve_coalesce_as_double({ {0x0020, 0x0037, 4}, //"ImageOrientationPatient".
                                                               {0x3002, 0x0010, 4}  //"RTImageOrientation".
                                                             }).value_or(1.0);
    const auto image_orien_r_z = retrieve_coalesce_as_double({ {0x0020, 0x0037, 5}, //"ImageOrientationPatient".
                                                               {0x3002, 0x0010, 5}  //"RTImageOrientation".
                                                             }).value_or(0.0);
    const vec3<double> image_orien_r = vec3<double>(image_orien_r_x,image_orien_r_y,image_orien_r_z).unit();

    const vec3<double> image_anchor  = vec3<double>(0.0,0.0,0.0); //Could us RTIMAGE IsocenterPosition (300a,012c) ?

    //Determine how many frames there are in the pixel data. A CT scan may just be a 2d jpeg or something, 
    // but dose pixel data is 3d data composed of 'frames' of stacked 2d data.
    const auto frame_count = retrieve_coalesce_as_long_int({ {0x0028, 0x0008, 0} }).value_or(0.0);
    if(frame_count != 0) throw std::domain_error("This routine only supports 2D images."
                                                 " Adapt the dose array loading code. Cannot continue");

    const auto image_rows  = retrieve_coalesce_as_long_int({ {0x0028, 0x0010, 0} }).value();
    const auto image_cols  = retrieve_coalesce_as_long_int({ {0x0028, 0x0011, 0} }).value();

    const auto image_pxldy = retrieve_coalesce_as_double({ {0x0028, 0x0030, 0}, //"PixelSpacing" -- spacing between adjacent rows.
                                                           {0x3002, 0x0011, 0}  //"ImagePlanePixelSpacing".
                                                         }).value();
    const auto image_pxldx = retrieve_coalesce_as_double({ {0x0028, 0x0030, 1}, //"PixelSpacing" -- spacing between adjacent columns.
                                                           {0x3002, 0x0011, 1}  //"ImagePlanePixelSpacing".
                                                         }).value();

    //For 2D images, there is often no thickness given. For CT we might have to compare to other files to figure this out.
    // For MR images, the thickness should be specified.
    //
    // Note: In general, images should be given a non-zero thickness since many core routines are build around slices of
    //       finite thickness.
    const auto image_thickness = retrieve_coalesce_as_double({ {0x0018, 0x0050, 0} }).value_or(1.0); //"SliceThickness"

    // -------------------------------------- Pixel Interpretation ------------------------------------------
    if( (TopDataSet->getTag(0x0040,0,0x9212) != nullptr)
    ||  (TopDataSet->getTag(0x0040,0,0x9216) != nullptr)
    ||  (TopDataSet->getTag(0x0040,0,0x9096) != nullptr)
    ||  (TopDataSet->getTag(0x0040,0,0x9211) != nullptr)
    ||  (TopDataSet->getTag(0x0040,0,0x9224) != nullptr)
    ||  (TopDataSet->getTag(0x0040,0,0x9225) != nullptr)
    ||  (TopDataSet->getTag(0x0040,0,0x9212) != nullptr)
    ||  (TopDataSet->getTag(0x0040,0,0x9210) != nullptr)
    ||  (TopDataSet->getTag(0x0028,0,0x3003) != nullptr)
    ||  (TopDataSet->getTag(0x0040,0,0x08EA) != nullptr) ){ 
        throw std::domain_error("This image contains a 'Real World Value' LUT (Look-Up Table), which is not presently"
                                " not supported. You will need to fix the code to handle this");
        // NOTE: See DICOM Supplement 49 "Enhanced MR Image Storage SOP Class" at 
        //       ftp://medical.nema.org/medical/dicom/final/sup49_ft.pdf 
        //       (or another document if it has been superceded) for more info.
        //
        //       It should be rather easy to implement this. I just haven't needed to yet.
        //       You could potentially just get Imebra to do it for you. See the LUT code elsewhere
        //       in this routine.
    }

    // --------------------------------------- Image Pixel Data ---------------------------------------------
    {    
        out->imagecoll.images.emplace_back();

        //--------------------------------------------------------------------------------------------------
        //Retrieve the pixel data from file. This is an excessively long exercise!
        ptr<puntoexe::imebra::image> firstImage;
        try{
            firstImage = TopDataSet->getImage(0);
        }catch(const std::exception &e){
            throw std::domain_error("This file does not have accessible pixel data."
                                    " The DICOM image loader should not be called for this file");
        }
    
        //Process image using modalityVOILUT transform to convert its pixel values into meaningful values.
        // From what I can tell, this conversion is necessary to transform the raw data from a possibly
        // manufacturer-specific, proprietary format into something physically meaningful for us. 
        //
        // I have not experimented with disabling this conversion. Leaving it intact causes the datum from
        // a Philips "Interra" machine's PAR/REC format to coincide with the exported DICOM data.
        ptr<imebra::transforms::transform> modVOILUT(new imebra::transforms::modalityVOILUT(TopDataSet));
        imbxUint32 width, height;
        firstImage->getSize(&width, &height);
        ptr<imebra::image> convertedImage(modVOILUT->allocateOutputImage(firstImage, width, height));
        modVOILUT->runTransform(firstImage, 0, 0, width, height, convertedImage, 0, 0);

    
        //Convert the 'convertedImage' into an image suitable for the viewing on screen. The VOILUT transform 
        // applies the contrast suggested by the dataSet to the image. Apply the first one we find. Relevant
        // DICOM tags reside around (0x0028,0x3010) and (0x0028,0x1050).
        //
        // This conversion uses the first suggested transformation found in the DICOM file, and will vary
        // from file to file. Generally, the transformation scales the pixel values to cover the range of the
        // available pixel range (i.e., u16). The transformation *CAN* induce clipping or truncation which 
        // cannot be recovered from!
        //
        // Therefore, in my opinion, it is never worthwhile to perform this conversion. If you want to window
        // or scale the values, you should do so as needed using the WindowCenter and WindowWidth values 
        // directly.
        //
        // Report available conversions:
        if(false){
            ptr<imebra::transforms::VOILUT> myVoiLut(new imebra::transforms::VOILUT(TopDataSet));
            std::vector<imbxUint32> VoiLutIds;
            for(imbxUint32 i = 0;  ; ++i){
                const auto VoiLutId = myVoiLut->getVOILUTId(i);
                if(VoiLutId == 0) break;
                VoiLutIds.push_back(VoiLutId);
            }
            //auto VoiLutIds = myVoiLut->getVOILUTIds();
            for(auto VoiLutId : VoiLutIds){
                const std::wstring VoiLutDescriptionWS = myVoiLut->getVOILUTDescription(VoiLutId);
                const std::string VoiLutDescription(VoiLutDescriptionWS.begin(), VoiLutDescriptionWS.end());
                FUNCINFO("Found 'presentation' VOI/LUT with description '" << VoiLutDescription << "' (not applying it!)");

                //Print the center and width of the VOI/LUT.
                imbxInt32 VoiLutCenter = std::numeric_limits<imbxInt32>::max();
                imbxInt32 VoiLutWidth  = std::numeric_limits<imbxInt32>::max();
                myVoiLut->getCenterWidth(&VoiLutCenter, &VoiLutWidth);
                if((VoiLutCenter != std::numeric_limits<imbxInt32>::max())
                || (VoiLutWidth  != std::numeric_limits<imbxInt32>::max())){
                    FUNCINFO("    - 'Presentation' VOI/LUT has centre = " << VoiLutCenter << " and width = " << VoiLutWidth);
                }
            }
        }
        //
        // Disable Imebra conversion:
        ptr<imebra::image> presImage(convertedImage);
        //
        // Enable Imebra conversion:
        //ptr<imebra::transforms::VOILUT> myVoiLut(new imebra::transforms::VOILUT(TopDataSet));
        //imbxUint32 lutId = myVoiLut->getVOILUTId(0);
        //myVoiLut->setVOILUT(lutId);
        //ptr<imebra::image> presImage(myVoiLut->allocateOutputImage(convertedImage, width, height));
        //myVoiLut->runTransform(convertedImage, 0, 0, width, height, presImage, 0, 0);
        //{
        //  //Print a description of the VOI/LUT if available.
        //  //const std::wstring VoiLutDescriptionWS = myVoiLut->getVOILUTDescription(lutId);
        //  //const std::string VoiLutDescription(VoiLutDescriptionWS.begin(), VoiLutDescriptionWS.end());
        //  //FUNCINFO("Using VOI/LUT with description '" << VoiLutDescription << "'");
        //
        //  //Print the center and width of the VOI/LUT.
        //  imbxInt32 VoiLutCenter = std::numeric_limits<imbxInt32>::max();
        //  imbxInt32 VoiLutWidth  = std::numeric_limits<imbxInt32>::max();
        //  myVoiLut->getCenterWidth(&VoiLutCenter, &VoiLutWidth);
        //  if((VoiLutCenter != std::numeric_limits<imbxInt32>::max())
        //  || (VoiLutWidth  != std::numeric_limits<imbxInt32>::max())){
        //      FUNCINFO("Using VOI/LUT with centre = " << VoiLutCenter << " and width = " << VoiLutWidth);
        //  }
        //}

 
        //Get the image in terms of 'RGB'/'MONOCHROME1'/'MONOCHROME2'/'YBR_FULL'/etc.. channels.
        //
        // This allows up to transform the data into a desired format before allocating any space.
        //
        // NOTE: The 'Photometric Interpretation' is specified in the DICOM file at 0x0028,0x0004 as a
        //       string. For instance "MONOCHROME2" is present in some MR images at the time of writing.
        //       It's not clear that I will want Imebra to transform the data under any circumstances, but
        //       to simplify things for now I'll assume we always want 'MONOCHROME2' format.
        //
        // NOTE: After some further digging, I believe letting Imebra convert to monochrome will allow
        //       us to handle compressed images without any extra work.
        puntoexe::imebra::transforms::colorTransforms::colorTransformsFactory*  pFactory = 
            puntoexe::imebra::transforms::colorTransforms::colorTransformsFactory::getColorTransformsFactory();
        ptr<puntoexe::imebra::transforms::transform> myColorTransform = 
            pFactory->getTransform(presImage->getColorSpace(), L"MONOCHROME2");//L"RGB");
        if(myColorTransform != nullptr){ //If we get a nullptr, we do not need to transform the image.
            ptr<puntoexe::imebra::image> rgbImage(myColorTransform->allocateOutputImage(presImage,width,height));
            myColorTransform->runTransform(presImage, 0, 0, width, height, rgbImage, 0, 0);
            presImage = rgbImage;
        }
    
        //Get a 'dataHandler' to access the image data waiting in 'presImage.' Get some image metadata.
        imbxUint32 rowSize, channelPixelSize, channelsNumber, sizeX, sizeY;
        //Select the image to use.
        // firstImage     -- Displays RTIMAGE, and CT(MR?) but neither CT nor RTIMAGE values are in HU.
        // convertedImage -- Works for CT (MR?) but not RTIMAGE.
        // presImage      -- Works for CT and MR, but not RTIMAGE.
        ptr<puntoexe::imebra::image> switchImage = ( modality == "RTIMAGE" ) ? firstImage : presImage;
        ptr<puntoexe::imebra::handlers::dataHandlerNumericBase> myHandler = 
            switchImage->getDataHandler(false, &rowSize, &channelPixelSize, &channelsNumber);
        presImage->getSize(&sizeX, &sizeY);
        //----------------------------------------------------------------------------------------------------

        if((static_cast<long int>(sizeX) != image_cols) || (static_cast<long int>(sizeY) != image_rows)){
            FUNCWARN("sizeX = " << sizeX << ", sizeY = " << sizeY << " and image_cols = " << image_cols << ", image_rows = " << image_rows);
            throw std::domain_error("The number of rows and columns in the image data differ when comparing sizeX/Y and img_rows/cols. Please verify");
            //If this issue arises, I have likely confused definition of X and Y. The DICOM standard specifically calls (0028,0010) 
            // a 'row'. Perhaps I've got many things backward...
        }

        out->imagecoll.images.back().metadata = get_metadata_top_level_tags(FilenameIn);
        out->imagecoll.images.back().init_orientation(image_orien_r,image_orien_c);

        const auto img_chnls = static_cast<long int>(channelsNumber);
        out->imagecoll.images.back().init_buffer(image_rows, image_cols, img_chnls); //Underlying type specifies per-pixel space allocated.

        const auto img_pxldz = image_thickness;
        out->imagecoll.images.back().init_spatial(image_pxldx,image_pxldy,img_pxldz, image_anchor, image_pos);

        //Sometimes Imebra returns a different number of bits than the DICOM header specifies. Presumably this
        // is for some reason (maybe even simplification of implementation, which is fair). Since I convert to
        // a float or uint32_t, the only practical concern is whether or not it will fit.
        const auto img_bits  = static_cast<unsigned int>(channelPixelSize*8); //16 bit, 32 bit, 8 bit, etc..
        if(img_bits > 32){
            throw std::domain_error("The number of bits returned by Imebra is too large to fit in uint32_t"
                                    " You can increase this if needed, or try to scale down to 32 bits");
        }
        out->bits = img_bits;

        //Write the data to our allocated memory. We do it pixel-by-pixel because the 'PixelRepresentation' could mean
        // the pixel locality is laid out in various ways (two ways?). This approach abstracts the issue away.
        imbxUint32 data_index = 0;
        for(long int row = 0; row < image_rows; ++row){
            for(long int col = 0; col < image_cols; ++col){
                for(long int chnl = 0; chnl < img_chnls; ++chnl){
                    //Let Imebra work out the conversion by asking for a double. Hope it can be narrowed if necessary!
                    const auto DoubleChannelValue = myHandler->getDouble(data_index);
                    const float OutgoingPixelValue = static_cast<float>(DoubleChannelValue);

                    out->imagecoll.images.back().reference(row,col,chnl) = OutgoingPixelValue;
                    ++data_index;
                } //Loop over channels.
            } //Loop over columns.
        } //Loop over rows.
    }
    return out;
}

//These 'shared' pointers will actually be unique. This routine just converts from unique to shared for you.
std::list<std::shared_ptr<Image_Array>>  Load_Image_Arrays(const std::list<std::string> &filenames){
    std::list<std::shared_ptr<Image_Array>> out;
    for(const auto & filename : filenames){
        out.push_back(Load_Image_Array(filename));
    }
    return out;
}

//Since many images must be loaded individually from a file, we will often have to collate them together.
//
//Note: Returns a nullptr if the collation was not successful. The input data will not be restored to the
//      exact way it was passed in. Returns a valid pointer to an empty Image_Array if there was no data
//      to collate.
//
//Note: Despite using shared_ptrs, if the collation fails some images may be collated while others weren't. 
//      Deep-copy images beforehand if this is something you aren't prepared to deal with.
//
std::unique_ptr<Image_Array> Collate_Image_Arrays(std::list<std::shared_ptr<Image_Array>> &in){
    std::unique_ptr<Image_Array> out(new Image_Array);
    if(in.empty()) return out;

    //Start from the end and work toward the beginning so we can easily pop the end. Keep all images in
    // the original list to ease collating to the first element.
    while(!in.empty()){
        auto pic_it = in.begin();
        const bool GeometricalOverlapOK = true;
        if(!out->imagecoll.Collate_Images((*pic_it)->imagecoll, GeometricalOverlapOK)){
            //We've encountered an issue and the images won't collate. Push the successfully collated
            // images back into the list and return a nullptr.
            in.push_back(std::move(out));
            return nullptr;
        }
        pic_it = in.erase(pic_it);
    }
    return out;
}


//--------------------- Dose -----------------------
//This routine reads a single DICOM dose file.
std::unique_ptr<Dose_Array>  Load_Dose_Array(const std::string &FilenameIn){
    auto metadata = get_metadata_top_level_tags(FilenameIn);

    std::unique_ptr<Dose_Array> out(new Dose_Array());

    using namespace puntoexe;
    ptr<puntoexe::stream> readStream(new puntoexe::stream);
    readStream->openFile(FilenameIn.c_str(), std::ios::in);

    ptr<puntoexe::streamReader> reader(new puntoexe::streamReader(readStream));
    ptr<imebra::dataSet> TopDataSet = imebra::codecs::codecFactory::getCodecFactory()->load(reader);

    //These should exist in all files. They appear to be the same for CT and DS files of the same set. Not sure
    // if this is *always* the case.
    const auto image_pos_x = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0032, 0));
    const auto image_pos_y = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0032, 1));
    const auto image_pos_z = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0032, 2));
    const vec3<double> image_pos(image_pos_x,image_pos_y,image_pos_z); //Only for first image!

    const auto image_orien_c_x = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0037, 0)); 
    const auto image_orien_c_y = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0037, 1));
    const auto image_orien_c_z = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0037, 2));
    const vec3<double> image_orien_c = vec3<double>(image_orien_c_x,image_orien_c_y,image_orien_c_z).unit();

    const auto image_orien_r_x = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0037, 3));
    const auto image_orien_r_y = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0037, 4));
    const auto image_orien_r_z = static_cast<double>(TopDataSet->getDouble(0x0020, 0, 0x0037, 5));
    const vec3<double> image_orien_r = vec3<double>(image_orien_r_x,image_orien_r_y,image_orien_r_z).unit();

    const vec3<double> image_stack_unit = (image_orien_c.Cross(image_orien_r)).unit(); //Unit vector denoting direction to stack images.
    const vec3<double> image_anchor  = vec3<double>(0.0,0.0,0.0);

    //Determine how many frames there are in the pixel data. A CT scan may just be a 2d jpeg or something, 
    // but dose pixel data is 3d data composed of 'frames' of stacked 2d data.
    const auto frame_count = static_cast<unsigned long int>(TopDataSet->getUnsignedLong(0x0028, 0, 0x0008, 0));
    if(frame_count == 0) throw std::domain_error("No frames were found in file '"_s + FilenameIn + "'. Is it a valid dose file?");

    //This is a redirection to another tag. I've never seen it be anything but (0x3004,0x000c).
    const auto frame_inc_pntrU  = static_cast<long int>(TopDataSet->getUnsignedLong(0x0028, 0, 0x0009, 0));
    const auto frame_inc_pntrL  = static_cast<long int>(TopDataSet->getUnsignedLong(0x0028, 0, 0x0009, 1));
    if((frame_inc_pntrU != static_cast<long int>(0x3004)) || (frame_inc_pntrL != static_cast<long int>(0x000c)) ){
        FUNCWARN(" frame increment pointer U,L = " << frame_inc_pntrU << "," << frame_inc_pntrL);
        throw std::domain_error("Dose file contains a frame increment pointer which we have not encountered before."
                                " Please ensure we can handle it properly");
    }

    std::list<double> gfov;
    for(unsigned long int i=0; i<frame_count; ++i){
        const auto val = static_cast<double>(TopDataSet->getDouble(0x3004, 0, 0x000c, i));
        gfov.push_back(val);
    }

    const double image_thickness = (gfov.size() > 1) ? ( *(++gfov.begin()) - *(gfov.begin()) ) : 1.0; //*NOT* the image separation!

    const auto image_rows  = static_cast<long int>(TopDataSet->getUnsignedLong(0x0028, 0, 0x0010, 0));
    const auto image_cols  = static_cast<long int>(TopDataSet->getUnsignedLong(0x0028, 0, 0x0011, 0));
    const auto image_pxldy = static_cast<double>(TopDataSet->getDouble(0x0028, 0, 0x0030, 0)); //Spacing between adjacent rows.
    const auto image_pxldx = static_cast<double>(TopDataSet->getDouble(0x0028, 0, 0x0030, 1)); //Spacing between adjacent columns.
    const auto image_bits  = static_cast<unsigned long int>(TopDataSet->getUnsignedLong(0x0028, 0, 0x0101, 0));
    const auto grid_scale  = static_cast<double>(TopDataSet->getDouble(0x3004, 0, 0x000e, 0));

    //Grab the image data for each individual frame.
    auto gfov_it = gfov.begin();
    for(unsigned long int curr_frame = 0; (curr_frame < frame_count) && (gfov_it != gfov.end()); ++curr_frame, ++gfov_it){
        out->imagecoll.images.emplace_back();

        //--------------------------------------------------------------------------------------------------
        //Retrieve the pixel data from file. This is an excessively long exercise!
        ptr<puntoexe::imebra::image> firstImage = TopDataSet->getImage(curr_frame); 	
        if(firstImage == nullptr) throw std::domain_error("This file does not have accessible pixel data. Double check the file");
    
        //Process image using modalityVOILUT transform to convert its pixel values into meaningful values.
        ptr<imebra::transforms::transform> modVOILUT(new imebra::transforms::modalityVOILUT(TopDataSet));
        imbxUint32 width, height;
        firstImage->getSize(&width, &height);
        ptr<imebra::image> convertedImage(modVOILUT->allocateOutputImage(firstImage, width, height));
        modVOILUT->runTransform(firstImage, 0, 0, width, height, convertedImage, 0, 0);
    
        //Convert the 'convertedImage' into an image suitable for the viewing on screen. The VOILUT transform 
        // applies the contrast suggested by the dataSet to the image. Apply the first one we find.
        //
        // I'm not sure how this affects dose values, if at all, so I've disabled it for now.
        //ptr<imebra::transforms::VOILUT> myVoiLut(new imebra::transforms::VOILUT(TopDataSet));
        //imbxUint32 lutId = myVoiLut->getVOILUTId(0);
        //myVoiLut->setVOILUT(lutId);
        //ptr<imebra::image> presImage(myVoiLut->allocateOutputImage(convertedImage, width, height));
        ptr<imebra::image> presImage = convertedImage;
        //myVoiLut->runTransform(convertedImage, 0, 0, width, height, presImage, 0, 0);
 
        //Get the image in terms of 'RGB'/'MONOCHROME1'/'MONOCHROME2'/'YBR_FULL'/etc.. channels.
        //
        // This allows up to transform the data into a desired format before allocating any space.
        puntoexe::imebra::transforms::colorTransforms::colorTransformsFactory*  pFactory = 
             puntoexe::imebra::transforms::colorTransforms::colorTransformsFactory::getColorTransformsFactory();
        ptr<puntoexe::imebra::transforms::transform> myColorTransform = 
             pFactory->getTransform(presImage->getColorSpace(), L"MONOCHROME2");//L"RGB");
        if(myColorTransform != nullptr){ //If we get a '0', we do not need to transform the image.
            ptr<puntoexe::imebra::image> rgbImage(myColorTransform->allocateOutputImage(presImage,width,height));
            myColorTransform->runTransform(presImage, 0, 0, width, height, rgbImage, 0, 0);
            presImage = rgbImage;
        }
    
        //Get a 'dataHandler' to access the image data waiting in 'presImage.' Get some image metadata.
        imbxUint32 rowSize, channelPixelSize, channelsNumber, sizeX, sizeY;
        ptr<puntoexe::imebra::handlers::dataHandlerNumericBase> myHandler = 
            presImage->getDataHandler(false, &rowSize, &channelPixelSize, &channelsNumber);
        presImage->getSize(&sizeX, &sizeY);
        //----------------------------------------------------------------------------------------------------

        if((static_cast<long int>(sizeX) != image_cols) || (static_cast<long int>(sizeY) != image_rows)){
            FUNCWARN("sizeX = " << sizeX << ", sizeY = " << sizeY << " and image_cols = " << image_cols << ", image_rows = " << image_rows);
            throw std::domain_error("The number of rows and columns in the image data differ when comparing sizeX/Y and img_rows/cols. Please verify");
            //If this issue arises, I have likely confused definition of X and Y. The DICOM standard specifically calls (0028,0010) 
            // a 'row'. Perhaps I've got many things backward...
        }

        out->imagecoll.images.back().metadata = metadata;
        out->imagecoll.images.back().init_orientation(image_orien_r,image_orien_c);

        const auto img_chnls = static_cast<long int>(channelsNumber);
        out->imagecoll.images.back().init_buffer(image_rows, image_cols, img_chnls);

        const auto img_pxldz = image_thickness;
        const auto gvof_offset = static_cast<double>(*gfov_it);  //Offset along \hat{z} from 
        const auto img_offset = image_pos + image_stack_unit * gvof_offset;
        out->imagecoll.images.back().init_spatial(image_pxldx,image_pxldy,img_pxldz, image_anchor, img_offset);

        out->imagecoll.images.back().metadata["GridFrameOffset"] = std::to_string(gvof_offset);
        out->imagecoll.images.back().metadata["Frame"] = std::to_string(curr_frame);
        out->imagecoll.images.back().metadata["ImagePositionPatient"] = img_offset.to_string();


        const auto img_bits  = static_cast<unsigned int>(channelPixelSize*8); //16 bit, 32 bit, 8 bit, etc..
        if(img_bits != image_bits){
            throw std::domain_error("The number of bits in each channel varies between the DICOM header ("_s
                                   + std::to_string(image_bits) 
                                   + ") and the transformed image data ("_s
                                   + std::to_string(img_bits)
                                   + ")");
            //Not sure what to do if this happens. Perhaps just go with the imebra result?
        }

        //Write the data to our allocated memory.
        imbxUint32 data_index = 0;
        for(long int row = 0; row < image_rows; ++row){
            for(long int col = 0; col < image_cols; ++col){
                for(long int chnl = 0; chnl < img_chnls; ++chnl){
                    const auto DoubleChannelValue = myHandler->getDouble(data_index);
                    const float OutgoingPixelValue = static_cast<float>(DoubleChannelValue) 
                                                     * static_cast<float>(grid_scale);
                    out->imagecoll.images.back().reference(row,col,chnl) = OutgoingPixelValue;

                    ++data_index;
                } //Loop over channels.
            } //Loop over columns.
        } //Loop over rows.
    } //Loop over frames.

    //Finally, pass the collection-specific items out.

    out->bits       = image_bits;
    out->grid_scale = 1.0; //grid_scale; <-- NOTE: pixels now hold dose directly and do not require scaling!
    out->filename   = FilenameIn;
    return out;
}

//These 'shared' pointers will actually be unique. This routine just converts from unique to shared for you.
std::list<std::shared_ptr<Dose_Array>>  Load_Dose_Arrays(const std::list<std::string> &filenames){
    std::list<std::shared_ptr<Dose_Array>> out;
    for(const auto & filename : filenames){
        out.push_back(Load_Dose_Array(filename));
    }
    return out;
}

static std::string Generate_Random_UID(long int len){
    std::string out;
    static const std::string alphanum(R"***(.0123456789)***");
    std::default_random_engine gen;

    try{
        std::random_device rd;  //Constructor can fail if many threads create instances (maybe limited fd's?).
        gen.seed(rd()); //Seed with a true random number.
    }catch(const std::exception &){
        const auto timeseed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        gen.seed(timeseed); //Seed with time. 
    }

    std::uniform_int_distribution<int> dist(0,alphanum.length()-1);
    out = "1.2.840.66.1.";
    char last = '.';
    while(static_cast<long int>(out.size()) != len){
        const auto achar = alphanum[dist(gen)];
        if((achar == '.') && (achar == last)) continue;
        if((achar == '.') && (static_cast<long int>(out.size()+1) == len)) continue;
        out += achar;
        last = achar;
    }
    return out;
}

static std::string Generate_Random_Int_Str(long int L, long int H){
    std::string out;
    std::default_random_engine gen;

    try{
        std::random_device rd;  //Constructor can fail if many threads create instances (maybe limited fd's?).
        gen.seed(rd()); //Seed with a true random number.
    }catch(const std::exception &){
        const auto timeseed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        gen.seed(timeseed); //Seed with time. 
    }

    std::uniform_int_distribution<long int> dist(L,H);
    return std::to_string(dist(gen));
}


//This routine writes contiguous images to a single DICOM dose file.
//
// NOTE: Images are assumed to be contiguous and non-overlapping. They are also assumed to share image characteristics,
//       such as number of rows, number of columns, voxel dimensions/extent, orientation, and geometric origin.
// 
// NOTE: Currently only the first channel is considered. Additional channels were not needed at the time of writing.
//       It would probably be best to export one channel per file if multiple channels were needed. Though it is
//       possible to have up to 3 samples per voxel, IIRC, it may complicate encoding significantly. (Not sure.)
// 
// NOTE: This routine will reorder images.
//
// NOTE: Images containin NaN's will probably be rejected by most programs! Filter them out beforehand.
//
// NOTE: Exported files were tested successfully with Varian Eclipse v11. A valid DICOM file is needed to link
//       existing UIDs. Images created from scratch and lacking, e.g., a valid FrameOfReferenceUID, have not been
//       tested.
//
// NOTE: Some round-off should be expected. It is necessary because the TransferSyntax appears to require integer voxel
//       intensities which are scaled by a floating-point number to get the final dose. There are facilities for
//       exchanging floating-point-valued images in DICOM, but portability would be suspect.
//
void Write_Dose_Array(std::shared_ptr<Image_Array> IA, const std::string &FilenameOut, ParanoiaLevel Paranoia){
    if( (IA == nullptr) 
    ||  IA->imagecoll.images.empty()){
        throw std::runtime_error("No images provided for export. Cannot continue.");
    }

    using namespace puntoexe;
    ptr<imebra::dataSet> tds(new imebra::dataSet);

    //Gather some basic info. Note that the following dimensions must be identical for all images for a multi-frame
    // RTDOSE file.
    const auto num_of_imgs = IA->imagecoll.images.size();
    const auto row_count = IA->imagecoll.images.front().rows;
    const auto col_count = IA->imagecoll.images.front().columns;

    auto max_dose = -std::numeric_limits<float>::infinity();
    for(const auto &p_img : IA->imagecoll.images){
        const long int channel = 0; // Ignore other channels for now. TODO.
        for(long int r = 0; r < row_count; r++){
            for(long int c = 0; c < col_count; c++){
                const auto val = p_img.value(r, c, channel);
                if(!std::isfinite(val)) throw std::domain_error("Found non-finite dose. Refusing to export.");
                if(val < 0.0f ) throw std::domain_error("Found a voxel with negative dose. Refusing to continue.");
                if(max_dose < val) max_dose = val;
            }
        }
    }
    if( max_dose < 0.0f ) throw std::invalid_argument("No voxels were found to export. Cannot continue.");
    const double full_dose_scaling = max_dose / static_cast<double>(std::numeric_limits<uint32_t>::max());
    const double dose_scaling = std::max(full_dose_scaling, 1.0E-5); //Because excess bits might get truncated!

    const auto pxl_dx = IA->imagecoll.images.front().pxl_dx;
    const auto pxl_dy = IA->imagecoll.images.front().pxl_dy;
    const auto PixelSpacing = std::to_string(pxl_dy) + R"***(\)***"_s + std::to_string(pxl_dx);

    const auto row_unit = IA->imagecoll.images.front().row_unit;
    const auto col_unit = IA->imagecoll.images.front().col_unit;
    const auto ortho_unit = col_unit.Cross(row_unit);
    const auto ImageOrientationPatient = std::to_string(col_unit.x) + R"***(\)***"_s
                                       + std::to_string(col_unit.y) + R"***(\)***"_s
                                       + std::to_string(col_unit.z) + R"***(\)***"_s
                                       + std::to_string(row_unit.x) + R"***(\)***"_s
                                       + std::to_string(row_unit.y) + R"***(\)***"_s
                                       + std::to_string(row_unit.z);

    //Re-order images so they are in spatial order with the 'bottom' defined in terms of row and column units.
    IA->imagecoll.Stable_Sort([&ortho_unit](const planar_image<float,double> &lhs, const planar_image<float,double> &rhs) -> bool {
        //Project each image onto the line defined by the ortho unit running through the origin. Sort by distance along
        //this line (i.e., the magnitude of the projection).
        if( (lhs.rows < 1) || (lhs.columns < 1) || (rhs.rows < 1) || (rhs.columns < 1) ){
            throw std::invalid_argument("Found an image containing no voxels. Refusing to continue."); //Just trim it?
        }
        return ( lhs.position(0,0).Dot(ortho_unit) < rhs.position(0,0).Dot(ortho_unit) );
    });

    const auto image_pos = IA->imagecoll.images.front().offset - IA->imagecoll.images.front().anchor;
    const auto ImagePositionPatient = std::to_string(image_pos.x) + R"***(\)***"_s
                                    + std::to_string(image_pos.y) + R"***(\)***"_s
                                    + std::to_string(image_pos.z);

    //Assume images abut (i.e., are contiguous) and perfectly parallel.
    const auto pxl_dz = IA->imagecoll.images.front().pxl_dz;
    const auto SliceThickness = std::to_string(pxl_dz);
    std::string GridFrameOffsetVector;
    for(size_t i = 0; i < num_of_imgs; ++i){
        const double z = pxl_dz*i;
        if(GridFrameOffsetVector.empty()){
            GridFrameOffsetVector += std::to_string(z);
        }else{
            GridFrameOffsetVector += R"***(\)***"_s + std::to_string(z);
        }
    }

    auto ds_OB_insert = [](ptr<imebra::dataSet> &ds, uint16_t group, uint16_t tag, std::string i_val) -> void {
        const uint16_t order = 0;
        
        //For OB type, we simply copy the string's buffer as-is. 
        const auto d_t = ds->getDefaultDataType(group, tag);

        if( d_t == "OB" ){
            auto tag_ptr = ds->getTag(group, order, tag, true);
            //const auto next_buff = tag_ptr->getBuffersCount() - 1;
            //auto rdh_ptr = tag_ptr->getDataHandlerRaw( next_buff, true, d_t );
            auto rdh_ptr = tag_ptr->getDataHandlerRaw( 0, true, d_t );
            rdh_ptr->copyFromMemory(reinterpret_cast<const uint8_t *>(i_val.data()),
                                    static_cast<uint32_t>(i_val.size()));
        }else{
            throw std::runtime_error("A non-OB VR type was passed to the OB VR type writer.");
        }
        return;
    };

    auto ds_insert = [&ds_OB_insert](ptr<imebra::dataSet> &ds, uint16_t group, uint16_t tag, std::string i_val) -> void {
        const uint16_t order = 0;
        uint32_t element = 0;

        //Search for '\' characters. If present, split the string up and register each token separately.
        auto tokens = SplitStringToVector(i_val, '\\', 'd');
        for(auto &val : tokens){
            //Attempt to convert to the default DICOM data type.
            const auto d_t = ds->getDefaultDataType(group, tag);

            //Types not requiring conversion from a string.
            if( ( d_t == "AE") || ( d_t == "AS") || ( d_t == "AT") ||
                ( d_t == "CS") || ( d_t == "DS") || ( d_t == "DT") ||
                ( d_t == "LO") || ( d_t == "LT") || ( d_t == "OW") ||
                ( d_t == "PN") || ( d_t == "SH") || ( d_t == "ST") ||
                ( d_t == "UT")   ){
                    ds->setString(group, order, tag, element++, val, d_t);
 
            //UIDs.
            }else if( d_t == "UI" ){   //UIDs.
                //UIDs were being altered in funny ways sometimes. Write raw bytes instead.
                auto tag_ptr = ds->getTag(group, order, tag, true);
                auto rdh_ptr = tag_ptr->getDataHandlerRaw( 0, true, d_t );
                rdh_ptr->copyFromMemory(reinterpret_cast<const uint8_t *>(val.data()),
                                        static_cast<uint32_t>(val.size()));

            //Time.
            }else if( ( d_t == "TM" ) ||   //Time.
                      ( d_t == "DA" )   ){ //Date.
                //Strip away colons. Also strip away everything after the leading non-numeric char.
                std::string digits_only(val);
                digits_only = PurgeCharsFromString(digits_only,":-");
                auto avec = SplitStringToVector(digits_only,'.','d');
                avec.resize(1);
                digits_only = Lineate_Vector(avec, "");

                //The 'easy' way resulted in non-printable garbage fouling the times. Have to write raw ASCII chars manually...
                auto tag_ptr = ds->getTag(group, order, tag, true);
                auto rdh_ptr = tag_ptr->getDataHandlerRaw( 0, true, d_t );
                rdh_ptr->copyFromMemory(reinterpret_cast<const uint8_t *>(digits_only.data()),
                                        static_cast<uint32_t>(digits_only.size()));

            //Binary types.
            }else if( d_t == "OB" ){
                return ds_OB_insert(ds, group, tag, i_val);

            //Numeric types.
            }else if(
                ( d_t == "FL") ||   //Floating-point.
                ( d_t == "FD") ||   //Floating-point double.
                ( d_t == "OF") ||   //"Other" floating-point.
                ( d_t == "OD")   ){ //"Other" floating-point double.
                    ds->setString(group, order, tag, element++, val, "DS"); //Try keep it as a string.
            }else if( ( d_t == "SL" ) ||   //Signed long int (32bit).
                      ( d_t == "SS" )   ){ //Signed short int (16bit).
                const auto conv = static_cast<int32_t>(std::stol(val));
                ds->setSignedLong(group, order, tag, element++, conv, d_t);

            }else if( ( d_t == "UL" ) ||   //Unsigned long int (32bit).
                      ( d_t == "US" )   ){ //Unsigned short int (16bit).
                const auto conv = static_cast<uint32_t>(std::stoul(val));
                ds->setUnsignedLong(group, order, tag, element++, conv, d_t);

            }else if( d_t == "IS" ){ //Integer string.
                    ds->setString(group, order, tag, element++, val, "IS");

            //Types we cannot process because they are special (e.g., sequences) or don't currently support.
            }else if( d_t == "SQ"){ //Sequence.
                throw std::runtime_error("Unable to write VR type SQ (sequence) with this routine.");
            }else if( d_t == "UN"){ //Unknown.
                throw std::runtime_error("Unable to write VR type UN (unknown) with this routine.");
            }else{
                throw std::runtime_error("Unknown VR type. Cannot write to tag.");
            }
        }
        return;
    };

    auto ds_seq_insert = [&ds_insert](ptr<imebra::dataSet> &ds, uint16_t seq_group, uint16_t seq_tag, 
                                                      uint16_t tag_group, uint16_t tag_tag, std::string tag_val) -> void {
        const uint32_t first_order = 0; // Always zero for modern DICOM files.

        //Get a reference to an existing sequence item, or create one if needed.
        const bool create_if_not_found = true;
        auto tag_ptr = ds->getTag(seq_group, first_order, seq_tag, create_if_not_found);
        if(tag_ptr == nullptr) return;

        //Prefer to append to an existing dataSet rather than creating an additional one.
        auto lds = tag_ptr->getDataSet(0);
        if( lds == nullptr ) lds = ptr<imebra::dataSet>(new imebra::dataSet);
        ds_insert(lds, tag_group, tag_tag, tag_val);
        tag_ptr->setDataSet( 0, lds );
        return;
    };

    auto fne = [](std::vector<std::string> l) -> std::string {
        //fne == "First non-empty".
        for(auto &s : l) if(!s.empty()) return s;
        throw std::runtime_error("All inputs were empty -- unable to provide a nonempty string.");
        return std::string();
    };

    auto foe = [](std::vector<std::string> l) -> std::string {
        //foe == "First non-empty Or Rmpty".
        for(auto &s : l) if(!s.empty()) return s;
        return std::string(); 
    };

    //Specify the list of acceptable character sets.
    {
        imebra::charsetsList::tCharsetsList suitableCharsets;
        suitableCharsets.emplace_back(L"ISO_IR 100"); // "Latin alphabet 1"
        //suitableCharsets.push_back(L"ISO_IR 192"); // utf-8
        tds->setCharsetsList(&suitableCharsets);
    }

    //Top-level stuff: metadata shared by all images.
    {
        auto cm = IA->imagecoll.get_common_metadata({});

        //Replace any metadata that might be used to underhandedly link patients, if requested.
        if((Paranoia == ParanoiaLevel::Medium) || (Paranoia == ParanoiaLevel::High)){
            //SOP Common Module.
            cm["InstanceCreationDate"] = "";
            cm["InstanceCreationTime"] = "";
            cm["InstanceCreatorUID"]   = Generate_Random_UID(60);

            //Patient Module.
            cm["PatientsBirthDate"] = "";
            cm["PatientsGender"]    = "";
            cm["PatientsBirthTime"] = "";

            //General Study Module.
            cm["StudyInstanceUID"] = "";
            cm["StudyDate"] = "";
            cm["StudyTime"] = "";
            cm["ReferringPhysiciansName"] = "";
            cm["StudyID"] = "";
            cm["AccessionNumber"] = "";
            cm["StudyDescription"] = "";

            //General Series Module.
            cm["SeriesInstanceUID"] = "";
            cm["SeriesNumber"] = "";
            cm["SeriesDate"] = "";
            cm["SeriesTime"] = "";
            cm["SeriesDescription"] = "";
            cm["RequestedProcedureID"] = "";
            cm["ScheduledProcedureStepID"] = "";
            cm["OperatorsName"] = "";

            //Patient Study Module.
            cm["PatientsMass"] = "";

            //Frame of Reference Module.
            cm["PositionReferenceIndicator"] = "";

            //General Equipment Module.
            cm["Manufacturer"] = "";
            cm["InstitutionName"] = "";
            cm["StationName"] = "";
            cm["InstitutionalDepartmentName"] = "";
            cm["ManufacturersModelName"] = "";
            cm["SoftwareVersions"] = "";

            //General Image Module.
            cm["ContentDate"] = "";
            cm["ContentTime"] = "";
            cm["AcquisitionNumber"] = "";
            cm["AcquisitionDate"] = "";
            cm["AcquisitionTime"] = "";
            cm["DerivationDescription"] = "";
            cm["ImagesInAcquisition"] = "";

            //RT Dose Module.
            cm[R"***(ReferencedRTPlanSequence/ReferencedSOPInstanceUID)***"] = "";
            if(0 != cm.count(R"***(ReferencedFractionGroupSequence/ReferencedFractionGroupNumber)***")){
                cm[R"***(ReferencedFractionGroupSequence/ReferencedFractionGroupNumber)***"] = "";
            }
            if(0 != cm.count(R"***(ReferencedBeamSequence/ReferencedBeamNumber)***")){
                cm[R"***(ReferencedBeamSequence/ReferencedBeamNumber)***"] = "";
            }
        }
        if(Paranoia == ParanoiaLevel::High){
            //Patient Module.
            cm["PatientsName"]      = "";
            cm["PatientID"]         = "";

            //Frame of Reference Module.
            cm["FrameofReferenceUID"] = "";
        }


        //Generate some UIDs that need to be duplicated.
        const auto SOPInstanceUID = Generate_Random_UID(60);

        //DICOM Header Metadata.
        ds_OB_insert(tds, 0x0002, 0x0001,  std::string(1,static_cast<char>(0))
                                         + std::string(1,static_cast<char>(1)) ); //"FileMetaInformationVersion".
        //ds_insert(tds, 0x0002, 0x0001, R"***(2/0/0/0/0/1)***"); //shtl); //"FileMetaInformationVersion".
        ds_insert(tds, 0x0002, 0x0002, "1.2.840.10008.5.1.4.1.1.481.2"); //"MediaStorageSOPClassUID" (Radiation Therapy Dose Storage)
        ds_insert(tds, 0x0002, 0x0003, SOPInstanceUID); //"MediaStorageSOPInstanceUID".
        ds_insert(tds, 0x0002, 0x0010, "1.2.840.10008.1.2.1"); //"TransferSyntaxUID".
        ds_insert(tds, 0x0002, 0x0013, "DICOMautomaton"); //"ImplementationVersionName".
        ds_insert(tds, 0x0002, 0x0012, "1.2.513.264.765.1.1.578"); //"ImplementationClassUID".

        //SOP Common Module.
        ds_insert(tds, 0x0008, 0x0016, "1.2.840.10008.5.1.4.1.1.481.2"); // "SOPClassUID"
        ds_insert(tds, 0x0008, 0x0018, SOPInstanceUID); // "SOPInstanceUID"
        //ds_insert(tds, 0x0008, 0x0005, "ISO_IR 100"); //fne({ cm["SpecificCharacterSet"], "ISO_IR 100" })); // Set above!
        ds_insert(tds, 0x0008, 0x0012, fne({ cm["InstanceCreationDate"], "19720101" }));
        ds_insert(tds, 0x0008, 0x0013, fne({ cm["InstanceCreationTime"], "010101" }));
        ds_insert(tds, 0x0008, 0x0014, foe({ cm["InstanceCreatorUID"] }));
        ds_insert(tds, 0x0008, 0x0114, foe({ cm["CodingSchemeExternalUID"] }));
        ds_insert(tds, 0x0020, 0x0013, foe({ cm["InstanceNumber"] }));

        //Patient Module.
        ds_insert(tds, 0x0010, 0x0010, fne({ cm["PatientsName"], "DICOMautomaton^DICOMautomaton" }));
        ds_insert(tds, 0x0010, 0x0020, fne({ cm["PatientID"], "DCMA_"_s + Generate_Random_String_of_Length(10) }));
        ds_insert(tds, 0x0010, 0x0030, fne({ cm["PatientsBirthDate"], "19720101" }));
        ds_insert(tds, 0x0010, 0x0040, fne({ cm["PatientsGender"], "O" }));
        ds_insert(tds, 0x0010, 0x0032, fne({ cm["PatientsBirthTime"], "010101" }));

        //General Study Module.
        ds_insert(tds, 0x0020, 0x000D, fne({ cm["StudyInstanceUID"], Generate_Random_UID(31) }));
        ds_insert(tds, 0x0008, 0x0020, fne({ cm["StudyDate"], "19720101" }));
        ds_insert(tds, 0x0008, 0x0030, fne({ cm["StudyTime"], "010101" }));
        ds_insert(tds, 0x0008, 0x0090, fne({ cm["ReferringPhysiciansName"], "UNSPECIFIED^UNSPECIFIED" }));
        ds_insert(tds, 0x0020, 0x0010, fne({ cm["StudyID"], "DCMA_"_s + Generate_Random_String_of_Length(10) }));
        ds_insert(tds, 0x0008, 0x0050, fne({ cm["AccessionNumber"], Generate_Random_String_of_Length(14) }));
        ds_insert(tds, 0x0008, 0x1030, fne({ cm["StudyDescription"], "UNSPECIFIED" }));

        //General Series Module.
        ds_insert(tds, 0x0008, 0x0060, "RTDOSE");
        ds_insert(tds, 0x0020, 0x000E, fne({ cm["SeriesInstanceUID"], Generate_Random_UID(31) }));
        ds_insert(tds, 0x0020, 0x0011, fne({ cm["SeriesNumber"], Generate_Random_Int_Str(5000, 4294967295) }));
        ds_insert(tds, 0x0008, 0x0021, foe({ cm["SeriesDate"] }));
        ds_insert(tds, 0x0008, 0x0031, foe({ cm["SeriesTime"] }));
        ds_insert(tds, 0x0008, 0x103E, fne({ cm["SeriesDescription"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0018, 0x0015, foe({ cm["BodyPartExamined"] }));
        ds_insert(tds, 0x0018, 0x5100, foe({ cm["PatientPosition"] }));
        ds_insert(tds, 0x0040, 0x1001, fne({ cm["RequestedProcedureID"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0040, 0x0009, fne({ cm["ScheduledProcedureStepID"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0008, 0x1070, fne({ cm["OperatorsName"], "UNSPECIFIED" }));

        //Patient Study Module.
        ds_insert(tds, 0x0010, 0x1030, foe({ cm["PatientsMass"] }));

        //Frame of Reference Module.
        ds_insert(tds, 0x0020, 0x0052, fne({ cm["FrameofReferenceUID"], Generate_Random_UID(32) }));
        ds_insert(tds, 0x0020, 0x1040, fne({ cm["PositionReferenceIndicator"], "BB" }));

        //General Equipment Module.
        ds_insert(tds, 0x0008, 0x0070, fne({ cm["Manufacturer"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0008, 0x0080, fne({ cm["InstitutionName"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0008, 0x1010, fne({ cm["StationName"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0008, 0x1040, fne({ cm["InstitutionalDepartmentName"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0008, 0x1090, fne({ cm["ManufacturersModelName"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0018, 0x1020, fne({ cm["SoftwareVersions"], "UNSPECIFIED" }));

        //General Image Module.
        ds_insert(tds, 0x0020, 0x0013, foe({ cm["InstanceNumber"] }));
        //ds_insert(tds, 0x0020, 0x0020, fne({ cm["PatientOrientation"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0008, 0x0023, foe({ cm["ContentDate"] }));
        ds_insert(tds, 0x0008, 0x0033, foe({ cm["ContentTime"] }));
        //ds_insert(tds, 0x0008, 0x0008, fne({ cm["ImageType"], "UNSPECIFIED" }));
        ds_insert(tds, 0x0020, 0x0012, foe({ cm["AcquisitionNumber"] }));
        ds_insert(tds, 0x0008, 0x0022, foe({ cm["AcquisitionDate"] }));
        ds_insert(tds, 0x0008, 0x0032, foe({ cm["AcquisitionTime"] }));
        ds_insert(tds, 0x0008, 0x2111, foe({ cm["DerivationDescription"] }));
        //insert_as_string_if_nonempty(0x0008, 0x9215, "DerivationCodeSequence"], "" }));
        ds_insert(tds, 0x0020, 0x1002, foe({ cm["ImagesInAcquisition"] }));
        ds_insert(tds, 0x0020, 0x4000, "Research image generated by DICOMautomaton. Not for clinical use!" ); //"ImageComments".
        ds_insert(tds, 0x0028, 0x0300, foe({ cm["QualityControlImage"] }));

        //Image Plane Module.
        ds_insert(tds, 0x0028, 0x0030, PixelSpacing );
        ds_insert(tds, 0x0020, 0x0037, ImageOrientationPatient );
        ds_insert(tds, 0x0020, 0x0032, ImagePositionPatient );
        ds_insert(tds, 0x0018, 0x0050, SliceThickness );
        ds_insert(tds, 0x0020, 0x1041, "" ); // foe({ cm["SliceLocation"] }));

        //Image Pixel Module.
        ds_insert(tds, 0x0028, 0x0002, fne({ cm["SamplesPerPixel"], "1" }));
        ds_insert(tds, 0x0028, 0x0004, fne({ cm["PhotometricInterpretation"], "MONOCHROME2" }));
        ds_insert(tds, 0x0028, 0x0010, fne({ std::to_string(row_count) })); // "Rows"
        ds_insert(tds, 0x0028, 0x0011, fne({ std::to_string(col_count) })); // "Columns"
        ds_insert(tds, 0x0028, 0x0100, "32" ); //fne({ cm["BitsAllocated"], "32" }));
        ds_insert(tds, 0x0028, 0x0101, "32" ); //fne({ cm["BitsStored"], "32" }));
        ds_insert(tds, 0x0028, 0x0102, "31" ); //fne({ cm["HighBit"], "31" }));
        ds_insert(tds, 0x0028, 0x0103, "0" ); // Unsigned.   fne({ cm["PixelRepresentation"], "0" }));
        ds_insert(tds, 0x0028, 0x0006, foe({ cm["PlanarConfiguration"] }));
        ds_insert(tds, 0x0028, 0x0034, foe({ cm["PixelAspectRatio"] }));

        //Multi-Frame Module.
        ds_insert(tds, 0x0028, 0x0008, fne({ std::to_string(num_of_imgs) })); // "NumberOfFrames".
        ds_insert(tds, 0x0028, 0x0009, fne({ cm["FrameIncrementPointer"], // Default to (3004,000c).
                                             R"***(12292\12)***" })); // Imebra default deserialization, but is brittle and depends on endianness.
                                             //"\x04\x30\x0c\x00" })); // Imebra won't accept this...
        ds_insert(tds, 0x3004, 0x000c, GridFrameOffsetVector );

        //Modality LUT Module.
        //insert_as_string_if_nonempty(0x0028, 0x3000, "ModalityLUTSequence"], "" }));
        ds_insert(tds, 0x0028, 0x3002, foe({ cm["LUTDescriptor"] }));
        ds_insert(tds, 0x0028, 0x3004, foe({ cm["ModalityLUTType"] }));
        ds_insert(tds, 0x0028, 0x3006, foe({ cm["LUTData"] }));
        //ds_insert(tds, 0x0028, 0x1052, foe({ cm["RescaleIntercept"] })); // These force interpretation by Imebra
        //ds_insert(tds, 0x0028, 0x1053, foe({ cm["RescaleSlope"] }));     //  as 8 byte pixel depth, regardless of
        //ds_insert(tds, 0x0028, 0x1054, foe({ cm["RescaleType"] }));      //  the actual depth (@ current settings).

        //RT Dose Module.
        //ds_insert(tds, 0x0028, 0x0002, fne({ cm["SamplesPerPixel"], "1" }));
        //ds_insert(tds, 0x0028, 0x0004, fne({ cm["PhotometricInterpretation"], "MONOCHROME2" }));
        //ds_insert(tds, 0x0028, 0x0100, fne({ cm["BitsAllocated"], "32" }));
        //ds_insert(tds, 0x0028, 0x0101, fne({ cm["BitsStored"], "32" }));
        //ds_insert(tds, 0x0028, 0x0102, fne({ cm["HighBit"], "31" }));
        //ds_insert(tds, 0x0028, 0x0103, fne({ cm["PixelRepresentation"], "0" }));
        ds_insert(tds, 0x3004, 0x0002, fne({ cm["DoseUnits"], "GY" }));
        ds_insert(tds, 0x3004, 0x0004, fne({ cm["DoseType"], "PHYSICAL" }));
        ds_insert(tds, 0x3004, 0x000a, fne({ cm["DoseSummationType"], "PLAN" }));
        ds_insert(tds, 0x3004, 0x000e, std::to_string(dose_scaling) ); //"DoseGridScaling"

        ds_seq_insert(tds, 0x300C, 0x0002, // "ReferencedRTPlanSequence" 
                           0x0008, 0x1150, // "ReferencedSOPClassUID"
                           fne({ cm[R"***(ReferencedRTPlanSequence/ReferencedSOPClassUID)***"],
                                 "1.2.840.10008.5.1.4.1.1.481.5" }) ); // "RTPlanStorage". Prefer existing UID.
        ds_seq_insert(tds, 0x300C, 0x0002, // "ReferencedRTPlanSequence"
                           0x0008, 0x1155, // "ReferencedSOPInstanceUID"
                           fne({ cm[R"***(ReferencedRTPlanSequence/ReferencedSOPInstanceUID)***"],
                                 Generate_Random_UID(32) }) );
  
        if(0 != cm.count(R"***(ReferencedFractionGroupSequence/ReferencedFractionGroupNumber)***")){
            ds_seq_insert(tds, 0x300C, 0x0020, // "ReferencedFractionGroupSequence"
                               0x300C, 0x0022, // "ReferencedFractionGroupNumber"
                               foe({ cm[R"***(ReferencedFractionGroupSequence/ReferencedFractionGroupNumber)***"] }) );
        }

        if(0 != cm.count(R"***(ReferencedBeamSequence/ReferencedBeamNumber)***")){
            ds_seq_insert(tds, 0x300C, 0x0004, // "ReferencedBeamSequence"
                               0x300C, 0x0006, // "ReferencedBeamNumber"
                               foe({ cm[R"***(ReferencedBeamSequence/ReferencedBeamNumber)***"] }) );
        }
    }

    //Insert the raw pixel data.
    std::vector<uint32_t> shtl;
    shtl.reserve(num_of_imgs * col_count * row_count);
    for(const auto &p_img : IA->imagecoll.images){

        //Convert each pixel to the required format, scaling by the dose factor as needed.
        const long int channel = 0; // Ignore other channels for now. TODO.
        for(long int r = 0; r < row_count; r++){
            for(long int c = 0; c < col_count; c++){
                const auto val = p_img.value(r, c, channel);
                const auto scaled = std::round( std::abs(val/dose_scaling) );
                auto as_uint = static_cast<uint32_t>(scaled);
                shtl.push_back(as_uint);
            }
        }
    }
    {
        auto tag_ptr = tds->getTag(0x7FE0, 0, 0x0010, true);
        //FUNCINFO("Re-reading the tag.  Type is " << tag_ptr->getDataType() << ",  #_of_buffers = " <<
        //     tag_ptr->getBuffersCount() << ",   buffer_0 has size = " << tag_ptr->getBufferSize(0));

        auto rdh_ptr = tag_ptr->getDataHandlerRaw(0, true, "OW");
        rdh_ptr->copyFromMemory(reinterpret_cast<const uint8_t *>(shtl.data()),
                                4*static_cast<uint32_t>(shtl.size()));
    }

    // Write the file.
    {  
        ptr<puntoexe::stream> outputStream(new puntoexe::stream);
        outputStream->openFile(FilenameOut, std::ios::out);
        ptr<streamWriter> writer(new streamWriter(outputStream));
        ptr<imebra::codecs::dicomCodec> writeCodec(new imebra::codecs::dicomCodec);
        writeCodec->write(writer, tds);
    }

    return;
}

