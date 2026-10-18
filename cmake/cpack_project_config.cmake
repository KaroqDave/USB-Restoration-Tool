# Loaded by cpack, not by configure. polkit reads actions only from
# /usr/share/polkit-1/actions, and the helper path is compiled in at configure
# time, so a package staged under any other prefix would install a helper that
# nothing can start.
if(NOT CPACK_USBRESTORE_INSTALL_PREFIX STREQUAL "/usr" OR NOT CPACK_PACKAGING_INSTALL_PREFIX STREQUAL "/usr")
    message(FATAL_ERROR
        "The ${CPACK_GENERATOR} package must be built for /usr: configure with -DCMAKE_INSTALL_PREFIX=/usr "
        "and do not override CPACK_PACKAGING_INSTALL_PREFIX. Configured prefix: "
        "'${CPACK_USBRESTORE_INSTALL_PREFIX}', packaging prefix: '${CPACK_PACKAGING_INSTALL_PREFIX}'.")
endif()
