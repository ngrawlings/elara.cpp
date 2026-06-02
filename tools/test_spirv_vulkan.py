#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ctypes
import math
import os
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

try:
    from PIL import ImageTk
    import tkinter as tk
except Exception:
    ImageTk = None
    tk = None


VK_SUCCESS = 0
VK_STRUCTURE_TYPE_APPLICATION_INFO = 0
VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1
VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2
VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3
VK_STRUCTURE_TYPE_SUBMIT_INFO = 4
VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5
VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16
VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO = 18
VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO = 29
VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO = 30
VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO = 33
VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO = 34
VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET = 35
VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 37
VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 39
VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 40
VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 42
VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12

VK_QUEUE_COMPUTE_BIT = 0x00000002
VK_BUFFER_USAGE_STORAGE_BUFFER_BIT = 0x00000020
VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 0x00000002
VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 0x00000004
VK_DESCRIPTOR_TYPE_STORAGE_BUFFER = 7
VK_COMMAND_BUFFER_LEVEL_PRIMARY = 0
VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT = 0x00000001
VK_PIPELINE_BIND_POINT_COMPUTE = 1
VK_SHADER_STAGE_COMPUTE_BIT = 0x00000020


def c_handle(name: str):
    return type(name, (ctypes.c_void_p,), {})


VkInstance = c_handle("VkInstance")
VkPhysicalDevice = c_handle("VkPhysicalDevice")
VkDevice = c_handle("VkDevice")
VkQueue = c_handle("VkQueue")
VkCommandPool = c_handle("VkCommandPool")
VkCommandBuffer = c_handle("VkCommandBuffer")
VkDescriptorSetLayout = c_handle("VkDescriptorSetLayout")
VkPipelineLayout = c_handle("VkPipelineLayout")
VkShaderModule = c_handle("VkShaderModule")
VkPipeline = c_handle("VkPipeline")
VkPipelineCache = c_handle("VkPipelineCache")
VkDescriptorPool = c_handle("VkDescriptorPool")
VkDescriptorSet = c_handle("VkDescriptorSet")
VkBuffer = c_handle("VkBuffer")
VkDeviceMemory = c_handle("VkDeviceMemory")


class VkApplicationInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("pApplicationName", ctypes.c_char_p),
        ("applicationVersion", ctypes.c_uint32),
        ("pEngineName", ctypes.c_char_p),
        ("engineVersion", ctypes.c_uint32),
        ("apiVersion", ctypes.c_uint32),
    ]


class VkInstanceCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("pApplicationInfo", ctypes.POINTER(VkApplicationInfo)),
        ("enabledLayerCount", ctypes.c_uint32),
        ("ppEnabledLayerNames", ctypes.c_void_p),
        ("enabledExtensionCount", ctypes.c_uint32),
        ("ppEnabledExtensionNames", ctypes.c_void_p),
    ]


class VkQueueFamilyProperties(ctypes.Structure):
    _fields_ = [
        ("queueFlags", ctypes.c_uint32),
        ("queueCount", ctypes.c_uint32),
        ("timestampValidBits", ctypes.c_uint32),
        ("minImageTransferGranularity", ctypes.c_uint32 * 3),
    ]


class VkDeviceQueueCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("queueFamilyIndex", ctypes.c_uint32),
        ("queueCount", ctypes.c_uint32),
        ("pQueuePriorities", ctypes.POINTER(ctypes.c_float)),
    ]


class VkDeviceCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("queueCreateInfoCount", ctypes.c_uint32),
        ("pQueueCreateInfos", ctypes.POINTER(VkDeviceQueueCreateInfo)),
        ("enabledLayerCount", ctypes.c_uint32),
        ("ppEnabledLayerNames", ctypes.c_void_p),
        ("enabledExtensionCount", ctypes.c_uint32),
        ("ppEnabledExtensionNames", ctypes.c_void_p),
        ("pEnabledFeatures", ctypes.c_void_p),
    ]


class VkCommandPoolCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("queueFamilyIndex", ctypes.c_uint32),
    ]


class VkCommandBufferAllocateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("commandPool", VkCommandPool),
        ("level", ctypes.c_uint32),
        ("commandBufferCount", ctypes.c_uint32),
    ]


class VkDescriptorSetLayoutBinding(ctypes.Structure):
    _fields_ = [
        ("binding", ctypes.c_uint32),
        ("descriptorType", ctypes.c_uint32),
        ("descriptorCount", ctypes.c_uint32),
        ("stageFlags", ctypes.c_uint32),
        ("pImmutableSamplers", ctypes.c_void_p),
    ]


class VkDescriptorSetLayoutCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("bindingCount", ctypes.c_uint32),
        ("pBindings", ctypes.POINTER(VkDescriptorSetLayoutBinding)),
    ]


class VkPipelineLayoutCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("setLayoutCount", ctypes.c_uint32),
        ("pSetLayouts", ctypes.POINTER(VkDescriptorSetLayout)),
        ("pushConstantRangeCount", ctypes.c_uint32),
        ("pPushConstantRanges", ctypes.c_void_p),
    ]


class VkShaderModuleCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("codeSize", ctypes.c_size_t),
        ("pCode", ctypes.POINTER(ctypes.c_uint32)),
    ]


class VkPipelineShaderStageCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("stage", ctypes.c_uint32),
        ("module", VkShaderModule),
        ("pName", ctypes.c_char_p),
        ("pSpecializationInfo", ctypes.c_void_p),
    ]


class VkComputePipelineCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("stage", VkPipelineShaderStageCreateInfo),
        ("layout", VkPipelineLayout),
        ("basePipelineHandle", VkPipeline),
        ("basePipelineIndex", ctypes.c_int32),
    ]


class VkDescriptorPoolSize(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_uint32),
        ("descriptorCount", ctypes.c_uint32),
    ]


class VkDescriptorPoolCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("maxSets", ctypes.c_uint32),
        ("poolSizeCount", ctypes.c_uint32),
        ("pPoolSizes", ctypes.POINTER(VkDescriptorPoolSize)),
    ]


class VkDescriptorSetAllocateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("descriptorPool", VkDescriptorPool),
        ("descriptorSetCount", ctypes.c_uint32),
        ("pSetLayouts", ctypes.POINTER(VkDescriptorSetLayout)),
    ]


class VkBufferCreateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("size", ctypes.c_uint64),
        ("usage", ctypes.c_uint32),
        ("sharingMode", ctypes.c_uint32),
        ("queueFamilyIndexCount", ctypes.c_uint32),
        ("pQueueFamilyIndices", ctypes.c_void_p),
    ]


class VkMemoryRequirements(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_uint64),
        ("alignment", ctypes.c_uint64),
        ("memoryTypeBits", ctypes.c_uint32),
    ]


class VkMemoryAllocateInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("allocationSize", ctypes.c_uint64),
        ("memoryTypeIndex", ctypes.c_uint32),
    ]


class VkDescriptorBufferInfo(ctypes.Structure):
    _fields_ = [
        ("buffer", VkBuffer),
        ("offset", ctypes.c_uint64),
        ("range", ctypes.c_uint64),
    ]


class VkWriteDescriptorSet(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("dstSet", VkDescriptorSet),
        ("dstBinding", ctypes.c_uint32),
        ("dstArrayElement", ctypes.c_uint32),
        ("descriptorCount", ctypes.c_uint32),
        ("descriptorType", ctypes.c_uint32),
        ("pImageInfo", ctypes.c_void_p),
        ("pBufferInfo", ctypes.POINTER(VkDescriptorBufferInfo)),
        ("pTexelBufferView", ctypes.c_void_p),
    ]


class VkCommandBufferBeginInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("flags", ctypes.c_uint32),
        ("pInheritanceInfo", ctypes.c_void_p),
    ]


class VkSubmitInfo(ctypes.Structure):
    _fields_ = [
        ("sType", ctypes.c_uint32),
        ("pNext", ctypes.c_void_p),
        ("waitSemaphoreCount", ctypes.c_uint32),
        ("pWaitSemaphores", ctypes.c_void_p),
        ("pWaitDstStageMask", ctypes.c_void_p),
        ("commandBufferCount", ctypes.c_uint32),
        ("pCommandBuffers", ctypes.POINTER(VkCommandBuffer)),
        ("signalSemaphoreCount", ctypes.c_uint32),
        ("pSignalSemaphores", ctypes.c_void_p),
    ]


class VkMemoryType(ctypes.Structure):
    _fields_ = [
        ("propertyFlags", ctypes.c_uint32),
        ("heapIndex", ctypes.c_uint32),
    ]


class VkMemoryHeap(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_uint64),
        ("flags", ctypes.c_uint32),
    ]


class VkPhysicalDeviceMemoryProperties(ctypes.Structure):
    _fields_ = [
        ("memoryTypeCount", ctypes.c_uint32),
        ("memoryTypes", VkMemoryType * 32),
        ("memoryHeapCount", ctypes.c_uint32),
        ("memoryHeaps", VkMemoryHeap * 16),
    ]


@dataclass
class SurfaceCommand:
    op: int
    x0: float
    y0: float
    x1: float
    y1: float
    x2: float
    y2: float
    value0: float
    r: float
    g: float
    b: float


def build_texture_payload(width: int, height: int, rgb_floats: list[tuple[float, float, float]]) -> bytes:
    words = [struct.unpack("<I", struct.pack("<i", width))[0], struct.unpack("<I", struct.pack("<i", height))[0]]
    for r, g, b in rgb_floats:
        words.append(struct.unpack("<I", struct.pack("<f", r))[0])
        words.append(struct.unpack("<I", struct.pack("<f", g))[0])
        words.append(struct.unpack("<I", struct.pack("<f", b))[0])
    return struct.pack("<" + "I" * len(words), *words)


def white_texture_payload() -> bytes:
    return build_texture_payload(1, 1, [(1.0, 1.0, 1.0)])


class VulkanError(RuntimeError):
    pass


class VulkanComputeRenderer:
    def __init__(self, spirv_path: Path, width: int, height: int, virtual_width: float, virtual_height: float):
        self.spirv_path = spirv_path
        self.width = width
        self.height = height
        self.virtual_width = virtual_width
        self.virtual_height = virtual_height
        self.lib = ctypes.CDLL("libvulkan.so.1")
        self.instance = VkInstance()
        self.phys_device = VkPhysicalDevice()
        self.device = VkDevice()
        self.compute_queue = VkQueue()
        self.queue_family = 0
        self.cmd_pool = VkCommandPool()
        self.cmd_buf = VkCommandBuffer()
        self.ds_layout = VkDescriptorSetLayout()
        self.pipe_layout = VkPipelineLayout()
        self.shader_module = VkShaderModule()
        self.pipeline = VkPipeline()
        self.desc_pool = VkDescriptorPool()
        self.mem_props = VkPhysicalDeviceMemoryProperties()
        self._bind()
        self._create()

    def _vk(self, name, restype, *argtypes):
        fn = getattr(self.lib, name)
        fn.restype = restype
        fn.argtypes = argtypes
        return fn

    def _bind(self):
        self.vkCreateInstance = self._vk("vkCreateInstance", ctypes.c_int32, ctypes.POINTER(VkInstanceCreateInfo), ctypes.c_void_p, ctypes.POINTER(VkInstance))
        self.vkDestroyInstance = self._vk("vkDestroyInstance", None, VkInstance, ctypes.c_void_p)
        self.vkEnumeratePhysicalDevices = self._vk("vkEnumeratePhysicalDevices", ctypes.c_int32, VkInstance, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(VkPhysicalDevice))
        self.vkGetPhysicalDeviceMemoryProperties = self._vk("vkGetPhysicalDeviceMemoryProperties", None, VkPhysicalDevice, ctypes.POINTER(VkPhysicalDeviceMemoryProperties))
        self.vkGetPhysicalDeviceQueueFamilyProperties = self._vk("vkGetPhysicalDeviceQueueFamilyProperties", None, VkPhysicalDevice, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(VkQueueFamilyProperties))
        self.vkCreateDevice = self._vk("vkCreateDevice", ctypes.c_int32, VkPhysicalDevice, ctypes.POINTER(VkDeviceCreateInfo), ctypes.c_void_p, ctypes.POINTER(VkDevice))
        self.vkDestroyDevice = self._vk("vkDestroyDevice", None, VkDevice, ctypes.c_void_p)
        self.vkGetDeviceQueue = self._vk("vkGetDeviceQueue", None, VkDevice, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(VkQueue))
        self.vkCreateCommandPool = self._vk("vkCreateCommandPool", ctypes.c_int32, VkDevice, ctypes.POINTER(VkCommandPoolCreateInfo), ctypes.c_void_p, ctypes.POINTER(VkCommandPool))
        self.vkDestroyCommandPool = self._vk("vkDestroyCommandPool", None, VkDevice, VkCommandPool, ctypes.c_void_p)
        self.vkAllocateCommandBuffers = self._vk("vkAllocateCommandBuffers", ctypes.c_int32, VkDevice, ctypes.POINTER(VkCommandBufferAllocateInfo), ctypes.POINTER(VkCommandBuffer))
        self.vkResetCommandBuffer = self._vk("vkResetCommandBuffer", ctypes.c_int32, VkCommandBuffer, ctypes.c_uint32)
        self.vkCreateDescriptorSetLayout = self._vk("vkCreateDescriptorSetLayout", ctypes.c_int32, VkDevice, ctypes.POINTER(VkDescriptorSetLayoutCreateInfo), ctypes.c_void_p, ctypes.POINTER(VkDescriptorSetLayout))
        self.vkDestroyDescriptorSetLayout = self._vk("vkDestroyDescriptorSetLayout", None, VkDevice, VkDescriptorSetLayout, ctypes.c_void_p)
        self.vkCreatePipelineLayout = self._vk("vkCreatePipelineLayout", ctypes.c_int32, VkDevice, ctypes.POINTER(VkPipelineLayoutCreateInfo), ctypes.c_void_p, ctypes.POINTER(VkPipelineLayout))
        self.vkDestroyPipelineLayout = self._vk("vkDestroyPipelineLayout", None, VkDevice, VkPipelineLayout, ctypes.c_void_p)
        self.vkCreateShaderModule = self._vk("vkCreateShaderModule", ctypes.c_int32, VkDevice, ctypes.POINTER(VkShaderModuleCreateInfo), ctypes.c_void_p, ctypes.POINTER(VkShaderModule))
        self.vkDestroyShaderModule = self._vk("vkDestroyShaderModule", None, VkDevice, VkShaderModule, ctypes.c_void_p)
        self.vkCreateComputePipelines = self._vk("vkCreateComputePipelines", ctypes.c_int32, VkDevice, VkPipelineCache, ctypes.c_uint32, ctypes.POINTER(VkComputePipelineCreateInfo), ctypes.c_void_p, ctypes.POINTER(VkPipeline))
        self.vkDestroyPipeline = self._vk("vkDestroyPipeline", None, VkDevice, VkPipeline, ctypes.c_void_p)
        self.vkCreateDescriptorPool = self._vk("vkCreateDescriptorPool", ctypes.c_int32, VkDevice, ctypes.POINTER(VkDescriptorPoolCreateInfo), ctypes.c_void_p, ctypes.POINTER(VkDescriptorPool))
        self.vkDestroyDescriptorPool = self._vk("vkDestroyDescriptorPool", None, VkDevice, VkDescriptorPool, ctypes.c_void_p)
        self.vkAllocateDescriptorSets = self._vk("vkAllocateDescriptorSets", ctypes.c_int32, VkDevice, ctypes.POINTER(VkDescriptorSetAllocateInfo), ctypes.POINTER(VkDescriptorSet))
        self.vkResetDescriptorPool = self._vk("vkResetDescriptorPool", ctypes.c_int32, VkDevice, VkDescriptorPool, ctypes.c_uint32)
        self.vkUpdateDescriptorSets = self._vk("vkUpdateDescriptorSets", None, VkDevice, ctypes.c_uint32, ctypes.POINTER(VkWriteDescriptorSet), ctypes.c_uint32, ctypes.c_void_p)
        self.vkCreateBuffer = self._vk("vkCreateBuffer", ctypes.c_int32, VkDevice, ctypes.POINTER(VkBufferCreateInfo), ctypes.c_void_p, ctypes.POINTER(VkBuffer))
        self.vkDestroyBuffer = self._vk("vkDestroyBuffer", None, VkDevice, VkBuffer, ctypes.c_void_p)
        self.vkGetBufferMemoryRequirements = self._vk("vkGetBufferMemoryRequirements", None, VkDevice, VkBuffer, ctypes.POINTER(VkMemoryRequirements))
        self.vkAllocateMemory = self._vk("vkAllocateMemory", ctypes.c_int32, VkDevice, ctypes.POINTER(VkMemoryAllocateInfo), ctypes.c_void_p, ctypes.POINTER(VkDeviceMemory))
        self.vkFreeMemory = self._vk("vkFreeMemory", None, VkDevice, VkDeviceMemory, ctypes.c_void_p)
        self.vkBindBufferMemory = self._vk("vkBindBufferMemory", ctypes.c_int32, VkDevice, VkBuffer, VkDeviceMemory, ctypes.c_uint64)
        self.vkMapMemory = self._vk("vkMapMemory", ctypes.c_int32, VkDevice, VkDeviceMemory, ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p))
        self.vkUnmapMemory = self._vk("vkUnmapMemory", None, VkDevice, VkDeviceMemory)
        self.vkBeginCommandBuffer = self._vk("vkBeginCommandBuffer", ctypes.c_int32, VkCommandBuffer, ctypes.POINTER(VkCommandBufferBeginInfo))
        self.vkEndCommandBuffer = self._vk("vkEndCommandBuffer", ctypes.c_int32, VkCommandBuffer)
        self.vkCmdBindPipeline = self._vk("vkCmdBindPipeline", None, VkCommandBuffer, ctypes.c_uint32, VkPipeline)
        self.vkCmdBindDescriptorSets = self._vk("vkCmdBindDescriptorSets", None, VkCommandBuffer, ctypes.c_uint32, VkPipelineLayout, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(VkDescriptorSet), ctypes.c_uint32, ctypes.c_void_p)
        self.vkCmdDispatch = self._vk("vkCmdDispatch", None, VkCommandBuffer, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32)
        self.vkQueueSubmit = self._vk("vkQueueSubmit", ctypes.c_int32, VkQueue, ctypes.c_uint32, ctypes.POINTER(VkSubmitInfo), ctypes.c_void_p)
        self.vkQueueWaitIdle = self._vk("vkQueueWaitIdle", ctypes.c_int32, VkQueue)

    def _check(self, result: int, message: str):
        if result != VK_SUCCESS:
            raise VulkanError(f"{message}: VkResult={result}")

    def _find_memory_type(self, type_bits: int, required: int) -> int:
        for i in range(self.mem_props.memoryTypeCount):
            flags = self.mem_props.memoryTypes[i].propertyFlags
            if (type_bits & (1 << i)) and (flags & required) == required:
                return i
        raise VulkanError("No compatible Vulkan memory type")

    def _alloc_buffer(self, size: int):
        buf = VkBuffer()
        mem = VkDeviceMemory()
        bi = VkBufferCreateInfo(
            sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            pNext=None,
            flags=0,
            size=size,
            usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            sharingMode=0,
            queueFamilyIndexCount=0,
            pQueueFamilyIndices=None,
        )
        self._check(self.vkCreateBuffer(self.device, ctypes.byref(bi), None, ctypes.byref(buf)), "vkCreateBuffer failed")
        reqs = VkMemoryRequirements()
        self.vkGetBufferMemoryRequirements(self.device, buf, ctypes.byref(reqs))
        ai = VkMemoryAllocateInfo(
            sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            pNext=None,
            allocationSize=reqs.size,
            memoryTypeIndex=self._find_memory_type(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        )
        self._check(self.vkAllocateMemory(self.device, ctypes.byref(ai), None, ctypes.byref(mem)), "vkAllocateMemory failed")
        self._check(self.vkBindBufferMemory(self.device, buf, mem, 0), "vkBindBufferMemory failed")
        return buf, mem, reqs.size

    def _map_write(self, memory: VkDeviceMemory, payload: bytes):
        mapped = ctypes.c_void_p()
        self._check(self.vkMapMemory(self.device, memory, 0, len(payload), 0, ctypes.byref(mapped)), "vkMapMemory write failed")
        ctypes.memmove(mapped.value, payload, len(payload))
        self.vkUnmapMemory(self.device, memory)

    def _map_read(self, memory: VkDeviceMemory, size: int) -> bytes:
        mapped = ctypes.c_void_p()
        self._check(self.vkMapMemory(self.device, memory, 0, size, 0, ctypes.byref(mapped)), "vkMapMemory read failed")
        data = ctypes.string_at(mapped.value, size)
        self.vkUnmapMemory(self.device, memory)
        return data

    def _create(self):
        app = VkApplicationInfo(
            sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
            pNext=None,
            pApplicationName=b"spirv-proof",
            applicationVersion=0,
            pEngineName=b"spirv-proof",
            engineVersion=0,
            apiVersion=(1 << 22) | (1 << 12),
        )
        ici = VkInstanceCreateInfo(
            sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            pNext=None,
            flags=0,
            pApplicationInfo=ctypes.pointer(app),
            enabledLayerCount=0,
            ppEnabledLayerNames=None,
            enabledExtensionCount=0,
            ppEnabledExtensionNames=None,
        )
        self._check(self.vkCreateInstance(ctypes.byref(ici), None, ctypes.byref(self.instance)), "vkCreateInstance failed")

        dev_count = ctypes.c_uint32()
        self._check(self.vkEnumeratePhysicalDevices(self.instance, ctypes.byref(dev_count), None), "vkEnumeratePhysicalDevices count failed")
        if dev_count.value == 0:
            raise VulkanError("No Vulkan physical devices")
        devs = (VkPhysicalDevice * dev_count.value)()
        self._check(self.vkEnumeratePhysicalDevices(self.instance, ctypes.byref(dev_count), devs), "vkEnumeratePhysicalDevices list failed")

        found = False
        for dev in devs:
            qf_count = ctypes.c_uint32()
            self.vkGetPhysicalDeviceQueueFamilyProperties(dev, ctypes.byref(qf_count), None)
            qfs = (VkQueueFamilyProperties * qf_count.value)()
            self.vkGetPhysicalDeviceQueueFamilyProperties(dev, ctypes.byref(qf_count), qfs)
            for idx in range(qf_count.value):
                if qfs[idx].queueFlags & VK_QUEUE_COMPUTE_BIT:
                    self.phys_device = dev
                    self.queue_family = idx
                    found = True
                    break
            if found:
                break
        if not found:
            raise VulkanError("No Vulkan device with compute support")

        self.vkGetPhysicalDeviceMemoryProperties(self.phys_device, ctypes.byref(self.mem_props))

        priority = ctypes.c_float(1.0)
        qci = VkDeviceQueueCreateInfo(
            sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            pNext=None,
            flags=0,
            queueFamilyIndex=self.queue_family,
            queueCount=1,
            pQueuePriorities=ctypes.pointer(priority),
        )
        dci = VkDeviceCreateInfo(
            sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            pNext=None,
            flags=0,
            queueCreateInfoCount=1,
            pQueueCreateInfos=ctypes.pointer(qci),
            enabledLayerCount=0,
            ppEnabledLayerNames=None,
            enabledExtensionCount=0,
            ppEnabledExtensionNames=None,
            pEnabledFeatures=None,
        )
        self._check(self.vkCreateDevice(self.phys_device, ctypes.byref(dci), None, ctypes.byref(self.device)), "vkCreateDevice failed")
        self.vkGetDeviceQueue(self.device, self.queue_family, 0, ctypes.byref(self.compute_queue))

        cpci = VkCommandPoolCreateInfo(
            sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            pNext=None,
            flags=0x00000002,
            queueFamilyIndex=self.queue_family,
        )
        self._check(self.vkCreateCommandPool(self.device, ctypes.byref(cpci), None, ctypes.byref(self.cmd_pool)), "vkCreateCommandPool failed")

        cbai = VkCommandBufferAllocateInfo(
            sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            pNext=None,
            commandPool=self.cmd_pool,
            level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            commandBufferCount=1,
        )
        self._check(self.vkAllocateCommandBuffers(self.device, ctypes.byref(cbai), ctypes.byref(self.cmd_buf)), "vkAllocateCommandBuffers failed")

        bindings = (VkDescriptorSetLayoutBinding * 3)()
        bindings[0] = VkDescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, None)
        bindings[1] = VkDescriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, None)
        bindings[2] = VkDescriptorSetLayoutBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, None)
        dslci = VkDescriptorSetLayoutCreateInfo(
            sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            pNext=None,
            flags=0,
            bindingCount=3,
            pBindings=bindings,
        )
        self._check(self.vkCreateDescriptorSetLayout(self.device, ctypes.byref(dslci), None, ctypes.byref(self.ds_layout)), "vkCreateDescriptorSetLayout failed")

        plci = VkPipelineLayoutCreateInfo(
            sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            pNext=None,
            flags=0,
            setLayoutCount=1,
            pSetLayouts=ctypes.pointer(self.ds_layout),
            pushConstantRangeCount=0,
            pPushConstantRanges=None,
        )
        self._check(self.vkCreatePipelineLayout(self.device, ctypes.byref(plci), None, ctypes.byref(self.pipe_layout)), "vkCreatePipelineLayout failed")

        words = struct.unpack("<" + "I" * (self.spirv_path.stat().st_size // 4), self.spirv_path.read_bytes())
        word_array = (ctypes.c_uint32 * len(words))(*words)
        smci = VkShaderModuleCreateInfo(
            sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            pNext=None,
            flags=0,
            codeSize=len(words) * 4,
            pCode=word_array,
        )
        self._check(self.vkCreateShaderModule(self.device, ctypes.byref(smci), None, ctypes.byref(self.shader_module)), "vkCreateShaderModule failed")
        self._shader_words_ref = word_array

        stage = VkPipelineShaderStageCreateInfo(
            sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            pNext=None,
            flags=0,
            stage=VK_SHADER_STAGE_COMPUTE_BIT,
            module=self.shader_module,
            pName=b"main",
            pSpecializationInfo=None,
        )
        cpci2 = VkComputePipelineCreateInfo(
            sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            pNext=None,
            flags=0,
            stage=stage,
            layout=self.pipe_layout,
            basePipelineHandle=VkPipeline(),
            basePipelineIndex=0,
        )
        self._check(self.vkCreateComputePipelines(self.device, VkPipelineCache(), 1, ctypes.byref(cpci2), None, ctypes.byref(self.pipeline)), "vkCreateComputePipelines failed")

        pool_size = VkDescriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3)
        dpci = VkDescriptorPoolCreateInfo(
            sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            pNext=None,
            flags=0,
            maxSets=1,
            poolSizeCount=1,
            pPoolSizes=ctypes.pointer(pool_size),
        )
        self._check(self.vkCreateDescriptorPool(self.device, ctypes.byref(dpci), None, ctypes.byref(self.desc_pool)), "vkCreateDescriptorPool failed")

    def render(self, commands: list[SurfaceCommand], texture_payload: bytes | None = None) -> Image.Image:
        pixel_bytes = self.width * self.height * 4
        header = struct.pack("<iiffi", self.width, self.height, float(self.virtual_width), float(self.virtual_height), len(commands))
        cmd_bytes = bytearray()
        for cmd in commands:
            cmd_bytes.extend(struct.pack("<Ifffffffffff", cmd.op, cmd.x0, cmd.y0, cmd.x1, cmd.y1, cmd.x2, cmd.y2, cmd.value0, 0.0, cmd.r, cmd.g, cmd.b))
        cmd_payload = header + bytes(cmd_bytes)
        if not commands:
            cmd_payload += struct.pack("<Ifffffffffff", 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.10, 0.11, 0.14)
        if texture_payload is None:
            texture_payload = white_texture_payload()

        px_buf, px_mem, _ = self._alloc_buffer(pixel_bytes)
        cd_buf, cd_mem, _ = self._alloc_buffer(len(cmd_payload))
        tx_buf, tx_mem, _ = self._alloc_buffer(len(texture_payload))
        try:
            self._map_write(px_mem, b"\x00" * pixel_bytes)
            self._map_write(cd_mem, cmd_payload)
            self._map_write(tx_mem, texture_payload)

            self._check(self.vkResetDescriptorPool(self.device, self.desc_pool, 0), "vkResetDescriptorPool failed")
            dsa = VkDescriptorSetAllocateInfo(
                sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                pNext=None,
                descriptorPool=self.desc_pool,
                descriptorSetCount=1,
                pSetLayouts=ctypes.pointer(self.ds_layout),
            )
            desc_set = VkDescriptorSet()
            self._check(self.vkAllocateDescriptorSets(self.device, ctypes.byref(dsa), ctypes.byref(desc_set)), "vkAllocateDescriptorSets failed")

            px_info = VkDescriptorBufferInfo(px_buf, 0, pixel_bytes)
            cd_info = VkDescriptorBufferInfo(cd_buf, 0, len(cmd_payload))
            tx_info = VkDescriptorBufferInfo(tx_buf, 0, len(texture_payload))
            writes = (VkWriteDescriptorSet * 3)()
            writes[0] = VkWriteDescriptorSet(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, None, desc_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, None, ctypes.pointer(px_info), None)
            writes[1] = VkWriteDescriptorSet(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, None, desc_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, None, ctypes.pointer(cd_info), None)
            writes[2] = VkWriteDescriptorSet(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, None, desc_set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, None, ctypes.pointer(tx_info), None)
            self.vkUpdateDescriptorSets(self.device, 3, writes, 0, None)

            self._check(self.vkResetCommandBuffer(self.cmd_buf, 0), "vkResetCommandBuffer failed")
            begin = VkCommandBufferBeginInfo(
                sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                pNext=None,
                flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                pInheritanceInfo=None,
            )
            self._check(self.vkBeginCommandBuffer(self.cmd_buf, ctypes.byref(begin)), "vkBeginCommandBuffer failed")
            self.vkCmdBindPipeline(self.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, self.pipeline)
            self.vkCmdBindDescriptorSets(self.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, self.pipe_layout, 0, 1, ctypes.pointer(desc_set), 0, None)
            groups_x = (self.width + 15) // 16
            groups_y = (self.height + 15) // 16
            self.vkCmdDispatch(self.cmd_buf, groups_x, groups_y, 1)
            self._check(self.vkEndCommandBuffer(self.cmd_buf), "vkEndCommandBuffer failed")

            submit = VkSubmitInfo(
                sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
                pNext=None,
                waitSemaphoreCount=0,
                pWaitSemaphores=None,
                pWaitDstStageMask=None,
                commandBufferCount=1,
                pCommandBuffers=ctypes.pointer(self.cmd_buf),
                signalSemaphoreCount=0,
                pSignalSemaphores=None,
            )
            self._check(self.vkQueueSubmit(self.compute_queue, 1, ctypes.byref(submit), None), "vkQueueSubmit failed")
            self._check(self.vkQueueWaitIdle(self.compute_queue), "vkQueueWaitIdle failed")

            raw = self._map_read(px_mem, pixel_bytes)
        finally:
            self.vkFreeMemory(self.device, tx_mem, None)
            self.vkDestroyBuffer(self.device, tx_buf, None)
            self.vkFreeMemory(self.device, cd_mem, None)
            self.vkDestroyBuffer(self.device, cd_buf, None)
            self.vkFreeMemory(self.device, px_mem, None)
            self.vkDestroyBuffer(self.device, px_buf, None)

        image = Image.frombytes("RGBA", (self.width, self.height), raw, "raw", "RGBA")
        return image

    def close(self):
        if self.device:
            if self.desc_pool:
                self.vkDestroyDescriptorPool(self.device, self.desc_pool, None)
                self.desc_pool = VkDescriptorPool()
            if self.pipeline:
                self.vkDestroyPipeline(self.device, self.pipeline, None)
                self.pipeline = VkPipeline()
            if self.shader_module:
                self.vkDestroyShaderModule(self.device, self.shader_module, None)
                self.shader_module = VkShaderModule()
            if self.pipe_layout:
                self.vkDestroyPipelineLayout(self.device, self.pipe_layout, None)
                self.pipe_layout = VkPipelineLayout()
            if self.ds_layout:
                self.vkDestroyDescriptorSetLayout(self.device, self.ds_layout, None)
                self.ds_layout = VkDescriptorSetLayout()
            if self.cmd_pool:
                self.vkDestroyCommandPool(self.device, self.cmd_pool, None)
                self.cmd_pool = VkCommandPool()
            self.vkDestroyDevice(self.device, None)
            self.device = VkDevice()
        if self.instance:
            self.vkDestroyInstance(self.instance, None)
            self.instance = VkInstance()


def _normalize3(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length <= 1.0e-9:
        return (0.0, 0.0, 0.0)
    return (v[0] / length, v[1] / length, v[2] / length)


def _cross3(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def _dot3(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def project_point(cam_pos, target_pos, point, width, height, fov_deg=60.0, near_z=0.08, far_z=12.0):
    forward = _normalize3((
        target_pos[0] - cam_pos[0],
        target_pos[1] - cam_pos[1],
        target_pos[2] - cam_pos[2],
    ))
    world_up = (0.0, 1.0, 0.0)
    right = _normalize3(_cross3(world_up, forward))
    up = _cross3(forward, right)

    rel = (
        point[0] - cam_pos[0],
        point[1] - cam_pos[1],
        point[2] - cam_pos[2],
    )
    vx = _dot3(rel, right)
    vy = _dot3(rel, up)
    vz = _dot3(rel, forward)
    if vz <= near_z or vz >= far_z:
        return None
    focal = (height * 0.5) / math.tan(math.radians(fov_deg) * 0.5)
    sx = width * 0.5 + (vx / vz) * focal
    sy2 = height * 0.5 - (vy / vz) * focal
    return sx, sy2


def clear_command(r: float = 0.10, g: float = 0.11, b: float = 0.14) -> SurfaceCommand:
    return SurfaceCommand(0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, r, g, b)


def line_command(x0: float, y0: float, x1: float, y1: float, width: float, r: float, g: float, b: float) -> SurfaceCommand:
    return SurfaceCommand(2, x0, y0, x1, y1, 0.0, 0.0, width, r, g, b)


def triangle_command(x0: float, y0: float, x1: float, y1: float, x2: float, y2: float, depth: float, r: float, g: float, b: float) -> SurfaceCommand:
    return SurfaceCommand(3, x0, y0, x1, y1, x2, y2, depth, r, g, b)


def textured_rect_command(x0: float, y0: float, width: float, height: float) -> SurfaceCommand:
    return SurfaceCommand(4, x0, y0, width, height, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)


def checker_texture_payload() -> bytes:
    width = 8
    height = 8
    texels: list[tuple[float, float, float]] = []
    for y in range(height):
        for x in range(width):
            if ((x // 2) + (y // 2)) % 2 == 0:
                texels.append((0.95, 0.90, 0.24))
            else:
                texels.append((0.18, 0.42, 0.92))
    return build_texture_payload(width, height, texels)


def textured_quad_proof_commands(width: int, height: int) -> list[SurfaceCommand]:
    quad_w = width * 0.54
    quad_h = height * 0.54
    return [
        clear_command(),
        textured_rect_command(width * 0.23, height * 0.20, quad_w, quad_h),
    ]


def triangle_proof_commands(width: int, height: int) -> list[SurfaceCommand]:
    return [
        clear_command(),
        triangle_command(width * 0.50, height * 0.16,
                         width * 0.22, height * 0.80,
                         width * 0.78, height * 0.72,
                         0.80, 0.96, 0.42, 0.18),
    ]


def depth_proof_commands(width: int, height: int) -> list[SurfaceCommand]:
    return [
        clear_command(),
        triangle_command(width * 0.45, height * 0.20,
                         width * 0.18, height * 0.82,
                         width * 0.72, height * 0.68,
                         0.80, 0.96, 0.42, 0.18),
        triangle_command(width * 0.60, height * 0.24,
                         width * 0.30, height * 0.86,
                         width * 0.84, height * 0.76,
                         0.25, 0.16, 0.70, 0.96),
    ]


def indexed_cube_commands_for_camera(width: int, height: int, cam_pos, target, angle: float) -> list[SurfaceCommand]:
    half = 1.0
    verts = [
        (-half, -half, -half), (half, -half, -half), (half, half, -half), (-half, half, -half),
        (-half, -half, half), (half, -half, half), (half, half, half), (-half, half, half),
    ]
    cr = math.cos(angle)
    sr = math.sin(angle)
    transformed = []
    for x, y, z in verts:
        rx = x * cr - z * sr
        rz = x * sr + z * cr
        transformed.append((rx, y, rz))
    projected = [project_point(cam_pos, target, p, width, height) for p in transformed]
    tri_indices = [
        (0, 1, 2), (0, 2, 3),
        (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1),
        (3, 2, 6), (3, 6, 7),
        (1, 5, 6), (1, 6, 2),
        (0, 3, 7), (0, 7, 4),
    ]
    commands = [clear_command()]
    for ia, ib, ic in tri_indices:
        pa = projected[ia]
        pb = projected[ib]
        pc = projected[ic]
        if pa is None or pb is None or pc is None:
            continue
        depth = 1.0 / max(0.001, (
            math.dist(cam_pos, transformed[ia]) +
            math.dist(cam_pos, transformed[ib]) +
            math.dist(cam_pos, transformed[ic])
        ) / 3.0)
        commands.append(triangle_command(
            pa[0], pa[1], pb[0], pb[1], pc[0], pc[1],
            depth, 0.92, 0.56, 0.18
        ))
    return commands


def indexed_cube_proof_commands(width: int, height: int) -> list[SurfaceCommand]:
    return indexed_cube_commands_for_camera(width, height, (3.0, 2.0, -5.8), (0.0, 0.0, 0.0), 0.55)


def append_indexed_cube_instance(commands: list[SurfaceCommand], width: int, height: int, cam_pos, target, base_verts, tri_indices, translate, scale, angle_y, color, cull_backfaces: bool = False):
    cr = math.cos(angle_y)
    sr = math.sin(angle_y)
    transformed = []
    for x, y, z in base_verts:
        sx = x * scale
        sy = y * scale
        sz = z * scale
        rx = sx * cr - sz * sr
        rz = sx * sr + sz * cr
        transformed.append((rx + translate[0], sy + translate[1], rz + translate[2]))
    projected = [project_point(cam_pos, target, p, width, height) for p in transformed]
    for ia, ib, ic in tri_indices:
        if cull_backfaces:
            ax, ay, az = transformed[ia]
            bx, by, bz = transformed[ib]
            cx, cy, cz = transformed[ic]
            ab = (bx - ax, by - ay, bz - az)
            ac = (cx - ax, cy - ay, cz - az)
            nx = ab[1] * ac[2] - ab[2] * ac[1]
            ny = ab[2] * ac[0] - ab[0] * ac[2]
            nz = ab[0] * ac[1] - ab[1] * ac[0]
            center = ((ax + bx + cx) / 3.0, (ay + by + cy) / 3.0, (az + bz + cz) / 3.0)
            view = (cam_pos[0] - center[0], cam_pos[1] - center[1], cam_pos[2] - center[2])
            if nx * view[0] + ny * view[1] + nz * view[2] >= 0.0:
                continue
        pa = projected[ia]
        pb = projected[ib]
        pc = projected[ic]
        if pa is None or pb is None or pc is None:
            continue
        depth = 1.0 / max(0.001, (
            math.dist(cam_pos, transformed[ia]) +
            math.dist(cam_pos, transformed[ib]) +
            math.dist(cam_pos, transformed[ic])
        ) / 3.0)
        commands.append(triangle_command(
            pa[0], pa[1], pb[0], pb[1], pc[0], pc[1],
            depth, color[0], color[1], color[2]
        ))


def instances_proof_commands(width: int, height: int) -> list[SurfaceCommand]:
    cam_pos = (5.8, 3.6, -8.0)
    target = (0.0, 0.5, 0.0)
    half = 1.0
    base_verts = [
        (-half, -half, -half), (half, -half, -half), (half, half, -half), (-half, half, -half),
        (-half, -half, half), (half, -half, half), (half, half, half), (-half, half, half),
    ]
    tri_indices = [
        (0, 1, 2), (0, 2, 3),
        (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1),
        (3, 2, 6), (3, 6, 7),
        (1, 5, 6), (1, 6, 2),
        (0, 3, 7), (0, 7, 4),
    ]
    instances = [
        ((-2.8, 0.0, 0.0), 0.85, 0.20, (0.92, 0.56, 0.18)),
        ((0.0, 0.3, 0.0), 1.10, 0.75, (0.22, 0.76, 0.94)),
        ((2.9, -0.2, 0.2), 0.95, -0.45, (0.40, 0.88, 0.34)),
        ((-1.2, 1.6, 1.2), 0.55, 1.10, (0.96, 0.42, 0.62)),
        ((2.1, 1.2, -0.9), 0.65, -1.00, (0.95, 0.86, 0.28)),
    ]
    commands = [clear_command()]
    for translate, scale, angle_y, color in instances:
        append_indexed_cube_instance(commands, width, height, cam_pos, target, base_verts, tri_indices, translate, scale, angle_y, color)
    return commands


def culling_proof_commands(width: int, height: int) -> list[SurfaceCommand]:
    cam_pos = (3.5, 2.4, -6.0)
    target = (0.0, 0.0, 0.0)
    half = 1.0
    base_verts = [
        (-half, -half, -half), (half, -half, -half), (half, half, -half), (-half, half, -half),
        (-half, -half, half), (half, -half, half), (half, half, half), (-half, half, half),
    ]
    tri_indices = [
        (0, 1, 2), (0, 2, 3),
        (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1),
        (3, 2, 6), (3, 6, 7),
        (1, 5, 6), (1, 6, 2),
        (0, 3, 7), (0, 7, 4),
    ]
    commands = [clear_command()]
    append_indexed_cube_instance(
        commands, width, height, cam_pos, target,
        base_verts, tri_indices,
        (0.0, 0.0, 0.0), 1.25, 0.85,
        (0.92, 0.56, 0.18),
        cull_backfaces=True,
    )
    return commands


def flat_shading_proof_commands(width: int, height: int) -> list[SurfaceCommand]:
    cam_pos = (4.6, 3.2, -5.2)
    target = (0.0, 0.0, 0.0)
    half = 1.0
    base_verts = [
        (-half, -half, -half), (half, -half, -half), (half, half, -half), (-half, half, -half),
        (-half, -half, half), (half, -half, half), (half, half, half), (-half, half, half),
    ]
    tri_indices = [
        (0, 1, 2), (0, 2, 3),
        (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1),
        (3, 2, 6), (3, 6, 7),
        (1, 5, 6), (1, 6, 2),
        (0, 3, 7), (0, 7, 4),
    ]
    angle_y = 0.45
    cr = math.cos(angle_y)
    sr = math.sin(angle_y)
    transformed = []
    for x, y, z in base_verts:
        rx = x * cr - z * sr
        rz = x * sr + z * cr
        transformed.append((rx, y, rz))
    projected = [project_point(cam_pos, target, p, width, height) for p in transformed]
    light_dir = _normalize3((-0.92, 0.35, -0.18))
    base_color = (0.92, 0.92, 0.92)
    commands = [clear_command()]
    for ia, ib, ic in tri_indices:
        ax, ay, az = transformed[ia]
        bx, by, bz = transformed[ib]
        cx, cy, cz = transformed[ic]
        ab = (bx - ax, by - ay, bz - az)
        ac = (cx - ax, cy - ay, cz - az)
        normal = _normalize3((
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        ))
        center = ((ax + bx + cx) / 3.0, (ay + by + cy) / 3.0, (az + bz + cz) / 3.0)
        view = (cam_pos[0] - center[0], cam_pos[1] - center[1], cam_pos[2] - center[2])
        pa = projected[ia]
        pb = projected[ib]
        pc = projected[ic]
        if pa is None or pb is None or pc is None:
            continue
        facing = normal[0] * view[0] + normal[1] * view[1] + normal[2] * view[2]
        if facing >= 0.0:
            continue
        diffuse = max(0.0, normal[0] * light_dir[0] + normal[1] * light_dir[1] + normal[2] * light_dir[2])
        brightness = 0.08 + 0.92 * diffuse
        color = (
            min(1.0, base_color[0] * brightness),
            min(1.0, base_color[1] * brightness),
            min(1.0, base_color[2] * brightness),
        )
        depth = 1.0 / max(0.001, (
            math.dist(cam_pos, transformed[ia]) +
            math.dist(cam_pos, transformed[ib]) +
            math.dist(cam_pos, transformed[ic])
        ) / 3.0)
        commands.append(triangle_command(
            pa[0], pa[1], pb[0], pb[1], pc[0], pc[1],
            depth, color[0], color[1], color[2]
        ))
    return commands


def materials_proof_commands(width: int, height: int) -> list[SurfaceCommand]:
    cam_pos = (6.2, 3.0, -8.5)
    target = (0.0, 0.6, 0.0)
    half = 1.0
    base_verts = [
        (-half, -half, -half), (half, -half, -half), (half, half, -half), (-half, half, -half),
        (-half, -half, half), (half, -half, half), (half, half, half), (-half, half, half),
    ]
    tri_indices = [
        (0, 1, 2), (0, 2, 3),
        (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1),
        (3, 2, 6), (3, 6, 7),
        (1, 5, 6), (1, 6, 2),
        (0, 3, 7), (0, 7, 4),
    ]
    materials = [
        ((-3.2, 0.0, 0.0), 0.95, 0.18, (0.92, 0.26, 0.22)),
        ((0.0, 0.4, 0.0), 1.15, 0.72, (0.26, 0.84, 0.36)),
        ((3.0, -0.1, 0.1), 0.90, -0.38, (0.22, 0.58, 0.94)),
        ((-1.4, 1.9, 1.1), 0.55, 1.05, (0.92, 0.82, 0.26)),
    ]
    light_dir = _normalize3((-0.8, 0.45, -0.25))
    commands = [clear_command()]
    for translate, scale, angle_y, base_color in materials:
        cr = math.cos(angle_y)
        sr = math.sin(angle_y)
        transformed = []
        for x, y, z in base_verts:
            sx = x * scale
            sy = y * scale
            sz = z * scale
            rx = sx * cr - sz * sr
            rz = sx * sr + sz * cr
            transformed.append((rx + translate[0], sy + translate[1], rz + translate[2]))
        projected = [project_point(cam_pos, target, p, width, height) for p in transformed]
        for ia, ib, ic in tri_indices:
            ax, ay, az = transformed[ia]
            bx, by, bz = transformed[ib]
            cx, cy, cz = transformed[ic]
            ab = (bx - ax, by - ay, bz - az)
            ac = (cx - ax, cy - ay, cz - az)
            normal = _normalize3((
                ab[1] * ac[2] - ab[2] * ac[1],
                ab[2] * ac[0] - ab[0] * ac[2],
                ab[0] * ac[1] - ab[1] * ac[0],
            ))
            center = ((ax + bx + cx) / 3.0, (ay + by + cy) / 3.0, (az + bz + cz) / 3.0)
            view = (cam_pos[0] - center[0], cam_pos[1] - center[1], cam_pos[2] - center[2])
            if normal[0] * view[0] + normal[1] * view[1] + normal[2] * view[2] >= 0.0:
                continue
            pa = projected[ia]
            pb = projected[ib]
            pc = projected[ic]
            if pa is None or pb is None or pc is None:
                continue
            diffuse = max(0.0, normal[0] * light_dir[0] + normal[1] * light_dir[1] + normal[2] * light_dir[2])
            brightness = 0.12 + 0.88 * diffuse
            color = (
                min(1.0, base_color[0] * brightness),
                min(1.0, base_color[1] * brightness),
                min(1.0, base_color[2] * brightness),
            )
            depth = 1.0 / max(0.001, (
                math.dist(cam_pos, transformed[ia]) +
                math.dist(cam_pos, transformed[ib]) +
                math.dist(cam_pos, transformed[ic])
            ) / 3.0)
            commands.append(triangle_command(
                pa[0], pa[1], pb[0], pb[1], pc[0], pc[1],
                depth, color[0], color[1], color[2]
            ))
    return commands


def camera_proof_images(renderer: VulkanComputeRenderer, width: int, height: int) -> list[Image.Image]:
    views = [
        ((3.0, 2.0, -5.8), 0.55),
        ((-4.2, 1.4, -4.8), -0.20),
        ((0.0, 4.8, -4.2), 0.85),
    ]
    images = []
    for cam_pos, angle in views:
        cmds = indexed_cube_commands_for_camera(width, height, cam_pos, (0.0, 0.0, 0.0), angle)
        images.append(renderer.render(cmds))
    return images


def cube_commands(frame_idx: int, width: int, height: int) -> list[SurfaceCommand]:
    angle = frame_idx * 0.22
    cam_radius = 3.6
    cam_x = math.cos(angle) * cam_radius
    cam_z = math.sin(angle) * cam_radius - 6.0
    cam_y = 1.4 + math.sin(angle * 0.7) * 0.35
    half = 1.0
    verts = [
        (-half, -half, -half), (half, -half, -half), (half, half, -half), (-half, half, -half),
        (-half, -half, half), (half, -half, half), (half, half, half), (-half, half, half),
    ]
    rot = angle * 1.6
    cr = math.cos(rot)
    sr = math.sin(rot)
    transformed = []
    for x, y, z in verts:
        rx = x * cr - z * sr
        rz = x * sr + z * cr
        transformed.append((rx, y, rz))
    edges = [
        (0, 1), (1, 2), (2, 3), (3, 0),
        (4, 5), (5, 6), (6, 7), (7, 4),
        (0, 4), (1, 5), (2, 6), (3, 7),
    ]
    projected = [project_point((cam_x, cam_y, cam_z), (0.0, 0.0, 0.0), p, width, height) for p in transformed]
    commands: list[SurfaceCommand] = [
        clear_command(),
    ]
    for a, b in edges:
        pa = projected[a]
        pb = projected[b]
        if pa is None or pb is None:
            continue
        commands.append(line_command(pa[0], pa[1], pb[0], pb[1], 2.0, 1.0, 0.58, 0.16))
    return commands


def parse_args():
    parser = argparse.ArgumentParser(description="Pure Python Vulkan SPIR-V proof for Orange Fortress shader")
    parser.add_argument("--spirv", default=str(Path.home() / ".elara" / "spirv.dat"))
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--fps", type=float, default=30.0)
    parser.add_argument("--output-dir", default="")
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--write-final", default="")
    parser.add_argument("--scene", choices=["cube", "triangle", "depth", "indexed-cube", "camera", "instances", "culling", "flat-shading", "materials", "textured-quad"], default="cube")
    return parser.parse_args()


def main():
    args = parse_args()
    spirv_path = Path(args.spirv).expanduser()
    if not spirv_path.is_file():
        raise SystemExit(f"Missing SPIR-V file: {spirv_path}")
    if spirv_path.stat().st_size % 4 != 0:
        raise SystemExit(f"Invalid SPIR-V file size: {spirv_path}")

    renderer = VulkanComputeRenderer(spirv_path, args.width, args.height, float(args.width), float(args.height))
    print(f"Loaded SPIR-V: {spirv_path}", flush=True)
    print(f"SPIR-V bytes: {spirv_path.stat().st_size}", flush=True)

    output_dir = Path(args.output_dir).expanduser() if args.output_dir else None
    if output_dir:
        output_dir.mkdir(parents=True, exist_ok=True)

    if args.scene == "triangle":
        frame_commands = lambda frame_idx: triangle_proof_commands(args.width, args.height)
    elif args.scene == "depth":
        frame_commands = lambda frame_idx: depth_proof_commands(args.width, args.height)
    elif args.scene == "indexed-cube":
        frame_commands = lambda frame_idx: indexed_cube_proof_commands(args.width, args.height)
    elif args.scene == "camera":
        frame_commands = None
    elif args.scene == "instances":
        frame_commands = lambda frame_idx: instances_proof_commands(args.width, args.height)
    elif args.scene == "culling":
        frame_commands = lambda frame_idx: culling_proof_commands(args.width, args.height)
    elif args.scene == "flat-shading":
        frame_commands = lambda frame_idx: flat_shading_proof_commands(args.width, args.height)
    elif args.scene == "materials":
        frame_commands = lambda frame_idx: materials_proof_commands(args.width, args.height)
    elif args.scene == "textured-quad":
        frame_commands = lambda frame_idx: textured_quad_proof_commands(args.width, args.height)
    else:
        frame_commands = lambda frame_idx: cube_commands(frame_idx, args.width, args.height)

    try:
        if args.scene == "camera":
            images = camera_proof_images(renderer, args.width, args.height)
            montage = Image.new("RGBA", (args.width * len(images), args.height))
            for i, image in enumerate(images):
                montage.paste(image, (i * args.width, 0))
                if output_dir:
                    image.save(output_dir / f"frame-{i:04d}.png")
            if args.write_final:
                montage.save(args.write_final)
            print("Render completed", flush=True)
            return
        if args.headless or tk is None or ImageTk is None:
            last_image = None
            for frame_idx in range(args.frames):
                texture_payload = checker_texture_payload() if args.scene == "textured-quad" else None
                image = renderer.render(frame_commands(frame_idx), texture_payload=texture_payload)
                last_image = image
                if output_dir:
                    image.save(output_dir / f"frame-{frame_idx:04d}.png")
            if args.write_final and last_image is not None:
                last_image.save(args.write_final)
            print("Render completed", flush=True)
            return

        root = tk.Tk()
        root.title("SPIR-V Proof")
        label = tk.Label(root)
        label.pack()
        state = {"frame": 0, "start": time.time()}

        def tick():
            if state["frame"] >= args.frames:
                if args.write_final and "last_image" in state:
                    state["last_image"].save(args.write_final)
                root.destroy()
                return
            texture_payload = checker_texture_payload() if args.scene == "textured-quad" else None
            image = renderer.render(frame_commands(state["frame"]), texture_payload=texture_payload)
            state["last_image"] = image
            if output_dir:
                image.save(output_dir / f"frame-{state['frame']:04d}.png")
            photo = ImageTk.PhotoImage(image)
            label.configure(image=photo)
            label.image = photo
            state["frame"] += 1
            delay_ms = max(1, int(1000.0 / args.fps))
            root.after(delay_ms, tick)

        root.after(0, tick)
        root.mainloop()
    finally:
        renderer.close()


if __name__ == "__main__":
    main()
