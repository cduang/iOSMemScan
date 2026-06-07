//
//  MemoryScanner.c
//  iOSMemScan
//
//  iOS 跨进程内存字符串扫描引擎 - 实现
//  使用纯 sysctl + Mach VM API（完全兼容 iOS SDK）
//

#include "MemoryScanner.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/sysctl.h>
#include <mach/mach_vm.h>

// kinfo_proc 结构体字段偏移量（从 <sys/sysctl.h> 定义推导）
// kp_proc.p_pid     = offset 0  (pid_t, 4 bytes)
// kp_proc.p_comm[16] = offset 68 on arm64 (在 eproc 结构体中)
// 使用 sysctl 原始缓冲区 + 固定偏移量，避免依赖 struct kinfo_proc

#pragma mark - 进程操作实现

pid_t *get_process_list(uint32_t *count) {
    if (!count) return NULL;

    int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };
    size_t buf_size = 0;

    if (sysctl(mib, 3, NULL, &buf_size, NULL, 0) != 0) {
        *count = 0;
        return NULL;
    }

    uint8_t *proc_buf = (uint8_t *)malloc(buf_size);
    if (!proc_buf) {
        *count = 0;
        return NULL;
    }

    if (sysctl(mib, 3, proc_buf, &buf_size, NULL, 0) != 0) {
        free(proc_buf);
        *count = 0;
        return NULL;
    }

    // KERN_PROC_ALL 返回 struct kinfo_proc 数组
    // kinfo_proc 结构体大小在 iOS arm64 上为 408 字节
    // pid_t (p_pid) 是 kp_proc 的第一个字段（offset 0）
    const size_t KINFO_PROC_SIZE = 408;
    uint32_t proc_count = (uint32_t)(buf_size / KINFO_PROC_SIZE);
    if (proc_count == 0) {
        free(proc_buf);
        *count = 0;
        return NULL;
    }

    pid_t *pid_array = (pid_t *)malloc(sizeof(pid_t) * proc_count);
    if (!pid_array) {
        free(proc_buf);
        *count = 0;
        return NULL;
    }

    // kp_proc.p_pid 是 kinfo_proc 的第一个字段（offset 0）
    for (uint32_t i = 0; i < proc_count; i++) {
        const uint8_t *entry = proc_buf + (i * KINFO_PROC_SIZE);
        pid_array[i] = *(const pid_t *)entry;
    }

    free(proc_buf);
    *count = proc_count;
    return pid_array;
}

bool get_process_name(pid_t pid, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return false;

    // 使用 KERN_PROCARGS2 获取进程参数信息
    int mib[3] = { CTL_KERN, KERN_PROCARGS2, pid };
    size_t buf_size = buffer_size;

    if (sysctl(mib, 3, buffer, &buf_size, NULL, 0) == 0 && buf_size > 0) {
        // KERN_PROCARGS2 返回数据格式：
        // [argc][exec_path][...nulls...][argv strings...]
        // exec_path 在 argc 之后
        if (buf_size > 4) {
            // argc 是 int (4 bytes)，之后紧跟可执行文件路径
            buffer[buf_size - 1] = '\0';
            return true;
        }
    }

    // 备用方法
    buffer[0] = '\0';
    return false;
}

bool get_process_path(pid_t pid, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return false;

    int mib[3] = { CTL_KERN, KERN_PROCARGS2, pid };
    size_t buf_size = buffer_size;

    if (sysctl(mib, 3, buffer, &buf_size, NULL, 0) == 0) {
        buffer[buffer_size - 1] = '\0';
        return true;
    }

    return get_process_name(pid, buffer, buffer_size);
}

#pragma mark - 内存遍历实现

kern_return_t get_memory_regions(task_t task, MemoryRegion **regions, uint32_t *count) {
    if (!regions || !count) return KERN_INVALID_ARGUMENT;

    *regions = NULL;
    *count = 0;

    uint32_t capacity = 256;
    MemoryRegion *region_array = (MemoryRegion *)malloc(sizeof(MemoryRegion) * capacity);
    if (!region_array) return KERN_RESOURCE_SHORTAGE;

    uint32_t region_count = 0;
    mach_vm_address_t address = 0;
    mach_vm_size_t size = 0;
    natural_t depth = 0;
    struct vm_region_submap_info_64 info;
    mach_msg_type_number_t info_count = VM_REGION_SUBMAP_INFO_COUNT_64;

    while (true) {
        kern_return_t kr = mach_vm_region_recurse(task, &address, &size, &depth,
                                                   (vm_region_recurse_info_t)&info, &info_count);
        if (kr != KERN_SUCCESS) {
            if (kr == KERN_NO_SPACE) break;
            break;
        }

        if (info.is_submap) {
            depth++;
            continue;
        }

        if (region_count >= capacity) {
            capacity *= 2;
            MemoryRegion *new_array = (MemoryRegion *)realloc(region_array,
                                                               sizeof(MemoryRegion) * capacity);
            if (!new_array) {
                free(region_array);
                return KERN_RESOURCE_SHORTAGE;
            }
            region_array = new_array;
        }

        MemoryRegion *region = &region_array[region_count];
        region->start_addr = address;
        region->end_addr = address + size;
        region->size = size;
        region->protection = info.protection;
        region->is_shared = info.is_shared;
        region->is_mapped = info.is_mapped_file_share;

        if (info.is_submap) {
            region->region_type = '?';
        } else if (info.is_mapped_file_share) {
            region->region_type = 'S';
        } else if ((info.protection & VM_PROT_EXECUTE) && !(info.protection & VM_PROT_WRITE)) {
            region->region_type = 'T';
        } else if ((info.protection & VM_PROT_READ) && (info.protection & VM_PROT_WRITE)) {
            region->region_type = 'D';
        } else {
            region->region_type = '?';
        }

        region_count++;
        address += size;
        depth = 0;
        info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
    }

    *regions = region_array;
    *count = region_count;
    return KERN_SUCCESS;
}

void print_memory_region(const MemoryRegion *region) {
    if (!region) return;
    printf("[%c] 0x%016llx - 0x%016llx [size: %lld] "
           "[prot: %c%c%c] [shared: %s] [mapped: %s]\n",
           region->region_type,
           region->start_addr, region->end_addr, region->size,
           (region->protection & VM_PROT_READ)  ? 'R' : '-',
           (region->protection & VM_PROT_WRITE) ? 'W' : '-',
           (region->protection & VM_PROT_EXECUTE)? 'X' : '-',
           region->is_shared ? "YES" : "NO",
           region->is_mapped ? "YES" : "NO");
}

#pragma mark - 内存扫描实现

static inline char to_lower_char(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 0x20) : c;
}

static bool find_string_in_buffer(const uint8_t *buf, size_t buf_len,
                                   const char *pattern, size_t pattern_len,
                                   bool case_sensitive,
                                   mach_vm_address_t base_addr,
                                   mach_vm_address_t *out_addr,
                                   size_t *out_offset) {
    if (buf_len < pattern_len) return false;

    for (size_t i = 0; i <= buf_len - pattern_len; i++) {
        bool found = true;
        for (size_t j = 0; j < pattern_len; j++) {
            char bc = (char)buf[i + j];
            char pc = pattern[j];
            if (!case_sensitive) {
                bc = to_lower_char(bc);
                pc = to_lower_char(pc);
            }
            if (bc != pc) {
                found = false;
                break;
            }
        }
        if (found) {
            *out_addr = base_addr + i;
            *out_offset = i;
            return true;
        }
    }
    return false;
}

static bool add_result(ScanResultSet *set, mach_vm_address_t addr,
                       const uint8_t *buf, size_t offset, size_t buf_len) {
    if (!set) return false;

    if (set->count >= set->capacity) {
        uint32_t new_cap = set->capacity ? set->capacity * 2 : 256;
        ScanResultItem *new_items = (ScanResultItem *)realloc(set->items,
                                                               sizeof(ScanResultItem) * new_cap);
        if (!new_items) return false;
        set->items = new_items;
        set->capacity = new_cap;
    }

    ScanResultItem *item = &set->items[set->count];
    item->address = addr;

    size_t ctx_start = (offset >= 32) ? (offset - 32) : 0;
    size_t ctx_end = (offset + 32 < buf_len) ? (offset + 32) : buf_len;
    item->context_len = ctx_end - ctx_start;
    if (item->context_len > sizeof(item->context)) {
        item->context_len = sizeof(item->context);
    }
    memcpy(item->context, buf + ctx_start, item->context_len);

    set->count++;
    return true;
}

ScanResultSet scan_process_memory(task_t task,
                                  const char *target_string,
                                  bool case_sensitive,
                                  bool match_unicode,
                                  uint32_t max_results,
                                  const char *region_filter) {
    ScanResultSet result = { NULL, 0, 0, NULL };

    if (!target_string || strlen(target_string) == 0) {
        return result;
    }

    size_t target_len = strlen(target_string);
    result.searched_string = (char *)malloc(target_len + 1);
    if (!result.searched_string) return result;
    strncpy(result.searched_string, target_string, target_len);
    result.searched_string[target_len] = '\0';

    uint8_t *unicode_pattern = NULL;
    size_t unicode_pattern_len = 0;
    if (match_unicode) {
        unicode_pattern_len = target_len * 2;
        unicode_pattern = (uint8_t *)malloc(unicode_pattern_len);
        if (unicode_pattern) {
            for (size_t i = 0; i < target_len; i++) {
                unicode_pattern[i * 2] = (uint8_t)target_string[i];
                unicode_pattern[i * 2 + 1] = 0x00;
            }
        }
    }

    mach_vm_address_t address = 0;
    mach_vm_size_t size = 0;
    natural_t depth = 0;
    struct vm_region_submap_info_64 info;
    mach_msg_type_number_t info_count = VM_REGION_SUBMAP_INFO_COUNT_64;

    const mach_vm_size_t READ_SIZE = 4096;
    uint8_t *buffer = (uint8_t *)malloc((size_t)READ_SIZE);
    if (!buffer) {
        free(unicode_pattern);
        return result;
    }

    while (true) {
        kern_return_t kr = mach_vm_region_recurse(task, &address, &size, &depth,
                                                    (vm_region_recurse_info_t)&info, &info_count);
        if (kr != KERN_SUCCESS) {
            if (kr == KERN_NO_SPACE) break;
            break;
        }

        if (info.is_submap) {
            depth++;
            address += size;
            continue;
        }

        if (region_filter) {
            char filter_type = region_filter[0];
            bool pass = false;
            switch (filter_type) {
                case 'D':
                    pass = (info.protection & VM_PROT_READ) && (info.protection & VM_PROT_WRITE);
                    break;
                case 'T':
                    pass = (info.protection & VM_PROT_EXECUTE) && !(info.protection & VM_PROT_WRITE);
                    break;
                case 'S':
                    pass = info.is_mapped_file_share;
                    break;
                case 'R':
                default:
                    pass = (info.protection & VM_PROT_READ) != 0;
                    break;
            }
            if (!pass) {
                address += size;
                depth = 0;
                info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
                continue;
            }
        } else {
            if (!(info.protection & VM_PROT_READ)) {
                address += size;
                depth = 0;
                info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
                continue;
            }
        }

        mach_vm_address_t current_addr = address;
        mach_vm_size_t remaining = size;

        while (remaining > 0) {
            mach_vm_size_t read_size = (remaining < READ_SIZE) ? remaining : READ_SIZE;

            mach_vm_size_t bytes_read = 0;
            kr = mach_vm_read_overwrite(task, current_addr, read_size,
                                         (mach_vm_address_t)buffer, &bytes_read);
            if (kr != KERN_SUCCESS) {
                current_addr += read_size;
                remaining -= read_size;
                continue;
            }

            size_t buf_len = (size_t)bytes_read;

            mach_vm_address_t found_addr = 0;
            size_t found_offset = 0;

            if (find_string_in_buffer(buffer, buf_len,
                                      target_string, target_len,
                                      case_sensitive,
                                      current_addr,
                                      &found_addr, &found_offset)) {
                add_result(&result, found_addr, buffer, found_offset, buf_len);
                if (max_results > 0 && result.count >= max_results) {
                    free(buffer);
                    free(unicode_pattern);
                    return result;
                }
            }

            if (match_unicode && unicode_pattern) {
                if (find_string_in_buffer(buffer, buf_len,
                                          (const char *)unicode_pattern,
                                          unicode_pattern_len,
                                          case_sensitive,
                                          current_addr,
                                          &found_addr, &found_offset)) {
                    add_result(&result, found_addr, buffer, found_offset, buf_len);
                    if (max_results > 0 && result.count >= max_results) {
                        free(buffer);
                        free(unicode_pattern);
                        return result;
                    }
                }
            }

            current_addr += read_size;
            remaining -= read_size;
        }

        address += size;
        depth = 0;
        info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
    }

    free(buffer);
    free(unicode_pattern);
    return result;
}

void free_scan_results(ScanResultSet *results) {
    if (!results) return;
    if (results->items) {
        free(results->items);
        results->items = NULL;
    }
    if (results->searched_string) {
        free(results->searched_string);
        results->searched_string = NULL;
    }
    results->count = 0;
    results->capacity = 0;
}

#pragma mark - 任务端口实现

kern_return_t get_task_for_pid(pid_t pid, task_t *task) {
    if (!task) return KERN_INVALID_ARGUMENT;
    return task_for_pid(mach_task_self(), pid, task);
}

void release_task(task_t task) {
    if (task != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), task);
    }
}

#pragma mark - 辅助函数实现

size_t read_process_memory(task_t task, mach_vm_address_t address,
                           void *buffer, size_t size) {
    if (!buffer || size == 0) return 0;

    mach_vm_size_t bytes_read = 0;
    kern_return_t kr = mach_vm_read_overwrite(task, address, size,
                                               (mach_vm_address_t)buffer, &bytes_read);
    if (kr != KERN_SUCCESS) return 0;

    return (size_t)bytes_read;
}

void print_scan_results(const ScanResultSet *results) {
    if (!results || !results->items) {
        printf("No results found.\n");
        return;
    }

    printf("=== Scan Results: \"%s\" ===\n",
           results->searched_string ? results->searched_string : "(null)");
    printf("Total matches: %u\n", results->count);
    printf("----------------------------------------\n");

    for (uint32_t i = 0; i < results->count; i++) {
        const ScanResultItem *item = &results->items[i];
        printf("[%4u] Address: 0x%016llx\n", i + 1, item->address);

        printf("      Hex: ");
        for (size_t j = 0; j < item->context_len && j < 64; j++) {
            printf("%02x ", item->context[j]);
            if (j == 31) printf("| ");
        }
        printf("\n");

        printf("      ASC: ");
        for (size_t j = 0; j < item->context_len && j < 64; j++) {
            char c = (char)item->context[j];
            printf("%c", (c >= 32 && c <= 126) ? c : '.');
            if (j == 31) printf("| ");
        }
        printf("\n\n");
    }
}

char *scan_results_to_json(const ScanResultSet *results) {
    if (!results || !results->items) {
        char *json = (char *)malloc(32);
        if (json) strcpy(json, "{\"count\":0,\"results\":[]}");
        return json;
    }

    size_t json_size = 1024;
    for (uint32_t i = 0; i < results->count; i++) {
        json_size += 128;
        json_size += results->items[i].context_len * 4;
    }

    char *json = (char *)malloc(json_size);
    if (!json) return NULL;

    char *ptr = json;
    int remaining = (int)json_size;

    int written = snprintf(ptr, remaining,
                           "{\"searched_string\":\"%s\",\"count\":%u,\"results\":[",
                           results->searched_string ? results->searched_string : "",
                           results->count);
    if (written > 0) { ptr += written; remaining -= written; }

    for (uint32_t i = 0; i < results->count; i++) {
        const ScanResultItem *item = &results->items[i];

        if (i > 0) {
            written = snprintf(ptr, remaining, ",");
            if (written > 0) { ptr += written; remaining -= written; }
        }

        written = snprintf(ptr, remaining,
                           "{\"address\":\"0x%016llx\",\"context_hex\":\"", item->address);
        if (written > 0) { ptr += written; remaining -= written; }

        for (size_t j = 0; j < item->context_len && j < 64 && remaining > 0; j++) {
            written = snprintf(ptr, remaining, "%02x", item->context[j]);
            if (written > 0) { ptr += written; remaining -= written; }
        }

        written = snprintf(ptr, remaining, "\",\"context_ascii\":\"");
        if (written > 0) { ptr += written; remaining -= written; }

        for (size_t j = 0; j < item->context_len && j < 64 && remaining > 0; j++) {
            char c = (char)item->context[j];
            if (c >= 32 && c <= 126 && c != '"' && c != '\\') {
                *ptr++ = c; remaining--;
            } else {
                written = snprintf(ptr, remaining, "\\x%02x", (unsigned char)c);
                if (written > 0) { ptr += written; remaining -= written; }
            }
        }

        written = snprintf(ptr, remaining, "\"}");
        if (written > 0) { ptr += written; remaining -= written; }
    }

    snprintf(ptr, remaining, "]}");
    return json;
}
