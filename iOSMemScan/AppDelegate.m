//
//  AppDelegate.m
//  iOSMemScan
//
//  Application Delegate 实现
//  配置应用主窗口和导航控制器
//

#import "AppDelegate.h"
#import "ViewControllers/ProcessListController.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
    
    // 设置深色风格
    if (@available(iOS 13.0, *)) {
        self.window.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    }
    
    // 创建进程列表作为根视图
    ProcessListController *processVC = [[ProcessListController alloc] initWithStyle:UITableViewStylePlain];
    UINavigationController *navController = [[UINavigationController alloc] initWithRootViewController:processVC];
    
    // 配置导航栏
    navController.navigationBar.prefersLargeTitles = YES;
    navController.navigationBar.tintColor = [UIColor systemBlueColor];
    
    self.window.rootViewController = navController;
    [self.window makeKeyAndVisible];
    
    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application {
    // 当应用即将从活动状态转为非活动状态时调用
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
    // 应用进入后台
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
    // 应用即将进入前台
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
    // 应用已变为活动状态
}

- (void)applicationWillTerminate:(UIApplication *)application {
    // 应用即将终止
}

@end
