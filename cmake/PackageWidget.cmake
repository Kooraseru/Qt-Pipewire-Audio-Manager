set(widget_id org.kooraseru.plasma.audiomanager)
set(stage "${BUILD_DIR}/widget-stage")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
    --prefix "${stage}" --component Widget COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${BUILD_DIR}/${widget_id}.plasmoid"
    --format=zip metadata.json contents
    WORKING_DIRECTORY "${stage}/share/plasma/plasmoids/${widget_id}"
    COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "Widget package: ${BUILD_DIR}/${widget_id}.plasmoid")
