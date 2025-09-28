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

        // 对于 AOT 程序集，创建 AOT 元数据镜像适配器
        // 注意：这里创建的对象需要由调用者管理生命周期
        return new AOTMetadataImage(ass);
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

        return method->methodDefinition;
    }

    const Il2CppFieldDefinition* UnifiedMetadataProvider::GetFieldDefinition(const FieldInfo* field)
    {
        if (!field)
        {
            return nullptr;
        }

        return field->fieldDefinition;
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

        Image* image = GetImageForAssembly(ass);
        if (!image)
        {
            return nullptr;
        }

        return image->GetMethodInfoFromToken(token, klassContainer, methodContainer, genericContext);
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

        Image* image = GetImageForAssembly(ass);
        if (!image)
        {
            return nullptr;
        }

        return image->GetIl2CppTypeFromToken(token, klassContainer, genericContext);
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

    // ==================== AOTMetadataImage 实现 ====================

    AOTMetadataImage::AOTMetadataImage(const Il2CppAssembly* assembly)
        : _targetAssembly(assembly)
    {
    }

    AOTMetadataImage::~AOTMetadataImage()
    {
    }

    MethodBody* AOTMetadataImage::GetMethodBody(uint32_t token)
    {
        // AOT 方法没有 IL 方法体
        return nullptr;
    }

    const Il2CppType* AOTMetadataImage::GetIl2CppTypeFromRawTypeDefIndex(uint32_t index)
    {
        const Il2CppTypeDefinition* typeDef = GetAOTTypeDefinition(index);
        if (!typeDef)
        {
            return nullptr;
        }

        return il2cpp::vm::GlobalMetadata::GetIl2CppTypeFromIndex(typeDef->byvalTypeIndex);
    }

    Il2CppGenericContainer* AOTMetadataImage::GetGenericContainerByRawIndex(uint32_t index)
    {
        return GetAOTGenericContainer(index);
    }

    Il2CppGenericContainer* AOTMetadataImage::GetGenericContainerByTypeDefRawIndex(int32_t typeDefIndex)
    {
        const Il2CppTypeDefinition* typeDef = GetAOTTypeDefinition(typeDefIndex);
        if (!typeDef)
        {
            return nullptr;
        }

        if (typeDef->genericContainerIndex != kGenericContainerIndexInvalid)
        {
            return GetAOTGenericContainer(typeDef->genericContainerIndex);
        }

        return nullptr;
    }

    const Il2CppMethodDefinition* AOTMetadataImage::GetMethodDefinitionFromRawIndex(uint32_t index)
    {
        return GetAOTMethodDefinition(index);
    }

    void AOTMetadataImage::ReadFieldRefInfoFromFieldDefToken(uint32_t rowIndex, FieldRefInfo& ret)
    {
        const Il2CppFieldDefinition* fieldDef = GetAOTFieldDefinition(rowIndex);
        if (!fieldDef)
        {
            ret.fieldDef = nullptr;
            return;
        }

        ret.fieldDef = fieldDef;
        ret.declaringType = il2cpp::vm::GlobalMetadata::GetIl2CppTypeFromIndex(fieldDef->typeIndex);
    }

    const Il2CppType* AOTMetadataImage::GetModuleIl2CppType(uint32_t moduleRowIndex, uint32_t typeNamespace, uint32_t typeName, bool raiseExceptionIfNotFound)
    {
        // 从 AOT 程序集中查找类型
        const Il2CppImage* image = _targetAssembly->image;
        for (uint32_t i = 0; i < image->typeCount; i++)
        {
            const Il2CppTypeDefinition* typeDef = (const Il2CppTypeDefinition*)il2cpp::vm::MetadataCache::GetAssemblyTypeHandle(image, i);
            const char* name = il2cpp::vm::GlobalMetadata::GetStringFromIndex(typeDef->nameIndex);
            const char* namespaze = il2cpp::vm::GlobalMetadata::GetStringFromIndex(typeDef->namespaceIndex);
            
            if (std::strcmp(name, typeName) == 0 && std::strcmp(namespaze, typeNamespace) == 0)
            {
                return il2cpp::vm::GlobalMetadata::GetIl2CppTypeFromIndex(typeDef->byvalTypeIndex);
            }
        }

        if (raiseExceptionIfNotFound)
        {
            il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetTypeLoadException("Type not found"));
        }

        return nullptr;
    }

    const Il2CppType* AOTMetadataImage::ReadTypeFromResolutionScope(uint32_t scope, uint32_t typeNamespace, uint32_t typeName)
    {
        // 简化实现，直接在当前程序集中查找
        return GetModuleIl2CppType(0, typeNamespace, typeName, false);
    }

    const Il2CppTypeDefinition* AOTMetadataImage::GetAOTTypeDefinition(uint32_t index)
    {
        const Il2CppImage* image = _targetAssembly->image;
        if (index >= image->typeCount)
        {
            return nullptr;
        }

        return (const Il2CppTypeDefinition*)il2cpp::vm::MetadataCache::GetAssemblyTypeHandle(image, index);
    }

    const Il2CppMethodDefinition* AOTMetadataImage::GetAOTMethodDefinition(uint32_t index)
    {
        const Il2CppImage* image = _targetAssembly->image;
        if (index >= image->methodCount)
        {
            return nullptr;
        }

        return il2cpp::vm::GlobalMetadata::GetMethodDefinitionFromIndex(image->methodStart + index);
    }

    const Il2CppFieldDefinition* AOTMetadataImage::GetAOTFieldDefinition(uint32_t index)
    {
        const Il2CppImage* image = _targetAssembly->image;
        if (index >= image->fieldCount)
        {
            return nullptr;
        }

        return il2cpp::vm::GlobalMetadata::GetFieldDefinitionFromIndex(image->fieldStart + index);
    }

    Il2CppGenericContainer* AOTMetadataImage::GetAOTGenericContainer(uint32_t index)
    {
        return (Il2CppGenericContainer*)il2cpp::vm::GlobalMetadata::GetGenericContainerFromIndex(index);
    }

    const MethodInfo* AOTMetadataImage::GetMethodInfoFromToken(uint32_t token, const Il2CppGenericContainer* klassContainer, const Il2CppGenericContainer* methodContainer, const Il2CppGenericContext* genericContext)
    {
        // 从 AOT 程序集获取方法信息
        const Il2CppImage* image = _targetAssembly->image;
        uint32_t methodIndex = DecodeTokenRowIndex(token);
        
        if (methodIndex >= image->methodCount)
        {
            return nullptr;
        }
        
        const Il2CppMethodDefinition* methodDef = il2cpp::vm::GlobalMetadata::GetMethodDefinitionFromIndex(image->methodStart + methodIndex);
        if (!methodDef)
        {
            return nullptr;
        }
        
        // 查找对应的 MethodInfo
        for (uint32_t i = 0; i < image->typeCount; i++)
        {
            const Il2CppTypeDefinition* typeDef = (const Il2CppTypeDefinition*)il2cpp::vm::MetadataCache::GetAssemblyTypeHandle(image, i);
            const Il2CppClass* klass = il2cpp::vm::Class::FromIl2CppType(il2cpp::vm::GlobalMetadata::GetIl2CppTypeFromIndex(typeDef->byvalTypeIndex), false);
            
            if (klass)
            {
                for (uint16_t j = 0; j < klass->method_count; j++)
                {
                    const MethodInfo* methodInfo = klass->methods[j];
                    if (methodInfo->methodDefinition == methodDef)
                    {
                        return methodInfo;
                    }
                }
            }
        }
        
        return nullptr;
    }

    const Il2CppType* AOTMetadataImage::GetIl2CppTypeFromToken(uint32_t token, const Il2CppGenericContainer* klassContainer, const Il2CppGenericContext* genericContext)
    {
        // 从 AOT 程序集获取类型信息
        const Il2CppImage* image = _targetAssembly->image;
        uint32_t typeIndex = DecodeTokenRowIndex(token);
        
        if (typeIndex >= image->typeCount)
        {
            return nullptr;
        }
        
        const Il2CppTypeDefinition* typeDef = (const Il2CppTypeDefinition*)il2cpp::vm::MetadataCache::GetAssemblyTypeHandle(image, typeIndex);
        if (!typeDef)
        {
            return nullptr;
        }
        
        return il2cpp::vm::GlobalMetadata::GetIl2CppTypeFromIndex(typeDef->byvalTypeIndex);
    }

}
}
