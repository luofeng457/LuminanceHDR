IF(NOT CMAKE_BUILD_TYPE)
SET(CMAKE_BUILD_TYPE "RelWithDebInfo" )
ENDIF()

# MESSAGE(${CMAKE_BUILD_TYPE})

IF(${CMAKE_BUILD_TYPE} STREQUAL "Release" OR ${CMAKE_BUILD_TYPE} STREQUAL "MinSizeRel")
ADD_DEFINITIONS("-DQT_NO_DEBUG_OUTPUT")
ENDIF()

# Holds al the file that will be translated
SET(FILES_TO_TRANSLATE )

SET(LUMINANCE_MODULES_CLI )
SET(LUMINANCE_MODULES_GUI )


