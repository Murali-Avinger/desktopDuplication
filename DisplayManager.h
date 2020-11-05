// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _DISPLAYMANAGER_H_
#define _DISPLAYMANAGER_H_

#include "ScreenCaptureImpl.hpp"
#include "CommonTypes.h"

using namespace CapUtils;

//
// Handles the task of processing frames
//
class DISPLAYMANAGER
{
    public:
        DISPLAYMANAGER();
        ~DISPLAYMANAGER();
        void InitD3D(DX_RESOURCES* Data);
        ID3D11Device* GetDevice();
        DUPL_RETURN ProcessFrame(_In_ FRAME_DATA* Data, _Inout_ ID3D11Texture2D* SharedSurf, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc);
        void CleanRefs();

        void setupFFMPEGBasedScreenEncode(int width, int height, int fps, int segmentDurationInSeconds,
                                            std::string outDirPath, std::string masterPlaylistFile);

    private:
        bool setupFFSessionInfo();
        int setHardwareFrameContext();

        void addFrame(UINT* data);
        DUPL_RETURN performCopying(ID3D11Texture2D* SharedSurf);

    // methods
        DUPL_RETURN CopyDirty(_In_ ID3D11Texture2D* SrcSurface, _Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(DirtyCount) RECT* DirtyBuffer, UINT DirtyCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc);
        DUPL_RETURN CopyMove(_Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(MoveCount) DXGI_OUTDUPL_MOVE_RECT* MoveBuffer, UINT MoveCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, INT TexWidth, INT TexHeight);
        void SetDirtyVert(_Out_writes_(NUMVERTICES) VERTEX* Vertices, _In_ RECT* Dirty, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ D3D11_TEXTURE2D_DESC* FullDesc, _In_ D3D11_TEXTURE2D_DESC* ThisDesc);
        void SetMoveRect(_Out_ RECT* SrcRect, _Out_ RECT* DestRect, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ DXGI_OUTDUPL_MOVE_RECT* MoveRect, INT TexWidth, INT TexHeight);

    // variables
        ID3D11Device* m_Device;
        ID3D11DeviceContext* m_DeviceContext;
        ID3D11Texture2D* m_MoveSurf;
        ID3D11VertexShader* m_VertexShader;
        ID3D11PixelShader* m_PixelShader;
        ID3D11InputLayout* m_InputLayout;
        ID3D11RenderTargetView* m_RTV;
        ID3D11SamplerState* m_SamplerLinear;
        BYTE* m_DirtyVertexBufferAlloc;
        UINT m_DirtyVertexBufferAllocSize;

        FFScreenSessionInfo ffScreenSessionInfo; // FMMPEG session info object to be used to output segmented streams.
        ScreenCaptureParams screenCaptureParams;
        std::string configFile;  // Config JSON file that defines screen capture parameters
        std::string playListFileName; // Playlist file to be written for playback
        std::string outputFilePath; // Output file path where segmented tarnsport streams should go
        int segmentDuration = 10; // Video segment duration that each transport stream should correspond to
        int srcheight = 0; // Source screen region height
        int srcwidth = 0;  // Source screen region width
};

#endif