//Write_File.h - A part of DICOMautomaton 2018. Written by hal clark.

#pragma once

#include <functional>
#include <string>

// This routine will write text to a file, protecting the write with a semaphore from concurrrent processes.
// The filename is claimed after the semaphore is acquired to avoid a race condition.
void Append_File( const std::function<std::string(void)>& gen_file_name,
                  const std::string& mutex_name,
                  const std::string& iff_newfile,
                  const std::string& body );
