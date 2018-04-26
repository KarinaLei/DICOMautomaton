//FVPicketFence.cc - A part of DICOMautomaton 2018. Written by hal clark.

#include <algorithm>
#include <list>
#include <map>
#include <string>    

#include "../Structs.h"
#include "../Regex_Selectors.h"
#include "FVPicketFence.h"
#include "CropImages.h"
#include "AutoCropImages.h"
#include "AnalyzePicketFence.h"
#include "PresentationImage.h"


OperationDoc OpArgDocFVPicketFence(void){
    OperationDoc out;
    out.name = "FVPicketFence";
    out.desc = "This operation performs a picket fence QA test using an RTIMAGE file.";

    out.notes.emplace_back(
        "This is a 'simplified' version of the full picket fence analysis program that uses defaults"
        " that are expected to be reasonable across a wide range of scenarios." 
    );


    out.args.splice( out.args.end(), OpArgDocCropImages().args );
    out.args.splice( out.args.end(), OpArgDocAutoCropImages().args );
    out.args.splice( out.args.end(), OpArgDocAnalyzePicketFence().args );
    out.args.splice( out.args.end(), OpArgDocPresentationImage().args );

    // Adjust the defaults to suit this particular workflow.
    for(auto &oparg : out.args){
        if(false){
        }else if(oparg.name == "ImageSelection"){
            oparg.default_val = "last";
            oparg.visibility  = OpArgVisibility::Hide;

        }else if(oparg.name == "RowsL"){
            oparg.default_val = "5px";
            oparg.visibility  = OpArgVisibility::Hide;
        }else if(oparg.name == "RowsH"){
            oparg.default_val = "5px";
            oparg.visibility  = OpArgVisibility::Hide;
        }else if(oparg.name == "ColumnsL"){
            oparg.default_val = "5px";
            oparg.visibility  = OpArgVisibility::Hide;
        }else if(oparg.name == "ColumnsH"){
            oparg.default_val = "5px";
            oparg.visibility  = OpArgVisibility::Hide;

        }else if(oparg.name == "RTIMAGE"){
            oparg.default_val = "true";
            oparg.visibility  = OpArgVisibility::Hide;

        }else if(oparg.name == "ThresholdDistance"){
            oparg.default_val = "0.5";

        }else if(oparg.name == "InteractivePlots"){
            oparg.default_val = "false";
            oparg.visibility  = OpArgVisibility::Hide;

        }else if(oparg.name == "UserComment"){
            oparg.visibility  = OpArgVisibility::Hide;

        }else if(oparg.name == "MLCROILabel"){
            oparg.visibility  = OpArgVisibility::Hide;

        }else if(oparg.name == "JunctionROILabel"){
            oparg.visibility  = OpArgVisibility::Hide;

        }else if(oparg.name == "PeakROILabel"){
            oparg.visibility  = OpArgVisibility::Hide;

        }else if(oparg.name == "DICOMMargin"){
            oparg.visibility  = OpArgVisibility::Hide;

        }else if(oparg.name == "ScaleFactor"){
            oparg.default_val = "1.5";
        }
    }

    return out;
}


Drover
FVPicketFence(Drover DICOM_data, 
              OperationArgPkg OptArgs,
              std::map<std::string, std::string> InvocationMetadata,
              std::string FilenameLex){

    DICOM_data = CropImages(std::move(DICOM_data), OptArgs, InvocationMetadata, FilenameLex);
    DICOM_data = AutoCropImages(std::move(DICOM_data), OptArgs, InvocationMetadata, FilenameLex);
    DICOM_data = AnalyzePicketFence(std::move(DICOM_data), OptArgs, InvocationMetadata, FilenameLex);
    DICOM_data = PresentationImage(std::move(DICOM_data), OptArgs, InvocationMetadata, FilenameLex);

    return DICOM_data;
}
