#
# Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
# Copyright (C) 2021+ WarheadCore <https://github.com/WarheadCore>
#

# Add source files
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ServerAutoShutdown_SC.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ServerAutoShutdown.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/ServerAutoShutdown.h")

# Add scripts to script loader
AC_ADD_SCRIPT_LOADER("ServerAutoShutdown" "${CMAKE_CURRENT_LIST_DIR}/src/server_auto_shutdown_loader.h")
