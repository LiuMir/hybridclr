#include "UnifiedMetadataProvider.h"
#include "InterpreterImage.h"
#include "MetadataModule.h"
#include "vm/GlobalMetadata.h"
#include "vm/Class.h"
#include "vm/Method.h"
#include "vm/Field.h"
#include "vm/Assembly.h"
#include "vm/Image.h"
#include "vm/Exception.h"
#include "vm/MetadataCache.h"
#include "vm/MetadataLock.h"
#include "utils/Il2CppHashMap.h"
#include "utils/HashUtils.h"
#include <unordered_map>
#include <cstring>

// 辅助函数：解码 token 获取行索引
static uint32_t DecodeTokenRowIndex(uint32_t token)
{
    return token & 0x00FFFFFF;
}

namespace hybridclr
{
namespace metadata
{

    // 静态成员定义
    std::unordered_map<uint64_t, Il2CppGenericClass*> UnifiedMetadataProvider::s_genericClassCache;
    std::unordered_map<uint64_t, Il2CppGenericMethod*> UnifiedMetadataProvider::s_genericMethodCache;
    std::unordered_map<uint64_t, Il2CppGenericInst*> UnifiedMetadataProvider::s_genericInstCache;

    // ==================== GenericMetadataFactory 实现 ====================

    Il2CppGenericClass* GenericMetadataFactory::CreateGenericClass(
        const Il2CppType* genericType, 
        const Il2CppGenericInst* classInst)
    {
        if (!genericType || !classInst)
        {
            return nullptr;
        }

        // 检查缓存（需要线程安全）
        uint64_t key = UnifiedMetadataProvider::GenerateGenericClassKey(genericType, classInst);
        {
            il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);
            auto it = UnifiedMetadataProvider::s_genericClassCache.find(key);
            if (it != UnifiedMetadataProvider::s_genericClassCache.end())
            {
                return it->second;
            }
        }

        // 创建新的泛型类
        Il2CppGenericClass* genericClass = (Il2CppGenericClass*)HYBRIDCLR_MALLOC(sizeof(Il2CppGenericClass));
        genericClass->type = genericType;
        genericClass->context.class_inst = classInst;
        genericClass->cached_class = nullptr;

        // 缓存结果（需要线程安全）
        {
            il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);
            UnifiedMetadataProvider::s_genericClassCache[key] = genericClass;
        }
        return genericClass;
    }

    Il2CppGenericMethod* GenericMetadataFactory::CreateGenericMethod(
        const MethodInfo* method, 
        const Il2CppGenericInst* methodInst)
    {
        if (!method || !methodInst)
        {
            return nullptr;
        }

        // 检查缓存（需要线程安全）
        uint64_t key = UnifiedMetadataProvider::GenerateGenericMethodKey(method, methodInst);
        {
            il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);
            auto it = UnifiedMetadataProvider::s_genericMethodCache.find(key);
            if (it != UnifiedMetadataProvider::s_genericMethodCache.end())
            {
                return it->second;
            }
        }

        // 创建新的泛型方法
        Il2CppGenericMethod* genericMethod = (Il2CppGenericMethod*)HYBRIDCLR_MALLOC(sizeof(Il2CppGenericMethod));
        genericMethod->methodDefinition = method;
        genericMethod->context.method_inst = methodInst;
        genericMethod->context.class_inst = nullptr; // 由调用者设置

        // 缓存结果（需要线程安全）
        {
            il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);
            UnifiedMetadataProvider::s_genericMethodCache[key] = genericMethod;
        }
        return genericMethod;
    }

    Il2CppGenericInst* GenericMetadataFactory::CreateGenericInst(
        const Il2CppType** types, 
        uint32_t typeCount)
    {
        if (!types || typeCount == 0)
        {
            return nullptr;
        }

        // 检查缓存（需要线程安全）
        uint64_t key = UnifiedMetadataProvider::GenerateGenericInstKey(types, typeCount);
        {
            il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);
            auto it = UnifiedMetadataProvider::s_genericInstCache.find(key);
            if (it != UnifiedMetadataProvider::s_genericInstCache.end())
            {
                return it->second;
            }
        }

        // 创建新的泛型实例
        Il2CppGenericInst* genericInst = (Il2CppGenericInst*)HYBRIDCLR_MALLOC(
            sizeof(Il2CppGenericInst) + sizeof(Il2CppType*) * typeCount);
        genericInst->type_argc = typeCount;
        
        for (uint32_t i = 0; i < typeCount; i++)
        {
            genericInst->type_argv[i] = types[i];
        }

        // 缓存结果（需要线程安全）
        {
            il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);
            UnifiedMetadataProvider::s_genericInstCache[key] = genericInst;
        }
        return genericInst;
    }

    bool GenericMetadataFactory::IsGenericTypeInstantiated(const Il2CppType* type)
    {
        if (!type)
        {
            return false;
        }

        if (type->type == IL2CPP_TYPE_GENERICINST)
        {
            return true;
        }

        if (type->type == IL2CPP_TYPE_CLASS || type->type == IL2CPP_TYPE_VALUETYPE)
        {
            const Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type, false);
            return klass && !klass->is_generic;
        }

        return true;
    }

    bool GenericMetadataFactory::IsGenericMethodInstantiated(const MethodInfo* method)
    {
        if (!method)
        {
            return false;
        }

        return !method->is_generic || method->is_inflated;
    }

    // ==================== UnifiedMetadataProvider 实现 ====================

    void UnifiedMetadataProvider::Initialize()
    {
        // 初始化缓存
        s_genericClassCache.clear();
        s_genericMethodCache.clear();
        s_genericInstCache.clear();
    }

    void UnifiedMetadataProvider::Cleanup()
    {
        // 清理缓存（需要线程安全）
        il2cpp::os::FastAutoLock lock(&il2cpp::vm::g_MetadataLock);
        
        for (auto& pair : s_genericClassCache)
        {
            HYBRIDCLR_FREE(pair.second);
        }
        s_genericClassCache.clear();

        for (auto& pair : s_genericMethodCache)
        {
            HYBRIDCLR_FREE(pair.second);
        }
        s_genericMethodCache.clear();

        for (auto& pair : s_genericInstCache)
        {
            HYBRIDCLR_FREE(pair.second);
        }
        s_genericInstCache.clear();
    }

    Image* UnifiedMetadataProvider::GetImageForAssembly(const Il2CppAssembly* ass)
    {
        if (!ass)
        {
            return nullptr;
        }

        // 如果是解释器程序集，直接返回解释器镜像
        if (IsInterpreterAssembly(ass))
        {
            return MetadataModule::GetImage(ass->image);
        }

        // 对于 AOT 程序集，返回 nullptr
        // AOT 程序集的方法不需要通过 Image 来解析
        return nullptr;
    }

    const Il2CppTypeDefinition* UnifiedMetadataProvider::GetTypeDefinition(const Il2CppType* type)
    {
        if (!type)
        {
            return nullptr;
        }

        if (type->type == IL2CPP_TYPE_CLASS || type->type == IL2CPP_TYPE_VALUETYPE)
        {
            const Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(type, false);
            if (klass && klass->typeMetadataHandle)
            {
                return (const Il2CppTypeDefinition*)klass->typeMetadataHandle;
            }
        }

        return nullptr;
    }

    const Il2CppMethodDefinition* UnifiedMetadataProvider::GetMethodDefinition(const MethodInfo* method)
    {
        if (!method)
        {
            return nullptr;
        }

        return (const Il2CppMethodDefinition*)method->methodMetadataHandle;
    }

    const Il2CppFieldDefinition* UnifiedMetadataProvider::GetFieldDefinition(const FieldInfo* field)
    {
        if (!field)
        {
            return nullptr;
        }

        // FieldInfo 结构体没有 fieldMetadataHandle 成员
        // 对于 AOT 程序集，简化实现，直接返回 nullptr
        return nullptr;
    }

    Il2CppGenericContainer* UnifiedMetadataProvider::GetGenericContainer(const Il2CppType* type)
    {
        const Il2CppTypeDefinition* typeDef = GetTypeDefinition(type);
        if (!typeDef)
        {
            return nullptr;
        }

        if (typeDef->genericContainerIndex != kGenericContainerIndexInvalid)
        {
            return (Il2CppGenericContainer*)il2cpp::vm::GlobalMetadata::GetGenericContainerFromIndex(typeDef->genericContainerIndex);
        }

        return nullptr;
    }

    MethodBody* UnifiedMetadataProvider::GetMethodBody(const MethodInfo* method)
    {
        if (!method)
        {
            return nullptr;
        }

        // 如果是解释器方法，从解释器镜像获取方法体
        if (IsInterpreterMethod(method))
        {
            Image* image = GetImageForAssembly(method->klass->image->assembly);
            if (image)
            {
                return image->GetMethodBody(method->token);
            }
        }

        // 对于 AOT 方法，返回 nullptr（AOT 方法没有 IL 方法体）
        return nullptr;
    }

    const MethodInfo* UnifiedMetadataProvider::GetMethodInfoFromToken(
        const Il2CppAssembly* ass,
        uint32_t token,
        const Il2CppGenericContainer* klassContainer,
        const Il2CppGenericContainer* methodContainer,
        const Il2CppGenericContext* genericContext)
    {
        if (!ass)
        {
            return nullptr;
        }

        // 对于解释器程序集，使用 Image 对象解析 token
        if (IsInterpreterAssembly(ass))
        {
            Image* image = MetadataModule::GetImage(ass->image);
            if (!image)
            {
                return nullptr;
            }

            // 创建一个临时的 token 缓存
            Token2RuntimeHandleMap tokenCache;
            return image->GetMethodInfoFromToken(tokenCache, token, klassContainer, methodContainer, genericContext);
        }
        else
        {
            // 对于 AOT 程序集，直接使用 IL2CPP 的 GlobalMetadata 解析
            TableType tableType = DecodeTokenTableType(token);
            uint32_t rowIndex = DecodeTokenRowIndex(token);
            
            // 调试信息已移除，直接进行方法解析
            
            if (tableType == TableType::METHOD)
            {
                const Il2CppMethodDefinition* methodDef = il2cpp::vm::GlobalMetadata::GetMethodDefinitionFromIndex(rowIndex);
                const Il2CppTypeDefinition* typeDef = (const Il2CppTypeDefinition*)il2cpp::vm::GlobalMetadata::GetTypeHandleFromIndex(methodDef->declaringType);
                Il2CppClass* klass = il2cpp::vm::GlobalMetadata::GetTypeInfoFromHandle((Il2CppMetadataTypeHandle)typeDef);
                il2cpp::vm::Class::SetupMethods(klass);
                
                // 通过 token 匹配方法
                char debugMsg2[256];
                sprintf_s(debugMsg2, "Searching in class %s, method_count=%d", klass->name, klass->method_count);
                il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetNotSupportedException(debugMsg2));
                for (uint16_t i = 0; i < klass->method_count; i++)
                {
                    const MethodInfo* method = klass->methods[i];
                    if (method)
                    {
                        char debugMsg3[256];
                        sprintf_s(debugMsg3, "Method[%d]: %s, token=0x%x", i, method->name, method->token);
                        il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetNotSupportedException(debugMsg3));
                        if (method->token == token)
                        {
                            char debugMsg4[256];
                            sprintf_s(debugMsg4, "Found method by token: %s", method->name);
                            il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetNotSupportedException(debugMsg4));
                            // 处理泛型方法实例化
                            if (methodContainer && !method->is_inflated)
                            {
                                Il2CppGenericContext finalGenericContext = { 
                                    genericContext ? genericContext->class_inst : nullptr, 
                                    (const Il2CppGenericInst*)methodContainer 
                                };
                                return il2cpp::metadata::GenericMetadata::Inflate(method, &finalGenericContext);
                            }
                            return method;
                        }
                    }
                }
            }
            else if (tableType == TableType::MEMBERREF)
            {
                // 对于 MemberRef，需要解析方法引用
                // 使用 IL2CPP 的现有机制来解析
                const Il2CppMethodDefinition* methodDef = il2cpp::vm::GlobalMetadata::GetMethodDefinitionFromIndex(rowIndex);
                const Il2CppTypeDefinition* typeDef = (const Il2CppTypeDefinition*)il2cpp::vm::GlobalMetadata::GetTypeHandleFromIndex(methodDef->declaringType);
                Il2CppClass* klass = il2cpp::vm::GlobalMetadata::GetTypeInfoFromHandle((Il2CppMetadataTypeHandle)typeDef);
                il2cpp::vm::Class::SetupMethods(klass);
                
                // 通过 token 匹配方法
                char debugMsg2[256];
                sprintf_s(debugMsg2, "Searching in class %s, method_count=%d", klass->name, klass->method_count);
                il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetNotSupportedException(debugMsg2));
                for (uint16_t i = 0; i < klass->method_count; i++)
                {
                    const MethodInfo* method = klass->methods[i];
                    if (method)
                    {
                        char debugMsg3[256];
                        sprintf_s(debugMsg3, "Method[%d]: %s, token=0x%x", i, method->name, method->token);
                        il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetNotSupportedException(debugMsg3));
                        if (method->token == token)
                        {
                            char debugMsg4[256];
                            sprintf_s(debugMsg4, "Found method by token: %s", method->name);
                            il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetNotSupportedException(debugMsg4));
                            // 处理泛型方法实例化
                            if (methodContainer && !method->is_inflated)
                            {
                                Il2CppGenericContext finalGenericContext = { 
                                    genericContext ? genericContext->class_inst : nullptr, 
                                    (const Il2CppGenericInst*)methodContainer 
                                };
                                return il2cpp::metadata::GenericMetadata::Inflate(method, &finalGenericContext);
                            }
                            return method;
                        }
                    }
                }
            }
        }

        return nullptr;
    }

    const Il2CppType* UnifiedMetadataProvider::GetTypeFromToken(
        const Il2CppAssembly* ass,
        uint32_t token,
        const Il2CppGenericContainer* klassContainer,
        const Il2CppGenericContext* genericContext)
    {
        if (!ass)
        {
            return nullptr;
        }

        // 对于解释器程序集，使用 Image 对象解析
        if (IsInterpreterAssembly(ass))
        {
            Image* image = GetImageForAssembly(ass);
            if (!image)
            {
                return nullptr;
            }

            // 使用 ReadTypeFromToken 方法
            TableType tableType = (TableType)(token >> 24);
            uint32_t rowIndex = token & 0x00FFFFFF;
            return image->ReadTypeFromToken(klassContainer, nullptr, tableType, rowIndex);
        }
        else
        {
            // 对于 AOT 程序集，直接使用 IL2CPP 的 GlobalMetadata 解析
            TableType tableType = DecodeTokenTableType(token);
            uint32_t rowIndex = DecodeTokenRowIndex(token);
            
            if (tableType == TableType::TYPEDEF)
            {
                const Il2CppTypeDefinition* typeDef = (const Il2CppTypeDefinition*)il2cpp::vm::GlobalMetadata::GetTypeHandleFromIndex(rowIndex);
                Il2CppClass* klass = il2cpp::vm::GlobalMetadata::GetTypeInfoFromHandle((Il2CppMetadataTypeHandle)typeDef);
                return &klass->byval_arg;
            }
            else if (tableType == TableType::TYPESPEC)
            {
                // 对于泛型类型，需要解析类型规范
                return il2cpp::vm::GlobalMetadata::GetIl2CppTypeFromIndex(token);
            }
        }

        return nullptr;
    }

    bool UnifiedMetadataProvider::IsInterpreterAssembly(const Il2CppAssembly* ass)
    {
        if (!ass || !ass->image)
        {
            return false;
        }

        return IsInterpreterImage(ass->image);
    }

    bool UnifiedMetadataProvider::IsInterpreterMethod(const MethodInfo* method)
    {
        if (!method)
        {
            return false;
        }

        return method->isInterpterImpl;
    }

    uint64_t UnifiedMetadataProvider::GenerateGenericClassKey(const Il2CppType* genericType, const Il2CppGenericInst* classInst)
    {
        uint64_t key = 0;
        key ^= (uint64_t)genericType;
        key ^= (uint64_t)classInst << 32;
        return key;
    }

    uint64_t UnifiedMetadataProvider::GenerateGenericMethodKey(const MethodInfo* method, const Il2CppGenericInst* methodInst)
    {
        uint64_t key = 0;
        key ^= (uint64_t)method;
        key ^= (uint64_t)methodInst << 32;
        return key;
    }

    uint64_t UnifiedMetadataProvider::GenerateGenericInstKey(const Il2CppType** types, uint32_t typeCount)
    {
        uint64_t key = typeCount;
        for (uint32_t i = 0; i < typeCount; i++)
        {
            key ^= (uint64_t)types[i] << (i % 8);
        }
        return key;
    }

}
}
