//
//  ProcessListController.m
//  iOSMemScan
//
//  进程列表视图控制器 - 实现
//  获取系统进程列表并显示，支持点击进入内存扫描
//

#import "ProcessListController.h"
#import "ScanViewController.h"
#include "../Core/MemoryScanner.h"

@interface ProcessListController ()

@property (nonatomic, strong) NSMutableArray<NSDictionary *> *processes;
@property (nonatomic, strong) UIActivityIndicatorView *loadingIndicator;
@property (nonatomic, strong) UIRefreshControl *refreshControl;
@property (nonatomic, strong) NSTimer *refreshTimer;

@end

@implementation ProcessListController

#pragma mark - 生命周期

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.title = @"进程列表";
    
    // 设置表格样式
    self.tableView.backgroundColor = [UIColor blackColor];
    self.tableView.separatorStyle = UITableViewCellSeparatorStyleSingleLine;
    self.tableView.separatorColor = [UIColor colorWithWhite:0.2 alpha:1.0];
    self.tableView.rowHeight = 60;
    
    // 加载指示器
    self.loadingIndicator = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleLarge];
    self.loadingIndicator.color = [UIColor whiteColor];
    self.loadingIndicator.center = self.view.center;
    self.loadingIndicator.hidesWhenStopped = YES;
    [self.view addSubview:self.loadingIndicator];
    
    // 下拉刷新
    self.refreshControl = [[UIRefreshControl alloc] init];
    self.refreshControl.tintColor = [UIColor whiteColor];
    [self.refreshControl addTarget:self action:@selector(refreshProcessList) forControlEvents:UIControlEventValueChanged];
    [self.tableView addSubview:self.refreshControl];
    
    // 导航栏按钮
    UIBarButtonItem *infoButton = [[UIBarButtonItem alloc] initWithTitle:@"关于"
                                                                   style:UIBarButtonItemStylePlain
                                                                  target:self
                                                                  action:@selector(showAbout)];
    self.navigationItem.rightBarButtonItem = infoButton;
    
    // 初始加载
    [self refreshProcessList];
    
    // 定时刷新（每3秒）
    self.refreshTimer = [NSTimer scheduledTimerWithTimeInterval:3.0
                                                         target:self
                                                       selector:@selector(refreshProcessList)
                                                       userInfo:nil
                                                        repeats:YES];
}

- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    [self.refreshTimer invalidate];
    self.refreshTimer = nil;
}

- (void)dealloc {
    [self.refreshTimer invalidate];
}

#pragma mark - 进程列表

- (void)refreshProcessList {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        uint32_t count = 0;
        pid_t *pids = get_process_list(&count);
        
        if (!pids || count == 0) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self.refreshControl endRefreshing];
                [self.loadingIndicator stopAnimating];
            });
            return;
        }
        
        NSMutableArray *newProcesses = [NSMutableArray array];
        
        for (uint32_t i = 0; i < count; i++) {
            pid_t pid = pids[i];
            
            char name[256] = {0};
            char path[1024] = {0};
            
            BOOL hasName = get_process_name(pid, name, sizeof(name));
            BOOL hasPath = get_process_path(pid, path, sizeof(path));
            
            if (hasName || hasPath) {
                NSString *nameStr = hasName ? [NSString stringWithUTF8String:name] : @"?";
                NSString *pathStr = hasPath ? [NSString stringWithUTF8String:path] : @"";
                
                // 过滤掉空名称的进程
                if (nameStr.length > 0) {
                    [newProcesses addObject:@{
                        @"pid": @(pid),
                        @"name": [nameStr copy],
                        @"path": [pathStr copy]
                    }];
                }
            }
        }
        
        free(pids);
        
        // 按进程名排序
        [newProcesses sortUsingComparator:^NSComparisonResult(NSDictionary *obj1, NSDictionary *obj2) {
            return [obj1[@"name"] localizedCaseInsensitiveCompare:obj2[@"name"]];
        }];
        
        dispatch_async(dispatch_get_main_queue(), ^{
            self.processes = newProcesses;
            [self.tableView reloadData];
            [self.refreshControl endRefreshing];
            [self.loadingIndicator stopAnimating];
        });
    });
}

#pragma mark - 关于

- (void)showAbout {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"iOSMemScan"
                                                                    message:@"iOS 跨进程内存字符串扫描工具\n\n"
                                                                           "基于 Mach VM API 实现\n"
                                                                           "支持 TrollStore 安装\n\n"
                                                                           "⚠️ 仅用于安全研究"
                                                             preferredStyle:UIAlertControllerStyleAlert];
    
    UIAlertAction *okAction = [UIAlertAction actionWithTitle:@"确定" style:UIAlertActionStyleDefault handler:nil];
    [alert addAction:okAction];
    [self presentViewController:alert animated:YES completion:nil];
}

#pragma mark - Table View 数据源

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return self.processes.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    static NSString *cellId = @"ProcessCell";
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:cellId];
    if (!cell) {
        cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:cellId];
        cell.backgroundColor = [UIColor colorWithWhite:0.1 alpha:1.0];
        cell.textLabel.textColor = [UIColor whiteColor];
        cell.detailTextLabel.textColor = [UIColor colorWithWhite:0.6 alpha:1.0];
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        
        // 选中背景色
        UIView *selectedView = [[UIView alloc] init];
        selectedView.backgroundColor = [UIColor colorWithWhite:0.2 alpha:0.5];
        cell.selectedBackgroundView = selectedView;
    }
    
    NSDictionary *proc = self.processes[indexPath.row];
    pid_t pid = [proc[@"pid"] intValue];
    NSString *name = proc[@"name"];
    NSString *path = proc[@"path"];
    
    cell.textLabel.text = [NSString stringWithFormat:@"[%d] %@", pid, name];
    
    if (path.length > 0) {
        cell.detailTextLabel.text = path;
    } else {
        cell.detailTextLabel.text = @"路径不可用";
    }
    
    return cell;
}

#pragma mark - Table View 委托

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    
    NSDictionary *proc = self.processes[indexPath.row];
    pid_t pid = [proc[@"pid"] intValue];
    NSString *name = proc[@"name"];
    
    ScanViewController *scanVC = [[ScanViewController alloc] initWithPid:pid processName:name];
    [self.navigationController pushViewController:scanVC animated:YES];
}

@end
