cmake_minimum_required (VERSION 3.6)

project(Tutorial20_MeshShader CXX)

set(SOURCE
    src/Tutorial20_MeshShader.cpp
    src/ufbx/ufbx.c
    src/octree/octree.cpp
    src/binvox/binvox_loader.cpp
)

set(INCLUDE
    src/Tutorial20_MeshShader.hpp
    src/voxelizer.h
    src/ufbx/ufbx.h
    src/DrawTask.h
    src/octree/octree.h
    src/octree/aabb.h
    src/binvox/binvox_loader.h
)

set(SHADERS
    assets/cube_ash.hlsl
    assets/cube_bestOc_ash.hlsl
    assets/cube_msh.hlsl
    assets/cube_psh.hlsl
    assets/structures.fxh
    assets/depth_reprojection_comp.hlsl
)

set(ASSETS
    assets/DGLogo.png
    assets/models/suzanne.fbx
    assets/models/sponza.fbx
    assets/models/suzanne.binvox
)

add_sample_app("Tutorial20_MeshShader" "DiligentSamples/Tutorials" "${SOURCE}" "${INCLUDE}" "${SHADERS}" "${ASSETS}")
