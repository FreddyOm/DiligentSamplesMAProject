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

#pragma once

#include "SampleBase.hpp"
#include "BasicMath.hpp"
#include "FirstPersonCamera.hpp"
#include "octree/octree.h"
#include <AdvancedMath.hpp>
#include <Timer.hpp>

namespace Diligent
{
    class Tutorial20_MeshShader final : public SampleBase
    {
    public:
        virtual void ModifyEngineInitInfo(const ModifyEngineInitInfoAttribs& Attribs) override final;
        virtual void Initialize(const SampleInitInfo& InitInfo) override final;
    
        virtual void Render() override final;
        virtual void Update(double CurrTime, double ElapsedTime) override final;
    
        virtual const Char* GetSampleName() const override final { return "Tutorial20: Mesh shader"; }
    
        ~Tutorial20_MeshShader();
    
    private:
        void CreateDrawTasksFromMesh(std::string meshPath);
        void PopulateOctree(std::string OTmodelPath);
        void CreateDrawTasks();
        
        void CreatePipelineState();
        void CreateDepthPrepassPipeline(Diligent::RefCntAutoPtr<Diligent::IShader>& pASBestOccluders, Diligent::RefCntAutoPtr<Diligent::IShader>& pMS);
        void CreateHiZMipGenerationPipeline(Diligent::ShaderCreateInfo& ShaderCI);
        void BindSortedIndexBuffer(std::vector<VoxelOC::VoxelBufData>& sortedNodeBuffer);
        void BindOctreeNodeBuffer(std::vector<VoxelOC::OctreeLeafNode>& octreeNodeBuffer);
        void BindBestOccluderBuffer(std::vector<VoxelOC::DepthPrepassDrawTask>& depthPrepassOTNodes);
        void CreateStatisticsBuffer();
        void CreateConstantsBuffer();

        void LoadTexture();
        void UpdateUI();

        void WindowResize(Uint32 Width, Uint32 Height) override;

        // 2 Pass Depth OC
        void                   DepthPrepass();
        void                   CreateHiZTextures();
        void                   GenerateHiZ();
    
        RefCntAutoPtr<IBuffer>      m_CubeBuffer;
        RefCntAutoPtr<ITextureView> m_CubeTextureSRV;
    
        RefCntAutoPtr<IBuffer> m_pStatisticsBuffer;
        RefCntAutoPtr<IBuffer> m_pStatisticsStaging;
        RefCntAutoPtr<IFence>  m_pStatisticsAvailable;
        Uint64                 m_FrameId               = 1; // Can't signal 0
        const Uint32           m_StatisticsHistorySize = 8;
    
        static constexpr Int32 ASGroupSize = 64; // max 1024
    
        Uint32                 m_DrawTaskCount          = 0;
        Uint32                 m_DepthPassDrawTaskCount = 0;
        float                  m_HiZSampleValue         = 0.f;
        float                  m_MinZValue              = 0.f;
        Uint32                 m_MipCount               = 0;

        RefCntAutoPtr<IBuffer> m_pVoxelPosBuffer;
        RefCntAutoPtr<IBuffer> m_pBestOccluderBuffer;
        RefCntAutoPtr<IBuffer> m_pOctreeNodeBuffer;
        RefCntAutoPtr<IBuffer> m_pConstants;
        RefCntAutoPtr<ITexture> m_pOverdrawTexture;

        Box overdrawUpdateBox;
        TextureSubResData subResData;
        std::vector<Uint32> clearData;

        RefCntAutoPtr<ITexture>                  m_pHiZPyramidTexture;
        std::vector<RefCntAutoPtr<ITextureView>> m_HiZMipUAVs;


        RefCntAutoPtr<IPipelineState>         m_pPSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pSRB;

        RefCntAutoPtr<IPipelineState> m_pDepthOnlyPSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pDepthOnlySRB;
    
        RefCntAutoPtr<IPipelineState>         m_pHiZComputePSO;
        RefCntAutoPtr<IShaderResourceBinding> m_pHiZComputeSRB;

        RefCntAutoPtr<IBuffer> m_pHiZConstantBuffer;

        StateTransitionDesc m_TransitionBarrier[1];
        StateTransitionDesc m_ResetTransitionBarrier[1];

        FirstPersonCamera fpc{};
        ViewFrustum       Frustum{};


        float4x4    m_ViewProjMatrix;
        float4x4    m_ViewMatrix;
        bool        m_MSDebugViz     = false;
        bool        m_OTDebugViz     = false;
        bool        m_FrustumCulling = true;
        bool        m_ShowOnlyBestOccluders = false;
        bool        m_SyncCamPosition  = true;
        const float m_FOV            = PI_F / 4.0f;
        const float m_CoTanHalfFov   = 1.0f / std::tan(m_FOV * 0.5f);
        float       m_LodScale       = 4.0f;
        float       m_CameraHeight   = 10.0f;
        float       m_CurrTime       = 0.0f;
        Uint32      m_VisibleCubes   = 0;
        Uint32      m_VisibleOTNodes = 0;
        bool        m_UseLight       = true;
        float       m_OCThreshold    = 0.0f;
        bool        m_OcclusionCulling = true;
        int         m_CullMode       = 0;
    
        float3 SceneCenter{60, 115, 20};
        std::vector<unsigned long long> visibleVoxels;
        std::vector<unsigned long long> visibleOctreeNodes;

        Timer               updateTimer;
        Timer               renderTimer;
        std::vector<double> frameUpdateTimes;
        std::vector<double> frameRenderTimes;
        std::vector<double> completeFrameTimes; 


        OctreeNode<VoxelOC::OctreeLeafNode>* m_pOcclusionOctreeRoot = nullptr;
    };

} // namespace Diligent
