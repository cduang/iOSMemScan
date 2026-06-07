//
//  MemoryScanner.c
//  iOSMemScan
//
//  iOS 跨进程内存字符串扫描引擎 - 实现
//  使用 Mach VM API 直接访问目标进程内存
//

#include "MemoryScanner.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libproc.h>
#include <sys/sysctl.h>
#include <mach/mach_vm.h>
#include <mach/thread_act.h>

#pragma mark - 进程操作实现

pid_t *get_process_list(uint32_t *count) {
    if (!count) return NULL;
    
    int32_t *pid_list = NULL;
    int32_t pid_count = 0;
    int32_t pid_capacity = 1024;
    
    pid_list = (int32_t *)malloc(sizeof(int32_t) * pid_capacity);
    if (!pid_list) return NULL;
    
    // 使用 proc_listallpids 获取所有 PID
    int actual_count = proc_listallpids((int *)pid_list, (int)(pid_capacity * sizeof(int32_t)));
    if (actual_count <= 0) {
        free(pid_list);
        *count = 0;
        return NULL;
    }
    
    // 如果容量不够，重新分配
    if (actual_count >= pid_capacity) {
        pid_capacity = actual_count + 256;
        int32_t *new_list = (int32_t *)realloc(pid_list, sizeof(int32_t) * pid_capacity);
        if (!new_list) {
            free(pid_list);
            *count = 0;
            return NULL;
        }
        pid_list = new_list;
        actual_count = proc_listallpids((int *)pid_list, (int)(pid_capacity * sizeof(int32_t)));
        if (actual_count <= 0) {
            free(pid_list);
            *count = 0;
            return NULL;
        }
    }
    
    *count = (uint32_t)actual_count;
    return (pid_t *)pid_list;
}

bool get_process_name(pid_t pid, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return false;
    
    // 使用 proc_name 获取进程名
    int ret = proc_name(pid, buffer, (uint32_t)buffer_size);
    if (ret <= 0) {
        // 备用方法：通过 sysctl
        struct kinfo_proc info;
        size_t info_size = sizeof(info);
        int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
        
        if (sysctl(mib, 4, &info, &info_size, NULL, 0) == 0) {
            strncpy(buffer, info.kp_proc.p_comm, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            return true;
        }
        buffer[0] = '\0';
        return false;
    }
    buffer[buffer_size - 1] = '\0';
    return true;
}

bool get_process_path(pid_t pid, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return false;
    
    // 使用 proc_pidpath 获取完整路径
    int ret = proc_pidpath(pid, buffer, (uint32_t)buffer_size);
    if (ret <= 0) {
        // 备用方法
        struct proc_bsdshortinfo info;
        int ret2 = proc_pidinfo(pid, PROC_PIDT_SHORTBSDINFO, 0, &info, sizeof(info));
        if (ret2 == sizeof(info)) {
            strncpy(buffer, info.pbsi_comm, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            return true;
        }
        buffer[0] = '\0';
        return false;
    }
    buffer[buffer_size - 1] = '\0';
    return true;
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
    
    kern_return_t kr;
    
    while (true) {
        // 遍历进程的 VM 区域
        kr = mach_vm_region_recurse(task, &address, &size, &depth,
                                    (vm_region_recurse_info_t)&info, &info_count);
        if (kr != KERN_SUCCESS) {
            if (kr == KERN_NO_SPACE) break; // 遍历完毕
            break;
        }
        
        // 跳过子映射（避免嵌套遍历重复区域）
        if (info.is_submap) {
            depth++;
            continue;
        }
        
        // 扩容
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
        
        // 判断区域类型
        if (info.is_submap) {
            region->region_type = '?';
        } else if (info.is_mapped_file_share) {
            region->region_type = 'S'; // 共享库/文件映射
        } else if ((info.protection & VM_PROT_EXECUTE) && !(info.protection & VM_PROT_WRITE)) {
            region->region_type = 'T'; // 可执行文本段
        } else if ((info.protection & VM_PROT_READ) && (info.protection & VM_PROT_WRITE)) {
            region->region_type = 'D'; // 数据段
        } else {
            region->region_type = '?';
        }
        
        region_count++;
        
        // 移动到下一个区域
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
           region->start_addr,
           region->end_addr,
           region->size,
           (region->protection & VM_PROT_READ)  ? 'R' : '-',
           (region->protection & VM_PROT_WRITE) ? 'W' : '-',
           (region->protection & VM_PROT_EXECUTE)? 'X' : '-',
           region->is_shared ? "YES" : "NO",
           region->is_mapped ? "YES" : "NO");
}

#pragma mark - 内存扫描实现

// 辅助函数：将字符转换为小写
static inline char to_lower_char(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 0x20) : c;
}

/// 在给定缓冲区中搜索字符串
static bool match_buffer(const uint8_t *buf, size_t buf_len,
                         const char *pattern, size_t pattern_len,
                         bool case_sensitive) {
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
        if (found) return true;
    }
    return false;
}

/// 在给定缓冲区中搜索字符串（返回第一个匹配位置）
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

/// 将结果添加到集合中
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
    
    // 复制上下文数据（匹配位置前后各32字节）
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
    
    // 保存搜索字符串副本
    size_t target_len = strlen(target_string);
    result.searched_string = (char *)malloc(target_len + 1);
    if (!result.searched_string) return result;
    strncpy(result.searched_string, target_string, target_len);
    result.searched_string[target_len] = '\0';
    
    // 如果是 Unicode 模式，构建 UTF-16 LE 版本的搜索字符串
    uint8_t *unicode_pattern = NULL;
    size_t unicode_pattern_len = 0;
    if (match_unicode) {
        unicode_pattern_len = target_len * 2;
        unicode_pattern = (uint8_t *)malloc(unicode_pattern_len);
        if (unicode_pattern) {
            for (size_t i = 0; i < target_len; i++) {
                unicode_pattern[i * 2] = (uint8_t)target_string[i];
                unicode_pattern[i * 2 + 1] = 0x00; // UTF-16 LE 低字节序
            }
        }
    }
    
    // 遍历内存区域
    mach_vm_address_t address = 0;
    mach_vm_size_t size = 0;
    natural_t depth = 0;
    struct vm_region_submap_info_64 info;
    mach_msg_type_number_t info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
    
    // 页面大小的读取缓冲区
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
        
        // 检查区域过滤器
        if (region_filter) {
            char filter_type = region_filter[0];
            bool pass = false;
            switch (filter_type) {
                case 'D': // 数据段（可读写）
                    pass = (info.protection & VM_PROT_READ) && (info.protection & VM_PROT_WRITE);
                    break;
                case 'T': // 文本段（可执行，不可写）
                    pass = (info.protection & VM_PROT_EXECUTE) && !(info.protection & VM_PROT_WRITE);
                    break;
                case 'S': // 共享库
                    pass = info.is_mapped_file_share;
                    break;
                case 'R': // 所有可读
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
            // 默认只扫描可读区域
            if (!(info.protection & VM_PROT_READ)) {
                address += size;
                depth = 0;
                info_count = VM_REGION_SUBMAP_INFO_COUNT_64;
                continue;
            }
        }
        
        // 分段读取并扫描
        mach_vm_address_t current_addr = address;
        mach_vm_size_t remaining = size;
        
        while (remaining > 0) {
            mach_vm_size_t read_size = (remaining < READ_SIZE) ? remaining : READ_SIZE;
            
            // 读取目标进程内存
            mach_vm_size_t bytes_read = 0;
            kr = mach_vm_read_overwrite(task, current_addr, read_size,
                                         (mach_vm_address_t)buffer, &bytes_read);
            if (kr != KERN_SUCCESS) {
                current_addr += read_size;
                remaining -= read_size;
                continue;
            }
            
            size_t buf_len = (size_t)bytes_read;
            
            // 扫描 ASCII 字符串
            mach_vm_address_t found_addr = 0;
            size_t found_offset = 0;
            
            if (find_string_in_buffer(buffer, buf_len,
                                      target_string, target_len,
                                      case_sensitive,
                                      current_addr,
                                      &found_addr, &found_offset)) {
                add_result(&result, found_addr, buffer, found_offset, buf_len);
                
                // 检查是否达到最大结果数
                if (max_results > 0 && result.count >= max_results) {
                    free(buffer);
                    free(unicode_pattern);
                    return result;
                }
            }
            
            // 扫描 Unicode (UTF-16 LE) 字符串
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
    
    // 使用 task_for_pid 获取目标进程的任务端口
    // 需要 com.apple.system-task-sport 或类似 entitlement
    // TrollStore 安装的 App 通常有这些权限
    kern_return_t kr = task_for_pid(mach_task_self(), pid, task);
    return kr;
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
        
        // 打印十六进制上下文
        printf("      Hex: ");
        for (size_t j = 0; j < item->context_len && j < 64; j++) {
            printf("%02x ", item->context[j]);
            if (j == 31) printf("| ");
        }
        printf("\n");
        
        // 打印 ASCII 上下文
        printf("      ASC: ");
        for (size_t j = 0; j < item->context_len && j < 64; j++) {
            char c = (char)item->context[j];
            if (c >= 32 && c <= 126) {
                printf("%c", c);
            } else {
                printf(".");
            }
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
    
    // 估算 JSON 大小
    size_t json_size = 1024; // 基础大小
    for (uint32_t i = 0; i < results->count; i++) {
        json_size += 128; // 每个结果约 128 字节
        json_size += results->items[i].context_len * 4; // hex 编码
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
                           "{\"address\":\"0x%016llx\",\"context_hex\":\"",
                           item->address);
        if (written > 0) { ptr += written; remaining -= written; }
        
        // Hex 编码上下文
        for (size_t j = 0; j < item->context_len && j < 64 && remaining > 0; j++) {
            written = snprintf(ptr, remaining, "%02x", item->context[j]);
            if (written > 0) { ptr += written; remaining -= written; }
        }
        
        written = snprintf(ptr, remaining, "\",\"context_ascii\":\"");
        if (written > 0) { ptr += written; remaining -= written; }
        
        // ASCII 编码上下文
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
