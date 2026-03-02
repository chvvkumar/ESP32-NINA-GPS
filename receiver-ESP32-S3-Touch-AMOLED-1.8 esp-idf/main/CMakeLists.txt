idf_component_register(
    SRCS "main.c"
         "wifi.c"
         "espnow.c"
         "display.c"
         "ui_tabs.c"
         "config.c"
         "web_server.c"
    INCLUDE_DIRS "."
)

# ── LVGL compiler flags ──────────────────────────────────────────────
idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -Wno-attributes
        -Wno-unused-function
)

# ── Git metadata → compile definitions ───────────────────────────────
if(GIT_TAG)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_GIT_TAG="${GIT_TAG}")
else()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_GIT_TAG="unknown")
endif()

if(GIT_SHA)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_GIT_SHA="${GIT_SHA}")
else()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_GIT_SHA="unknown")
endif()

if(GIT_BRANCH)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_GIT_BRANCH="${GIT_BRANCH}")
else()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_GIT_BRANCH="unknown")
endif()
