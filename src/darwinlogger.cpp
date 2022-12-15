//
//  darwinlogger.cpp
//  p44mbrd
//
//  Created by Lukas Zeller on 15.12.22.
//

#include <platform/logging/LogV.h>
#include "logger.hpp"

namespace chip {
namespace Logging {
namespace Platform {

void LogV(const char * module, uint8_t category, const char * msg, va_list v)
{
  if (LOGENABLED(LOG_INFO)) {
    globalLogger.logV(LOG_INFO, true, msg, v);
  }
}

} // namespace Platform
} // namespace Logging
} // namespace chip
