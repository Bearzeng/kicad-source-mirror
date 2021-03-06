#add_definitions(-DPRINT_STATISTICS_3D_VIEWER)
add_definitions(-DPCBNEW)

configure_file( 3d_plugin_dir.h.in 3d_plugin_dir.h @ONLY )

include_directories(BEFORE ${INC_BEFORE})
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ../pcbnew
    3d_canvas
    3d_cache
    3d_rendering
    3d_viewer
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include/gal/opengl
    ${INC_AFTER}
    )

# directories
set( DIR_DLG 3d_cache/dialogs )
set( DIR_RAY 3d_rendering/3d_render_raytracing )
set( DIR_RAY_ACC ${DIR_RAY}/accelerators )
set( DIR_RAY_2D ${DIR_RAY}/shapes2D )
set( DIR_RAY_3D ${DIR_RAY}/shapes3D )
set( DIR_3D_PLUGINS ${CMAKE_SOURCE_DIR}/plugins/ldr )

set(3D-VIEWER_SRCS
    ${DIR_3D_PLUGINS}/pluginldr.cpp
    ${DIR_3D_PLUGINS}/3d/pluginldr3D.cpp
    3d_cache/3d_cache.cpp
    3d_cache/3d_plugin_manager.cpp
    ${DIR_DLG}/3d_cache_dialogs.cpp
    ${DIR_DLG}/dlg_select_3dmodel_base.cpp
    ${DIR_DLG}/dlg_select_3dmodel.cpp
    ${DIR_DLG}/panel_prev_3d_base.cpp
    ${DIR_DLG}/panel_prev_3d.cpp
    3d_canvas/board_adapter.cpp
    3d_canvas/create_layer_items.cpp
    3d_canvas/create_3Dgraphic_brd_items.cpp
    3d_canvas/create_layer_poly.cpp
    3d_canvas/eda_3d_canvas.cpp
    3d_canvas/eda_3d_canvas_pivot.cpp
    3d_model_viewer/c3d_model_viewer.cpp
    3d_rendering/3d_render_ogl_legacy/c_ogl_3dmodel.cpp
    3d_rendering/3d_render_ogl_legacy/ogl_legacy_utils.cpp
    3d_rendering/3d_render_ogl_legacy/c3d_render_createscene_ogl_legacy.cpp
    3d_rendering/3d_render_ogl_legacy/c3d_render_ogl_legacy.cpp
    3d_rendering/3d_render_ogl_legacy/clayer_triangles.cpp
    ${DIR_RAY_ACC}/caccelerator.cpp
    ${DIR_RAY_ACC}/cbvh_packet_traversal.cpp
    ${DIR_RAY_ACC}/cbvh_pbrt.cpp
    ${DIR_RAY_ACC}/ccontainer.cpp
    ${DIR_RAY_ACC}/ccontainer2d.cpp
    ${DIR_RAY}/PerlinNoise.cpp
    ${DIR_RAY}/c3d_render_createscene.cpp
    ${DIR_RAY}/c3d_render_raytracing.cpp
    ${DIR_RAY}/cfrustum.cpp
    ${DIR_RAY}/cmaterial.cpp
    ${DIR_RAY}/mortoncodes.cpp
    ${DIR_RAY}/ray.cpp
    ${DIR_RAY}/raypacket.cpp
    ${DIR_RAY_2D}/cbbox2d.cpp
    ${DIR_RAY_2D}/cfilledcircle2d.cpp
    ${DIR_RAY_2D}/citemlayercsg2d.cpp
    ${DIR_RAY_2D}/cobject2d.cpp
    ${DIR_RAY_2D}/cpolygon2d.cpp
    ${DIR_RAY_2D}/cpolygon4pts2d.cpp
    ${DIR_RAY_2D}/cring2d.cpp
    ${DIR_RAY_2D}/croundsegment2d.cpp
    ${DIR_RAY_2D}/ctriangle2d.cpp
    ${DIR_RAY_3D}/cbbox.cpp
    ${DIR_RAY_3D}/cbbox_ray.cpp
    ${DIR_RAY_3D}/ccylinder.cpp
    ${DIR_RAY_3D}/cdummyblock.cpp
    ${DIR_RAY_3D}/clayeritem.cpp
    ${DIR_RAY_3D}/cobject.cpp
    ${DIR_RAY_3D}/cplane.cpp
    ${DIR_RAY_3D}/croundseg.cpp
    ${DIR_RAY_3D}/ctriangle.cpp
    3d_rendering/buffers_debug.cpp
    3d_rendering/c3d_render_base.cpp
    3d_rendering/ccamera.cpp
    3d_rendering/ccolorrgb.cpp
    3d_rendering/cimage.cpp
    3d_rendering/cpostshader.cpp
    3d_rendering/cpostshader_ssao.cpp
    3d_rendering/ctrack_ball.cpp
    3d_rendering/test_cases.cpp
    3d_rendering/trackball.cpp
    3d_viewer/3d_menubar.cpp
    3d_viewer/3d_toolbar.cpp
    3d_viewer/dialogs/dialog_3D_view_option.cpp
    3d_viewer/dialogs/dialog_3D_view_option_base.cpp
    3d_viewer/tools/3d_actions.cpp
    3d_viewer/tools/3d_conditions.cpp
    3d_viewer/tools/3d_controller.cpp
    3d_viewer/eda_3d_viewer.cpp
    3d_viewer/3d_viewer_settings.cpp
    common_ogl/cogl_att_list.cpp
    common_ogl/ogl_utils.cpp
    3d_fastmath.cpp
    3d_math.cpp
    )

add_library(3d-viewer STATIC ${3D-VIEWER_SRCS})
add_dependencies( 3d-viewer pcbcommon )

target_link_libraries( 3d-viewer
                       gal
                       kimath
                       ${Boost_LIBRARIES}
                       ${wxWidgets_LIBRARIES}
                       ${OPENGL_LIBRARIES}
                       kicad_3dsg )

target_include_directories( 3d-viewer PRIVATE
    $<TARGET_PROPERTY:nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>
    )

add_subdirectory( 3d_cache )
