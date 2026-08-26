/*
#############################################################################################
# Alice2 (ASDK) SDK 1.0.0.0
# Generated with the CodeRedGenerator v1.2.0
# ========================================================================================= #
# File: GFxUI_structs.hpp
# ========================================================================================= #
# Credits: ItsBranK, TheFeckless
# Links: www.github.com/CodeRedModding/CodeRed-Generator
#############################################################################################
*/
#pragma once

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

/*
# ========================================================================================= #
# Structs
# ========================================================================================= #
*/

// ScriptStruct GFxUI.GFxMovie.ASValue
// 0x0018
struct FASValue
{
	uint8_t                                            Type;                                          // 0x0000 (0x0001) [0x0000000000000001] (CPF_Edit)    
	uint8_t                                            UnknownData00[0x3];                              // 0x0001 (0x0003) MISSED OFFSET
	uint32_t                                           B : 1;                                         // 0x0004 (0x0004) [0x0000000000000001] [0x00000001] (CPF_Edit)
	float                                              N;                                             // 0x0008 (0x0004) [0x0000000000000001] (CPF_Edit)    
	class FString                                      S;                                             // 0x000C (0x000C) [0x0000000000500001] (CPF_Edit | CPF_NeedCtorLink)
};

// ScriptStruct GFxUI.GFxMovie.GFxDataStoreBinding
// 0x0098
struct FGFxDataStoreBinding
{
	struct FUIDataStoreBinding                         DataSource;                                    // 0x0000 (0x0030) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	class FString                                      VarPath;                                       // 0x0030 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	class FString                                      ModelId;                                       // 0x003C (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	class FString                                      ControlId;                                     // 0x0048 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	uint32_t                                           bEditable : 1;                                 // 0x0054 (0x0004) [0x0000000000000001] [0x00000001] (CPF_Edit)
	class TArray<class FName>                          CellTags;                                      // 0x0058 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	class TArray<uint8_t>                              ModelIdUtf8;                                   // 0x0064 (0x000C) [0x0000000000402002] (CPF_Const | CPF_Transient | CPF_NeedCtorLink)
	class TArray<uint8_t>                              ControlIdUtf8;                                 // 0x0070 (0x000C) [0x0000000000402002] (CPF_Const | CPF_Transient | CPF_NeedCtorLink)
	class UUIListElementProvider*                      ListDataProvider_Object;                       // 0x007C (0x0008) [0x0000000000002002] (CPF_Const | CPF_Transient)
	class UUIListElementProvider*                      ListDataProvider_Interface;                    // 0x007C (0x0008) [0x0000000000002002] (CPF_Const | CPF_Transient)
	class TArray<class FName>                          FullCellTags;                                  // 0x0084 (0x000C) [0x0000000000402002] (CPF_Const | CPF_Transient | CPF_NeedCtorLink)
	struct FPointer                                    ModelRef;                                      // 0x0090 (0x0004) [0x0000000000003002] (CPF_Const | CPF_Native | CPF_Transient)
	struct FPointer                                    ControlRef;                                    // 0x0094 (0x0004) [0x0000000000003002] (CPF_Const | CPF_Native | CPF_Transient)
};

// ScriptStruct GFxUI.GFxMovie.ExternalTexture
// 0x0010
struct FExternalTexture
{
	class FString                                      Resource;                                      // 0x0000 (0x000C) [0x0000000000400001] (CPF_Edit | CPF_NeedCtorLink)
	class UTexture*                                    Texture;                                       // 0x000C (0x0004) [0x0000000000000001] (CPF_Edit)    
};

// ScriptStruct GFxUI.GFxEngine.GCReference
// 0x000C
struct FGCReference
{
	class UObject*                                     m_object;                                      // 0x0000 (0x0004) [0x0000000000000002] (CPF_Const)   
	int32_t                                            m_count;                                       // 0x0004 (0x0004) [0x0000000000000000]               
	int32_t                                            m_statid;                                      // 0x0008 (0x0004) [0x0000000000000000]               
};

// ScriptStruct GFxUI.GFxValue.ASDisplayInfo
// 0x002C
struct FASDisplayInfo
{
	float                                              X;                                             // 0x0000 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              Y;                                             // 0x0004 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              Z;                                             // 0x0008 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              Rotation;                                      // 0x000C (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              XRotation;                                     // 0x0010 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              YRotation;                                     // 0x0014 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              XScale;                                        // 0x0018 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              YScale;                                        // 0x001C (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              ZScale;                                        // 0x0020 (0x0004) [0x0000000000000001] (CPF_Edit)    
	float                                              Alpha;                                         // 0x0024 (0x0004) [0x0000000000000001] (CPF_Edit)    
	uint32_t                                           Visible : 1;                                   // 0x0028 (0x0004) [0x0000000000000001] [0x00000001] (CPF_Edit)
	uint32_t                                           hasX : 1;                                      // 0x0028 (0x0004) [0x0000000000000001] [0x00000002] (CPF_Edit)
	uint32_t                                           hasY : 1;                                      // 0x0028 (0x0004) [0x0000000000000001] [0x00000004] (CPF_Edit)
	uint32_t                                           hasZ : 1;                                      // 0x0028 (0x0004) [0x0000000000000001] [0x00000008] (CPF_Edit)
	uint32_t                                           hasRotation : 1;                               // 0x0028 (0x0004) [0x0000000000000001] [0x00000010] (CPF_Edit)
	uint32_t                                           hasXRotation : 1;                              // 0x0028 (0x0004) [0x0000000000000001] [0x00000020] (CPF_Edit)
	uint32_t                                           hasYRotation : 1;                              // 0x0028 (0x0004) [0x0000000000000001] [0x00000040] (CPF_Edit)
	uint32_t                                           hasXScale : 1;                                 // 0x0028 (0x0004) [0x0000000000000001] [0x00000080] (CPF_Edit)
	uint32_t                                           hasYScale : 1;                                 // 0x0028 (0x0004) [0x0000000000000001] [0x00000100] (CPF_Edit)
	uint32_t                                           hasZScale : 1;                                 // 0x0028 (0x0004) [0x0000000000000001] [0x00000200] (CPF_Edit)
	uint32_t                                           hasAlpha : 1;                                  // 0x0028 (0x0004) [0x0000000000000001] [0x00000400] (CPF_Edit)
	uint32_t                                           hasVisible : 1;                                // 0x0028 (0x0004) [0x0000000000000001] [0x00000800] (CPF_Edit)
};

// ScriptStruct GFxUI.GFxValue.ASColorTransform
// 0x0020
struct FASColorTransform
{
	struct FLinearColor                                Multiply;                                      // 0x0000 (0x0010) [0x0000000000000001] (CPF_Edit)    
	struct FLinearColor                                Add;                                           // 0x0010 (0x0010) [0x0000000000000001] (CPF_Edit)    
};

/*
# ========================================================================================= #
#
# ========================================================================================= #
*/

#ifdef _MSC_VER
#pragma pack(pop)
#endif
