//
//  MemoryScanner.h
//  iOSMemScan
//
//  iOS 跨进程内存字符串扫描引擎
//  基于 Mach 内核 API 实现
//

#ifndef MemoryScanner_h
#define MemoryScanner_h

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <mach/mach.h>

#pragma mark - 数据结构

/// 内存区域信息
typedef struct {
    mach_vm_address_t start_addr;   ///< 起始地址
    mach_vm_address_t end_addr;     ///< 结束地址
    mach_vm_size_t    size;         ///< 区域大小
    vm_prot_t         protection;   ///< 内存保护属性
    boolean_t         is_shared;    ///< 是否共享内存
    boolean_t         is_mapped;    ///< 是否已映射文件
    char              region_type;  ///< 区域类型: D=数据, T=文本, S=共享库, ?=未知
} MemoryRegion;

/// 扫描结果项
typedef struct {
    mach_vm_address_t address;      ///< 匹配地址
    uint8_t           context[64];  ///< 上下文数据（匹配位置前后各32字节）
    size_t            context_len;  ///< 上下文有效长度
} ScanResultItem;

/// 扫描结果集合
typedef struct {
    ScanResultItem *items;         ///< 结果数组
    uint32_t        count;         ///< 结果数量
    uint32_t        capacity;      ///< 数组容量
    char           *searched_string; ///< 搜索的字符串
} ScanResultSet;

#pragma mark - 进程操作

/// 获取系统中所有进程的列表
/// @param count 输出参数，进程数量
/// @return 进程PID数组，需要在用完后调用 free()
pid_t *get_process_list(uint32_t *count);

/// 获取进程名称
/// @param pid 进程ID
/// @param buffer 输出缓冲区
/// @param buffer_size 缓冲区大小
/// @return 成功返回 true
bool get_process_name(pid_t pid, char *buffer, size_t buffer_size);

/// 获取进程可执行文件的完整路径
/// @param pid 进程ID
/// @param buffer 输出缓冲区
/// @param buffer_size 缓冲区大小
/// @return 成功返回 true
bool get_process_path(pid_t pid, char *buffer, size_t buffer_size);

#pragma mark - 内存遍历

/// 获取目标进程的内存区域列表
/// @param task 目标进程的任务端口
/// @param regions 输出参数，MemoryRegion 数组，用完后调用 free()
/// @param count 输出参数，区域数量
/// @return KERN_SUCCESS 表示成功
kern_return_t get_memory_regions(task_t task, MemoryRegion **regions, uint32_t *count);

/// 打印内存区域信息（调试用）
void print_memory_region(const MemoryRegion *region);

#pragma mark - 内存扫描

/// 在目标进程的内存中扫描字符串
/// @param task 目标进程的任务端口
/// @param target_string 要搜索的字符串（UTF-8编码）
/// @param case_sensitive 是否区分大小写
/// @param match_unicode 是否同时搜索 Unicode (UTF-16 LE) 编码
/// @param max_results 最大结果数（0=无限制）
/// @param region_filter 区域类型过滤器，NULL表示所有可读区域
/// @return ScanResultSet 结构体，用完后调用 free_scan_results() 释放
ScanResultSet scan_process_memory(task_t           task,
                                  const char      *target_string,
                                  bool             case_sensitive,
                                  bool             match_unicode,
                                  uint32_t         max_results,
                                  const char      *region_filter);

/// 释放扫描结果
void free_scan_results(ScanResultSet *results);

#pragma mark - 任务端口

/// 通过 PID 获取任务端口（需要适当 Entitlement）
/// @param pid 进程ID
/// @param task 输出参数，任务端口
/// @return KERN_SUCCESS 表示成功
kern_return_t get_task_for_pid(pid_t pid, task_t *task);

/// 释放任务端口
void release_task(task_t task);

#pragma mark - 辅助函数

/// 读取目标进程指定地址的内存
/// @param task 目标进程的任务端口
/// @param address 要读取的地址
/// @param size 要读取的大小
/// @param buffer 输出缓冲区（由调用者分配）
/// @return 实际读取的字节数
size_t read_process_memory(task_t task, mach_vm_address_t address,
                           void *buffer, size_t size);

/// 打印扫描结果
void print_scan_results(const ScanResultSet *results);

/// 将结果导出为 JSON 字符串
/// @param results 扫描结果
/// @return 需要调用 free() 释放的 JSON 字符串
char *scan_results_to_json(const ScanResultSet *results);

#endif /* MemoryScanner_h */
