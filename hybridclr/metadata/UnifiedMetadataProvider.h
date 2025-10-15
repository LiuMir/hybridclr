#pragma once

#include "Image.h"
#include "vm/Assembly.h"
#include "vm/Class.h"
#include "vm/Method.h"

namespace hybridclr
{
namespace metadata
{

    // 泛型元数据工厂类
    class GenericMetadataFactory
    {
    public:
        // 创建泛型类实例
        static Il2CppGenericClass* CreateGenericClass(
            const Il2CppType* genericType, 
            const Il2CppGenericInst* classInst);
        
        // 创建泛型方法实例
        static Il2CppGenericMethod* CreateGenericMethod(
            const MethodInfo* method, 
            const Il2CppGenericInst* methodInst);
        
        // 创建泛型实例
        static Il2CppGenericInst* CreateGenericInst(
            const Il2CppType** types, 
            uint32_t typeCount);
        
        // 检查泛型类型是否已实例化
        static bool IsGenericTypeInstantiated(const Il2CppType* type);
        
        // 检查泛型方法是否已实例化
        static bool IsGenericMethodInstantiated(const MethodInfo* method);
    };

    // 统一元数据提供者
    class UnifiedMetadataProvider
    {
        friend class GenericMetadataFactory;
    public:
        // 获取程序集对应的元数据镜像
        static Image* GetImageForAssembly(const Il2CppAssembly* ass);
        
        // 获取类型定义
        static const Il2CppTypeDefinition* GetTypeDefinition(const Il2CppType* type);
        
        // 获取方法定义
        static const Il2CppMethodDefinition* GetMethodDefinition(const MethodInfo* method);
        
        // 获取字段定义
        static const Il2CppFieldDefinition* GetFieldDefinition(const FieldInfo* field);
        
        // 获取泛型容器
        static Il2CppGenericContainer* GetGenericContainer(const Il2CppType* type);
        
        // 获取方法体
        static MethodBody* GetMethodBody(const MethodInfo* method);
        
        // 从 token 获取方法信息（优化版本：直接接收 image 和 tokenCache）
        static const MethodInfo* GetMethodInfoFromToken(
            Image* image,
            Token2RuntimeHandleMap& tokenCache,
            uint32_t token,
            const Il2CppGenericContainer* klassContainer,
            const Il2CppGenericContainer* methodContainer,
            const Il2CppGenericContext* genericContext);
        
        // 从 token 获取方法信息（兼容版本：保持向后兼容）
        static const MethodInfo* GetMethodInfoFromToken(
            const Il2CppAssembly* ass,
            uint32_t token,
            const Il2CppGenericContainer* klassContainer,
            const Il2CppGenericContainer* methodContainer,
            const Il2CppGenericContext* genericContext);
        
        // 从 token 获取类型信息
        static const Il2CppType* GetTypeFromToken(
            const Il2CppAssembly* ass,
            uint32_t token,
            const Il2CppGenericContainer* klassContainer,
            const Il2CppGenericContext* genericContext);
        
        // 检查是否为解释器程序集
        static bool IsInterpreterAssembly(const Il2CppAssembly* ass);
        
        // 检查是否为解释器方法
        static bool IsInterpreterMethod(const MethodInfo* method);
        
        // 初始化统一元数据提供者
        static void Initialize();
        
        // 清理资源
        static void Cleanup();
        
    private:
        // 缓存已创建的泛型实例
        static std::unordered_map<uint64_t, Il2CppGenericClass*> s_genericClassCache;
        static std::unordered_map<uint64_t, Il2CppGenericMethod*> s_genericMethodCache;
        static std::unordered_map<uint64_t, Il2CppGenericInst*> s_genericInstCache;
        
        // 生成缓存键
        static uint64_t GenerateGenericClassKey(const Il2CppType* genericType, const Il2CppGenericInst* classInst);
        static uint64_t GenerateGenericMethodKey(const MethodInfo* method, const Il2CppGenericInst* methodInst);
        static uint64_t GenerateGenericInstKey(const Il2CppType** types, uint32_t typeCount);
    };

}
}
