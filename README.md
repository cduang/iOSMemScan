# iOSMemScan - iOS 跨进程内存字符串扫描工具

[![Build](https://github.com/your-username/iOSMemScan/actions/workflows/build.yml/badge.svg)](https://github.com/your-username/iOSMemScan/actions/workflows/build.yml)

**iOSMemScan** 是一款基于 **C 语言/Mach VM API** 编写的 iOS 跨进程内存字符串扫描工具。它允许你在已越狱或安装了 **TrollStore** 的 iOS 设备上，直接读取并搜索任意进程的内存空间，快速定位字符串在内存中的地址。

> ⚠️ **仅用于安全研究与学习**，请勿用于非法用途。

---

## ✨ 功能特点

- **🔍 跨进程内存扫描** — 利用 Mach 内核 API (`mach_vm_region_recurse` / `mach_vm_read_overwrite`) 读取目标进程内存
- **📋 实时进程列表** — 自动获取系统中所有运行进程，按名称排序，支持 3 秒自动刷新
- **🔤 双编码支持** — 同时支持 **ASCII** 和 **UTF-16 LE (Unicode)** 字符串搜索
- **🎯 区域过滤扫描** — 可选择只扫描 `数据段` / `文本段` / `共享库`，提高搜索效率
- **📊 结果交互展示** — 地址、十六进制上下文、ASCII 上下文一目了然，支持点击复制
- **📤 JSON 导出** — 支持将扫描结果导出为 JSON 文件并分享
- **🏗️ TrollStore 原生支持** — 完整的 Entitlements 配置，使用 `ldid` 签名，无缝集成 TrollStore
- **🤖 GitHub Actions 自动构建** — Push/Tag 自动编译打包为 `.ipa`，开箱即用

---

## 📱 系统要求

| 项目 | 要求 |
|------|------|
| **iOS 版本** | 15.0 及以上 |
| **架构** | arm64 (iPhone 6s 及以上) |
| **安装方式** | [TrollStore](https://github.com/opa334/TrollStore) |
| **开发环境** | macOS + Xcode 15+ |

---

## 🚀 安装方式

### 方法一：GitHub Actions 自动构建（推荐）

1. **Fork** 本仓库
2. 进入仓库 **Actions** 标签页
3. 选择 **"Build iOSMemScan (.ipa for TrollStore)"**
4. 点击 **"Run workflow"** → 选择分支 → **Run**
5. 等待构建完成，下载 **Artifacts** 中的 `iOSMemScan-v1.0.zip`
6. 解压得到 `iOSMemScan.ipa`
7. 通过 TrollStore 安装

### 方法二：本地构建

```bash
# 1. 克隆仓库
git clone https://github.com/your-username/iOSMemScan.git
cd iOSMemScan

# 2. 安装 XcodeGen
brew install xcodegen

# 3. 生成 Xcode 项目
cd iOSMemScan
xcodegen generate

# 4. 编译（不需要 Apple 开发者账号）
xcodebuild build \
  -project iOSMemScan.xcodeproj \
  -scheme iOSMemScan \
  -configuration Release \
  -sdk iphoneos \
  CODE_SIGN_IDENTITY="" \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGNING_ALLOWED=NO

# 5. 安装 ldid
# 从 https://github.com/ProcursusTeam/ldid 下载

# 6. 签名
ldid -SResources/Entitlements.plist \
  "build/Release-iphoneos/iOSMemScan.app/iOSMemScan"

# 7. 打包为 IPA
mkdir -p Payload
cp -R "build/Release-iphoneos/iOSMemScan.app" Payload/
zip -qr iOSMemScan.ipa Payload/
```

---

## 🎮 使用说明

### 主界面 — 进程列表

应用启动后会自动显示当前系统中所有正在运行的进程列表，每 3 秒自动刷新。

- **进程名** — 显示进程名称和 PID
- **路径** — 显示可执行文件路径
- **点击** — 进入该进程的内存扫描界面

### 内存扫描界面

选择目标进程后进入扫描界面：

1. **输入搜索字符串** — 在顶部文本框输入要搜索的内容
2. **选择编码模式** — `ASCII` 或 `ASCII+Unicode`（同时搜索 UTF-16 LE）
3. **选择大小写** — 区分/忽略大小写
4. **区域过滤** — 选择要扫描的内存区域类型：
   - **所有可读** — 扫描所有可读内存区域（最全面，但较慢）
   - **数据段** — 仅扫描堆/栈/全局变量区域（最常用）
   - **文本段** — 仅扫描程序代码段
   - **共享库** — 仅扫描动态库映射区域
5. **点击搜索** — 开始扫描，结果将显示在下方列表

### 扫描结果

每个结果项显示：
- **地址** — 字符串在目标进程内存中的虚拟地址（格式: `0xXXXXXXXXXXXXXXXX`）
- **HEX** — 匹配位置前后各 32 字节的十六进制内容
- **ASCII** — 匹配位置前后各 32 字节的 ASCII 可读字符

点击结果项可查看详情，支持：
- 复制地址
- 复制完整上下文
- 导出全部结果为 JSON

---

## 🧠 技术原理

### 核心 API

```
┌─────────────────────────────────────────────────────────────────┐
│                    iOSMemScan 架构图                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                    UI 层 (UIKit)                         │  │
│  │  ┌──────────────┐  ┌──────────────────────────────────┐  │  │
│  │  │ 进程列表      │  │ 扫描界面                        │  │  │
│  │  │ (UITableView)│  │ (TextField + TableView + Export) │  │  │
│  │  └──────┬───────┘  └────────────┬─────────────────────┘  │  │
│  └─────────┼───────────────────────┼────────────────────────┘  │
│            │                       │                           │
│  ┌─────────┴───────────────────────┴────────────────────────┐  │
│  │              C 核心引擎 (MemoryScanner)                  │  │
│  │                                                         │  │
│  │  proc_listallpids()  →  获取进程列表                    │  │
│  │  proc_name()         →  获取进程名                      │  │
│  │  proc_pidpath()      →  获取进程路径                    │  │
│  │  task_for_pid()      →  获取任务端口                    │  │
│  │  mach_vm_region()    →  遍历内存区域                    │  │
│  │  mach_vm_read()      →  读取远程内存                    │  │
│  │  memcmp()            →  字符串匹配                      │  │
│  └─────────────────────────────────────────────────────────┘  │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 关键数据结构

- **`MemoryRegion`** — 描述进程的每个内存区域（地址范围、保护属性、类型）
- **`ScanResultItem`** — 每次匹配的结果（地址 + 上下文数据）
- **`ScanResultSet`** — 完整的扫描结果集合
- **`VM_REGION_SUBMAP_INFO_64`** — Mach 内核的内存区域信息结构

### TrollStore 兼容性

TrollStore 利用 iOS 的 CoreTrust 漏洞，允许安装使用任意 Entitlements 的 `.ipa` 文件。本项目利用这一特性申请了 `com.apple.system-task-sport`、`com.apple.private.memorystatus` 等关键权限，使 App 能够调用 `task_for_pid()` 获取其他进程的任务端口。

---

## 📁 项目结构

```
iOSMemScan/
├── .github/workflows/
│   └── build.yml              # GitHub Actions 自动构建配置
├── iOSMemScan/
│   ├── main.m                  # 应用入口
│   ├── AppDelegate.h/m         # 应用委托
│   ├── Core/
│   │   ├── MemoryScanner.h     # 扫描引擎头文件（C API）
│   │   └── MemoryScanner.c     # 扫描引擎实现（Mach VM API）
│   ├── ViewControllers/
│   │   ├── ProcessListController.h/m  # 进程列表界面
│   │   └── ScanViewController.h/m     # 扫描主界面
│   ├── Resources/
│   │   ├── Info.plist          # 应用配置
│   │   ├── Entitlements.plist  # TrollStore 权限
│   │   └── LaunchScreen.storyboard  # 启动页面
│   └── Views/                  # 自定义视图（预留）
├── project.yml                 # XcodeGen 项目配置
└── README.md                   # 本文件
```

---

## ⚙️ 自定义构建

### 修改 Bundle Identifier

编辑 [`project.yml`](project.yml) 中的 `PRODUCT_BUNDLE_IDENTIFIER`：

```yaml
settings:
  base:
    PRODUCT_BUNDLE_IDENTIFIER: com.yourname.memscan
```

### 添加更多 Entitlements

编辑 [`Resources/Entitlements.plist`](iOSMemScan/Resources/Entitlements.plist) 添加需要的权限。

### 调整扫描限制

在 [`ScanViewController.m`](iOSMemScan/ViewControllers/ScanViewController.m) 中修改 `maxResults` 变量：

```objc
uint32_t maxResults = 5000; // 增加最大结果数
```

---

## 🔒 安全注意事项

- ⚠️ 本工具**仅限安全研究和学习**使用
- ⚠️ 在没有授权的情况下扫描其他进程的内存可能违反相关法律法规
- ⚠️ 请仅在你自己拥有的设备上使用
- ⚠️ 扫描系统关键进程（如 `kernel_task`）可能导致系统不稳定

---

## 📜 许可证

本项目基于 **MIT License** 开源。

```
MIT License

Copyright (c) 2024 iOSMemScan

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files...
```

---

## 🙏 致谢

- [TrollStore](https://github.com/opa334/TrollStore) — 永久签名工具
- [Procursus ldid](https://github.com/ProcursusTeam/ldid) — iOS 签名工具
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) — 项目文件生成器
- Apple Mach Kernel API 文档
