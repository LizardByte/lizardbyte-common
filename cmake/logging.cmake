# Reuse a consumer-provided spdlog target, otherwise use the pinned submodule.
if(NOT TARGET spdlog::spdlog)
    set(SPDLOG_INSTALL ${LIZARDBYTE_COMMON_INSTALL})
    add_subdirectory(third-party/spdlog)
endif()
