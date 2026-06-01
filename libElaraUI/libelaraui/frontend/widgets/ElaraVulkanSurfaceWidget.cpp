#include "ElaraVulkanSurfaceWidget.h"
#include "ElaraRootWidget.h"

#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace elara {

namespace {

// --- Vulkan type definitions (no system headers needed) ---

typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef uint32_t VkSampleCountFlags;

struct VkInstance_T;   typedef VkInstance_T*   VkInstance;
struct VkPhysDevice_T; typedef VkPhysDevice_T* VkPhysicalDevice;
struct VkDevice_T;     typedef VkDevice_T*     VkDevice;
struct VkQueue_T;      typedef VkQueue_T*      VkQueue;
struct VkCmdPool_T;    typedef VkCmdPool_T*    VkCommandPool;
struct VkCmdBuf_T;     typedef VkCmdBuf_T*     VkCommandBuffer;
struct VkDSLayout_T;   typedef VkDSLayout_T*   VkDescriptorSetLayout;
struct VkPipeLayout_T; typedef VkPipeLayout_T* VkPipelineLayout;
struct VkShaderMod_T;  typedef VkShaderMod_T*  VkShaderModule;
struct VkPipeline_T;   typedef VkPipeline_T*   VkPipeline;
struct VkPipeCache_T;  typedef VkPipeCache_T*  VkPipelineCache;
struct VkDescPool_T;   typedef VkDescPool_T*   VkDescriptorPool;
struct VkDescSet_T;    typedef VkDescSet_T*    VkDescriptorSet;
struct VkBuffer_T;     typedef VkBuffer_T*     VkBuffer;
struct VkMemory_T;     typedef VkMemory_T*     VkDeviceMemory;

typedef uint64_t VkNonDispHandle;

#define VK_SUCCESS                     0
#define VK_STRUCTURE_TYPE_APPLICATION_INFO                   0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO               1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO           2
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO                 3
#define VK_STRUCTURE_TYPE_SUBMIT_INFO                        4
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO           39
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO       40
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO          42
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 37
#define VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO        33
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO       34
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET               35
#define VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO        30
#define VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO       29
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO  18
#define VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO          16
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO                 12
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO               5
#define VK_STRUCTURE_TYPE_PUSH_CONSTANT_RANGE                (VkStructureType)0  // not a real stype

#define VK_QUEUE_COMPUTE_BIT              0x00000002
#define VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 0x00000020
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  0x00000002
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 0x00000004
#define VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 7
#define VK_COMMAND_BUFFER_LEVEL_PRIMARY   0
#define VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 0x00000001
#define VK_PIPELINE_BIND_POINT_COMPUTE    1
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 0x00000800
#define VK_SHADER_STAGE_COMPUTE_BIT       0x00000020
#define VK_ACCESS_SHADER_WRITE_BIT        0x00000100
#define VK_ACCESS_SHADER_READ_BIT         0x00000020
#define VK_PIPELINE_CACHE_CREATE_FLAG_BITS_MAX_ENUM 0x7FFFFFFF

typedef int32_t  VkResult;
typedef uint32_t VkStructureType;
typedef uint32_t VkQueueFlags;
typedef uint32_t VkMemoryPropertyFlags;
typedef uint32_t VkBufferUsageFlags;
typedef uint32_t VkDescriptorType;
typedef uint32_t VkPipelineStageFlags;
typedef uint32_t VkAccessFlags;
typedef uint32_t VkShaderStageFlags;

struct VkApplicationInfo {
    VkStructureType sType;
    const void*     pNext;
    const char*     pApplicationName;
    uint32_t        applicationVersion;
    const char*     pEngineName;
    uint32_t        engineVersion;
    uint32_t        apiVersion;
};
struct VkInstanceCreateInfo {
    VkStructureType          sType;
    const void*              pNext;
    VkFlags                  flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t                 enabledLayerCount;
    const char* const*       ppEnabledLayerNames;
    uint32_t                 enabledExtensionCount;
    const char* const*       ppEnabledExtensionNames;
};
struct VkQueueFamilyProperties {
    VkQueueFlags queueFlags;
    uint32_t     queueCount;
    uint32_t     timestampValidBits;
    uint32_t     minImageTransferGranularityWidth;
    uint32_t     minImageTransferGranularityHeight;
    uint32_t     minImageTransferGranularityDepth;
};
struct VkDeviceQueueCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    uint32_t        queueFamilyIndex;
    uint32_t        queueCount;
    const float*    pQueuePriorities;
};
struct VkDeviceCreateInfo {
    VkStructureType                  sType;
    const void*                      pNext;
    VkFlags                          flags;
    uint32_t                         queueCreateInfoCount;
    const VkDeviceQueueCreateInfo*   pQueueCreateInfos;
    uint32_t                         enabledLayerCount;
    const char* const*               ppEnabledLayerNames;
    uint32_t                         enabledExtensionCount;
    const char* const*               ppEnabledExtensionNames;
    const void*                      pEnabledFeatures;
};
struct VkMemoryType {
    VkMemoryPropertyFlags propertyFlags;
    uint32_t              heapIndex;
};
struct VkMemoryHeap {
    VkDeviceSize size;
    VkFlags      flags;
};
struct VkPhysicalDeviceMemoryProperties {
    uint32_t    memoryTypeCount;
    VkMemoryType memoryTypes[32];
    uint32_t    memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
};
struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t     memoryTypeBits;
};
struct VkMemoryAllocateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkDeviceSize    allocationSize;
    uint32_t        memoryTypeIndex;
};
struct VkBufferCreateInfo {
    VkStructureType    sType;
    const void*        pNext;
    VkFlags            flags;
    VkDeviceSize       size;
    VkBufferUsageFlags usage;
    int                sharingMode;
    uint32_t           queueFamilyIndexCount;
    const uint32_t*    pQueueFamilyIndices;
};
struct VkCommandPoolCreateInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    uint32_t        queueFamilyIndex;
};
struct VkCommandBufferAllocateInfo {
    VkStructureType    sType;
    const void*        pNext;
    VkCommandPool      commandPool;
    int                level;
    uint32_t           commandBufferCount;
};
struct VkCommandBufferBeginInfo {
    VkStructureType sType;
    const void*     pNext;
    VkFlags         flags;
    const void*     pInheritanceInfo;
};
struct VkDescriptorSetLayoutBinding {
    uint32_t           binding;
    VkDescriptorType   descriptorType;
    uint32_t           descriptorCount;
    VkShaderStageFlags stageFlags;
    const void*        pImmutableSamplers;
};
struct VkDescriptorSetLayoutCreateInfo {
    VkStructureType                       sType;
    const void*                           pNext;
    VkFlags                               flags;
    uint32_t                              bindingCount;
    const VkDescriptorSetLayoutBinding*   pBindings;
};
struct VkPushConstantRange {
    VkShaderStageFlags stageFlags;
    uint32_t           offset;
    uint32_t           size;
};
struct VkPipelineLayoutCreateInfo {
    VkStructureType             sType;
    const void*                 pNext;
    VkFlags                     flags;
    uint32_t                    setLayoutCount;
    const VkDescriptorSetLayout* pSetLayouts;
    uint32_t                    pushConstantRangeCount;
    const VkPushConstantRange*  pPushConstantRanges;
};
struct VkShaderModuleCreateInfo {
    VkStructureType  sType;
    const void*      pNext;
    VkFlags          flags;
    size_t           codeSize;
    const uint32_t*  pCode;
};
struct VkPipelineShaderStageCreateInfo {
    VkStructureType    sType;
    const void*        pNext;
    VkFlags            flags;
    VkShaderStageFlags stage;
    VkShaderModule     module;
    const char*        pName;
    const void*        pSpecializationInfo;
};
struct VkComputePipelineCreateInfo {
    VkStructureType                  sType;
    const void*                      pNext;
    VkFlags                          flags;
    VkPipelineShaderStageCreateInfo  stage;
    VkPipelineLayout                 layout;
    VkPipeline                       basePipelineHandle;
    int32_t                          basePipelineIndex;
};
struct VkDescriptorPoolSize {
    VkDescriptorType type;
    uint32_t         descriptorCount;
};
struct VkDescriptorPoolCreateInfo {
    VkStructureType          sType;
    const void*              pNext;
    VkFlags                  flags;
    uint32_t                 maxSets;
    uint32_t                 poolSizeCount;
    const VkDescriptorPoolSize* pPoolSizes;
};
struct VkDescriptorSetAllocateInfo {
    VkStructureType             sType;
    const void*                 pNext;
    VkDescriptorPool            descriptorPool;
    uint32_t                    descriptorSetCount;
    const VkDescriptorSetLayout* pSetLayouts;
};
struct VkDescriptorBufferInfo {
    VkBuffer     buffer;
    VkDeviceSize offset;
    VkDeviceSize range;
};
struct VkWriteDescriptorSet {
    VkStructureType            sType;
    const void*                pNext;
    VkDescriptorSet            dstSet;
    uint32_t                   dstBinding;
    uint32_t                   dstArrayElement;
    uint32_t                   descriptorCount;
    VkDescriptorType           descriptorType;
    const void*                pImageInfo;
    const VkDescriptorBufferInfo* pBufferInfo;
    const void*                pTexelBufferView;
};
struct VkSubmitInfo {
    VkStructureType          sType;
    const void*              pNext;
    uint32_t                 waitSemaphoreCount;
    const void*              pWaitSemaphores;
    const VkPipelineStageFlags* pWaitDstStageMask;
    uint32_t                 commandBufferCount;
    const VkCommandBuffer*   pCommandBuffers;
    uint32_t                 signalSemaphoreCount;
    const void*              pSignalSemaphores;
};

// Per-frame kernel command struct (matches SPIR-V flat uint layout)
struct VkSurfaceKernelCommand {
    uint32_t op;
    float x0, y0, x1, y1;
    float value0, value1;
    float r, g, b;
};
static_assert(sizeof(VkSurfaceKernelCommand) == 40, "command struct size mismatch");

struct VkSceneCameraState {
    double x;
    double y;
    double z;
    double yaw_deg;
    double pitch_deg;
    double roll_deg;
    double fov_deg;
    double near_z;
    double far_z;
};

struct VkSceneMaterialState {
    double r;
    double g;
    double b;
};

struct VkSceneInstanceState {
    int id;
    int mesh_id;
    int material_id;
    double x;
    double y;
    double z;
    double yaw_deg;
    double pitch_deg;
    double roll_deg;
    double sx;
    double sy;
    double sz;
    double r;
    double g;
    double b;
};

struct VkScenePoint2D {
    bool visible;
    double x;
    double y;
    double z;
};

static double vkSceneMilli(double value) {
    return value / 1000.0;
}

static double vkSceneMilliDeg(double value) {
    return value / 1000.0;
}

static int vkSceneClampInt(int value, int min_value, int max_value) {
    if(value < min_value) return min_value;
    if(value > max_value) return max_value;
    return value;
}

static VkSurfaceKernelCommand vkLineCommand(double x0, double y0, double x1, double y1,
                                            double line_width, double r, double g, double b) {
    VkSurfaceKernelCommand kc;
    kc.op = 2u;
    kc.x0 = (float)x0;
    kc.y0 = (float)y0;
    kc.x1 = (float)x1;
    kc.y1 = (float)y1;
    kc.value0 = (float)line_width;
    kc.value1 = 0.0f;
    kc.r = (float)r;
    kc.g = (float)g;
    kc.b = (float)b;
    return kc;
}

static VkScenePoint2D vkProjectScenePoint(const VkSceneCameraState& cam,
                                          double wx, double wy, double wz,
                                          int width, int height) {
    double dx = wx - cam.x;
    double dy = wy - cam.y;
    double dz = wz - cam.z;
    double yaw = -cam.yaw_deg * 3.14159265358979323846 / 180.0;
    double pitch = -cam.pitch_deg * 3.14159265358979323846 / 180.0;
    double cy = cos(yaw);
    double sy = sin(yaw);
    double cp = cos(pitch);
    double sp = sin(pitch);

    double vx = (dx * cy) - (dz * sy);
    double vz = (dx * sy) + (dz * cy);
    double vy = (dy * cp) - (vz * sp);
    vz = (dy * sp) + (vz * cp);

    VkScenePoint2D out;
    out.visible = false;
    out.x = 0.0;
    out.y = 0.0;
    out.z = vz;
    if(vz <= cam.near_z || vz >= cam.far_z) {
        return out;
    }

    double fov = cam.fov_deg > 1.0 ? cam.fov_deg : 60.0;
    double focal = ((double)height * 0.5) / tan((fov * 3.14159265358979323846 / 180.0) * 0.5);
    out.x = ((double)width * 0.5) + ((vx / vz) * focal);
    out.y = ((double)height * 0.5) - ((vy / vz) * focal);
    out.visible = true;
    return out;
}

static void vkAppendSceneInstanceWireframe(std::vector<VkSurfaceKernelCommand>& out,
                                           const VkSceneCameraState& cam,
                                           const VkSceneInstanceState& inst,
                                           int width,
                                           int height) {
    double hx = inst.sx * 0.5;
    double hy = inst.sy * 0.5;
    double hz = inst.sz * 0.5;
    double yaw = inst.yaw_deg * 3.14159265358979323846 / 180.0;
    double cy = cos(yaw);
    double sy = sin(yaw);
    double corners[8][3] = {
        {-hx, -hy, -hz}, { hx, -hy, -hz}, { hx,  hy, -hz}, {-hx,  hy, -hz},
        {-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz},
    };
    VkScenePoint2D pts[8];
    for(int i = 0; i < 8; i++) {
        double lx = corners[i][0];
        double ly = corners[i][1];
        double lz = corners[i][2];
        double wx = inst.x + (lx * cy) - (lz * sy);
        double wz = inst.z + (lx * sy) + (lz * cy);
        double wy = inst.y + ly;
        pts[i] = vkProjectScenePoint(cam, wx, wy, wz, width, height);
    }
    const int edges[12][2] = {
        {0,1}, {1,2}, {2,3}, {3,0},
        {4,5}, {5,6}, {6,7}, {7,4},
        {0,4}, {1,5}, {2,6}, {3,7},
    };
    for(int i = 0; i < 12; i++) {
        const VkScenePoint2D& a = pts[edges[i][0]];
        const VkScenePoint2D& b = pts[edges[i][1]];
        if(!a.visible || !b.visible) {
            continue;
        }
        out.push_back(vkLineCommand(a.x, a.y, b.x, b.y, 2.0, inst.r, inst.g, inst.b));
    }
}

// Push constants (20 bytes)
// 5-word header prepended to the command buffer (replaces push constants)
struct VkSurfaceCmdHeader {
    int32_t width;
    int32_t height;
    float   virtual_width;
    float   virtual_height;
    int32_t command_count;
};
static_assert(sizeof(VkSurfaceCmdHeader) == 20, "header size mismatch");

// Pre-compiled SPIR-V compute shader
static const uint32_t kVulkanSurfaceSPIRV[] = {
    0x07230203u, 0x00010300u, 0x00000000u, 0x000000d5u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0006000fu, 0x00000005u, 0x00000028u, 0x6e69616du, 0x00000000u, 0x00000013u, 0x00060010u, 0x00000028u,
    0x00000011u, 0x00000010u, 0x00000010u, 0x00000001u, 0x00040047u, 0x00000009u, 0x00000006u, 0x00000004u,
    0x00030047u, 0x0000000au, 0x00000002u, 0x00050048u, 0x0000000au, 0x00000000u, 0x00000023u, 0x00000000u,
    0x00040047u, 0x00000011u, 0x00000022u, 0x00000000u, 0x00040047u, 0x00000011u, 0x00000021u, 0x00000000u,
    0x00030047u, 0x0000000bu, 0x00000002u, 0x00050048u, 0x0000000bu, 0x00000000u, 0x00000023u, 0x00000000u,
    0x00040047u, 0x00000012u, 0x00000022u, 0x00000000u, 0x00040047u, 0x00000012u, 0x00000021u, 0x00000001u,
    0x00040048u, 0x0000000bu, 0x00000000u, 0x00000018u, 0x00040047u, 0x00000013u, 0x0000000bu, 0x0000001cu,
    0x00020013u, 0x00000002u, 0x00020014u, 0x00000003u, 0x00040015u, 0x00000004u, 0x00000020u, 0x00000001u,
    0x00040015u, 0x00000005u, 0x00000020u, 0x00000000u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u,
    0x00000007u, 0x00000006u, 0x00000002u, 0x00040017u, 0x00000008u, 0x00000005u, 0x00000003u, 0x0003001du,
    0x00000009u, 0x00000005u, 0x0003001eu, 0x0000000au, 0x00000009u, 0x0003001eu, 0x0000000bu, 0x00000009u,
    0x00040020u, 0x0000000cu, 0x0000000cu, 0x0000000au, 0x00040020u, 0x0000000du, 0x0000000cu, 0x0000000bu,
    0x00040020u, 0x0000000eu, 0x0000000cu, 0x00000005u, 0x00040020u, 0x0000000fu, 0x00000001u, 0x00000008u,
    0x00030021u, 0x00000010u, 0x00000002u, 0x0004003bu, 0x0000000cu, 0x00000011u, 0x0000000cu, 0x0004003bu,
    0x0000000du, 0x00000012u, 0x0000000cu, 0x0004003bu, 0x0000000fu, 0x00000013u, 0x00000001u, 0x0004002bu,
    0x00000004u, 0x00000014u, 0x00000000u, 0x0004002bu, 0x00000004u, 0x00000015u, 0x00000001u, 0x0004002bu,
    0x00000004u, 0x00000016u, 0x00000002u, 0x0004002bu, 0x00000004u, 0x00000017u, 0x00000003u, 0x0004002bu,
    0x00000004u, 0x00000018u, 0x00000004u, 0x0004002bu, 0x00000004u, 0x00000019u, 0x00000005u, 0x0004002bu,
    0x00000004u, 0x0000001au, 0x00000007u, 0x0004002bu, 0x00000004u, 0x0000001bu, 0x00000008u, 0x0004002bu,
    0x00000004u, 0x0000001cu, 0x00000009u, 0x0004002bu, 0x00000004u, 0x0000001du, 0x0000000au, 0x0004002bu,
    0x00000005u, 0x0000001eu, 0x00000008u, 0x0004002bu, 0x00000005u, 0x0000001fu, 0x00000010u, 0x0004002bu,
    0x00000005u, 0x00000020u, 0xff000000u, 0x0004002bu, 0x00000006u, 0x00000021u, 0x00000000u, 0x0004002bu,
    0x00000006u, 0x00000022u, 0x3f800000u, 0x0004002bu, 0x00000006u, 0x00000023u, 0x437f0000u, 0x0004002bu,
    0x00000006u, 0x00000024u, 0x3dcccccdu, 0x0004002bu, 0x00000006u, 0x00000025u, 0x3de147aeu, 0x0004002bu,
    0x00000006u, 0x00000026u, 0x3e0f5c29u, 0x0004002bu, 0x00000006u, 0x00000027u, 0x3fc00000u, 0x00050036u,
    0x00000002u, 0x00000028u, 0x00000000u, 0x00000010u, 0x000200f8u, 0x00000029u, 0x0004003du, 0x00000008u,
    0x00000039u, 0x00000013u, 0x00050051u, 0x00000005u, 0x0000003au, 0x00000039u, 0x00000000u, 0x00050051u,
    0x00000005u, 0x0000003bu, 0x00000039u, 0x00000001u, 0x0004007cu, 0x00000005u, 0x0000003cu, 0x00000014u,
    0x00060041u, 0x0000000eu, 0x0000003du, 0x00000012u, 0x00000014u, 0x0000003cu, 0x0004003du, 0x00000005u,
    0x0000003eu, 0x0000003du, 0x0004007cu, 0x00000004u, 0x0000003fu, 0x0000003eu, 0x0004007cu, 0x00000005u,
    0x00000040u, 0x00000015u, 0x00060041u, 0x0000000eu, 0x00000041u, 0x00000012u, 0x00000014u, 0x00000040u,
    0x0004003du, 0x00000005u, 0x00000042u, 0x00000041u, 0x0004007cu, 0x00000004u, 0x00000043u, 0x00000042u,
    0x0004007cu, 0x00000005u, 0x00000044u, 0x00000016u, 0x00060041u, 0x0000000eu, 0x00000045u, 0x00000012u,
    0x00000014u, 0x00000044u, 0x0004003du, 0x00000005u, 0x00000046u, 0x00000045u, 0x0004007cu, 0x00000006u,
    0x00000047u, 0x00000046u, 0x0004007cu, 0x00000005u, 0x00000048u, 0x00000017u, 0x00060041u, 0x0000000eu,
    0x00000049u, 0x00000012u, 0x00000014u, 0x00000048u, 0x0004003du, 0x00000005u, 0x0000004au, 0x00000049u,
    0x0004007cu, 0x00000006u, 0x0000004bu, 0x0000004au, 0x0004007cu, 0x00000005u, 0x0000004cu, 0x00000018u,
    0x00060041u, 0x0000000eu, 0x0000004du, 0x00000012u, 0x00000014u, 0x0000004cu, 0x0004003du, 0x00000005u,
    0x0000004eu, 0x0000004du, 0x0004007cu, 0x00000004u, 0x0000004fu, 0x0000004eu, 0x0004007cu, 0x00000005u,
    0x00000050u, 0x0000003fu, 0x0004007cu, 0x00000005u, 0x00000051u, 0x00000043u, 0x000500b0u, 0x00000003u,
    0x00000052u, 0x0000003au, 0x00000050u, 0x000500b0u, 0x00000003u, 0x00000053u, 0x0000003bu, 0x00000051u,
    0x000500a7u, 0x00000003u, 0x00000054u, 0x00000052u, 0x00000053u, 0x000300f7u, 0x0000002au, 0x00000000u,
    0x000400fau, 0x00000054u, 0x0000002au, 0x00000055u, 0x000200f8u, 0x00000055u, 0x000100fdu, 0x000200f8u,
    0x0000002au, 0x0004006fu, 0x00000006u, 0x00000056u, 0x0000003au, 0x0004006fu, 0x00000006u, 0x00000057u,
    0x0000003bu, 0x00040070u, 0x00000006u, 0x00000058u, 0x0000003fu, 0x00040070u, 0x00000006u, 0x00000059u,
    0x00000043u, 0x00050088u, 0x00000006u, 0x0000005bu, 0x00000056u, 0x00000058u, 0x00050085u, 0x00000006u,
    0x0000005au, 0x0000005bu, 0x00000047u, 0x00050088u, 0x00000006u, 0x0000005du, 0x00000057u, 0x00000059u,
    0x00050085u, 0x00000006u, 0x0000005cu, 0x0000005du, 0x0000004bu, 0x00050088u, 0x00000006u, 0x0000005eu,
    0x00000047u, 0x00000058u, 0x0007000cu, 0x00000006u, 0x0000005fu, 0x00000001u, 0x00000028u, 0x0000005eu,
    0x00000027u, 0x000200f9u, 0x0000002bu, 0x000200f8u, 0x0000002bu, 0x000700f5u, 0x00000004u, 0x00000060u,
    0x00000014u, 0x0000002au, 0x000000c0u, 0x0000002du, 0x000700f5u, 0x00000006u, 0x00000061u, 0x00000024u,
    0x0000002au, 0x000000bdu, 0x0000002du, 0x000700f5u, 0x00000006u, 0x00000062u, 0x00000025u, 0x0000002au,
    0x000000beu, 0x0000002du, 0x000700f5u, 0x00000006u, 0x00000063u, 0x00000026u, 0x0000002au, 0x000000bfu,
    0x0000002du, 0x000500b1u, 0x00000003u, 0x00000064u, 0x00000060u, 0x0000004fu, 0x000400f6u, 0x0000002eu,
    0x0000002du, 0x00000000u, 0x000400fau, 0x00000064u, 0x0000002cu, 0x0000002eu, 0x000200f8u, 0x0000002cu,
    0x00050084u, 0x00000004u, 0x00000065u, 0x00000060u, 0x0000001du, 0x00050080u, 0x00000004u, 0x00000066u,
    0x00000065u, 0x00000019u, 0x0004007cu, 0x00000005u, 0x00000067u, 0x00000066u, 0x00060041u, 0x0000000eu,
    0x00000068u, 0x00000012u, 0x00000014u, 0x00000067u, 0x0004003du, 0x00000005u, 0x00000069u, 0x00000068u,
    0x0004007cu, 0x00000004u, 0x0000006au, 0x00000069u, 0x00050080u, 0x00000004u, 0x0000006cu, 0x00000066u,
    0x00000015u, 0x0004007cu, 0x00000005u, 0x0000006bu, 0x0000006cu, 0x00060041u, 0x0000000eu, 0x0000006du,
    0x00000012u, 0x00000014u, 0x0000006bu, 0x0004003du, 0x00000005u, 0x0000006eu, 0x0000006du, 0x0004007cu,
    0x00000006u, 0x0000006fu, 0x0000006eu, 0x00050080u, 0x00000004u, 0x00000071u, 0x00000066u, 0x00000016u,
    0x0004007cu, 0x00000005u, 0x00000070u, 0x00000071u, 0x00060041u, 0x0000000eu, 0x00000072u, 0x00000012u,
    0x00000014u, 0x00000070u, 0x0004003du, 0x00000005u, 0x00000073u, 0x00000072u, 0x0004007cu, 0x00000006u,
    0x00000074u, 0x00000073u, 0x00050080u, 0x00000004u, 0x00000076u, 0x00000066u, 0x00000017u, 0x0004007cu,
    0x00000005u, 0x00000075u, 0x00000076u, 0x00060041u, 0x0000000eu, 0x00000077u, 0x00000012u, 0x00000014u,
    0x00000075u, 0x0004003du, 0x00000005u, 0x00000078u, 0x00000077u, 0x0004007cu, 0x00000006u, 0x00000079u,
    0x00000078u, 0x00050080u, 0x00000004u, 0x0000007bu, 0x00000066u, 0x00000018u, 0x0004007cu, 0x00000005u,
    0x0000007au, 0x0000007bu, 0x00060041u, 0x0000000eu, 0x0000007cu, 0x00000012u, 0x00000014u, 0x0000007au,
    0x0004003du, 0x00000005u, 0x0000007du, 0x0000007cu, 0x0004007cu, 0x00000006u, 0x0000007eu, 0x0000007du,
    0x00050080u, 0x00000004u, 0x00000080u, 0x00000066u, 0x0000001au, 0x0004007cu, 0x00000005u, 0x0000007fu,
    0x00000080u, 0x00060041u, 0x0000000eu, 0x00000081u, 0x00000012u, 0x00000014u, 0x0000007fu, 0x0004003du,
    0x00000005u, 0x00000082u, 0x00000081u, 0x0004007cu, 0x00000006u, 0x00000083u, 0x00000082u, 0x00050080u,
    0x00000004u, 0x00000085u, 0x00000066u, 0x0000001bu, 0x0004007cu, 0x00000005u, 0x00000084u, 0x00000085u,
    0x00060041u, 0x0000000eu, 0x00000086u, 0x00000012u, 0x00000014u, 0x00000084u, 0x0004003du, 0x00000005u,
    0x00000087u, 0x00000086u, 0x0004007cu, 0x00000006u, 0x00000088u, 0x00000087u, 0x00050080u, 0x00000004u,
    0x0000008au, 0x00000066u, 0x0000001cu, 0x0004007cu, 0x00000005u, 0x00000089u, 0x0000008au, 0x00060041u,
    0x0000000eu, 0x0000008bu, 0x00000012u, 0x00000014u, 0x00000089u, 0x0004003du, 0x00000005u, 0x0000008cu,
    0x0000008bu, 0x0004007cu, 0x00000006u, 0x0000008du, 0x0000008cu, 0x000500aau, 0x00000003u, 0x0000008eu,
    0x0000006au, 0x00000014u, 0x000300f7u, 0x00000030u, 0x00000000u, 0x000400fau, 0x0000008eu, 0x0000002fu,
    0x00000030u, 0x000200f8u, 0x0000002fu, 0x000200f9u, 0x00000030u, 0x000200f8u, 0x00000030u, 0x000700f5u,
    0x00000006u, 0x0000008fu, 0x00000083u, 0x0000002fu, 0x00000061u, 0x0000002cu, 0x000700f5u, 0x00000006u,
    0x00000090u, 0x00000088u, 0x0000002fu, 0x00000062u, 0x0000002cu, 0x000700f5u, 0x00000006u, 0x00000091u,
    0x0000008du, 0x0000002fu, 0x00000063u, 0x0000002cu, 0x000500aau, 0x00000003u, 0x00000092u, 0x0000006au,
    0x00000015u, 0x000300f7u, 0x00000032u, 0x00000000u, 0x000400fau, 0x00000092u, 0x00000031u, 0x00000032u,
    0x000200f8u, 0x00000031u, 0x00050081u, 0x00000006u, 0x00000093u, 0x0000006fu, 0x00000079u, 0x00050081u,
    0x00000006u, 0x00000094u, 0x00000074u, 0x0000007eu, 0x000500bbu, 0x00000003u, 0x00000095u, 0x0000005au,
    0x0000006fu, 0x000500b8u, 0x00000003u, 0x00000096u, 0x0000005au, 0x00000093u, 0x000500bbu, 0x00000003u,
    0x00000097u, 0x0000005cu, 0x00000074u, 0x000500b8u, 0x00000003u, 0x00000098u, 0x0000005cu, 0x00000094u,
    0x000500a7u, 0x00000003u, 0x00000099u, 0x00000095u, 0x00000096u, 0x000500a7u, 0x00000003u, 0x0000009au,
    0x00000097u, 0x00000098u, 0x000500a7u, 0x00000003u, 0x0000009bu, 0x00000099u, 0x0000009au, 0x000300f7u,
    0x00000036u, 0x00000000u, 0x000400fau, 0x0000009bu, 0x00000035u, 0x00000036u, 0x000200f8u, 0x00000035u,
    0x000200f9u, 0x00000036u, 0x000200f8u, 0x00000036u, 0x000700f5u, 0x00000006u, 0x0000009cu, 0x00000083u,
    0x00000035u, 0x0000008fu, 0x00000031u, 0x000700f5u, 0x00000006u, 0x0000009du, 0x00000088u, 0x00000035u,
    0x00000090u, 0x00000031u, 0x000700f5u, 0x00000006u, 0x0000009eu, 0x0000008du, 0x00000035u, 0x00000091u,
    0x00000031u, 0x000200f9u, 0x00000032u, 0x000200f8u, 0x00000032u, 0x000700f5u, 0x00000006u, 0x0000009fu,
    0x0000009cu, 0x00000036u, 0x0000008fu, 0x00000030u, 0x000700f5u, 0x00000006u, 0x000000a0u, 0x0000009du,
    0x00000036u, 0x00000090u, 0x00000030u, 0x000700f5u, 0x00000006u, 0x000000a1u, 0x0000009eu, 0x00000036u,
    0x00000091u, 0x00000030u, 0x000500aau, 0x00000003u, 0x000000a2u, 0x0000006au, 0x00000016u, 0x000300f7u,
    0x00000034u, 0x00000000u, 0x000400fau, 0x000000a2u, 0x00000033u, 0x00000034u, 0x000200f8u, 0x00000033u,
    0x00050083u, 0x00000006u, 0x000000a3u, 0x00000079u, 0x0000006fu, 0x00050083u, 0x00000006u, 0x000000a4u,
    0x0000007eu, 0x00000074u, 0x00050085u, 0x00000006u, 0x000000a5u, 0x000000a3u, 0x000000a3u, 0x00050085u,
    0x00000006u, 0x000000a6u, 0x000000a4u, 0x000000a4u, 0x00050081u, 0x00000006u, 0x000000a7u, 0x000000a5u,
    0x000000a6u, 0x00050083u, 0x00000006u, 0x000000a8u, 0x0000005au, 0x0000006fu, 0x00050083u, 0x00000006u,
    0x000000a9u, 0x0000005cu, 0x00000074u, 0x00050085u, 0x00000006u, 0x000000aau, 0x000000a8u, 0x000000a3u,
    0x00050085u, 0x00000006u, 0x000000abu, 0x000000a9u, 0x000000a4u, 0x00050081u, 0x00000006u, 0x000000acu,
    0x000000aau, 0x000000abu, 0x00050088u, 0x00000006u, 0x000000adu, 0x000000acu, 0x000000a7u, 0x0008000cu,
    0x00000006u, 0x000000aeu, 0x00000001u, 0x0000002bu, 0x000000adu, 0x00000021u, 0x00000022u, 0x00050085u,
    0x00000006u, 0x000000afu, 0x000000a3u, 0x000000aeu, 0x00050085u, 0x00000006u, 0x000000b0u, 0x000000a4u,
    0x000000aeu, 0x00050081u, 0x00000006u, 0x000000b1u, 0x0000006fu, 0x000000afu, 0x00050081u, 0x00000006u,
    0x000000b2u, 0x00000074u, 0x000000b0u, 0x00050083u, 0x00000006u, 0x000000b3u, 0x0000005au, 0x000000b1u,
    0x00050083u, 0x00000006u, 0x000000b4u, 0x0000005cu, 0x000000b2u, 0x00050085u, 0x00000006u, 0x000000b5u,
    0x000000b3u, 0x000000b3u, 0x00050085u, 0x00000006u, 0x000000b6u, 0x000000b4u, 0x000000b4u, 0x00050081u,
    0x00000006u, 0x000000b7u, 0x000000b5u, 0x000000b6u, 0x0006000cu, 0x00000006u, 0x000000b8u, 0x00000001u,
    0x0000001fu, 0x000000b7u, 0x000500bau, 0x00000003u, 0x000000b9u, 0x000000b8u, 0x0000005fu, 0x000300f7u,
    0x00000038u, 0x00000000u, 0x000400fau, 0x000000b9u, 0x00000037u, 0x00000038u, 0x000200f8u, 0x00000037u,
    0x000200f9u, 0x00000038u, 0x000200f8u, 0x00000038u, 0x000700f5u, 0x00000006u, 0x000000bau, 0x00000083u,
    0x00000037u, 0x0000009fu, 0x00000033u, 0x000700f5u, 0x00000006u, 0x000000bbu, 0x00000088u, 0x00000037u,
    0x000000a0u, 0x00000033u, 0x000700f5u, 0x00000006u, 0x000000bcu, 0x0000008du, 0x00000037u, 0x000000a1u,
    0x00000033u, 0x000200f9u, 0x00000034u, 0x000200f8u, 0x00000034u, 0x000700f5u, 0x00000006u, 0x000000bdu,
    0x000000bau, 0x00000038u, 0x0000009fu, 0x00000032u, 0x000700f5u, 0x00000006u, 0x000000beu, 0x000000bbu,
    0x00000038u, 0x000000a0u, 0x00000032u, 0x000700f5u, 0x00000006u, 0x000000bfu, 0x000000bcu, 0x00000038u,
    0x000000a1u, 0x00000032u, 0x000200f9u, 0x0000002du, 0x000200f8u, 0x0000002du, 0x00050080u, 0x00000004u,
    0x000000c0u, 0x00000060u, 0x00000015u, 0x000200f9u, 0x0000002bu, 0x000200f8u, 0x0000002eu, 0x0008000cu,
    0x00000006u, 0x000000c1u, 0x00000001u, 0x0000002bu, 0x00000061u, 0x00000021u, 0x00000022u, 0x0008000cu,
    0x00000006u, 0x000000c2u, 0x00000001u, 0x0000002bu, 0x00000062u, 0x00000021u, 0x00000022u, 0x0008000cu,
    0x00000006u, 0x000000c3u, 0x00000001u, 0x0000002bu, 0x00000063u, 0x00000021u, 0x00000022u, 0x00050085u,
    0x00000006u, 0x000000c4u, 0x000000c1u, 0x00000023u, 0x00050085u, 0x00000006u, 0x000000c5u, 0x000000c2u,
    0x00000023u, 0x00050085u, 0x00000006u, 0x000000c6u, 0x000000c3u, 0x00000023u, 0x0004006du, 0x00000005u,
    0x000000c7u, 0x000000c4u, 0x0004006du, 0x00000005u, 0x000000c8u, 0x000000c5u, 0x0004006du, 0x00000005u,
    0x000000c9u, 0x000000c6u, 0x000500c4u, 0x00000005u, 0x000000cau, 0x000000c8u, 0x0000001eu, 0x000500c4u,
    0x00000005u, 0x000000cbu, 0x000000c9u, 0x0000001fu, 0x000500c5u, 0x00000005u, 0x000000ccu, 0x000000c7u,
    0x000000cau, 0x000500c5u, 0x00000005u, 0x000000cdu, 0x000000ccu, 0x000000cbu, 0x000500c5u, 0x00000005u,
    0x000000ceu, 0x000000cdu, 0x00000020u, 0x0004007cu, 0x00000004u, 0x000000cfu, 0x0000003bu, 0x00050084u,
    0x00000004u, 0x000000d0u, 0x000000cfu, 0x0000003fu, 0x0004007cu, 0x00000004u, 0x000000d1u, 0x0000003au,
    0x00050080u, 0x00000004u, 0x000000d2u, 0x000000d0u, 0x000000d1u, 0x0004007cu, 0x00000005u, 0x000000d3u,
    0x000000d2u, 0x00060041u, 0x0000000eu, 0x000000d4u, 0x00000011u, 0x00000014u, 0x000000d3u, 0x0003003eu,
    0x000000d4u, 0x000000ceu, 0x000100fdu, 0x00010038u,
};
static const size_t kVulkanSurfaceSPIRVSize = sizeof(kVulkanSurfaceSPIRV);

} // namespace

class ElaraVulkanSurfaceWidget::VulkanRuntime {
private:
    void*          library_handle;
    VkInstance     instance;
    VkPhysicalDevice phys_device;
    VkDevice       device;
    VkQueue        compute_queue;
    uint32_t       queue_family;
    VkCommandPool  cmd_pool;
    VkCommandBuffer cmd_buf;
    VkDescriptorSetLayout ds_layout;
    VkPipelineLayout pipe_layout;
    VkShaderModule  shader_module;
    VkPipeline      pipeline;
    VkDescriptorPool desc_pool;
    VkPhysicalDeviceMemoryProperties mem_props;

    typedef VkResult (*FnCreateInstance)(const VkInstanceCreateInfo*, const void*, VkInstance*);
    typedef void     (*FnDestroyInstance)(VkInstance, const void*);
    typedef VkResult (*FnEnumPhysDevs)(VkInstance, uint32_t*, VkPhysicalDevice*);
    typedef void     (*FnGetPhysDevMemProps)(VkPhysicalDevice, VkPhysicalDeviceMemoryProperties*);
    typedef void     (*FnGetPhysDevQueueFamilyProps)(VkPhysicalDevice, uint32_t*, VkQueueFamilyProperties*);
    typedef VkResult (*FnCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const void*, VkDevice*);
    typedef void     (*FnDestroyDevice)(VkDevice, const void*);
    typedef void     (*FnGetDeviceQueue)(VkDevice, uint32_t, uint32_t, VkQueue*);
    typedef VkResult (*FnCreateCmdPool)(VkDevice, const VkCommandPoolCreateInfo*, const void*, VkCommandPool*);
    typedef void     (*FnDestroyCmdPool)(VkDevice, VkCommandPool, const void*);
    typedef VkResult (*FnAllocCmdBufs)(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer*);
    typedef void     (*FnFreeCmdBufs)(VkDevice, VkCommandPool, uint32_t, const VkCommandBuffer*);
    typedef VkResult (*FnCreateDSLayout)(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const void*, VkDescriptorSetLayout*);
    typedef void     (*FnDestroyDSLayout)(VkDevice, VkDescriptorSetLayout, const void*);
    typedef VkResult (*FnCreatePipeLayout)(VkDevice, const VkPipelineLayoutCreateInfo*, const void*, VkPipelineLayout*);
    typedef void     (*FnDestroyPipeLayout)(VkDevice, VkPipelineLayout, const void*);
    typedef VkResult (*FnCreateShaderMod)(VkDevice, const VkShaderModuleCreateInfo*, const void*, VkShaderModule*);
    typedef void     (*FnDestroyShaderMod)(VkDevice, VkShaderModule, const void*);
    typedef VkResult (*FnCreateComputePipelines)(VkDevice, VkPipelineCache, uint32_t, const VkComputePipelineCreateInfo*, const void*, VkPipeline*);
    typedef void     (*FnDestroyPipeline)(VkDevice, VkPipeline, const void*);
    typedef VkResult (*FnCreateDescPool)(VkDevice, const VkDescriptorPoolCreateInfo*, const void*, VkDescriptorPool*);
    typedef void     (*FnDestroyDescPool)(VkDevice, VkDescriptorPool, const void*);
    typedef VkResult (*FnAllocDescSets)(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet*);
    typedef VkResult (*FnResetDescPool)(VkDevice, VkDescriptorPool, VkFlags);
    typedef void     (*FnUpdateDescSets)(VkDevice, uint32_t, const VkWriteDescriptorSet*, uint32_t, const void*);
    typedef VkResult (*FnCreateBuffer)(VkDevice, const VkBufferCreateInfo*, const void*, VkBuffer*);
    typedef void     (*FnDestroyBuffer)(VkDevice, VkBuffer, const void*);
    typedef void     (*FnGetBufMemReqs)(VkDevice, VkBuffer, VkMemoryRequirements*);
    typedef VkResult (*FnAllocMem)(VkDevice, const VkMemoryAllocateInfo*, const void*, VkDeviceMemory*);
    typedef void     (*FnFreeMem)(VkDevice, VkDeviceMemory, const void*);
    typedef VkResult (*FnBindBufMem)(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize);
    typedef VkResult (*FnMapMem)(VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void**);
    typedef void     (*FnUnmapMem)(VkDevice, VkDeviceMemory);
    typedef VkResult (*FnBeginCmdBuf)(VkCommandBuffer, const VkCommandBufferBeginInfo*);
    typedef VkResult (*FnEndCmdBuf)(VkCommandBuffer);
    typedef void     (*FnCmdBindPipeline)(VkCommandBuffer, int, VkPipeline);
    typedef void     (*FnCmdBindDescSets)(VkCommandBuffer, int, VkPipelineLayout, uint32_t, uint32_t, const VkDescriptorSet*, uint32_t, const uint32_t*);
    typedef void     (*FnCmdPushConstants)(VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void*);
    typedef void     (*FnCmdDispatch)(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
    typedef VkResult (*FnQueueSubmit)(VkQueue, uint32_t, const VkSubmitInfo*, void*);
    typedef VkResult (*FnQueueWaitIdle)(VkQueue);

    FnCreateInstance          vkCreateInstance;
    FnDestroyInstance         vkDestroyInstance;
    FnEnumPhysDevs            vkEnumeratePhysicalDevices;
    FnGetPhysDevMemProps      vkGetPhysicalDeviceMemoryProperties;
    FnGetPhysDevQueueFamilyProps vkGetPhysicalDeviceQueueFamilyProperties;
    FnCreateDevice            vkCreateDevice;
    FnDestroyDevice           vkDestroyDevice;
    FnGetDeviceQueue          vkGetDeviceQueue;
    FnCreateCmdPool           vkCreateCommandPool;
    FnDestroyCmdPool          vkDestroyCommandPool;
    FnAllocCmdBufs            vkAllocateCommandBuffers;
    FnFreeCmdBufs             vkFreeCommandBuffers;
    FnCreateDSLayout          vkCreateDescriptorSetLayout;
    FnDestroyDSLayout         vkDestroyDescriptorSetLayout;
    FnCreatePipeLayout        vkCreatePipelineLayout;
    FnDestroyPipeLayout       vkDestroyPipelineLayout;
    FnCreateShaderMod         vkCreateShaderModule;
    FnDestroyShaderMod        vkDestroyShaderModule;
    FnCreateComputePipelines  vkCreateComputePipelines;
    FnDestroyPipeline         vkDestroyPipeline;
    FnCreateDescPool          vkCreateDescriptorPool;
    FnDestroyDescPool         vkDestroyDescriptorPool;
    FnAllocDescSets           vkAllocateDescriptorSets;
    FnResetDescPool           vkResetDescriptorPool;
    FnUpdateDescSets          vkUpdateDescriptorSets;
    FnCreateBuffer            vkCreateBuffer;
    FnDestroyBuffer           vkDestroyBuffer;
    FnGetBufMemReqs           vkGetBufferMemoryRequirements;
    FnAllocMem                vkAllocateMemory;
    FnFreeMem                 vkFreeMemory;
    FnBindBufMem              vkBindBufferMemory;
    FnMapMem                  vkMapMemory;
    FnUnmapMem                vkUnmapMemory;
    FnBeginCmdBuf             vkBeginCommandBuffer;
    FnEndCmdBuf               vkEndCommandBuffer;
    FnCmdBindPipeline         vkCmdBindPipeline;
    FnCmdBindDescSets         vkCmdBindDescriptorSets;
    FnCmdPushConstants        vkCmdPushConstants;
    FnCmdDispatch             vkCmdDispatch;
    FnQueueSubmit             vkQueueSubmit;
    FnQueueWaitIdle           vkQueueWaitIdle;

    uint32_t findMemoryType(uint32_t type_bits, VkMemoryPropertyFlags required) const {
        for(uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if((type_bits & (1u << i)) &&
               (mem_props.memoryTypes[i].propertyFlags & required) == required) {
                return i;
            }
        }
        return 0xFFFFFFFFu;
    }

    bool allocBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags mem_flags,
        VkBuffer* out_buf,
        VkDeviceMemory* out_mem
    ) {
        VkBufferCreateInfo bi;
        memset(&bi, 0, sizeof(bi));
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = size;
        bi.usage = usage;
        bi.sharingMode = 0; // exclusive

        if(vkCreateBuffer(device, &bi, 0, out_buf) != VK_SUCCESS) {
            return false;
        }

        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(device, *out_buf, &reqs);

        uint32_t mem_type = findMemoryType(reqs.memoryTypeBits, mem_flags);
        if(mem_type == 0xFFFFFFFFu) {
            vkDestroyBuffer(device, *out_buf, 0);
            *out_buf = 0;
            return false;
        }

        VkMemoryAllocateInfo ai;
        memset(&ai, 0, sizeof(ai));
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = reqs.size;
        ai.memoryTypeIndex = mem_type;

        if(vkAllocateMemory(device, &ai, 0, out_mem) != VK_SUCCESS) {
            vkDestroyBuffer(device, *out_buf, 0);
            *out_buf = 0;
            return false;
        }

        if(vkBindBufferMemory(device, *out_buf, *out_mem, 0) != VK_SUCCESS) {
            vkFreeMemory(device, *out_mem, 0);
            vkDestroyBuffer(device, *out_buf, 0);
            *out_buf  = 0;
            *out_mem  = 0;
            return false;
        }

        return true;
    }

public:
    VulkanRuntime()
        : library_handle(0), instance(0), phys_device(0), device(0),
          compute_queue(0), queue_family(0), cmd_pool(0), cmd_buf(0),
          ds_layout(0), pipe_layout(0), shader_module(0), pipeline(0),
          desc_pool(0),
          vkCreateInstance(0), vkDestroyInstance(0),
          vkEnumeratePhysicalDevices(0), vkGetPhysicalDeviceMemoryProperties(0),
          vkGetPhysicalDeviceQueueFamilyProperties(0),
          vkCreateDevice(0), vkDestroyDevice(0), vkGetDeviceQueue(0),
          vkCreateCommandPool(0), vkDestroyCommandPool(0),
          vkAllocateCommandBuffers(0), vkFreeCommandBuffers(0),
          vkCreateDescriptorSetLayout(0), vkDestroyDescriptorSetLayout(0),
          vkCreatePipelineLayout(0), vkDestroyPipelineLayout(0),
          vkCreateShaderModule(0), vkDestroyShaderModule(0),
          vkCreateComputePipelines(0), vkDestroyPipeline(0),
          vkCreateDescriptorPool(0), vkDestroyDescriptorPool(0),
          vkAllocateDescriptorSets(0), vkResetDescriptorPool(0),
          vkUpdateDescriptorSets(0),
          vkCreateBuffer(0), vkDestroyBuffer(0),
          vkGetBufferMemoryRequirements(0),
          vkAllocateMemory(0), vkFreeMemory(0), vkBindBufferMemory(0),
          vkMapMemory(0), vkUnmapMemory(0),
          vkBeginCommandBuffer(0), vkEndCommandBuffer(0),
          vkCmdBindPipeline(0), vkCmdBindDescriptorSets(0),
          vkCmdPushConstants(0), vkCmdDispatch(0),
          vkQueueSubmit(0), vkQueueWaitIdle(0)
    {
        memset(&mem_props, 0, sizeof(mem_props));
    }

    ~VulkanRuntime() {
        if(device) {
            if(desc_pool)    vkDestroyDescriptorPool(device, desc_pool, 0);
            if(pipeline)     vkDestroyPipeline(device, pipeline, 0);
            if(shader_module) vkDestroyShaderModule(device, shader_module, 0);
            if(pipe_layout)  vkDestroyPipelineLayout(device, pipe_layout, 0);
            if(ds_layout)    vkDestroyDescriptorSetLayout(device, ds_layout, 0);
            if(cmd_pool)     vkDestroyCommandPool(device, cmd_pool, 0);
            vkDestroyDevice(device, 0);
        }
        if(instance && vkDestroyInstance) {
            vkDestroyInstance(instance, 0);
        }
        if(library_handle) {
            dlclose(library_handle);
        }
    }

    bool load(String* status) {
        library_handle = dlopen("libvulkan.so.1", RTLD_LAZY);
        if(!library_handle) {
            if(status) *status = "Vulkan runtime library not found";
            return false;
        }

#define ELARA_LOAD_VK(name, type) \
        name = (type)dlsym(library_handle, #name); \
        if(!name) { \
            if(status) *status = String("Missing Vulkan symbol: ") + #name; \
            return false; \
        }

        ELARA_LOAD_VK(vkCreateInstance,                    FnCreateInstance)
        ELARA_LOAD_VK(vkDestroyInstance,                   FnDestroyInstance)
        ELARA_LOAD_VK(vkEnumeratePhysicalDevices,          FnEnumPhysDevs)
        ELARA_LOAD_VK(vkGetPhysicalDeviceMemoryProperties, FnGetPhysDevMemProps)
        ELARA_LOAD_VK(vkGetPhysicalDeviceQueueFamilyProperties, FnGetPhysDevQueueFamilyProps)
        ELARA_LOAD_VK(vkCreateDevice,                      FnCreateDevice)
        ELARA_LOAD_VK(vkDestroyDevice,                     FnDestroyDevice)
        ELARA_LOAD_VK(vkGetDeviceQueue,                    FnGetDeviceQueue)
        ELARA_LOAD_VK(vkCreateCommandPool,                 FnCreateCmdPool)
        ELARA_LOAD_VK(vkDestroyCommandPool,                FnDestroyCmdPool)
        ELARA_LOAD_VK(vkAllocateCommandBuffers,            FnAllocCmdBufs)
        ELARA_LOAD_VK(vkFreeCommandBuffers,                FnFreeCmdBufs)
        ELARA_LOAD_VK(vkCreateDescriptorSetLayout,         FnCreateDSLayout)
        ELARA_LOAD_VK(vkDestroyDescriptorSetLayout,        FnDestroyDSLayout)
        ELARA_LOAD_VK(vkCreatePipelineLayout,              FnCreatePipeLayout)
        ELARA_LOAD_VK(vkDestroyPipelineLayout,             FnDestroyPipeLayout)
        ELARA_LOAD_VK(vkCreateShaderModule,                FnCreateShaderMod)
        ELARA_LOAD_VK(vkDestroyShaderModule,               FnDestroyShaderMod)
        ELARA_LOAD_VK(vkCreateComputePipelines,            FnCreateComputePipelines)
        ELARA_LOAD_VK(vkDestroyPipeline,                   FnDestroyPipeline)
        ELARA_LOAD_VK(vkCreateDescriptorPool,              FnCreateDescPool)
        ELARA_LOAD_VK(vkDestroyDescriptorPool,             FnDestroyDescPool)
        ELARA_LOAD_VK(vkAllocateDescriptorSets,            FnAllocDescSets)
        ELARA_LOAD_VK(vkResetDescriptorPool,               FnResetDescPool)
        ELARA_LOAD_VK(vkUpdateDescriptorSets,              FnUpdateDescSets)
        ELARA_LOAD_VK(vkCreateBuffer,                      FnCreateBuffer)
        ELARA_LOAD_VK(vkDestroyBuffer,                     FnDestroyBuffer)
        ELARA_LOAD_VK(vkGetBufferMemoryRequirements,       FnGetBufMemReqs)
        ELARA_LOAD_VK(vkAllocateMemory,                    FnAllocMem)
        ELARA_LOAD_VK(vkFreeMemory,                        FnFreeMem)
        ELARA_LOAD_VK(vkBindBufferMemory,                  FnBindBufMem)
        ELARA_LOAD_VK(vkMapMemory,                         FnMapMem)
        ELARA_LOAD_VK(vkUnmapMemory,                       FnUnmapMem)
        ELARA_LOAD_VK(vkBeginCommandBuffer,                FnBeginCmdBuf)
        ELARA_LOAD_VK(vkEndCommandBuffer,                  FnEndCmdBuf)
        ELARA_LOAD_VK(vkCmdBindPipeline,                   FnCmdBindPipeline)
        ELARA_LOAD_VK(vkCmdBindDescriptorSets,             FnCmdBindDescSets)
        ELARA_LOAD_VK(vkCmdPushConstants,                  FnCmdPushConstants)
        ELARA_LOAD_VK(vkCmdDispatch,                       FnCmdDispatch)
        ELARA_LOAD_VK(vkQueueSubmit,                       FnQueueSubmit)
        ELARA_LOAD_VK(vkQueueWaitIdle,                     FnQueueWaitIdle)

#undef ELARA_LOAD_VK
        return true;
    }

    bool initialize(String* status) {
        VkApplicationInfo app;
        memset(&app, 0, sizeof(app));
        app.sType      = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.apiVersion = (1u << 22) | (1u << 12) | 0u; // Vulkan 1.1.0

        VkInstanceCreateInfo ici;
        memset(&ici, 0, sizeof(ici));
        ici.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ici.pApplicationInfo = &app;

        if(vkCreateInstance(&ici, 0, &instance) != VK_SUCCESS) {
            if(status) *status = "Failed to create Vulkan instance";
            return false;
        }

        uint32_t dev_count = 0;
        vkEnumeratePhysicalDevices(instance, &dev_count, 0);
        if(dev_count == 0) {
            if(status) *status = "No Vulkan physical devices";
            return false;
        }
        std::vector<VkPhysicalDevice> devs(dev_count);
        vkEnumeratePhysicalDevices(instance, &dev_count, &devs[0]);

        for(uint32_t di = 0; di < dev_count && !phys_device; di++) {
            uint32_t qf_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devs[di], &qf_count, 0);
            std::vector<VkQueueFamilyProperties> qfs(qf_count);
            vkGetPhysicalDeviceQueueFamilyProperties(devs[di], &qf_count, &qfs[0]);

            for(uint32_t qi = 0; qi < qf_count; qi++) {
                if(qfs[qi].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    phys_device  = devs[di];
                    queue_family = qi;
                    break;
                }
            }
        }

        if(!phys_device) {
            if(status) *status = "No Vulkan device with compute support";
            return false;
        }

        vkGetPhysicalDeviceMemoryProperties(phys_device, &mem_props);

        float priority = 1.0f;
        VkDeviceQueueCreateInfo qci;
        memset(&qci, 0, sizeof(qci));
        qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = queue_family;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &priority;

        VkDeviceCreateInfo dci;
        memset(&dci, 0, sizeof(dci));
        dci.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos    = &qci;

        if(vkCreateDevice(phys_device, &dci, 0, &device) != VK_SUCCESS) {
            if(status) *status = "Failed to create Vulkan device";
            return false;
        }

        vkGetDeviceQueue(device, queue_family, 0, &compute_queue);

        // Command pool
        VkCommandPoolCreateInfo cpci;
        memset(&cpci, 0, sizeof(cpci));
        cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.flags            = 0x00000002; // RESET_COMMAND_BUFFER_BIT
        cpci.queueFamilyIndex = queue_family;
        if(vkCreateCommandPool(device, &cpci, 0, &cmd_pool) != VK_SUCCESS) {
            if(status) *status = "Failed to create Vulkan command pool";
            return false;
        }

        VkCommandBufferAllocateInfo cbai;
        memset(&cbai, 0, sizeof(cbai));
        cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool        = cmd_pool;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        if(vkAllocateCommandBuffers(device, &cbai, &cmd_buf) != VK_SUCCESS) {
            if(status) *status = "Failed to allocate Vulkan command buffer";
            return false;
        }

        // Descriptor set layout: binding 0 = pixel storage, binding 1 = cmd storage (readonly)
        VkDescriptorSetLayoutBinding bindings[2];
        memset(bindings, 0, sizeof(bindings));
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dslci;
        memset(&dslci, 0, sizeof(dslci));
        dslci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 2;
        dslci.pBindings    = bindings;
        if(vkCreateDescriptorSetLayout(device, &dslci, 0, &ds_layout) != VK_SUCCESS) {
            if(status) *status = "Failed to create Vulkan descriptor set layout";
            return false;
        }

        VkPipelineLayoutCreateInfo plci;
        memset(&plci, 0, sizeof(plci));
        plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount         = 1;
        plci.pSetLayouts            = &ds_layout;
        plci.pushConstantRangeCount = 0;
        plci.pPushConstantRanges    = 0;
        if(vkCreatePipelineLayout(device, &plci, 0, &pipe_layout) != VK_SUCCESS) {
            if(status) *status = "Failed to create Vulkan pipeline layout";
            return false;
        }

        // Shader module from embedded SPIR-V
        VkShaderModuleCreateInfo smci;
        memset(&smci, 0, sizeof(smci));
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = kVulkanSurfaceSPIRVSize;
        smci.pCode    = kVulkanSurfaceSPIRV;
        if(vkCreateShaderModule(device, &smci, 0, &shader_module) != VK_SUCCESS) {
            if(status) *status = "Failed to create Vulkan shader module";
            return false;
        }

        // Compute pipeline
        VkComputePipelineCreateInfo cpci2;
        memset(&cpci2, 0, sizeof(cpci2));
        cpci2.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci2.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci2.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci2.stage.module = shader_module;
        cpci2.stage.pName  = "main";
        cpci2.layout       = pipe_layout;
        if(vkCreateComputePipelines(device, 0, 1, &cpci2, 0, &pipeline) != VK_SUCCESS) {
            if(status) *status = "Failed to create Vulkan compute pipeline";
            return false;
        }

        // Descriptor pool (enough for many resets-per-frame)
        VkDescriptorPoolSize pool_size;
        pool_size.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_size.descriptorCount = 2;
        VkDescriptorPoolCreateInfo dpci;
        memset(&dpci, 0, sizeof(dpci));
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets       = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes    = &pool_size;
        if(vkCreateDescriptorPool(device, &dpci, 0, &desc_pool) != VK_SUCCESS) {
            if(status) *status = "Failed to create Vulkan descriptor pool";
            return false;
        }

        if(status) *status = "Vulkan active";
        return true;
    }

    bool render(
        const std::vector<VkSurfaceKernelCommand>& cmd_data,
        int width,
        int height,
        float virtual_width,
        float virtual_height,
        std::vector<unsigned char>* pixels,
        String* status
    ) {
        if(!pixels || width <= 0 || height <= 0 || !device) {
            if(status) *status = "Vulkan render path unavailable";
            return false;
        }

        size_t pixel_bytes = (size_t)width * (size_t)height * 4u;

        // Build command buffer: 5-word header followed by command records
        VkSurfaceCmdHeader hdr;
        hdr.width          = width;
        hdr.height         = height;
        hdr.virtual_width  = virtual_width;
        hdr.virtual_height = virtual_height;
        hdr.command_count  = (int32_t)cmd_data.size();

        size_t hdr_bytes  = sizeof(VkSurfaceCmdHeader);
        size_t cmds_bytes = cmd_data.empty()
            ? sizeof(VkSurfaceKernelCommand)
            : sizeof(VkSurfaceKernelCommand) * cmd_data.size();
        size_t cmd_data_size = hdr_bytes + cmds_bytes;

        pixels->assign(pixel_bytes, 0);

        uint32_t mem_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VkBuffer      px_buf = 0; VkDeviceMemory px_mem = 0;
        VkBuffer      cd_buf = 0; VkDeviceMemory cd_mem = 0;

        if(!allocBuffer(pixel_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mem_flags, &px_buf, &px_mem)) {
            if(status) *status = "Failed to allocate Vulkan pixel buffer";
            return false;
        }
        if(!allocBuffer(cmd_data_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mem_flags, &cd_buf, &cd_mem)) {
            vkDestroyBuffer(device, px_buf, 0);
            vkFreeMemory(device, px_mem, 0);
            if(status) *status = "Failed to allocate Vulkan command buffer";
            return false;
        }

        // Upload header + command data
        {
            void* mapped = 0;
            if(vkMapMemory(device, cd_mem, 0, cmd_data_size, 0, &mapped) == VK_SUCCESS) {
                memcpy(mapped, &hdr, hdr_bytes);
                if(cmd_data.empty()) {
                    memset((char*)mapped + hdr_bytes, 0, sizeof(VkSurfaceKernelCommand));
                } else {
                    memcpy((char*)mapped + hdr_bytes, &cmd_data[0], cmds_bytes);
                }
                vkUnmapMemory(device, cd_mem);
            }
        }

        // Reset and allocate descriptor set
        vkResetDescriptorPool(device, desc_pool, 0);
        VkDescriptorSet ds = 0;
        {
            VkDescriptorSetAllocateInfo dsai;
            memset(&dsai, 0, sizeof(dsai));
            dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dsai.descriptorPool     = desc_pool;
            dsai.descriptorSetCount = 1;
            dsai.pSetLayouts        = &ds_layout;
            if(vkAllocateDescriptorSets(device, &dsai, &ds) != VK_SUCCESS) {
                vkDestroyBuffer(device, cd_buf, 0); vkFreeMemory(device, cd_mem, 0);
                vkDestroyBuffer(device, px_buf, 0); vkFreeMemory(device, px_mem, 0);
                if(status) *status = "Failed to allocate Vulkan descriptor set";
                return false;
            }
        }

        // Update descriptor set
        VkDescriptorBufferInfo px_bi; memset(&px_bi, 0, sizeof(px_bi));
        px_bi.buffer = px_buf; px_bi.range = pixel_bytes;
        VkDescriptorBufferInfo cd_bi; memset(&cd_bi, 0, sizeof(cd_bi));
        cd_bi.buffer = cd_buf; cd_bi.range = cmd_data_size;

        VkWriteDescriptorSet writes[2];
        memset(writes, 0, sizeof(writes));
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = ds;
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].pBufferInfo     = &px_bi;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = ds;
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo     = &cd_bi;
        vkUpdateDescriptorSets(device, 2, writes, 0, 0);

        // Record and submit command buffer
        VkCommandBufferBeginInfo cbbi;
        memset(&cbbi, 0, sizeof(cbbi));
        cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult rc = vkBeginCommandBuffer(cmd_buf, &cbbi);
        if(rc == VK_SUCCESS) {
            vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    pipe_layout, 0, 1, &ds, 0, 0);

            uint32_t gx = ((uint32_t)width  + 15u) / 16u;
            uint32_t gy = ((uint32_t)height + 15u) / 16u;
            vkCmdDispatch(cmd_buf, gx, gy, 1);

            rc = vkEndCommandBuffer(cmd_buf);
        }
        if(rc == VK_SUCCESS) {
            VkSubmitInfo si;
            memset(&si, 0, sizeof(si));
            si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers    = &cmd_buf;
            rc = vkQueueSubmit(compute_queue, 1, &si, 0);
        }
        if(rc == VK_SUCCESS) {
            rc = vkQueueWaitIdle(compute_queue);
        }

        bool ok = (rc == VK_SUCCESS);

        if(ok) {
            void* mapped = 0;
            if(vkMapMemory(device, px_mem, 0, pixel_bytes, 0, &mapped) == VK_SUCCESS) {
                memcpy(&(*pixels)[0], mapped, pixel_bytes);
                vkUnmapMemory(device, px_mem);
            } else {
                ok = false;
            }
        }

        vkDestroyBuffer(device, cd_buf, 0); vkFreeMemory(device, cd_mem, 0);
        vkDestroyBuffer(device, px_buf, 0); vkFreeMemory(device, px_mem, 0);

        if(ok) {
            if(!pixels->empty() && width > 0 && height > 0) {
                int sample_x = width / 2;
                int top_y = height / 4;
                int bottom_y = (height * 3) / 4;
                size_t top_idx = ((size_t)top_y * (size_t)width + (size_t)sample_x) * 4u;
                size_t bottom_idx = ((size_t)bottom_y * (size_t)width + (size_t)sample_x) * 4u;
                if(bottom_idx + 3u < pixels->size() && top_idx + 3u < pixels->size()) {
                    if(status) {
                        *status = String("Vulkan ")
                            + String(width) + String("x") + String(height)
                            + String(" top=")
                            + String((int)(*pixels)[top_idx + 0u]) + String(",")
                            + String((int)(*pixels)[top_idx + 1u]) + String(",")
                            + String((int)(*pixels)[top_idx + 2u])
                            + String(" bottom=")
                            + String((int)(*pixels)[bottom_idx + 0u]) + String(",")
                            + String((int)(*pixels)[bottom_idx + 1u]) + String(",")
                            + String((int)(*pixels)[bottom_idx + 2u]);
                    }
                }
            }
            else if(status) {
                *status = String("Vulkan active: ") + String(width) + "x" + String(height);
            }
        } else {
            if(status) *status = "Vulkan kernel execution failed";
        }
        return ok;
    }
};

// -------------------------------------------------------------------

ElaraVulkanSurfaceCommand::ElaraVulkanSurfaceCommand(Type command_type)
    : type(command_type),
      x0(0), y0(0), x1(0), y1(0),
      value0(0), value1(0),
      r(1), g(1), b(1),
      text("") {}

ElaraVulkanSurfaceWidget::ElaraVulkanSurfaceWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraCanvasWidget(root_widget, widget_handle),
    backend_id("vulkan"),
    kernel_name(""),
    overlay_text(""),
    virtual_width(1000.0),
    virtual_height(1000.0),
    execution_status("Vulkan pending"),
    logged_execution_status(""),
    command_revision(0),
    drawn_revision(0),
    vulkan_runtime(0) {
    setPaletteMaster("panel");
}

ElaraVulkanSurfaceWidget::~ElaraVulkanSurfaceWidget() {
    if(vulkan_runtime) {
        delete vulkan_runtime;
        vulkan_runtime = 0;
    }
}

double ElaraVulkanSurfaceWidget::scaleX(double value) const {
    if(virtual_width <= 0) return value;
    return (value / virtual_width) * (double)width;
}

double ElaraVulkanSurfaceWidget::scaleY(double value) const {
    if(virtual_height <= 0) return value;
    return (value / virtual_height) * (double)height;
}

void ElaraVulkanSurfaceWidget::clearCommands() {
    Mutex::Lock lock(commands_mutex);
    commands.clear();
    command_revision++;
}

void ElaraVulkanSurfaceWidget::addClear(double red, double green, double blue) {
    Mutex::Lock lock(commands_mutex);
    Ref<ElaraVulkanSurfaceCommand> cmd(new ElaraVulkanSurfaceCommand(ElaraVulkanSurfaceCommand::CLEAR));
    cmd->r = red; cmd->g = green; cmd->b = blue;
    commands.push(cmd);
    command_revision++;
}

void ElaraVulkanSurfaceWidget::addRect(double x, double y, double w, double h, double red, double green, double blue) {
    Mutex::Lock lock(commands_mutex);
    Ref<ElaraVulkanSurfaceCommand> cmd(new ElaraVulkanSurfaceCommand(ElaraVulkanSurfaceCommand::RECT));
    cmd->x0 = x; cmd->y0 = y; cmd->x1 = w; cmd->y1 = h;
    cmd->r = red; cmd->g = green; cmd->b = blue;
    commands.push(cmd);
    command_revision++;
}

void ElaraVulkanSurfaceWidget::addLine(double x0, double y0, double x1, double y1, double red, double green, double blue) {
    Mutex::Lock lock(commands_mutex);
    Ref<ElaraVulkanSurfaceCommand> cmd(new ElaraVulkanSurfaceCommand(ElaraVulkanSurfaceCommand::LINE));
    cmd->x0 = x0; cmd->y0 = y0; cmd->x1 = x1; cmd->y1 = y1;
    cmd->r = red; cmd->g = green; cmd->b = blue;
    commands.push(cmd);
    command_revision++;
}

void ElaraVulkanSurfaceWidget::addText(double x, double y, const String& value, double size, double red, double green, double blue) {
    Mutex::Lock lock(commands_mutex);
    Ref<ElaraVulkanSurfaceCommand> cmd(new ElaraVulkanSurfaceCommand(ElaraVulkanSurfaceCommand::TEXT));
    cmd->x0 = x; cmd->y0 = y; cmd->value0 = size;
    cmd->text = value;
    cmd->r = red; cmd->g = green; cmd->b = blue;
    commands.push(cmd);
    command_revision++;
}

void ElaraVulkanSurfaceWidget::addSceneCommand(int scene_op, double a0, double a1, double a2, double a3, double a4, double a5, double a6) {
    Mutex::Lock lock(commands_mutex);
    Ref<ElaraVulkanSurfaceCommand> cmd(new ElaraVulkanSurfaceCommand((ElaraVulkanSurfaceCommand::Type)scene_op));
    cmd->x0 = a0;
    cmd->y0 = a1;
    cmd->x1 = a2;
    cmd->y1 = a3;
    cmd->value0 = a4;
    cmd->value1 = a5;
    cmd->r = a6;
    cmd->g = 0.0;
    cmd->b = 0.0;
    commands.push(cmd);
    command_revision++;
}

void ElaraVulkanSurfaceWidget::setBackendId(const String& value) { backend_id = value; }
String ElaraVulkanSurfaceWidget::getBackendId() const            { return backend_id; }

void ElaraVulkanSurfaceWidget::setKernelName(const String& value) { kernel_name = value; }
String ElaraVulkanSurfaceWidget::getKernelName() const            { return kernel_name; }

void ElaraVulkanSurfaceWidget::setOverlayText(const String& value) { overlay_text = value; }
String ElaraVulkanSurfaceWidget::getOverlayText() const            { return overlay_text; }

void ElaraVulkanSurfaceWidget::setVirtualSize(double width_value, double height_value) {
    virtual_width  = width_value  > 0 ? width_value  : 1000.0;
    virtual_height = height_value > 0 ? height_value : 1000.0;
}

double ElaraVulkanSurfaceWidget::getVirtualWidth()  const { return virtual_width; }
double ElaraVulkanSurfaceWidget::getVirtualHeight() const { return virtual_height; }

void ElaraVulkanSurfaceWidget::drawEmptyState(ElaraDrawContext* ctx) {
    ctx->setColor(0.15, 0.17, 0.20);
    ctx->fillRect(0, 0, width, height);

    ctx->setColor(0.25, 0.28, 0.32);
    for(int x = 0; x < width; x += 24) ctx->line(x, 0, x, height, 1.0);
    for(int y = 0; y < height; y += 24) ctx->line(0, y, width, y, 1.0);

    ctx->setColor(0.31, 0.76, 0.47);
    ctx->drawText(16, 26, "Vulkan Surface", 16);

    ctx->setColor(0.82, 0.84, 0.88);
    ctx->drawText(16, 46, String("backend: ") + backend_id, 12);

    if(kernel_name.length() > 0) {
        ctx->drawText(16, 64, String("kernel: ") + kernel_name, 12);
    }

    if(overlay_text.length() > 0) {
        ctx->drawText(16, 86, overlay_text, 12);
    } else {
        ctx->drawText(16, 86, "Awaiting root-node scene commands", 12);
    }

    if(execution_status.length() > 0) {
        ctx->drawText(16, 106, execution_status, 12);
    }
}

bool ElaraVulkanSurfaceWidget::renderVulkan(int pixel_width, int pixel_height) {
    if(pixel_width <= 0 || pixel_height <= 0) {
        execution_status = "Vulkan skipped: invalid surface size";
        return false;
    }

    if(!vulkan_runtime) {
        vulkan_runtime = new VulkanRuntime();
        if(!vulkan_runtime->load(&execution_status))       return false;
        if(!vulkan_runtime->initialize(&execution_status)) return false;
    }

    // Copy commands under lock — RPC thread can clear/add commands concurrently.
    std::vector<VkSurfaceKernelCommand> kernel_cmds;
    {
        VkSceneCameraState scene_camera;
        scene_camera.x = 0.0;
        scene_camera.y = 0.62;
        scene_camera.z = -0.90;
        scene_camera.yaw_deg = 0.0;
        scene_camera.pitch_deg = 0.0;
        scene_camera.roll_deg = 0.0;
        scene_camera.fov_deg = 60.0;
        scene_camera.near_z = 0.08;
        scene_camera.far_z = 12.0;
        VkSceneMaterialState scene_materials[16];
        std::vector<VkSceneInstanceState> scene_instances;
        bool has_scene_commands = false;
        int scene_command_count = 0;
        int wire_line_count = 0;
        for(int i = 0; i < 16; i++) {
            scene_materials[i].r = 1.0;
            scene_materials[i].g = 0.52;
            scene_materials[i].b = 0.08;
        }

        Mutex::Lock lock(commands_mutex);
        for(int i = 0; i < (int)commands.length(); i++) {
            Ref<ElaraVulkanSurfaceCommand> cmd = commands[i];
            if(!cmd || cmd->type == ElaraVulkanSurfaceCommand::TEXT) continue;

            if((int)cmd->type >= 10) {
                has_scene_commands = true;
                scene_command_count++;
                if(cmd->type == ElaraVulkanSurfaceCommand::SCENE_CAMERA_VIEW) {
                    scene_camera.x = vkSceneMilli(cmd->x0);
                    scene_camera.y = vkSceneMilli(cmd->y0);
                    scene_camera.z = vkSceneMilli(cmd->x1);
                    scene_camera.yaw_deg = vkSceneMilliDeg(cmd->y1);
                    scene_camera.pitch_deg = vkSceneMilliDeg(cmd->value0);
                    scene_camera.roll_deg = vkSceneMilliDeg(cmd->value1);
                    scene_camera.fov_deg = vkSceneMilliDeg(cmd->r);
                } else if(cmd->type == ElaraVulkanSurfaceCommand::SCENE_CAMERA_CLIP) {
                    scene_camera.near_z = vkSceneMilli(cmd->x0);
                    scene_camera.far_z = vkSceneMilli(cmd->y0);
                    if(scene_camera.near_z < 0.01) scene_camera.near_z = 0.01;
                    if(scene_camera.far_z <= scene_camera.near_z) scene_camera.far_z = scene_camera.near_z + 10.0;
                } else if(cmd->type == ElaraVulkanSurfaceCommand::SCENE_ENVIRONMENT) {
                    VkSurfaceKernelCommand kc;
                    kc.op = 0u;
                    kc.x0 = kc.y0 = kc.x1 = kc.y1 = kc.value0 = kc.value1 = 0.0f;
                    kc.r = (float)(((double)vkSceneClampInt((int)cmd->x0, 0, 255)) / 255.0);
                    kc.g = (float)(((double)vkSceneClampInt((int)cmd->y0, 0, 255)) / 255.0);
                    kc.b = (float)(((double)vkSceneClampInt((int)cmd->x1, 0, 255)) / 255.0);
                    kernel_cmds.push_back(kc);
                } else if(cmd->type == ElaraVulkanSurfaceCommand::SCENE_MATERIAL_PBR) {
                    int mat = vkSceneClampInt((int)cmd->x0, 0, 15);
                    scene_materials[mat].r = ((double)vkSceneClampInt((int)cmd->y0, 0, 255)) / 255.0;
                    scene_materials[mat].g = ((double)vkSceneClampInt((int)cmd->x1, 0, 255)) / 255.0;
                    scene_materials[mat].b = ((double)vkSceneClampInt((int)cmd->y1, 0, 255)) / 255.0;
                } else if(cmd->type == ElaraVulkanSurfaceCommand::SCENE_INSTANCE) {
                    VkSceneInstanceState inst;
                    inst.id = (int)cmd->x0;
                    inst.mesh_id = (int)cmd->y0;
                    inst.material_id = vkSceneClampInt((int)cmd->x1, 0, 15);
                    inst.x = vkSceneMilli(cmd->y1);
                    inst.y = vkSceneMilli(cmd->value0);
                    inst.z = vkSceneMilli(cmd->value1);
                    inst.yaw_deg = 0.0;
                    inst.pitch_deg = 0.0;
                    inst.roll_deg = 0.0;
                    inst.sx = 0.35;
                    inst.sy = 0.35;
                    inst.sz = 0.35;
                    inst.r = scene_materials[inst.material_id].r;
                    inst.g = scene_materials[inst.material_id].g;
                    inst.b = scene_materials[inst.material_id].b;
                    scene_instances.push_back(inst);
                } else if(cmd->type == ElaraVulkanSurfaceCommand::SCENE_INSTANCE_XFORM) {
                    for(size_t j = 0; j < scene_instances.size(); j++) {
                        if(scene_instances[j].id == (int)cmd->x0) {
                            scene_instances[j].yaw_deg = vkSceneMilliDeg(cmd->y0);
                            scene_instances[j].pitch_deg = vkSceneMilliDeg(cmd->x1);
                            scene_instances[j].roll_deg = vkSceneMilliDeg(cmd->y1);
                            scene_instances[j].sx = vkSceneMilli(cmd->value0);
                            scene_instances[j].sy = vkSceneMilli(cmd->value1);
                            scene_instances[j].sz = vkSceneMilli(cmd->r);
                        }
                    }
                }
                continue;
            }

            VkSurfaceKernelCommand kc;
            kc.op     = (uint32_t)cmd->type;
            kc.x0     = (float)cmd->x0;
            kc.y0     = (float)cmd->y0;
            kc.x1     = (float)cmd->x1;
            kc.y1     = (float)cmd->y1;
            kc.value0 = (float)cmd->value0;
            kc.value1 = (float)cmd->value1;
            kc.r      = (float)cmd->r;
            kc.g      = (float)cmd->g;
            kc.b      = (float)cmd->b;
            kernel_cmds.push_back(kc);
        }
        if(has_scene_commands) {
            for(size_t i = 0; i < scene_instances.size(); i++) {
                size_t before = kernel_cmds.size();
                vkAppendSceneInstanceWireframe(kernel_cmds, scene_camera, scene_instances[i], pixel_width, pixel_height);
                wire_line_count += (int)(kernel_cmds.size() - before);
            }
        }

        if(has_scene_commands) {
            execution_status = String("scene cmds=") + String(scene_command_count)
                + String(" instances=") + String((int)scene_instances.size())
                + String(" wire_lines=") + String(wire_line_count)
                + String(" gpu_cmds=") + String((int)kernel_cmds.size());
        }
    }

    String render_status;
    bool rendered = vulkan_runtime->render(
        kernel_cmds,
        pixel_width, pixel_height,
        (float)virtual_width, (float)virtual_height,
        &pixel_buffer,
        &render_status
    );
    if(rendered && render_status.length() && execution_status.indexOf("scene cmds=") < 0) {
        execution_status = render_status;
    } else if(!rendered && render_status.length()) {
        execution_status = render_status;
    }
    return rendered;
}

void ElaraVulkanSurfaceWidget::drawCpuCommands(ElaraDrawContext* ctx) {
    execution_status = "CPU surface fallback active";

    Mutex::Lock lock(commands_mutex);
    for(int i = 0; i < (int)commands.length(); i++) {
        Ref<ElaraVulkanSurfaceCommand> cmd = commands[i];
        if(!cmd || cmd->type == ElaraVulkanSurfaceCommand::TEXT) {
            continue;
        }

        ctx->setColor(cmd->r, cmd->g, cmd->b);

        if(cmd->type == ElaraVulkanSurfaceCommand::CLEAR) {
            ctx->fillRect(0, 0, width, height);
        } else if(cmd->type == ElaraVulkanSurfaceCommand::RECT) {
            ctx->fillRect(
                scaleX(cmd->x0),
                scaleY(cmd->y0),
                scaleX(cmd->x1),
                scaleY(cmd->y1)
            );
        } else if(cmd->type == ElaraVulkanSurfaceCommand::LINE) {
            double line_width = cmd->value0 > 0.0 ? cmd->value0 : 1.0;
            ctx->line(
                scaleX(cmd->x0),
                scaleY(cmd->y0),
                scaleX(cmd->x1),
                scaleY(cmd->y1),
                line_width
            );
        }
    }
}

void ElaraVulkanSurfaceWidget::drawCanvas(ElaraDrawContext* ctx) {
    unsigned long current_revision = 0;
    int current_count = 0;
    bool revision_changed = false;
    {
        Mutex::Lock lock(commands_mutex);
        current_revision = command_revision;
        current_count = (int)commands.length();
        if(commands.length() <= 0) {
            drawEmptyState(ctx);
            return;
        }
    }

    if(width <= 0 || height <= 0) {
        execution_status = "Vulkan skipped: invalid widget bounds";
        pixel_buffer.clear();
    } else if(renderVulkan((int)width, (int)height) && !pixel_buffer.empty()) {
        ctx->drawBitmapRgba(0, 0, (int)width, (int)height, &pixel_buffer[0], (int)width * 4);
    } else {
        pixel_buffer.clear();
    }
    if(execution_status.length() > 0 && execution_status != logged_execution_status) {
        printf("[vulkan-surface] status: %s\n", execution_status.operator char *());
        fflush(stdout);
        logged_execution_status = execution_status;
    }

    Mutex::Lock lock(commands_mutex);
    if(commands.length() <= 0) {
        drawEmptyState(ctx);
        return;
    }
    revision_changed = (drawn_revision != command_revision);
    drawn_revision = command_revision;

    for(int i = 0; i < (int)commands.length(); i++) {
        Ref<ElaraVulkanSurfaceCommand> cmd = commands[i];
        if(!cmd || cmd->type != ElaraVulkanSurfaceCommand::TEXT) continue;
        ctx->setColor(cmd->r, cmd->g, cmd->b);
        ctx->drawText(scaleX(cmd->x0), scaleY(cmd->y0), cmd->text, scaleY(cmd->value0));
    }

    ctx->setColor(0.95, 0.96, 0.98);
    if(overlay_text.length() > 0) {
        ctx->drawText(14, 22, overlay_text, 12);
    }
    if(execution_status.length() > 0) {
        ctx->drawText(14, 40, execution_status, 12);
    }
    ctx->drawText(14, 58, String("cmd_rev=") + String((int)current_revision) + String(" draw_rev=") + String((int)drawn_revision) + String(" cmds=") + String(current_count), 12);
}

void ElaraVulkanSurfaceWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);
    if(button == 1) {
        ElaraWidget* node = this;
        while(node && node->getParent()) {
            node = node->getParent();
        }
        ElaraRootWidget* root_widget = dynamic_cast<ElaraRootWidget*>(node);
        if(root_widget) {
            root_widget->setFocus(getHandle());
        }
    }
}

void ElaraVulkanSurfaceWidget::onMouseMove(double px, double py) {
    emitMouseMove(px, py);
}

void ElaraVulkanSurfaceWidget::onMouseUp(int button, double px, double py) {
    emitMouseUp(button, px, py);
}

void ElaraVulkanSurfaceWidget::onKeyDown(unsigned int keyval) {
    emitKeyDown(keyval);
}

void ElaraVulkanSurfaceWidget::onKeyDown(unsigned int keyval, unsigned int modifiers) {
    (void)modifiers;
    onKeyDown(keyval);
}

void ElaraVulkanSurfaceWidget::onKeyUp(unsigned int keyval) {
    emitKeyUp(keyval);
}

void ElaraVulkanSurfaceWidget::onKeyUp(unsigned int keyval, unsigned int modifiers) {
    (void)modifiers;
    onKeyUp(keyval);
}

} // namespace elara
