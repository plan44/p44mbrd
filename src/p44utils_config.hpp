//
//  Copyright (c) 2014-2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44utils.
//
//  p44utils is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44utils is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44utils. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __p44utils__config__
#define __p44utils__config__

// connectedhomeip definition
#include <system/SystemBuildConfig.h>

#ifndef ENABLE_NAMED_ERRORS
  #define ENABLE_NAMED_ERRORS P44_CPP17_FEATURE // Enable if compiler can do C++17
#endif
#ifndef ENABLE_P44SCRIPT
  #define ENABLE_P44SCRIPT 0 // Scripting support in some of the p44utils components
#endif
#ifndef ENABLE_P44LRGRAPHICS
  #define ENABLE_P44LRGRAPHICS 0 // p44lrgraphics support in some of the p44utils components
#endif
#ifndef ENABLE_APPLICATION_SUPPORT
  #define ENABLE_APPLICATION_SUPPORT 0 // support for Application (e.g. domain specific commandline options) in other parts of P44 utils
#endif
#ifndef ENABLE_JSON_APPLICATION
  #define ENABLE_JSON_APPLICATION 0 // enables JSON utilities in Application, requires json-c
#endif

#if CHIP_SYSTEM_CONFIG_USE_LIBEV
  #define MAINLOOP_LIBEV_BASED 1
#else
  #error "using p44 mainloop needs libev support in CHIP"
#endif


#endif // __p44utils__config__
