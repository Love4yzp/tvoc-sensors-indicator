# ESP-IDF host (linux target) unit test for the UI interaction layer.
#
# Runs the REAL esp_event loop + POSIX FreeRTOS on the host — no hardware — to
# prove the invariant the UI-freeze fix relies on: ui_event_post() never blocks,
# even when the view-event queue is full, so it can never deadlock the LVGL lock
# or the event loop.
#
#   idf.py --preview set-target linux
#   idf.py build
#   ./build/host_ui_event_test.elf        (or: ./dev test host)
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main)
project(host_ui_event_test)
