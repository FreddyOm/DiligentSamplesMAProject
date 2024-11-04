/*
 *  Copyright 2019-2022 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include <array>
#include "Tutorial20_MeshShader.hpp"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "ShaderMacroHelper.hpp"
#include "imgui.h"
#include "ImGuiUtils.hpp"
#include "FastRand.hpp"
#include <set>
#include <unordered_set>
#include <d3d12.h>
#include "../../../../DiligentCore/Graphics/GraphicsEngineD3D12/include/d3dx12_win.h"

#include "binvox/binvox_loader.h"

extern std::vector<AABB> OTVoxelBoundBuffer;

namespace Diligent
{
    namespace
    {
        #include "../assets/structures.fxh"
        
        struct DrawStatistics
        {
            Uint32 visibleCubes;
            Uint32 visibleOctreeNodes;
        };
        
        static_assert(sizeof(OctreeLeafNode) % 16 == 0, "Structure must be 16-byte aligned");
    
    } // namespace

    SampleBase* CreateSample()
    {
        return new Tutorial20_MeshShader();
    }
    
    void Tutorial20_MeshShader::CreateDrawTasks()
    {
        // In this tutorial draw tasks contain:
        //  * cube position in the grid
        //  * cube scale factor
        //  * time that is used for animation and will be updated in the shader.
        // Additionally you can store model transformation matrix, mesh and material IDs, etc.
    
        const int2          GridDim{256, 256};
        FastRandReal<float> Rnd{0, 0.f, 1.f};
    
        std::vector<OctreeLeafNode> DrawTasks;
        DrawTasks.resize(static_cast<size_t>(GridDim.x) * static_cast<size_t>(GridDim.y));
    
        for (int y = 0; y < GridDim.y; ++y)
        {
            for (int x = 0; x < GridDim.x; ++x)
            {
                int   idx = x + y * GridDim.x;
                auto& dst = DrawTasks[idx];
    
                dst.BasePosAndScale.x  = static_cast<float>((x - GridDim.x / 2) * 2);
                dst.BasePosAndScale.y  = static_cast<float>((y - GridDim.y / 2) * 2);
                dst.BasePosAndScale.w  = 1.f; // 0.5 .. 1
                dst.RandomValue        = {Rnd(), 0, 0, 0};
            }
        }
    
        BufferDesc BuffDesc;
        BuffDesc.Name              = "Draw tasks buffer";
        BuffDesc.Usage             = USAGE_DEFAULT;
        BuffDesc.BindFlags         = BIND_SHADER_RESOURCE;
        BuffDesc.Mode              = BUFFER_MODE_STRUCTURED;
        BuffDesc.ElementByteStride = sizeof(DrawTasks[0]);
        BuffDesc.Size              = sizeof(DrawTasks[0]) * static_cast<Uint32>(DrawTasks.size());
    
        BufferData BufData;
        BufData.pData    = DrawTasks.data();
        BufData.DataSize = BuffDesc.Size;
    
        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_pVoxelPosBuffer);
        VERIFY_EXPR(m_pVoxelPosBuffer != nullptr);
    
        m_DrawTaskCount = static_cast<Uint32>(DrawTasks.size());
    }
    
    void Tutorial20_MeshShader::CreateStatisticsBuffer()
    {
        // This buffer is used as an atomic counter in the amplification shader to show
        // how many cubes are rendered with and without frustum culling.
    
        BufferDesc BuffDesc;
        BuffDesc.Name      = "Statistics buffer";
        BuffDesc.Usage     = USAGE_DEFAULT;
        BuffDesc.BindFlags = BIND_UNORDERED_ACCESS;
        BuffDesc.Mode      = BUFFER_MODE_RAW;
        BuffDesc.Size      = sizeof(DrawStatistics);
    
        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_pStatisticsBuffer);
        VERIFY_EXPR(m_pStatisticsBuffer != nullptr);
    
        // Staging buffer is needed to read the data from statistics buffer.
    
        BuffDesc.Name           = "Statistics staging buffer";
        BuffDesc.Usage          = USAGE_STAGING;
        BuffDesc.BindFlags      = BIND_NONE;
        BuffDesc.Mode           = BUFFER_MODE_UNDEFINED;
        BuffDesc.CPUAccessFlags = CPU_ACCESS_READ;
        BuffDesc.Size           = sizeof(DrawStatistics) * m_StatisticsHistorySize;
    
        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_pStatisticsStaging);
        VERIFY_EXPR(m_pStatisticsStaging != nullptr);
    
        FenceDesc FDesc;
        FDesc.Name = "Statistics available";
        m_pDevice->CreateFence(FDesc, &m_pStatisticsAvailable);
    }
    
    void Tutorial20_MeshShader::CreateConstantsBuffer()
    {
        BufferDesc BuffDesc;
        BuffDesc.Name           = "Constant buffer";
        BuffDesc.Usage          = USAGE_DYNAMIC;
        BuffDesc.BindFlags      = BIND_UNIFORM_BUFFER;
        BuffDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
        BuffDesc.Size           = sizeof(Constants);
    
        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_pConstants);
        VERIFY_EXPR(m_pConstants != nullptr);
    }
    
    void Tutorial20_MeshShader::LoadTexture()
    {
        TextureLoadInfo loadInfo;
        loadInfo.IsSRGB = true;
        RefCntAutoPtr<ITexture> pTex;
        CreateTextureFromFile("DGLogo.png", loadInfo, m_pDevice, &pTex);
        VERIFY_EXPR(pTex != nullptr);
    
        m_CubeTextureSRV = pTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        VERIFY_EXPR(m_CubeTextureSRV != nullptr);
    }
    
    Tutorial20_MeshShader::~Tutorial20_MeshShader()
    {
        delete m_pOcclusionOctreeRoot;
    }
    
    void Tutorial20_MeshShader::CreateDrawTasksFromMesh(std::string meshPath)
    {    
        PopulateOctree(meshPath);

        VERIFY_EXPR(m_pOcclusionOctreeRoot != nullptr);
        VERIFY_EXPR(m_pOcclusionOctreeRoot->gridSize > 0);
        VERIFY_EXPR(m_pOcclusionOctreeRoot->nodeBuffer.size() > 0);
        
        // Buffer where objects in one node are stored contigously (start index + length)
        std::vector<VoxelOC::VoxelBufData>  orderedVoxelDataBuffer{};
        orderedVoxelDataBuffer.reserve(OTVoxelBoundBuffer.size());
        
        // Buffer for all octree nodes which include at least one voxel
        std::vector<VoxelOC::OctreeLeafNode> OTLeafNodes{};
        OTLeafNodes.reserve(static_cast<int>(OTVoxelBoundBuffer.size() / 2.0f));

        // Buffer for full octree nodes which represent best occluders
        std::vector<VoxelOC::DepthPrepassDrawTask> depthPrepassOTNodes;
        depthPrepassOTNodes.reserve(OTLeafNodes.size());
        
        {
            // Visist all nodes and fill the given buffers with data
            m_pOcclusionOctreeRoot->QueryAllNodes(orderedVoxelDataBuffer, OTLeafNodes);
            VERIFY_EXPR(orderedVoxelDataBuffer.size() > 0 && OTLeafNodes.size() > 0);

            // Visit all nodes and search for "full" nodes
            m_pOcclusionOctreeRoot->QueryBestOccluders(depthPrepassOTNodes);
            VERIFY_EXPR(depthPrepassOTNodes.size() > 0);        // Couldn't find any best occluders
        }
        
        // Temporary buffer for octree insertion - can now be cleared (but was formerly used in QueryAllNodes()!)
        OTVoxelBoundBuffer.clear();
        // Assign some more (debug) data to the draw tasks (= octree leaf nodes)
        FastRandReal<float> Rnd{0, 0.f, 1.f};

#if DILIGENT_DEBUG
        
        for (auto& voxPos : orderedVoxelDataBuffer)
        {
            VERIFY_EXPR(voxPos.BasePosAndScale.w == 1);
        }

#endif

        for (auto& task : OTLeafNodes)
        {
            task.RandomValue.x = Rnd();
            task.RandomValue.y = 0;
            task.RandomValue.z = 0;
            VERIFY_EXPR(task.BasePosAndScale.w >= 4);
        }

        for (auto& task : depthPrepassOTNodes)
        {
            task.BestOccluderCount = static_cast<int>(depthPrepassOTNodes.size());
            VERIFY_EXPR(task.BasePositionAndScale.w >= 4);
        }


        // Bind buffer resources to GPU
        BindSortedIndexBuffer(orderedVoxelDataBuffer);
        BindOctreeNodeBuffer(OTLeafNodes);

        m_DepthPassDrawTaskCount        = static_cast<Uint32>(orderedVoxelDataBuffer.size());
        m_DepthPassDrawTaskCountAligned = (m_DepthPassDrawTaskCount / 64) + (ASGroupSize - ((m_DepthPassDrawTaskCount / 64) % ASGroupSize));
        VERIFY_EXPR(m_DepthPassDrawTaskCountAligned % ASGroupSize == 0);

        BindVisibilityBuffer(orderedVoxelDataBuffer);
        
        // Set draw task count
        m_DrawTaskCount = static_cast<Uint32>(OTLeafNodes.size());
        VERIFY_EXPR(m_DrawTaskCount % ASGroupSize == 0);        
    }

    void Tutorial20_MeshShader::PopulateOctree(std::string OTmodelPath)
    {
        BinvoxData data = read_binvox(OTmodelPath);

        AABB worldBounds       = {{0, 0, 0}, {(float)data.width, (float)data.height, (float)data.depth}};
        m_pOcclusionOctreeRoot = new OctreeNode<VoxelOC::OctreeLeafNode>(worldBounds, OTVoxelBoundBuffer, (size_t)(worldBounds.max.x - worldBounds.min.x), worldBounds, ASGroupSize);

        for (int z = 0; z < data.depth; ++z)
        {
            for (int y = 0; y < data.height; ++y)
            {
                for (int x = 0; x < data.width; ++x)
                {
                    size_t index = get_index(x, y, z, data);
                    if (data.voxels[index] > 0)
                    {
                        AABB voxelBounds = {{(float)x, (float)y, (float)z}, {x + 1.f, y + 1.f, z + 1.f}};
                        OTVoxelBoundBuffer.push_back(std::move(voxelBounds));
                        m_pOcclusionOctreeRoot->InsertObject(OTVoxelBoundBuffer.size() - 1, voxelBounds);
                    }
                }
            }
        }
    }

    void Tutorial20_MeshShader::BindSortedIndexBuffer(std::vector<VoxelOC::VoxelBufData>& orderedVoxelDataBuffer)
    {
        BufferDesc BuffDesc;
        BuffDesc.Name              = "Ordered voxel data buffer";
        BuffDesc.Usage             = USAGE_DEFAULT;
        BuffDesc.BindFlags         = BIND_SHADER_RESOURCE;
        BuffDesc.Mode              = BUFFER_MODE_STRUCTURED;
        BuffDesc.ElementByteStride = sizeof(orderedVoxelDataBuffer[0]);
        BuffDesc.Size              = sizeof(orderedVoxelDataBuffer[0]) * static_cast<Uint32>(orderedVoxelDataBuffer.size());

        BufferData BufData;
        BufData.pData    = orderedVoxelDataBuffer.data();
        BufData.DataSize = BuffDesc.Size;

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_pVoxelPosBuffer);
        VERIFY_EXPR(m_pVoxelPosBuffer != nullptr);
    }

    void Tutorial20_MeshShader::BindOctreeNodeBuffer(std::vector<VoxelOC::OctreeLeafNode>& octreeNodeBuffer)
    {
        // Realign octree node buffer
        //octreeNodeBuffer.resize(octreeNodeBuffer.size() + ASGroupSize - (octreeNodeBuffer.size() % ASGroupSize));
        VERIFY_EXPR(octreeNodeBuffer.size() % ASGroupSize == 0);

        BufferDesc BuffDesc;
        BuffDesc.Name              = "Octree node buffer";
        BuffDesc.Usage             = USAGE_DEFAULT;
        BuffDesc.BindFlags         = BIND_SHADER_RESOURCE;
        BuffDesc.Mode              = BUFFER_MODE_STRUCTURED;
        BuffDesc.ElementByteStride = sizeof(octreeNodeBuffer[0]);
        BuffDesc.Size              = sizeof(octreeNodeBuffer[0]) * static_cast<Uint32>(octreeNodeBuffer.size());

        BufferData BufData;
        BufData.pData    = octreeNodeBuffer.data();
        BufData.DataSize = BuffDesc.Size;

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_pOctreeNodeBuffer);
        VERIFY_EXPR(m_pOctreeNodeBuffer != nullptr);
    }
    
    void Tutorial20_MeshShader::BindVisibilityBuffer(std::vector<VoxelOC::VoxelBufData>& orderedVoxelDataBuffer)
    {
        if (orderedVoxelDataBuffer.size() == 0) return;

        std::vector<Uint32> vizBuf(orderedVoxelDataBuffer.size(), 0);

        BufferDesc BuffDesc;
        BuffDesc.Name              = "Visibility buffer";
        BuffDesc.Usage             = USAGE_DEFAULT;
        BuffDesc.BindFlags         = BIND_UNORDERED_ACCESS;
        BuffDesc.Mode              = BUFFER_MODE_RAW;
        BuffDesc.ElementByteStride = sizeof(vizBuf[0]);
        BuffDesc.Size              = sizeof(Uint32) * static_cast<Uint32>(vizBuf.size());

        BufferData BufData;
        BufData.pData    = vizBuf.data();
        BufData.DataSize = BuffDesc.Size;

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_pVisibilityBuffer);
        VERIFY_EXPR(m_pVisibilityBuffer != nullptr);
    }

    void Tutorial20_MeshShader::CreatePipelineState()
    {
        // Pipeline state object encompasses configuration of all GPU stages
    
        GraphicsPipelineStateCreateInfo PSOCreateInfo;
        PipelineStateDesc&              PSODesc = PSOCreateInfo.PSODesc;
    
        PSODesc.Name = "Mesh shader";
    
        PSODesc.PipelineType                                                = PIPELINE_TYPE_MESH;
        PSOCreateInfo.GraphicsPipeline.NumRenderTargets                     = 1;
        PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                        = m_pSwapChain->GetDesc().ColorBufferFormat;
        PSOCreateInfo.GraphicsPipeline.DSVFormat                            = m_pSwapChain->GetDesc().DepthBufferFormat;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode              = CULL_MODE_BACK;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FillMode              = FILL_MODE_SOLID; 
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = False;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.DepthBias             = 0;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.DepthBiasClamp        = 0.0f;
        PSOCreateInfo.GraphicsPipeline.RasterizerDesc.SlopeScaledDepthBias  = 0.0f;
        PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable         = True;
    
        // Topology is defined in the mesh shader, this value is not used.
        PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_UNDEFINED;
    
        // Define variable type that will be used by default.
        PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    
        // For Direct3D12 we must use the new DXIL compiler that supports mesh shaders.
        ShaderCI.ShaderCompiler = SHADER_COMPILER_DXC;
    
        ShaderCI.Desc.UseCombinedTextureSamplers = true;
    
        // Create a shader source stream factory to load shaders from files.
        RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
        m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
        ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
    
        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("GROUP_SIZE", ASGroupSize);
    
        ShaderCI.Macros = Macros;
    
        RefCntAutoPtr<IShader> pAS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_AMPLIFICATION;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Amplification shader - AS";
            ShaderCI.FilePath        = "cube_ash.hlsl";
    
            m_pDevice->CreateShader(ShaderCI, &pAS);
            VERIFY_EXPR(pAS != nullptr);
        }

        RefCntAutoPtr<IShader> pASOcclusionQuery;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_AMPLIFICATION;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Mesh shader raster oc query - AS";
            ShaderCI.FilePath        = "raster_oc_ash.hlsl";

            m_pDevice->CreateShader(ShaderCI, &pASOcclusionQuery);
            VERIFY_EXPR(pASOcclusionQuery != nullptr);
        }
    
        RefCntAutoPtr<IShader> pMSOcclusionQuery;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_MESH;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Mesh shader raster oc query - MS";
            ShaderCI.FilePath        = "raster_oc_msh.hlsl";

            m_pDevice->CreateShader(ShaderCI, &pMSOcclusionQuery);
            VERIFY_EXPR(pMSOcclusionQuery != nullptr);
        }

        RefCntAutoPtr<IShader> pMS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_MESH;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Mesh shader - MS";
            ShaderCI.FilePath        = "cube_msh.hlsl";
    
            m_pDevice->CreateShader(ShaderCI, &pMS);
            VERIFY_EXPR(pMS != nullptr);
        }
    
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Mesh shader - PS";
            ShaderCI.FilePath        = "cube_psh.hlsl";
    
            m_pDevice->CreateShader(ShaderCI, &pPS);
            VERIFY_EXPR(pPS != nullptr);
        }

        // clang-format off
        // Define immutable sampler for g_Texture. Immutable samplers should be used whenever possible
        SamplerDesc SamLinearClampDesc
        {
            FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, 
            TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
        };

        ImmutableSamplerDesc ImtblSamplers[] = 
        {
            {SHADER_TYPE_PIXEL, "g_Texture", SamLinearClampDesc}
        };
        // clang-format on
        PSODesc.ResourceLayout.ImmutableSamplers    = ImtblSamplers;
        PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);


        ShaderResourceVariableDesc shaderResourceVariableDesc[1];

        shaderResourceVariableDesc[0].Name         = "VisibilityBuffer";
        shaderResourceVariableDesc[0].ShaderStages = SHADER_TYPE_AMPLIFICATION;
        shaderResourceVariableDesc[0].Type         = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        shaderResourceVariableDesc[0].Flags        = SHADER_VARIABLE_FLAG_NONE;

        PSOCreateInfo.PSODesc.ResourceLayout.Variables = &shaderResourceVariableDesc[0];
        PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(shaderResourceVariableDesc);

        PSOCreateInfo.pAS = pAS;
        PSOCreateInfo.pMS = pMS;
        PSOCreateInfo.pPS = pPS;
    
        m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);
        VERIFY_EXPR(m_pPSO != nullptr);
        
        m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
        VERIFY_EXPR(m_pSRB != nullptr);
    
        if (m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "Statistics"))
            m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "Statistics")->Set(m_pStatisticsBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
        
        if (m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VoxelPositionBuffer")) 
            m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VoxelPositionBuffer")->Set(m_pVoxelPosBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        
        if (m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "OctreeNodes"))
            m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "OctreeNodes")->Set(m_pOctreeNodeBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));

        if (m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VisibilityBuffer"))
            m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VisibilityBuffer")->Set(m_pVisibilityBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));

        if (m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "cbConstants"))
            m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "cbConstants")->Set(m_pConstants);
        
        if (m_pSRB->GetVariableByName(SHADER_TYPE_MESH, "cbConstants"))
            m_pSRB->GetVariableByName(SHADER_TYPE_MESH, "cbConstants")->Set(m_pConstants);
    
        if (m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture"))
            m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_CubeTextureSRV);

        if (m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbConstants"))
            m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbConstants")->Set(m_pConstants);


        CreateDepthPrepassPipeline(pASOcclusionQuery, pMSOcclusionQuery);
    }

    void Tutorial20_MeshShader::CreateDepthPrepassPipeline(Diligent::RefCntAutoPtr<Diligent::IShader>& pASOcclusionQuery, Diligent::RefCntAutoPtr<Diligent::IShader>& pMSOcclusionQuery)
    {
        // Create depth pass pipeline state
        GraphicsPipelineStateCreateInfo PSOCreateDepthOnlyPLInfo;
        PipelineStateDesc&              PSODepthOnlyPLDesc = PSOCreateDepthOnlyPLInfo.PSODesc;

        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable      = true;
        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.DepthStencilDesc.DepthFunc        = COMPARISON_FUNC_LESS;

        // Disable color output
        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.NumRenderTargets = 0;
        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RTVFormats[0]    = TEX_FORMAT_UNKNOWN;
        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.DSVFormat        = m_pSwapChain->GetDesc().DepthBufferFormat;

        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RasterizerDesc.CullMode             = CULL_MODE_BACK;
        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RasterizerDesc.FillMode             = FILL_MODE_SOLID;
        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RasterizerDesc.DepthBias            = 0;
        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RasterizerDesc.DepthBiasClamp       = 0.0f;
        PSOCreateDepthOnlyPLInfo.GraphicsPipeline.RasterizerDesc.SlopeScaledDepthBias = 0.0f;

        
        // Mesh shading pipeline setup
        PSODepthOnlyPLDesc.Name                               = "Raster occlusion pipeline";
        PSODepthOnlyPLDesc.PipelineType                       = PIPELINE_TYPE_MESH;
        PSODepthOnlyPLDesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

        ShaderResourceVariableDesc shaderResourceVariableDesc[2];

        shaderResourceVariableDesc[0].Name         = "VisibilityBuffer";
        shaderResourceVariableDesc[0].ShaderStages = SHADER_TYPE_AMPLIFICATION;
        shaderResourceVariableDesc[0].Type         = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        shaderResourceVariableDesc[0].Flags        = SHADER_VARIABLE_FLAG_NONE;

        shaderResourceVariableDesc[1].Name         = "VisibilityBuffer";
        shaderResourceVariableDesc[1].ShaderStages = SHADER_TYPE_MESH;
        shaderResourceVariableDesc[1].Type         = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        shaderResourceVariableDesc[1].Flags        = SHADER_VARIABLE_FLAG_NONE;

        PSOCreateDepthOnlyPLInfo.PSODesc.ResourceLayout.Variables = &shaderResourceVariableDesc[0];
        PSOCreateDepthOnlyPLInfo.PSODesc.ResourceLayout.NumVariables = _countof(shaderResourceVariableDesc);

        // No pixel shader needed for basic depth-only pass
        PSOCreateDepthOnlyPLInfo.pAS = pASOcclusionQuery;
        PSOCreateDepthOnlyPLInfo.pMS = pMSOcclusionQuery;
        PSOCreateDepthOnlyPLInfo.pPS = nullptr;

        // Create the pipeline state
        m_pDevice->CreateGraphicsPipelineState(PSOCreateDepthOnlyPLInfo, &m_pDepthOnlyPSO);
        VERIFY_EXPR(m_pDepthOnlyPSO != nullptr);

        // Create and populate the SRB
        m_pDepthOnlyPSO->CreateShaderResourceBinding(&m_pDepthOnlySRB, true);
        VERIFY_EXPR(m_pDepthOnlySRB != nullptr);

        // Bind resources

       if (m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VoxelPositionBuffer"))
            m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VoxelPositionBuffer")->Set(m_pVoxelPosBuffer->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));

        if (m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VisibilityBuffer"))
            m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VisibilityBuffer")->Set(m_pVisibilityBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));

        if (m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "cbConstants"))
            m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "cbConstants")->Set(m_pConstants);

       if (m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_MESH, "VisibilityBuffer"))
            m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_MESH, "VisibilityBuffer")->Set(m_pVisibilityBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));

        if (m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_MESH, "cbConstants"))
            m_pDepthOnlySRB->GetVariableByName(SHADER_TYPE_MESH, "cbConstants")->Set(m_pConstants);
    }

    void Tutorial20_MeshShader::CreateHiZMipGenerationPipeline(Diligent::ShaderCreateInfo& ShaderCI)
    {
        // Create compute pipeline state
        ComputePipelineStateCreateInfo HiZPSOCreateInfo{};
        HiZPSOCreateInfo.PSODesc.Name         = "HiZ Generation PSO";
        HiZPSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;

        // Create HiZ Buffer compute shader resource
        RefCntAutoPtr<IShader> pCS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_COMPUTE;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "HiZ Generation CS";
            ShaderCI.FilePath        = "generate_HiZ.hlsl";

            m_pDevice->CreateShader(ShaderCI, &pCS);
            VERIFY_EXPR(pCS != nullptr);
        }
        HiZPSOCreateInfo.pCS = pCS;

        BufferDesc CBDesc;
        CBDesc.Name           = "HiZ Constants";
        CBDesc.Size           = sizeof(HiZConstants);
        CBDesc.Usage          = USAGE_DYNAMIC;
        CBDesc.BindFlags      = BIND_UNIFORM_BUFFER;
        CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;

        m_pDevice->CreateBuffer(CBDesc, nullptr, &m_pHiZConstantBuffer);
        VERIFY_EXPR(m_pHiZConstantBuffer);

        PipelineResourceLayoutDesc PRLDesc{};
        PRLDesc.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

        ShaderResourceVariableDesc shaderResourceVariableDesc[3];

        shaderResourceVariableDesc[0].Name         = "InputTexture";
        shaderResourceVariableDesc[0].ShaderStages = SHADER_TYPE_COMPUTE;
        shaderResourceVariableDesc[0].Type         = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        shaderResourceVariableDesc[0].Flags        = SHADER_VARIABLE_FLAG_NONE;

        shaderResourceVariableDesc[1].Name         = "OutputTexture";
        shaderResourceVariableDesc[1].ShaderStages = SHADER_TYPE_COMPUTE;
        shaderResourceVariableDesc[1].Type         = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        shaderResourceVariableDesc[1].Flags        = SHADER_VARIABLE_FLAG_NONE;

        shaderResourceVariableDesc[2].Name         = "Constants";
        shaderResourceVariableDesc[2].ShaderStages = SHADER_TYPE_COMPUTE;
        shaderResourceVariableDesc[2].Type         = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
        shaderResourceVariableDesc[2].Flags        = SHADER_VARIABLE_FLAG_NONE;

        PRLDesc.NumVariables = _countof(shaderResourceVariableDesc);
        PRLDesc.Variables    = &shaderResourceVariableDesc[0];

        HiZPSOCreateInfo.PSODesc.ResourceLayout = PRLDesc;

        // Create compute pipeline state object
        m_pDevice->CreateComputePipelineState(HiZPSOCreateInfo, &m_pHiZComputePSO);
        VERIFY_EXPR(m_pHiZComputePSO != nullptr);

        m_pHiZComputePSO->CreateShaderResourceBinding(&m_pHiZComputeSRB, true);
        VERIFY_EXPR(m_pHiZComputeSRB != nullptr);
    }

    void Tutorial20_MeshShader::DepthPrepass()
    {
        m_pImmediateContext->SetPipelineState(m_pDepthOnlyPSO);
        m_pImmediateContext->CommitShaderResources(m_pDepthOnlySRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Set depth-stencil view
        auto* pDSV = m_pSwapChain->GetDepthBufferDSV();
        //m_pImmediateContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        m_pImmediateContext->SetRenderTargets(0, nullptr, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Draw best occluders. Task count doesn't change, since the buffers are all the same, we just discard 
        // more invocations.
        VERIFY_EXPR(m_DepthPassDrawTaskCountAligned % ASGroupSize == 0);

        DrawMeshAttribs drawAttrs{m_DepthPassDrawTaskCountAligned, DRAW_FLAG_VERIFY_ALL};
        m_pImmediateContext->DrawMesh(drawAttrs);
        
        // Unset depth buffer when copying
        m_pImmediateContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    }

    void Tutorial20_MeshShader::UpdateUI()
    {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Culling Options");

            ImGui::Checkbox("Enable Occlusion Culling", &m_OcclusionCulling);
            if (m_OcclusionCulling)
            {
                static const char* items[] = {"Cull Octree Nodes", "Cull Meshlets"};
                ImGui::Combo("Culling Mode", &m_CullMode, items, IM_ARRAYSIZE(items));
                ImGui::Checkbox("Show Best Occluders only", &m_ShowOnlyBestOccluders);
                ImGui::SliderFloat("Depth Bias", &m_OCThreshold, 0.0f, 0.1f, "%.5f", ImGuiSliderFlags_Logarithmic);
            }

            ImGui::Checkbox("Enable Frustum Culling", &m_FrustumCulling);

            ImGui::Spacing();
            ImGui::Text("Visualization Options");

            ImGui::Checkbox("Meshlet Visualization", &m_MSDebugViz);
            if (m_MSDebugViz)
                ImGui::Checkbox("Octree Debug Visualization", &m_OTDebugViz);

            ImGui::Spacing();
            ImGui::Text("Misc Options");

            ImGui::Checkbox("Enable Light", &m_UseLight);
            ImGui::Checkbox("Syncronize Camera Position", &m_SyncCamPosition);

            ImGui::Spacing();

            if (ImGui::Button("Reset Camera"))
            {
                fpc.SetPos({80, 130, -310});
                fpc.SetRotation(0, 0);
            }

            ImGui::Spacing();
            ImGui::Text("Statistics");

            ImGui::Text("Visible cubes: %d", m_VisibleCubes);
            ImGui::Text("Visible octree nodes: %d", m_VisibleOTNodes);
        }
        ImGui::End();
    }
    
    void Tutorial20_MeshShader::ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs)
    {
        SampleBase::ModifyEngineInitInfo(Attribs);
    
        Attribs.EngineCI.Features.MeshShaders = DEVICE_FEATURE_STATE_ENABLED;
    }
    
    void Tutorial20_MeshShader::Initialize(const SampleInitInfo& InitInfo)
    {
        SampleBase::Initialize(InitInfo);
    
        fpc.SetMoveSpeed(30.f);
    
        LoadTexture();
        //CreateDrawTasks();
        CreateDrawTasksFromMesh("models/lucy.binvox");
        CreateStatisticsBuffer();
        CreateConstantsBuffer();
        CreatePipelineState();
    }
    
    void Tutorial20_MeshShader::Render()
    {
        auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
        auto* pDSV = m_pSwapChain->GetDepthBufferDSV();
        // Clear the back buffer and depth buffer
        //const float ClearColor[] = {0.350f, 0.350f, 0.350f, 1.0f};
        const float ClearColor[] = {0.1f, 0.1f, 0.1f, 1.0f};
        
        m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Reset statistics
        DrawStatistics stats;
        std::memset(&stats, 0, sizeof(stats));
        m_pImmediateContext->UpdateBuffer(m_pStatisticsBuffer, 0, sizeof(stats), &stats, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    
        {
            // Map the buffer and write current view, view-projection matrix and other constants.
            MapHelper<Constants> CBConstants(m_pImmediateContext, m_pConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            CBConstants->ViewMat        = m_ViewMatrix.Transpose();
            CBConstants->ViewProjMat    = m_ViewProjMatrix.Transpose();

            CBConstants->DepthBias      = m_OCThreshold;
            CBConstants->VoxelCount     = m_DepthPassDrawTaskCount; 
            
            CBConstants->RenderOptions = 0;
            CBConstants->RenderOptions |= (m_CullMode << 0);
            CBConstants->RenderOptions |= ((m_OcclusionCulling ? 1 : 0) << 1);
            CBConstants->RenderOptions |= ((m_FrustumCulling ? 1 : 0) << 2);
            CBConstants->RenderOptions |= ((m_ShowOnlyBestOccluders ? 1 : 0) << 3);
            CBConstants->RenderOptions |= ((m_UseLight ? 1 : 0) << 4);
            CBConstants->RenderOptions |= ((m_MSDebugViz ? 1 : 0) << 5);
            CBConstants->RenderOptions |= ((m_OTDebugViz ? 1 : 0) << 6);

            // Calculate frustum planes from view-projection matrix.
            if (m_SyncCamPosition)
                ExtractViewFrustumPlanesFromMatrix(m_ViewProjMatrix, Frustum, false);
    
            // Each frustum plane must be normalized.
            for (uint i = 0; i < _countof(CBConstants->Frustum); ++i)
            {
                Plane3D plane  = Frustum.GetPlane(static_cast<ViewFrustum::PLANE_IDX>(i));
                float   invlen = 1.0f / length(plane.Normal);
                plane.Normal *= invlen;
                plane.Distance *= invlen;
    
                CBConstants->Frustum[i] = plane;
            }

            // Draw best occluders to depth buffer
            DepthPrepass();

            // Clear Depth Stencil to avoid flickering (Remove later, should be able to just )
            m_pImmediateContext->SetRenderTargets(0, nullptr, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }        

        // Reset pipeline state to normally draw to back buffer
        m_pImmediateContext->SetPipelineState(m_pPSO);

        m_pSRB->GetVariableByName(SHADER_TYPE_AMPLIFICATION, "VisibilityBuffer")->Set(m_pVisibilityBuffer->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS));
        
        m_pImmediateContext->CommitShaderResources(m_pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Amplification shader executes 32 threads per group and the task count must be aligned to 32
        // to prevent loss of tasks or access outside of the data array.
        VERIFY_EXPR(m_DrawTaskCount % ASGroupSize == 0);
    
        DrawMeshAttribs drawAttrs{m_DrawTaskCount, DRAW_FLAG_VERIFY_ALL};
        m_pImmediateContext->DrawMesh(drawAttrs);
    
        // Copy statistics to staging buffer
        {
            m_VisibleCubes = 0;
    
            m_pImmediateContext->CopyBuffer(m_pStatisticsBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                            m_pStatisticsStaging, static_cast<Uint32>(m_FrameId % m_StatisticsHistorySize) * sizeof(DrawStatistics), sizeof(DrawStatistics),
                                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    
            // We should use synchronizations to safely access the mapped memory.
            m_pImmediateContext->EnqueueSignal(m_pStatisticsAvailable, m_FrameId);
    
            // Read statistics from previous frame.
            Uint64 AvailableFrameId = m_pStatisticsAvailable->GetCompletedValue();
    
            // Synchronize
            if (m_FrameId - AvailableFrameId > m_StatisticsHistorySize)
            {
                // In theory we should never get here as we wait for more than enough
                // frames.
                AvailableFrameId = m_FrameId - m_StatisticsHistorySize;
                m_pStatisticsAvailable->Wait(AvailableFrameId);
            }
    
            // Read the staging data
            if (AvailableFrameId > 0)
            {
                MapHelper<DrawStatistics> StagingData(m_pImmediateContext, m_pStatisticsStaging, MAP_READ, MAP_FLAG_DO_NOT_WAIT);
                if (StagingData)
                {
                    m_VisibleCubes   = StagingData[AvailableFrameId % m_StatisticsHistorySize].visibleCubes;
                    m_VisibleOTNodes = StagingData[AvailableFrameId % m_StatisticsHistorySize].visibleOctreeNodes;
                }
            }
    
            m_pImmediateContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            m_pImmediateContext->Flush();
            m_pImmediateContext->FinishFrame();
            
            ++m_FrameId;
        }
    }
    
    void Tutorial20_MeshShader::Update(double CurrTime, double ElapsedTime)
    {
        SampleBase::Update(CurrTime, ElapsedTime);
        UpdateUI();

        fpc.Update(GetInputController(), (float)ElapsedTime);

        // Set camera position
        float4x4 View = fpc.GetViewMatrix();
    
        // Get pretransform matrix that rotates the scene according the surface orientation
        auto SrfPreTransform = GetSurfacePretransformMatrix(float3{0, 0, 1});
    
        // Get projection matrix adjusted to the current screen orientation
        auto Proj = GetAdjustedProjectionMatrix(m_FOV, 1.f, 500.f);
    
        // Compute view and view-projection matrices
        m_ViewMatrix = View * SrfPreTransform;
        m_ViewProjMatrix = m_ViewMatrix * Proj;
    }

} // namespace Diligent
