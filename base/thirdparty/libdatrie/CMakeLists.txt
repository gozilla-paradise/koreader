list(APPEND CFG_CMD COMMAND env)
append_autotools_vars(CFG_CMD)
list(APPEND CFG_CMD
    ${SOURCE_DIR}/configure --host=${CHOST} --prefix=${STAGING_DIR}
    --disable-$<IF:$<BOOL:${MONOLIBTIC}>,shared,static>
    --enable-$<IF:$<BOOL:${MONOLIBTIC}>,static,shared>
    --disable-doxygen-doc
    --enable-silent-rules
)

list(APPEND BUILD_CMD COMMAND make)
list(APPEND INSTALL_CMD COMMAND make install)
# libthai's data/Makefile.am hardcodes the tool name `trietool-0.2`
# even though libdatrie installs the binary as plain `trietool`.
if(CMAKE_CROSSCOMPILING)
    # The installed `trietool` is a target binary (e.g. Android ARM)
    # that cannot run on the build host, but libthai's data stage must
    # invoke it during the build. Find a host `trietool` (e.g. from
    # `brew install libdatrie` on macOS or a distro package on Linux)
    # and overwrite STAGING_DIR/bin/trietool{,-0.2} with symlinks to it.
    find_program(HOST_TRIETOOL trietool REQUIRED)
    list(APPEND INSTALL_CMD
        COMMAND ${CMAKE_COMMAND} -E rm -f ${STAGING_DIR}/bin/trietool ${STAGING_DIR}/bin/trietool-0.2
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${HOST_TRIETOOL} ${STAGING_DIR}/bin/trietool
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${HOST_TRIETOOL} ${STAGING_DIR}/bin/trietool-0.2
    )
else()
    list(APPEND INSTALL_CMD COMMAND ${CMAKE_COMMAND} -E create_symlink trietool ${STAGING_DIR}/bin/trietool-0.2)
endif()

if(NOT MONOLIBTIC)
    set(LIB_SPEC datrie VERSION 1)
    if(APPLE)
        append_shared_lib_fix_commands(INSTALL_CMD ${LIB_SPEC} ID)
    endif()
    append_shared_lib_install_commands(INSTALL_CMD ${LIB_SPEC})
endif()

external_project(
    DOWNLOAD URL e26b5aa008b5f3588ab38d2dce9e9325
    https://linux.thai.net/pub/thailinux/software/libthai/libdatrie-0.2.13.tar.xz
    CONFIGURE_COMMAND ${CFG_CMD}
    BUILD_COMMAND ${BUILD_CMD}
    INSTALL_COMMAND ${INSTALL_CMD}
)
