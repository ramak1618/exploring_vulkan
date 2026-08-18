#include <stdbool.h>
#include <stdint.h>

#include <sys/mman.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <time.h>

#include <math.h>
#include "random.h"

#include <vulkan/vulkan.h>

#include <wayland-client.h>
#include <vulkan/vulkan_wayland.h>

#include "xdg-shell.h"

#include <xkbcommon/xkbcommon.h>

#include "event_queue.c" 
#include "movement.c"

const float pi = 3.1415926535f;

#ifndef SHADER_DIR
#define SHADER_DIR "./"
#endif

struct window {
    struct wl_display* display;
    struct wl_registry* registry;
    struct wl_registry_listener registry_listener;
    struct wl_compositor* compositor;
    struct wl_surface* surface;
    struct xdg_wm_base* wm_base;
    struct xdg_surface* xurface;
    struct xdg_surface_listener surface_listener;
    struct xdg_toplevel* toplevel;
    struct xdg_toplevel_listener toplevel_listener;
    struct wl_seat* seat;
    struct wl_keyboard* keyboard;
    struct wl_keyboard_listener keyboard_listener;

    bool failed;
    bool running;
};

void registry_bind_callback(void* data, struct wl_registry* registry, uint32_t name, const char* iface, uint32_t version) {
    struct window* wnd = (struct window*) data;

    if(strcmp(iface, wl_compositor_interface.name) == 0) {
        wnd->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);
        if(!wnd->compositor) {
            fprintf(stderr, "Could not bind compositor\n");
            wnd->failed = true;
        }
        return;
    }

    if(strcmp(iface, xdg_wm_base_interface.name) == 0) {
        wnd->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
        if(!wnd->wm_base) {
            fprintf(stderr, "Could not bind wm_base\n");
            wnd->failed = true;
        }
        return;
    }

    if(strcmp(iface, wl_seat_interface.name) == 0) {
        wnd->seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
        if(!wnd->seat) {
            fprintf(stderr, "Could not bind seat\n");
            wnd->failed = true;
        }
    }
}

void noop_registry_remove(void* data, struct wl_registry* registry, uint32_t name) {
    (void) data;
    (void) registry;
    (void) name;
}

void surface_conf(void* data, struct xdg_surface* surface, uint32_t serial) {
    (void) data;
    xdg_surface_ack_configure(surface, serial);
}

void noop_toplevel_conf(void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states) {
    (void) data;
    (void) toplevel;
    (void) width;
    (void) height;
    (void) states;
}

void toplevel_close(void* data, struct xdg_toplevel* toplevel) {
    (void) toplevel;
    *(bool*) data = false;
}

void noop_toplevel_conf_bounds(void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height) {
    (void) data;
    (void) toplevel;
    (void) width;
    (void) height;
}

void noop_toplevel_wm_caps(void* data, struct xdg_toplevel* toplevel, struct wl_array* caps) {
    (void) data;
    (void) toplevel;
    (void) caps;
}

struct keyboard_data {
    struct xkb_context* ctx;
    struct xkb_state* state;

    struct xkb_keymap* keymap;
    bool failed;

    struct event_queue evqueue;
};

void keyboard_keymap(void* data, struct wl_keyboard* keyboard, enum wl_keyboard_keymap_format format, int fd, uint32_t size) {
    (void) keyboard;

    struct keyboard_data* stuff = data;
    if(!stuff->ctx) {
        fprintf(stderr, "xkb context not set!\n");
        stuff->failed = true;
        return;
    }

    if(format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        fprintf(stderr, "keyboard format not xkb_v1!\n");
        stuff->failed = true;
        return;
    }

    void* keymap_buffer = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(keymap_buffer == MAP_FAILED) {
        fprintf(stderr, "failed to map keymap buffer!\n");
        stuff->failed = true;
        return;
    }

    stuff->keymap = xkb_keymap_new_from_buffer(stuff->ctx, keymap_buffer, size, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    stuff->state = xkb_state_new(stuff->keymap);

    munmap(keymap_buffer, size);
}

void keyboard_repeat_info(void* data, struct wl_keyboard* keyboard, int32_t rate, int32_t delay) {
    (void) data;
    (void) keyboard;
    (void) rate;
    (void) delay;
}

void keyboard_enter(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface, struct wl_array* keys) {
    (void) data;
    (void) keyboard;
    (void) serial;
    (void) surface;
    (void) keys;
}

void keyboard_modifiers(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    (void) keyboard;
    (void) serial;

    struct keyboard_data* stuff = data;

    xkb_state_update_mask(stuff->state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

void keyboard_key(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, enum wl_keyboard_key_state state) {
    (void) keyboard;
    (void) serial;
    (void) time; // The reason to not use this time is that the base of this time is unknown and hard to sync with CLOCK_MONOTONIC

    struct keyboard_data* stuff = data;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);


    struct event e = {
        .sym = xkb_state_key_get_one_sym(stuff->state, key+8),
        .state = state,
        .time = ts,
    };
    event_enqueue(&stuff->evqueue, &e);
}

void keyboard_leave(void* data, struct wl_keyboard* keyboard, uint32_t serial, struct wl_surface* surface) {
    (void) data;
    (void) keyboard;
    (void) serial;
    (void) surface;
}

void destroy_window(struct window* wnd) {
    if(wnd->keyboard)
        wl_keyboard_destroy(wnd->keyboard);
    if(wnd->seat)
        wl_seat_destroy(wnd->seat);
    if(wnd->toplevel)
        xdg_toplevel_destroy(wnd->toplevel);
    if(wnd->xurface)
        xdg_surface_destroy(wnd->xurface);
    if(wnd->wm_base)
        xdg_wm_base_destroy(wnd->wm_base);
    if(wnd->surface)
        wl_surface_destroy(wnd->surface);
    if(wnd->compositor)
        wl_compositor_destroy(wnd->compositor);
    if(wnd->registry)
        wl_registry_destroy(wnd->registry);
    if(wnd->display)
        wl_display_disconnect(wnd->display);
}

struct vk_swapchain {
    uint32_t image_count;

    VkExtent2D extent;
    VkFormat format;

    VkSwapchainKHR chain;

    VkImage* images;
    VkImageView* image_views;
};

bool create_swapchain(VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family_index, VkSurfaceKHR vk_surface, uint32_t width, uint32_t height, struct vk_swapchain* swapchain) {
    VkSurfaceCapabilitiesKHR surface_caps = {0};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, vk_surface, &surface_caps);

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = vk_surface,
        .minImageCount = surface_caps.minImageCount,
        .imageFormat = VK_FORMAT_R8G8B8A8_SRGB,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = (VkExtent2D) {width, height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &queue_family_index,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if(vkCreateSwapchainKHR(device, &create_info, NULL, &swapchain->chain) != VK_SUCCESS) {
        fprintf(stderr, "Swapchain create error!\n");
        return false;
    }

    swapchain->image_count = create_info.minImageCount;
    swapchain->extent = create_info.imageExtent;
    swapchain->format = create_info.imageFormat;
    swapchain->images = malloc(sizeof(VkImage) * swapchain->image_count);
    swapchain->image_views = malloc(sizeof(VkImage) * swapchain->image_count);

    if(!swapchain->images || !swapchain->image_views) {
        fprintf(stderr, "malloc err!\n");
        return false;
    }

    vkGetSwapchainImagesKHR(device, swapchain->chain, &swapchain->image_count, swapchain->images);

    for(uint32_t i=0; i<swapchain->image_count; i++) {
        VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0, 
            .image = swapchain->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain->format,
            .components = (VkComponentMapping) {0}, // eq. SWIZZLE_IDENTITY on all 4.
            .subresourceRange = (VkImageSubresourceRange) {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        vkCreateImageView(device, &create_info, NULL, &swapchain->image_views[i]);
    }

    return true;
}

void destroy_swapchain(VkDevice device, struct vk_swapchain* swapchain) {
    if(swapchain->images)
        free(swapchain->images);
    if(swapchain->image_views) {
        for(uint32_t i=0; i<swapchain->image_count; i++) {
            vkDestroyImageView(device, swapchain->image_views[i], NULL);
        }
        free(swapchain->image_views);
    }
    if(swapchain->chain)
        vkDestroySwapchainKHR(device, swapchain->chain, NULL);
}

bool load_shader(VkDevice device, const char* shader_file, VkShaderModule* module) {
    bool result = true;

    FILE* file = fopen(shader_file, "r");
    if(!file) {
        fprintf(stderr, "Cannot open file: %s\n", shader_file);
        return false;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint32_t* buf = malloc(size);

    if(!buf) {
        fprintf(stderr, "malloc error!\n");
        result = false;
    }
    else {
        fread(buf, 1, size, file);
        VkShaderModuleCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .codeSize = size,
            .pCode = buf,
        };

        if(vkCreateShaderModule(device, &create_info, NULL, module) != VK_SUCCESS) {
            fprintf(stderr, "shader module create error!\n");
            result = false;
        }
    }

    free(buf);
    fclose(file);

    return result;
}

bool create_graphics_pipeline(VkDevice device, const char* vertf, const char* fragf, const struct vk_swapchain* swapchain, const VkPipelineVertexInputStateCreateInfo* vertex_input_state, const VkPipelineLayout layout, VkPipeline* gripeline) {
    VkShaderModule modules[2] = {0};
    if(!load_shader(device, vertf, &modules[0]))
        return false;

    if(!load_shader(device, fragf, &modules[1])) {
        vkDestroyShaderModule(device, modules[0], NULL);
        return false;
    }

    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = modules[0],
            .pName = "main",
            .pSpecializationInfo = NULL,
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = modules[1],
            .pName = "main",
            .pSpecializationInfo = NULL,
        },
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport vp = {0.0f, 0.0f, (float)swapchain->extent.width, (float)swapchain->extent.height, 0.0f, 1.0f};
    VkRect2D scissor = { {0, 0}, swapchain->extent };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = &vp,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 0.0f,
        .pSampleMask = NULL,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        // other params are unused..
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = 0,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f,
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_CLEAR,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
        .blendConstants = {0.f, 0.f, 0.f, 0.f}
    };

    VkPipelineRenderingCreateInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = NULL,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchain->format,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &render_info,
        .flags = 0,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pTessellationState = NULL,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState = &multisample_state,
        .pDepthStencilState = &depth_stencil_state,
        .pColorBlendState = &color_blend_state,
        .pDynamicState = NULL,
        .layout = layout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    };

    if(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &create_info, NULL, gripeline) != VK_SUCCESS) {
        fprintf(stderr, "Graphics Pipeline create fail!\n");
        vkDestroyShaderModule(device, modules[0], NULL);
        vkDestroyShaderModule(device, modules[1], NULL);
        return false;
    }

    vkDestroyShaderModule(device, modules[0], NULL);
    vkDestroyShaderModule(device, modules[1], NULL);
    return true;
}

struct vk_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
};

bool create_buffer(VkDevice device, uint32_t host_coherent_mask, uint32_t host_cached_mask, uint32_t queue_family_index, VkDeviceSize size, VkBufferUsageFlags usage, struct vk_buffer* buffer) {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &queue_family_index,
    };

    if(vkCreateBuffer(device, &create_info, NULL, &buffer->buffer) != VK_SUCCESS) {
        fprintf(stderr, "vk buffer create error!\n");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device, buffer->buffer, &mem_reqs);

    uint32_t mem_types = mem_reqs.memoryTypeBits & host_coherent_mask & host_cached_mask;

    if(mem_types == 0)
        mem_types = mem_reqs.memoryTypeBits & host_coherent_mask;

    if(mem_types == 0) {
        fprintf(stderr, "Cannot allocate buffer as no host-coherent memory type found!\n");
        return false;
    }

    uint32_t idx = 0;
    while((mem_types & 0x1) == 0) {
        mem_types >>= 1;
        idx ++;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = idx,
    };

    if(vkAllocateMemory(device, &alloc_info, NULL, &buffer->memory) != VK_SUCCESS) {
        fprintf(stderr, "buffer allocate error!\n");
        return false;
    }

    vkBindBufferMemory(device, buffer->buffer, buffer->memory, 0);

    return true;
}

void destroy_buffer(VkDevice device, struct vk_buffer* buffer) {
    if(buffer->memory)
        vkFreeMemory(device, buffer->memory, NULL);

    if(buffer->buffer)
        vkDestroyBuffer(device, buffer->buffer, NULL);
}

struct globals {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice physical_device;
    uint32_t queue_family_index;
    uint32_t host_coherent_mask;
    uint32_t host_cached_mask;
    VkDevice device;
    VkQueue queue;
    struct window wnd;
    VkSurfaceKHR vk_surface;
    struct vk_swapchain swapchain;
    struct keyboard_data keydata;
    VkPipelineLayout gripeline_layout;
    VkPipeline gripeline;
    VkCommandPool command_pool;
    uint32_t frames_in_flight;
    VkCommandBuffer* cmd_bufs;
    VkSemaphore* image_available_semaphores;
    VkSemaphore* render_finished_semaphores;
    VkFence* frame_finished_fences;
    struct vk_buffer vertex_buffer;
    VkImage depth_buffer;
    VkImageView depth_view;
    VkDeviceMemory depth_mem;
};

void destroy_globals(struct globals* stuff) {
    if(stuff->depth_mem)
        vkFreeMemory(stuff->device, stuff->depth_mem, NULL);

    if(stuff->depth_view) 
        vkDestroyImageView(stuff->device, stuff->depth_view, NULL);

    if(stuff->depth_buffer)
        vkDestroyImage(stuff->device, stuff->depth_buffer, NULL);

    destroy_buffer(stuff->device, &stuff->vertex_buffer);

    if(stuff->image_available_semaphores) {
        for(uint32_t i=0; i<stuff->frames_in_flight; i++) {
            if(stuff->image_available_semaphores[i])
                vkDestroySemaphore(stuff->device, stuff->image_available_semaphores[i], NULL);
        }
        free(stuff->image_available_semaphores);
    }
    if(stuff->render_finished_semaphores) {
        for(uint32_t i=0; i<stuff->swapchain.image_count; i++) {
            if(stuff->render_finished_semaphores[i])
                vkDestroySemaphore(stuff->device, stuff->render_finished_semaphores[i], NULL);
        }
        free(stuff->render_finished_semaphores);
    }
    if(stuff->frame_finished_fences) {
        for(uint32_t i=0; i<stuff->frames_in_flight; i++) {
            if(stuff->frame_finished_fences[i])
                vkDestroyFence(stuff->device, stuff->frame_finished_fences[i], NULL);
        }
        free(stuff->frame_finished_fences);
    }

    if(stuff->cmd_bufs) 
        free(stuff->cmd_bufs);

    if(stuff->command_pool)
        vkDestroyCommandPool(stuff->device, stuff->command_pool, NULL);

    if(stuff->gripeline)
        vkDestroyPipeline(stuff->device, stuff->gripeline, NULL);

    if(stuff->gripeline_layout) 
        vkDestroyPipelineLayout(stuff->device, stuff->gripeline_layout, NULL);

    destroy_swapchain(stuff->device, &stuff->swapchain);

    if(stuff->vk_surface)
        vkDestroySurfaceKHR(stuff->instance, stuff->vk_surface, NULL);

    if(stuff->keydata.state)
        xkb_state_unref(stuff->keydata.state);

    if(stuff->keydata.keymap) 
        xkb_keymap_unref(stuff->keydata.keymap);

    if(stuff->keydata.ctx)
        xkb_context_unref(stuff->keydata.ctx);

    destroy_window(&stuff->wnd);

    if(stuff->device)
        vkDestroyDevice(stuff->device, NULL);

    if(stuff->debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(stuff->instance, "vkDestroyDebugUtilsMessengerEXT");
        if(vkDestroyDebugUtilsMessengerEXT)
            vkDestroyDebugUtilsMessengerEXT(stuff->instance, stuff->debug_messenger, NULL);
    }
    if(stuff->instance)
        vkDestroyInstance(stuff->instance, NULL);
}

VkBool32 debug_callback(VkDebugUtilsMessageSeverityFlagsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    (void) messageSeverity;
    (void) messageTypes;
    (void) pUserData;

    fprintf(stderr, "%s", pCallbackData->pMessage);
    return VK_FALSE;
}

int main() {
    struct globals globs = {0};

#ifdef DISABLE_VALIDATION
    const bool enabled_validation_layers = false;
#else
    const bool enabled_validation_layers = true;
#endif

    pcg32_random_t rng;
    pcg32_srandom_r(&rng, 31415926, 1618);

    // boring stuff
    {
        VkApplicationInfo app_info = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = NULL,
            .pApplicationName = "",
            .applicationVersion = 0,
            .pEngineName = NULL,
            .engineVersion = 0,
            .apiVersion = VK_API_VERSION_1_4,
        };
        const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
        const char* extensions[] = {"VK_KHR_surface", "VK_KHR_wayland_surface", "VK_EXT_debug_utils"};

        VkDebugUtilsMessengerCreateInfoEXT debug_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = NULL,
            .flags = 0,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debug_callback,
            .pUserData = NULL,
        };

        VkInstanceCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = &debug_info,
            .flags = 0,
            .pApplicationInfo = &app_info,
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = layers,
            .enabledExtensionCount = 2,
            .ppEnabledExtensionNames = extensions,
        };

        if(enabled_validation_layers) {
            create_info.enabledLayerCount++;
            create_info.enabledExtensionCount++;
        }

        switch(vkCreateInstance(&create_info, NULL, &globs.instance)) {
            case VK_ERROR_EXTENSION_NOT_PRESENT:
                fprintf(stderr, "Requested extensions not present...\n Likely that you are'nt running on wayland!\n");
                destroy_globals(&globs);
                return 1;
            case VK_ERROR_LAYER_NOT_PRESENT:
                fprintf(stderr, "Validation layers not present (simply disable it)\n");
                destroy_globals(&globs);
                return 1;
            case VK_ERROR_INCOMPATIBLE_DRIVER:
                fprintf(stderr, "Vulkan 1.4 is not supported by your driver.. (check if your drivers are up to date)\n");
                destroy_globals(&globs);
                return 1;
            case VK_SUCCESS:
                break;
            default:
                fprintf(stderr, "Instance create faill!!!\n");
                destroy_globals(&globs);
                return 1;
        }

        if(enabled_validation_layers) {
            PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(globs.instance, "vkCreateDebugUtilsMessengerEXT");
            if(!vkCreateDebugUtilsMessengerEXT) {
                fprintf(stderr, "debug messenger create function fetch fail!\n");
                destroy_globals(&globs);
                return 1;
            }
            if(vkCreateDebugUtilsMessengerEXT(globs.instance, &debug_info, NULL, &globs.debug_messenger) != VK_SUCCESS) {
               fprintf(stderr, "debug messenger create fail!\n");
               destroy_globals(&globs);
               return 1;
            } 
        }
    }

    {
        globs.wnd.display = wl_display_connect(NULL);
        globs.wnd.registry = wl_display_get_registry(globs.wnd.display);
        globs.wnd.registry_listener = (struct wl_registry_listener) {
            .global = registry_bind_callback,
            .global_remove = noop_registry_remove,
        };

        wl_registry_add_listener(globs.wnd.registry, &globs.wnd.registry_listener, &globs.wnd);
        (void) wl_display_roundtrip(globs.wnd.display);

        if(globs.wnd.failed) {
            destroy_globals(&globs);
            return 1;
        }
    }

    {
        globs.wnd.surface = wl_compositor_create_surface(globs.wnd.compositor);
        globs.wnd.xurface = xdg_wm_base_get_xdg_surface(globs.wnd.wm_base, globs.wnd.surface);
        globs.wnd.surface_listener = (struct xdg_surface_listener) {
            .configure = surface_conf
        };
        xdg_surface_add_listener(globs.wnd.xurface, &globs.wnd.surface_listener, NULL);
        globs.wnd.toplevel = xdg_surface_get_toplevel(globs.wnd.xurface);
        globs.wnd.toplevel_listener = (struct xdg_toplevel_listener) {
            .configure = noop_toplevel_conf,
            .close = toplevel_close,
            .configure_bounds = noop_toplevel_conf_bounds,
            .wm_capabilities = noop_toplevel_wm_caps
        };
        xdg_toplevel_add_listener(globs.wnd.toplevel, &globs.wnd.toplevel_listener, &globs.wnd.running);

        wl_surface_commit(globs.wnd.surface);
        wl_display_roundtrip(globs.wnd.display);

        globs.wnd.running = true;

        VkWaylandSurfaceCreateInfoKHR create_info = {
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .pNext = NULL,
            .flags = 0,
            .display = globs.wnd.display,
            .surface = globs.wnd.surface,
        };
        if(vkCreateWaylandSurfaceKHR(globs.instance, &create_info, NULL, &globs.vk_surface) != VK_SUCCESS) {
            fprintf(stderr, "vk surface create fail!\n");
            destroy_globals(&globs);
            return 1;
        }

    }    

    // Keyboard:
    {
        globs.wnd.keyboard = wl_seat_get_keyboard(globs.wnd.seat);

        globs.keydata.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if(!globs.keydata.ctx) {
            fprintf(stderr, "xkb context create error!\n");
            destroy_globals(&globs);
            return 1;
        }
        globs.wnd.keyboard_listener = (struct wl_keyboard_listener) {
            .keymap = keyboard_keymap,
            .enter = keyboard_enter,
            .leave = keyboard_leave,
            .key = keyboard_key,
            .modifiers = keyboard_modifiers,
            .repeat_info = keyboard_repeat_info,
        };

        globs.keydata.evqueue.front = 0;
        globs.keydata.evqueue.rear = 0;

        wl_keyboard_add_listener(globs.wnd.keyboard, &globs.wnd.keyboard_listener, &globs.keydata);
    }

    // Pick Physical Device
    {
        uint32_t device_count = 0;
        vkEnumeratePhysicalDevices(globs.instance, &device_count, NULL);
        if(device_count == 0) {
            fprintf(stderr, "No GPUs that support vulkan found!\n");
            destroy_globals(&globs);
            return 1;
        }
        VkPhysicalDevice* physical_devices = malloc(sizeof(VkPhysicalDevice) * device_count);
        vkEnumeratePhysicalDevices(globs.instance, &device_count, physical_devices);

        VkPhysicalDeviceProperties2* dev_props = malloc(sizeof(VkPhysicalDeviceProperties2) * device_count);
        int32_t* scores = malloc(sizeof(int32_t) * device_count);
        int32_t* qfi = malloc(sizeof(uint32_t) * device_count);

        for(uint32_t i=0; i<device_count; i++) {
            scores[i] = 0;

            dev_props[i] = (VkPhysicalDeviceProperties2) {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = NULL,
                .properties = (VkPhysicalDeviceProperties) {0},
            };
            vkGetPhysicalDeviceProperties2(physical_devices[i], &dev_props[i]);

            // selecting the GPU
            if(dev_props[i].properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                scores[i] += 1000; // Discrete GPU >>> Integrated GPU
            }

            // if API version is < 1.4, we cant use it..
            if(dev_props[i].properties.apiVersion < VK_API_VERSION_1_4) {
                scores[i] = -1;
                printf("Queried Device: %s does not support Vulkan 1.4.\n", dev_props[i].properties.deviceName);
                continue;
            }

            // Make sure the graphics card has a queue that supports GRAPHICS AND TRANSFER and can present too.
            uint32_t qfam_count; 
            vkGetPhysicalDeviceQueueFamilyProperties2(physical_devices[i], &qfam_count, NULL);
            VkQueueFamilyProperties2* qfamprops = malloc(sizeof(VkQueueFamilyProperties2) * qfam_count);
            for(uint32_t j=0; j<qfam_count; j++) {
                qfamprops[j] = (VkQueueFamilyProperties2) {
                    .sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2,
                    .pNext = NULL,
                    .queueFamilyProperties = (VkQueueFamilyProperties) {0},
                };
            }
            vkGetPhysicalDeviceQueueFamilyProperties2(physical_devices[i], &qfam_count, qfamprops);

            qfi[i] = -1;
            for(uint32_t j=0; j<qfam_count; j++) {
                if( !(qfamprops[j].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT) || !(qfamprops[j].queueFamilyProperties.queueFlags & VK_QUEUE_TRANSFER_BIT) ) {
                    continue;
                }
                VkBool32 present_support;
                (void) vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, globs.vk_surface, &present_support);
                if(present_support == VK_FALSE)
                    continue;

                // if reached here, it means that this queue is OK to use
                    qfi[i] = j;
                    break;
                // endif
            }

            if(qfi[i] == -1) {
                scores[i] = -1;
                printf("Queried Device: %s does not have a Queue Family that supports both GRAPHICS and TRANSFER operations along with present support\n", dev_props[i].properties.deviceName);
                continue;
            }

            free(qfamprops);
        }

        // select the GPU with highest non-negative score
        int32_t idx = -1;
        for(uint32_t i=0; i<device_count; i++) {
            if(idx == -1 && scores[i] >= 0) {
                idx = i;
                continue;
            }
            if(scores[i] > scores[idx]) {
                idx = i;
                continue;
            }
        }

        if(idx == -1) {
            fprintf(stderr, "No suitable GPU found!\n");
            destroy_globals(&globs);
            return 1;
        }

        globs.physical_device = physical_devices[idx];
        globs.queue_family_index = qfi[idx];

        printf("Chosen Physical Device: %s\n", dev_props[idx].properties.deviceName);
        printf("Press ENTER to continue");
        
        char dummy;
        fread(&dummy, 1, 1, stdin);

        free(dev_props);
        free(scores);
        free(qfi);
        free(physical_devices);
    }

    // Get different memory type masks
    {
        VkPhysicalDeviceMemoryProperties2 mem_props = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
            .pNext = NULL,
            .memoryProperties = (VkPhysicalDeviceMemoryProperties) {0},
        };

        vkGetPhysicalDeviceMemoryProperties2(globs.physical_device, &mem_props);

        for(uint32_t i=0; i<VK_MAX_MEMORY_TYPES; i++) {
            if(mem_props.memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                globs.host_coherent_mask |= (1 << i);

            if(mem_props.memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
                globs.host_cached_mask |= (1 << i);
        }
    }

    // Create Device
    {
        float one = 1.f;
        VkDeviceQueueCreateInfo queue_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .queueFamilyIndex = globs.queue_family_index,
            .queueCount = 1,
            .pQueuePriorities = &one,
        };

        const char* extensions[] = {"VK_KHR_swapchain"};

        VkPhysicalDeviceFeatures features = {0};

        VkPhysicalDeviceSynchronization2Features syncfeats = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
            .pNext = NULL,
            .synchronization2 = VK_TRUE,
        };

        VkPhysicalDeviceDynamicRenderingFeatures dynamic_rendering = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
            .pNext = &syncfeats,
            .dynamicRendering = VK_TRUE,
        };

        VkDeviceCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &dynamic_rendering,
            .flags = 0,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queue_info,
            .enabledExtensionCount = 1,
            .ppEnabledExtensionNames = extensions,
            .pEnabledFeatures = &features,
        };

        if(vkCreateDevice(globs.physical_device, &create_info, NULL, &globs.device) != VK_SUCCESS) {
            fprintf(stderr, "device create error!\n");
            destroy_globals(&globs);
            return 1;
        }

        vkGetDeviceQueue(globs.device, globs.queue_family_index, 0, &globs.queue);
    }

    
    if(!create_swapchain(globs.physical_device, globs.device, globs.queue_family_index, globs.vk_surface, 800, 600, &globs.swapchain)) {
        destroy_globals(&globs);
        return 1;
    }

    // Camera
    float F = 100.f;
    float N = 0.1f;
    float f = 1.0f;

    float aspect_ratio = (float) globs.swapchain.extent.width / (float) globs.swapchain.extent.height;
    float vp_h = 2.0f;
    float vp_w = aspect_ratio * vp_h;
    float vp_ws = 2.0f / vp_w;
    float vp_hs = 2.0f / vp_h;
    
    //float aaaa = 1/sqrt(2);
    struct camera cam = {
        {0.f, 0.f, -3.f},
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f}
    };

    float camera_data[] = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,

         vp_ws*f,    0.0f,        0.0f,        0.0f,
           0.0f,    vp_hs*f,      0.0f,        0.0f,
           0.0f,     0.0f,       F/(F-N),      1.0f,
           0.0f,     0.0f,     -N*F/(F-N),     0.0f,
    };
    uint32_t camera_data_size = 32 * 4;

    uint32_t vertex_size = 6*4;
    uint32_t num_vertices = 1000 * 100;
    uint32_t vertex_data_size = num_vertices * vertex_size;
 
    // REMEMBER TO UPDATE VERTEX SHADER CHANGES HERE
    {
        VkVertexInputBindingDescription vertex_binding_desc[] = {
            {
                .binding = 0,
                .stride = vertex_size,
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            },
        };

        VkVertexInputAttributeDescription vertex_attr_desc[] = {
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = 0,
            },
            {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT,
                .offset = 3*4,
            },
        };

        VkPipelineVertexInputStateCreateInfo vertex_input_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = vertex_binding_desc,
            .vertexAttributeDescriptionCount = 2,
            .pVertexAttributeDescriptions = vertex_attr_desc,
        };

        VkPushConstantRange push_constant_range = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = camera_data_size,
        };

        char vert_fpath[256];
        char frag_fpath[256];

        snprintf(vert_fpath, 256, "%s%s", SHADER_DIR, "vert.spv");
        snprintf(frag_fpath, 256, "%s%s", SHADER_DIR, "frag.spv");

        VkPipelineLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .setLayoutCount = 0,
            .pSetLayouts = NULL,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range,
        };

        if(vkCreatePipelineLayout(globs.device, &layout_info, NULL, &globs.gripeline_layout) != VK_SUCCESS) {
            fprintf(stderr, "Could not create graphics pipeline layout!\n");
            destroy_globals(&globs);
            return 1;
        }

        if(!create_graphics_pipeline(globs.device, vert_fpath, frag_fpath, &globs.swapchain, &vertex_input_state, globs.gripeline_layout, &globs.gripeline)) {
            destroy_globals(&globs);
            return 1;
        }
    }
    
    {
        VkCommandPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = globs.queue_family_index,
        };
        vkCreateCommandPool(globs.device, &create_info, NULL, &globs.command_pool);

        globs.frames_in_flight = 2;

        globs.cmd_bufs = malloc(globs.frames_in_flight * sizeof(VkCommandBuffer));
        if(!globs.cmd_bufs) {
            fprintf(stderr, "malloc err!\n");
            return 1;
        }
        for(uint32_t i=0; i<globs.frames_in_flight; i++)
            globs.cmd_bufs[i] = VK_NULL_HANDLE;

        VkCommandBufferAllocateInfo cmd_buf_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = NULL,
            .commandPool = globs.command_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = globs.frames_in_flight,
        };
        if(vkAllocateCommandBuffers(globs.device, &cmd_buf_info, globs.cmd_bufs) != VK_SUCCESS) {
            fprintf(stderr, "could not allocate draw command bufs!\n");
            destroy_globals(&globs);
            return 1;
        }

        globs.image_available_semaphores = malloc(globs.frames_in_flight * sizeof(VkSemaphore));
        globs.render_finished_semaphores = malloc(globs.swapchain.image_count * sizeof(VkSemaphore));
        globs.frame_finished_fences = malloc(globs.frames_in_flight * sizeof(VkFence));

        bool success = true;
        for(uint32_t i=0; i<globs.frames_in_flight; i++) {
            if(globs.image_available_semaphores) {
                globs.image_available_semaphores[i] = VK_NULL_HANDLE;
                VkSemaphoreCreateInfo create_info = {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                    .pNext = NULL,
                    .flags = 0,
                };
                if(vkCreateSemaphore(globs.device, &create_info, NULL, &globs.image_available_semaphores[i]) != VK_SUCCESS) {
                    fprintf(stderr, "semaphore create error!!\n");
                    success = false;
                }
            }
            if(globs.frame_finished_fences) {
                globs.frame_finished_fences[i] = VK_NULL_HANDLE;
                VkFenceCreateInfo create_info = {
                    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                    .pNext = NULL,
                    .flags = VK_FENCE_CREATE_SIGNALED_BIT,
                };
                if(vkCreateFence(globs.device, &create_info, NULL, &globs.frame_finished_fences[i]) != VK_SUCCESS) {
                    fprintf(stderr, "fence create error!\n");
                    success = false;
                }
            }
        }

        for(uint32_t i=0; i<globs.swapchain.image_count; i++) {
            if(globs.render_finished_semaphores) {
                globs.render_finished_semaphores[i] = VK_NULL_HANDLE;
                VkSemaphoreCreateInfo create_info = {
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                    .pNext = NULL,
                    .flags = 0,
                };
                if(vkCreateSemaphore(globs.device, &create_info, NULL, &globs.render_finished_semaphores[i]) != VK_SUCCESS) {
                    fprintf(stderr, "semaphore create error!\n");
                    success = false;
                }
            }
        }

        if(!globs.image_available_semaphores || !globs.render_finished_semaphores || !globs.frame_finished_fences) {
            fprintf(stderr, "malloc err!!\n");
            destroy_globals(&globs);
            return 1;
        }

        if(!success) {
            destroy_globals(&globs);
            return 1;
        }
    }

    // create depth image buffer
    {
        VkImageCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = (VkExtent3D) { globs.swapchain.extent.width, globs.swapchain.extent.height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &globs.queue_family_index,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if(vkCreateImage(globs.device, &create_info, NULL, &globs.depth_buffer) != VK_SUCCESS) {
            fprintf(stderr, "depth image create error!\n");
            destroy_globals(&globs);
            return 1;
        }

        VkMemoryRequirements mem_reqs;
        vkGetImageMemoryRequirements(globs.device, globs.depth_buffer, &mem_reqs);

        uint32_t mem_types = mem_reqs.memoryTypeBits & globs.host_coherent_mask & globs.host_cached_mask;

        if(mem_types == 0)
            mem_types = mem_reqs.memoryTypeBits & globs.host_coherent_mask;

        if(mem_types == 0) {
            fprintf(stderr, "Cannot allocate depth buffer as no host-coherent memory type found!\n");
            return false;
        }

        uint32_t idx = 0;
        while((mem_types & 0x1) == 0) {
            mem_types >>= 1;
            idx ++;
        }

        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = NULL,
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex = idx,
        };

        vkAllocateMemory(globs.device, &alloc_info, NULL, &globs.depth_mem);

        vkBindImageMemory(globs.device, globs.depth_buffer, globs.depth_mem, 0);

        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL,
        };

        VkImageMemoryBarrier2 barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = globs.depth_buffer,
            .subresourceRange = (VkImageSubresourceRange) { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
        };

        VkDependencyInfo dep_info = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = NULL,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = NULL,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
            
        vkBeginCommandBuffer(globs.cmd_bufs[0], &begin_info);
        vkCmdPipelineBarrier2(globs.cmd_bufs[0], &dep_info);
        vkEndCommandBuffer(globs.cmd_bufs[0]);

        VkCommandBufferSubmitInfo cmd_buf_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = NULL,
            .commandBuffer = globs.cmd_bufs[0],
            .deviceMask = 0,
        };

        VkSubmitInfo2 submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = NULL,
            .flags = 0,
            .waitSemaphoreInfoCount = 0,
            .pWaitSemaphoreInfos = NULL,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cmd_buf_info,
            .signalSemaphoreInfoCount = 0,
            .pSignalSemaphoreInfos = NULL,
        };

        vkQueueSubmit2(globs.queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(globs.queue);
    }

    // create buffers
    {
        if(!create_buffer(globs.device, globs.host_coherent_mask, globs.host_cached_mask, globs.queue_family_index, vertex_data_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &globs.vertex_buffer)) {
            fprintf(stderr, "vertex buffer create error!\n");
            destroy_globals(&globs);
            return 1;
        }
        
        void* raw_memp;

        vkMapMemory(globs.device, globs.vertex_buffer.memory, 0, vertex_data_size, 0, &raw_memp);

        float* data = raw_memp;
        
        /*
        float R = 0.75f;
        float a = 1.5f;
        */

        /*
        float R = 1.f;
        float a = -1.f;
        */
        
        float R = 2.0f;
        float a = 1.0f;

        for(uint32_t i=0; i<num_vertices; i++) {
            float theta = uniform_unit_float(&rng) * 2.f * pi;
            float phi =  uniform_unit_float(&rng) * 2.f * pi;

            data[6*i + 0] =  R*cosf(phi) + a*cosf(theta)*cosf(phi);
            data[6*i + 1] =  R*sinf(phi) + a*cosf(theta)*sinf(phi);
            data[6*i + 2] =  a*sinf(theta);
            data[6*i + 3] =  log1pf( (cosf(phi)*cosf(theta) + 1.f) * 0.5f );
            data[6*i + 4] =  log1pf( (sinf(phi)*cosf(theta) + 1.f) * 0.5f );
            data[6*i + 5] =  log1pf( (sinf(theta) + 1.f) * 0.5f ) ;
        }

        vkUnmapMemory(globs.device, globs.vertex_buffer.memory);
    }

    {
        VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .image = globs.depth_buffer,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .components = 0, 
            .subresourceRange = (VkImageSubresourceRange) { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
        };

        if(vkCreateImageView(globs.device, &create_info, NULL, &globs.depth_view) != VK_SUCCESS) {
            fprintf(stderr, "Depth view create error!\n");
            destroy_globals(&globs);
            return 1;
        }
    }


    // MAIN LOOP
    uint32_t frame_index = 0;

    struct timespec init_time;
    clock_gettime(CLOCK_MONOTONIC, &init_time);
    struct timespec time = init_time;

    float target_spf = 1 / 120.f;
    struct timespec spf = {(int32_t)target_spf, (int32_t) (target_spf * 1e9) };


    /** KEY STATES **/
    bool key_heldmap[NUM_CAM_MOVES] = {0};

    while(globs.wnd.running && wl_display_dispatch_pending(globs.wnd.display) != -1) {
        // calculate FPS
        struct timespec frame_start_time;
        clock_gettime(CLOCK_MONOTONIC, &frame_start_time);

        float delta_time = frame_start_time.tv_sec - time.tv_sec + (frame_start_time.tv_nsec - time.tv_nsec)/1e9f;
        //float fps = 1.0f / delta_time;
        //printf("fps: %f\n", fps);

        time = frame_start_time;

        // Process events
        while(!event_queue_is_empty(&globs.keydata.evqueue)) {
            struct event e;
            event_dequeue(&globs.keydata.evqueue, &e);
            enum camera_movement movement_type = keymap(e.sym);

            if(movement_type == MOVEMENT_NONE)
                continue;

            if(e.state == WL_KEYBOARD_KEY_STATE_PRESSED)
                key_heldmap[movement_type] = true;
            if(e.state == WL_KEYBOARD_KEY_STATE_RELEASED) {
                key_heldmap[movement_type] = false;
                update_camera(&cam, movement_type, 1.f, time.tv_sec - e.time.tv_sec + (time.tv_nsec - e.time.tv_nsec)/1e9f);
            }
        }
        for(uint32_t i=0; i<NUM_CAM_MOVES; i++) {
            if(key_heldmap[i]) {
                update_camera(&cam, i, 1.f, delta_time);
            }
        }

        build_view_matrix(camera_data, &cam);

        //printf("(%f, %f, %f)\n", cam.pos[0], cam.pos[1], cam.pos[2]);
        //printf("(%f, %f, %f)r\n(%f, %f, %f)d\n(%f, %f, %f)f\n", cam.right[0], cam.right[1], cam.right[2], cam.down[0], cam.down[1], cam.down[2], cam.forward[0], cam.forward[1], cam.forward[2]);

        // Wait for current frame to be free..
        (void) vkWaitForFences(globs.device, 1, &globs.frame_finished_fences[frame_index], VK_TRUE, UINT64_MAX);
        (void) vkResetFences(globs.device, 1, &globs.frame_finished_fences[frame_index]);

        // Get the next free image to render, and signal image_available_semaphore of this frame when acquired
        uint32_t image_index;
        (void) vkAcquireNextImageKHR(globs.device, globs.swapchain.chain, UINT64_MAX, globs.image_available_semaphores[frame_index], VK_NULL_HANDLE, &image_index);

        // Image Layout Changing Barriers
        VkImageMemoryBarrier2 color_attach_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = globs.swapchain.images[image_index],
            .subresourceRange = (VkImageSubresourceRange) {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        VkImageMemoryBarrier2 present_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = globs.swapchain.images[image_index],
            .subresourceRange = (VkImageSubresourceRange) {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };

        VkDependencyInfo color_attach_barrier_dep_info = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = NULL,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = NULL,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &color_attach_barrier,
        };

        VkDependencyInfo present_barrier_dep_info = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = 0,
            .memoryBarrierCount = 0,
            .pMemoryBarriers = NULL,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = NULL,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &present_barrier,
        };

        VkRenderingAttachmentInfo image_attachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = NULL,
            .imageView = globs.swapchain.image_views[image_index],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = (VkClearValue) { 
                .color = (VkClearColorValue) {
                    .float32 = {0.f, 0.f, 0.f, 0.f}
                }
            },
        };

        VkRenderingAttachmentInfo depth_attachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = NULL,
            .imageView = globs.depth_view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = (VkClearValue) {
                .depthStencil = (VkClearDepthStencilValue) {
                    .depth = 1.f,
                }
            },
        };

        // Record the command buffer
        (void) vkResetCommandBuffer(globs.cmd_bufs[frame_index], 0);

        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL,
        };

        (void) vkBeginCommandBuffer(globs.cmd_bufs[frame_index], &begin_info);


        VkRenderingInfo rendering_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .pNext = NULL,
            .flags = 0,
            .renderArea = (VkRect2D) { (VkOffset2D) {0, 0},  globs.swapchain.extent },
            .layerCount = 1,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = &image_attachment,
            .pDepthAttachment = &depth_attachment,
            .pStencilAttachment = NULL,
        };

        vkCmdPipelineBarrier2(globs.cmd_bufs[frame_index], &color_attach_barrier_dep_info);

        vkCmdBeginRendering(globs.cmd_bufs[frame_index], &rendering_info);
            vkCmdBindPipeline(globs.cmd_bufs[frame_index], VK_PIPELINE_BIND_POINT_GRAPHICS, globs.gripeline);

            VkDeviceSize offset  = 0;

            vkCmdPushConstants(globs.cmd_bufs[frame_index], globs.gripeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, camera_data_size, camera_data);
            vkCmdBindVertexBuffers(globs.cmd_bufs[frame_index], 0, 1, &globs.vertex_buffer.buffer, &offset);
                    
            vkCmdDraw(globs.cmd_bufs[frame_index], num_vertices, 1, 0, 0);
        vkCmdEndRendering(globs.cmd_bufs[frame_index]);

        vkCmdPipelineBarrier2(globs.cmd_bufs[frame_index], &present_barrier_dep_info);
        (void) vkEndCommandBuffer(globs.cmd_bufs[frame_index]);

        // Submit the commands
        VkCommandBufferSubmitInfo cmd_buf_submit_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = NULL,
            .commandBuffer = globs.cmd_bufs[frame_index],
            .deviceMask = 0,
        };

        VkSemaphoreSubmitInfo image_available_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = NULL,
            .semaphore = globs.image_available_semaphores[frame_index],
            .value = 0, /* ignored, this is NOT a timeline semaphore */
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, /* i.e. run the vertex and frag shaders and then wait */
            .deviceIndex = 0,
        };

        VkSemaphoreSubmitInfo render_finished_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = NULL,
            .semaphore = globs.render_finished_semaphores[image_index],
            .value = 0, /* ignored, this is NOT a timeline semaphore */
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, /* i.e. signal render_finished ONLY after attaching to swapchain image */
            .deviceIndex = 0,
        };

        VkSubmitInfo2 submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = NULL,
            .flags = 0,
            .waitSemaphoreInfoCount = 1,
            .pWaitSemaphoreInfos = &image_available_info,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cmd_buf_submit_info,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &render_finished_info,
        };
        (void) vkQueueSubmit2(globs.queue, 1, &submit_info, globs.frame_finished_fences[frame_index]);

        // Present the image whenever the image is done rendering
        VkPresentInfoKHR present_info = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = NULL,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &globs.render_finished_semaphores[image_index],
            .swapchainCount = 1,
            .pSwapchains = &globs.swapchain.chain,
            .pImageIndices = &image_index,
            .pResults = NULL,
        };
        (void) vkQueuePresentKHR(globs.queue, &present_info);

        // Update frame index
        frame_index = (frame_index + 1) % globs.frames_in_flight;


        // cap fps
        struct timespec frame_end_time;
        clock_gettime(CLOCK_MONOTONIC, &frame_end_time);
        int32_t delta_time_sec = (int32_t)frame_end_time.tv_sec - (int32_t)frame_start_time.tv_sec;
        int32_t delta_time_nsec = (int32_t)frame_end_time.tv_nsec - (int32_t)frame_start_time.tv_nsec;
        if(delta_time_nsec < 0) {
            delta_time_nsec += 1000 * 1000 * 1000;
            delta_time_sec--;
        }

        if(delta_time_sec <= spf.tv_sec && delta_time_nsec <= spf.tv_nsec) {
            struct timespec sleep_time = {spf.tv_sec - delta_time_sec, spf.tv_nsec - delta_time_nsec };
            nanosleep(&sleep_time, NULL);
        }
   }

    vkDeviceWaitIdle(globs.device);

    destroy_globals(&globs);
    return 0;
}
