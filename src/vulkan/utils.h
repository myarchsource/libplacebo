/*
 * This file is part of libplacebo.
 *
 * libplacebo is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libplacebo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libplacebo. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "common.h"

// Return a human-readable name for various vulkan enums
const char *vk_res_str(VkResult res);
const char *vk_obj_str(VkDebugReportObjectTypeEXT obj);

// Enum translation boilerplate
VkExternalMemoryHandleTypeFlagBitsKHR vk_mem_handle_type(enum pl_handle_type);
VkExternalSemaphoreHandleTypeFlagBitsKHR vk_sync_handle_type(enum pl_handle_type);

// Check for compatibility of a VkExternalMemoryProperties
bool vk_external_mem_check(const VkExternalMemoryPropertiesKHR *props,
                           enum pl_handle_type handle_type,
                           bool check_import);

// Static lists of external handle types we should try probing for
extern const enum pl_handle_type vk_mem_handle_list[];
extern const enum pl_handle_type vk_sync_handle_list[];

// Convenience macros to simplify a lot of common boilerplate
#define VK_ASSERT(res, str)                               \
    do {                                                  \
        if (res != VK_SUCCESS) {                          \
            PL_ERR(vk, str ": %s", vk_res_str(res));      \
            goto error;                                   \
        }                                                 \
    } while (0)

#define VK(cmd)                                           \
    do {                                                  \
        PL_TRACE(vk, #cmd);                               \
        VkResult res ## __LINE__ = (cmd);                 \
        VK_ASSERT(res ## __LINE__, #cmd);                 \
    } while (0)
