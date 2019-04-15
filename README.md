
![DICOMautomaton logo](artifacts/DCMA_cycle_opti.svg)

[![Build Status](https://travis-ci.com/hdclark/DICOMautomaton.svg?branch=master)](https://travis-ci.com/hdclark/DICOMautomaton)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![LOC](https://tokei.rs/b1/gitlab/hdeanclark/DICOMautomaton)](https://gitlab.com/hdeanclark/DICOMautomaton)
[![Language](https://img.shields.io/github/languages/top/hdclark/DICOMautomaton.svg)](https://gitlab.com/hdeanclark/DICOMautomaton)
[![Hit count](http://hits.dwyl.io/hdclark/DICOMautomaton.svg)](http://hits.dwyl.io/hdclark/DICOMautomaton)

# About

`DICOMautomaton` is a collection of tools for analyzing *medical physics* data,
specifically dosimetric and medical imaging data in the DICOM format. It has
become something of a platform that provides a variety of functionality.
`DICOMautomaton` is designed for easily developing customized workflows.


The basic workflow is:

  1. Files are loaded (from a DB or various types of files).

  2. A list of operations are provided and sequentially performed, mutating the
     data state.

  3. Files of various kinds can be written or a viewer can be invoked. Both
     are implemented as operations that can be chained together sequentially.

Some operations are interactive. Others will run on their own for days (weeks).
Each operation provides a description of the parameters that can be configured.
To see this documentation, invoke:

    $>  dicomautomaton_dispatcher -u

and for general information invoke:

    $>  dicomautomaton_dispatcher -h

NOTE: `DICOMautomaton` should NOT be used for clinical purposes. It is suitable
for research or support tool purposes only.


# License and Copying

All materials herein which may be copywrited, where applicable, are. Copyright
2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019 Hal Clark.

See [LICENSE.txt] for details about the license. Informally, `DICOMautomaton` is
available under a GPLv3+ license. The Imebra library is bundled for convenience
and was not written by the author; consult the Imebra license file
[src/imebra/license.txt].

All liability is herefore disclaimed. The person(s) who use this source and/or
software do so strictly under their own volition. They assume all associated
liability for use and misuse, including but not limited to damages, harm,
injury, and death which may result, including but not limited to that arising
from unforeseen or unanticipated implementation defects.


# Dependencies

Dependencies are listed in [PKGBUILD], using Arch Linux package
naming conventions, and in [CMakeLists.txt] using Debian package naming
conventions.

Notably, `DICOMautomaton` depends on the author's "Ygor", "Explicator", 
and "YgorClustering" projects which are hosted at:

  - Ygor: [https://gitlab.com/hdeanclark/Ygor] and
    [https://github.com/hdclark/Ygor].

  - Explicator: [https://gitlab.com/hdeanclark/Explicator] and
    [https://github.com/hdclark/Explicator].

  - YgorClustering (needed only for compilation):
    [https://gitlab.com/hdeanclark/YgorClustering] and
    [https://github.com/hdclark/YgorClustering].

  
# Installation

This project uses CMake. Use the usual commands to compile:

     $>  cd /path/to/source/directory
     $>  mkdir build && cd build/

Then, iff by-passing your package manager:

     $>  cmake ../ -DCMAKE_INSTALL_PREFIX=/usr
     $>  make && sudo make install

Or, if building for Debian:

     $>  cmake ../ -DCMAKE_INSTALL_PREFIX=/usr
     $>  make && make package
     $>  sudo apt install ./*.deb

Or, if building for Arch Linux:

     $>  rsync -aC --exclude build ../ ./
     $>  makepkg --syncdeps --noconfirm # Optionally also [--install].


# Known Issues

- The `SFML_Viewer` operation hangs on some systems after viewing a plot with
  Gnuplot. This stems from a known issue in Ygor. 


# Project Home

The `DICOMautomaton` homepage can be found at [http://www.halclark.ca/]. Source
code is available at [https://gitlab.com/hdeanclark/DICOMautomaton/] and
[https://github.com/hdclark/DICOMautomaton/].


