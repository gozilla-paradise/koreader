list(APPEND CFG_CMD COMMAND env)
append_autotools_vars(CFG_CMD)
list(APPEND CFG_CMD
    "PATH=${STAGING_DIR}/bin:$ENV{PATH}"
    DATRIE_CFLAGS=-I${STAGING_DIR}/include
    DATRIE_LIBS=-L${STAGING_DIR}/lib\ -ldatrie
    ${SOURCE_DIR}/configure --host=${CHOST} --prefix=${STAGING_DIR}
    --disable-$<IF:$<BOOL:${MONOLIBTIC}>,shared,static>
    --enable-$<IF:$<BOOL:${MONOLIBTIC}>,static,shared>
    --disable-doxygen-doc
    --enable-silent-rules
)

list(APPEND BUILD_CMD COMMAND env "PATH=${STAGING_DIR}/bin:$ENV{PATH}" make)
list(APPEND INSTALL_CMD COMMAND env "PATH=${STAGING_DIR}/bin:$ENV{PATH}" make install)

if(NOT MONOLIBTIC)
    set(LIB_SPEC thai VERSION 0)
    if(APPLE)
        append_shared_lib_fix_commands(INSTALL_CMD ${LIB_SPEC} ID)
    endif()
    append_shared_lib_install_commands(INSTALL_CMD ${LIB_SPEC})
endif()

# Copy the generated stock dictionary into the KOReader bundle so the
# Thai segmenter (ThaiSegmenter::configure) can find it at runtime.
list(APPEND INSTALL_CMD COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}/data/thai)
list(APPEND INSTALL_CMD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STAGING_DIR}/share/libthai/thbrk.tri ${OUTPUT_DIR}/data/thai/thbrk.tri)

external_project(
    DOWNLOAD URL c1fe8255d2bdfc5ea4f68dd9aff8b7f1
    https://linux.thai.net/pub/thailinux/software/libthai/libthai-0.1.29.tar.xz
    CONFIGURE_COMMAND ${CFG_CMD}
    BUILD_COMMAND ${BUILD_CMD}
    INSTALL_COMMAND ${INSTALL_CMD}
    DEPENDS libdatrie
)
