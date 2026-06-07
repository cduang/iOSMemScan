//
//  ScanViewController.h
//  iOSMemScan
//
//  内存扫描主界面 - 头文件
//

#import <UIKit/UIKit.h>

@interface ScanViewController : UIViewController

/// 初始化扫描视图控制器
/// @param pid 目标进程 ID
/// @param processName 目标进程名称
- (instancetype)initWithPid:(pid_t)pid processName:(NSString *)processName;

@end
