# Portable ZIP for delivery: both programs, their DLLs, a user guide, third-party licenses and
# the com0com driver files the Simulator installs on request. Build it with
#   cmake --workflow --preset package
# Needs POSMV_RUNTIME_DIR (the compiler's bin directory) from the caller.
option(POSMV_BUNDLE_COM0COM "Bundle the com0com 2.2.2.0 installer, its source and license" ON)
get_filename_component(POSMV_MSYS_ROOT "${POSMV_RUNTIME_DIR}/../.." ABSOLUTE)

install(TARGETS posmv-sweep posmv-simulator RUNTIME DESTINATION .)
# DLLs: the same recursive scan the build uses, run against the installed programs.
install(CODE "
  foreach(app posmv-sweep.exe posmv-simulator.exe)
    set(APP \"\${CMAKE_INSTALL_PREFIX}/\${app}\")
    set(DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
    set(RUNTIME_DIR \"${POSMV_RUNTIME_DIR}\")
    set(OBJDUMP \"${CMAKE_OBJDUMP}\")
    include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployRuntime.cmake\")
  endforeach()
")
if(POSMV_BUNDLE_COM0COM)
  install(CODE "
    set(DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
    set(DOWNLOAD_CACHE \"${CMAKE_BINARY_DIR}/third_party\")
    include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/Com0com.cmake\")
  ")
endif()
install(FILES docs/delivery/README.txt DESTINATION .)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  install(FILES LICENSE DESTINATION . RENAME LICENSE.txt)
else()
  message(WARNING "No LICENSE file: the package will not state the project's own license")
endif()
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE POSMV_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
string(TIMESTAMP POSMV_BUILD_DATE "%Y-%m-%d")
install(CODE "file(WRITE \"\${CMAKE_INSTALL_PREFIX}/BUILD-INFO.txt\"
  \"POSMV-Sweep ${PROJECT_VERSION}\\nCommit ${POSMV_COMMIT}\\nBuilt ${POSMV_BUILD_DATE} (${CMAKE_BUILD_TYPE})\\n\")")
# Last, so it sees every DLL installed above.
install(CODE "
  set(DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
  set(MSYS_ROOT \"${POSMV_MSYS_ROOT}\")
  set(COM0COM ${POSMV_BUNDLE_COM0COM})
  include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThirdPartyNotices.cmake\")
")

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME POSMV-Sweep)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "POSMV-Sweep-${PROJECT_VERSION}-win64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
