# Pull in the production ui_event.c (kept LVGL-free for exactly this reason) and
# test the real function, not a copy.
set(MAIN_SRC "${CMAKE_CURRENT_LIST_DIR}/../../../main")

idf_component_register(
    SRCS "test_app_main.c"
         "test_ui_event.c"
         "${MAIN_SRC}/ui/ui_event.c"
    INCLUDE_DIRS "." "${MAIN_SRC}" "${MAIN_SRC}/ui"
    REQUIRES unity esp_event
    WHOLE_ARCHIVE)
