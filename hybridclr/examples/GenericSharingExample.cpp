/*
 * 完全泛型共享机制使用示例
 * 
 * 这个示例展示了如何使用新的统一元数据提供者来替代 LoadMetadataForAOTAssembly
 * 现在解释器可以直接使用 AOT 程序集的元数据，无需额外的同源镜像
 */

#include "metadata/UnifiedMetadataProvider.h"
#include "vm/Assembly.h"
#include "vm/Class.h"
#include "vm/Method.h"
#include "vm/Type.h"

namespace hybridclr
{
namespace examples
{

    // 示例：使用完全泛型共享机制
    class GenericSharingExample
    {
    public:
        // 初始化完全泛型共享机制
        static void InitializeGenericSharing()
        {
            // 初始化统一元数据提供者
            hybridclr::metadata::UnifiedMetadataProvider::Initialize();
            
            // 现在不再需要调用 LoadMetadataForAOTAssembly
            // 解释器可以直接访问 AOT 程序集的元数据
        }

        // 示例：直接使用 AOT 程序集的泛型类型
        static void UseAOTGenericTypes()
        {
            // 获取 System.Collections.Generic.List<T> 类型
            const Il2CppAssembly* mscorlib = il2cpp::vm::Assembly::GetLoadedAssembly("mscorlib");
            if (mscorlib)
            {
                // 使用统一元数据提供者获取镜像
                hybridclr::metadata::Image* image = 
                    hybridclr::metadata::UnifiedMetadataProvider::GetImageForAssembly(mscorlib);
                
                if (image)
                {
                    // 直接访问 AOT 程序集的元数据，无需同源镜像
                    const Il2CppType* listType = image->GetModuleIl2CppType(0, "System.Collections.Generic", "List`1", true);
                    
                    if (listType)
                    {
                        // 创建泛型实例 List<string>
                        const Il2CppType* stringType = il2cpp_defaults.string_class->byval_arg;
                        const Il2CppType* types[] = { stringType };
                        
                        Il2CppGenericInst* genericInst = 
                            hybridclr::metadata::GenericMetadataFactory::CreateGenericInst(types, 1);
                        
                        Il2CppGenericClass* genericClass = 
                            hybridclr::metadata::GenericMetadataFactory::CreateGenericClass(listType, genericInst);
                        
                        // 现在可以使用泛型类型了
                        const Il2CppClass* instantiatedClass = 
                            il2cpp::vm::Class::FromGenericClass(genericClass);
                        
                        if (instantiatedClass)
                        {
                            // 成功获取了 List<string> 类型，无需额外的元数据加载
                            printf("Successfully created List<string> type using generic sharing\n");
                        }
                    }
                }
            }
        }

        // 示例：直接调用 AOT 程序集的方法
        static void CallAOTMethods()
        {
            // 获取 System.String 类型
            const Il2CppClass* stringClass = il2cpp_defaults.string_class;
            
            // 使用统一元数据提供者获取方法信息
            const MethodInfo* methodInfo = 
                hybridclr::metadata::UnifiedMetadataProvider::GetMethodInfoFromToken(
                    stringClass->image->assembly, 
                    stringClass->methods[0]->token,  // 假设是第一个方法
                    nullptr, nullptr, nullptr);
            
            if (methodInfo)
            {
                // 直接调用 AOT 方法，无需额外的元数据准备
                printf("Successfully resolved AOT method using generic sharing\n");
            }
        }

        // 示例：检查泛型类型是否已实例化
        static void CheckGenericInstantiation()
        {
            // 使用一个实际的泛型类型，而不是 generic_class
            const Il2CppType* genericType = il2cpp_defaults.object_class->byval_arg;
            const Il2CppType* instantiatedType = il2cpp_defaults.string_class->byval_arg;
            
            bool isGenericInstantiated = 
                hybridclr::metadata::GenericMetadataFactory::IsGenericTypeInstantiated(genericType);
            bool isStringInstantiated = 
                hybridclr::metadata::GenericMetadataFactory::IsGenericTypeInstantiated(instantiatedType);
            
            printf("Generic type instantiated: %s\n", isGenericInstantiated ? "true" : "false");
            printf("String type instantiated: %s\n", isStringInstantiated ? "true" : "false");
        }

        // 清理资源
        static void Cleanup()
        {
            hybridclr::metadata::UnifiedMetadataProvider::Cleanup();
        }
    };

    // 使用示例
    void DemonstrateGenericSharing()
    {
        printf("=== 完全泛型共享机制演示 ===\n");
        
        // 1. 初始化
        GenericSharingExample::InitializeGenericSharing();
        
        // 2. 使用 AOT 泛型类型
        GenericSharingExample::UseAOTGenericTypes();
        
        // 3. 调用 AOT 方法
        GenericSharingExample::CallAOTMethods();
        
        // 4. 检查泛型实例化
        GenericSharingExample::CheckGenericInstantiation();
        
        // 5. 清理
        GenericSharingExample::Cleanup();
        
        printf("=== 演示完成 ===\n");
    }

}
}
