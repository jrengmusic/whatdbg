/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION
   ID:                          jreng_core
   vendor:                      Jubilant Research of Eclectic of Novelty in Generating music
   version:                     0.0.1
   name:                        JRENG Core
   description:                 JRENG Core
   website:                     https://jrengmusic.com
   license:                     Proprietary
   dependencies:                juce_core
   OSXFrameworks:
   iOSFrameworks:
  END_JUCE_MODULE_DECLARATION
 *******************************************************************************/

#pragma once

//==============================================================================
#include <ciso646>
#include <assert.h>
#include <any>
#include <juce_core/juce_core.h>

#include "context/jreng_context.h"
#include "utilities/jreng_owner.h"
#include "utilities/jreng_range.h"
#include "utilities/jreng_zip.h"
#include "utilities/jreng_math.h"
#include "utilities/jreng_toInt.h"
#include "utilities/jreng_any_owner.h"
#include "function_map/jreng_function_map.h"
