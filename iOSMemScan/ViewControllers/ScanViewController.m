//
//  ScanViewController.m
//  iOSMemScan
//
//  内存扫描主界面 - 实现
//  提供字符串搜索、结果展示、导出等功能
//

#import "ScanViewController.h"
#include "../Core/MemoryScanner.h"

@interface ScanViewController () <UITableViewDataSource, UITableViewDelegate, UITextFieldDelegate>

// 目标进程信息
@property (nonatomic, assign) pid_t targetPid;
@property (nonatomic, copy) NSString *targetProcessName;
@property (nonatomic, assign) task_t targetTask;

// UI 控件
@property (nonatomic, strong) UITextField *searchField;
@property (nonatomic, strong) UIButton *searchButton;
@property (nonatomic, strong) UISegmentedControl *encodingControl;
@property (nonatomic, strong) UISegmentedControl *caseControl;
@property (nonatomic, strong) UISegmentedControl *regionControl;
@property (nonatomic, strong) UITableView *resultsTableView;
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UIActivityIndicatorView *spinner;
@property (nonatomic, strong) UIButton *exportButton;
@property (nonatomic, strong) UIButton *clearButton;

// 扫描结果
@property (nonatomic, strong) NSMutableArray<NSDictionary *> *results;
@property (nonatomic, assign) BOOL isScanning;
@property (nonatomic, assign) BOOL taskConnected;

@end

@implementation ScanViewController

#pragma mark - 初始化

- (instancetype)initWithPid:(pid_t)pid processName:(NSString *)processName {
    self = [super init];
    if (self) {
        _targetPid = pid;
        _targetProcessName = [processName copy];
        _targetTask = MACH_PORT_NULL;
        _results = [NSMutableArray array];
        _isScanning = NO;
        _taskConnected = NO;
        
        // 尝试连接目标进程
        kern_return_t kr = get_task_for_pid(pid, &_targetTask);
        _taskConnected = (kr == KERN_SUCCESS);
    }
    return self;
}

- (void)dealloc {
    if (_targetTask != MACH_PORT_NULL) {
        release_task(_targetTask);
    }
}

#pragma mark - 视图生命周期

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.title = [NSString stringWithFormat:@"扫描: %@", self.targetProcessName];
    self.view.backgroundColor = [UIColor blackColor];
    
    [self setupUI];
    
    // 显示任务连接状态
    if (!self.taskConnected) {
        [self showAlert:@"连接失败"
                message:[NSString stringWithFormat:@"无法连接到进程 %@ (PID: %d)。\n\n"
                         "可能的原因：\n"
                         "1. 进程已退出\n"
                         "2. 权限不足\n"
                         "3. 需要 TrollStore 安装",
                         self.targetProcessName, self.targetPid]];
    }
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    
    CGFloat safeTop = self.view.safeAreaInsets.top;
    CGFloat width = self.view.bounds.size.width;
    CGFloat padding = 12;
    
    CGFloat y = safeTop + 10;
    
    // 搜索框
    self.searchField.frame = CGRectMake(padding, y, width - padding * 2 - 80, 40);
    self.searchButton.frame = CGRectMake(width - padding - 70, y, 70, 40);
    y += 50;
    
    // 编码选择
    self.encodingControl.frame = CGRectMake(padding, y, (width - padding * 2 - 5) / 2, 32);
    self.caseControl.frame = CGRectMake(padding + (width - padding * 2 - 5) / 2 + 5, y,
                                        (width - padding * 2 - 5) / 2, 32);
    y += 42;
    
    // 区域过滤器
    self.regionControl.frame = CGRectMake(padding, y, width - padding * 2, 32);
    y += 42;
    
    // 状态标签
    self.statusLabel.frame = CGRectMake(padding, y, width - padding * 2 - 100, 24);
    self.exportButton.frame = CGRectMake(width - padding - 80, y, 80, 24);
    y += 34;
    
    // 操作按钮行
    self.clearButton.frame = CGRectMake(padding, y, width - padding * 2, 36);
    y += 46;
    
    // 结果列表
    self.resultsTableView.frame = CGRectMake(0, y, width, self.view.bounds.size.height - y);
    
    // 加载指示器
    self.spinner.center = self.view.center;
}

#pragma mark - UI 设置

- (void)setupUI {
    // 搜索框
    self.searchField = [[UITextField alloc] init];
    self.searchField.placeholder = @"输入要搜索的字符串...";
    self.searchField.textColor = [UIColor whiteColor];
    self.searchField.backgroundColor = [UIColor colorWithWhite:0.15 alpha:1.0];
    self.searchField.borderStyle = UITextBorderStyleRoundedRect;
    self.searchField.autocorrectionType = UITextAutocorrectionTypeNo;
    self.searchField.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.searchField.returnKeyType = UIReturnKeySearch;
    self.searchField.delegate = self;
    self.searchField.clearButtonMode = UITextFieldViewModeWhileEditing;
    self.searchField.leftView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 8, 0)];
    self.searchField.leftViewMode = UITextFieldViewModeAlways;
    [self.view addSubview:self.searchField];
    
    // 搜索按钮
    self.searchButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.searchButton setTitle:@"搜索" forState:UIControlStateNormal];
    [self.searchButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    self.searchButton.backgroundColor = [UIColor systemBlueColor];
    self.searchButton.layer.cornerRadius = 8;
    self.searchButton.titleLabel.font = [UIFont boldSystemFontOfSize:15];
    [self.searchButton addTarget:self action:@selector(startScan) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.searchButton];
    
    // 编码选择
    self.encodingControl = [[UISegmentedControl alloc] initWithItems:@[@"ASCII", @"ASCII+Unicode"]];
    self.encodingControl.selectedSegmentIndex = 1;
    self.encodingControl.tintColor = [UIColor systemBlueColor];
    [self.view addSubview:self.encodingControl];
    
    // 大小写选择
    self.caseControl = [[UISegmentedControl alloc] initWithItems:@[@"区分大小写", @"忽略大小写"]];
    self.caseControl.selectedSegmentIndex = 0;
    self.caseControl.tintColor = [UIColor systemBlueColor];
    [self.view addSubview:self.caseControl];
    
    // 区域过滤
    self.regionControl = [[UISegmentedControl alloc] initWithItems:@[@"所有可读", @"数据段", @"文本段"]];
    self.regionControl.selectedSegmentIndex = 0;
    self.regionControl.tintColor = [UIColor systemBlueColor];
    [self.view addSubview:self.regionControl];
    
    // 状态标签
    self.statusLabel = [[UILabel alloc] init];
    self.statusLabel.textColor = [UIColor colorWithWhite:0.6 alpha:1.0];
    self.statusLabel.font = [UIFont systemFontOfSize:12];
    self.statusLabel.text = @"就绪";
    [self.view addSubview:self.statusLabel];
    
    // 导出按钮
    self.exportButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.exportButton setTitle:@"导出JSON" forState:UIControlStateNormal];
    self.exportButton.titleLabel.font = [UIFont systemFontOfSize:12];
    [self.exportButton addTarget:self action:@selector(exportResults) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.exportButton];
    
    // 清除按钮
    self.clearButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.clearButton setTitle:@"清除结果" forState:UIControlStateNormal];
    self.clearButton.backgroundColor = [UIColor colorWithWhite:0.15 alpha:1.0];
    self.clearButton.layer.cornerRadius = 8;
    [self.clearButton addTarget:self action:@selector(clearResults) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:self.clearButton];
    
    // 结果列表
    self.resultsTableView = [[UITableView alloc] init];
    self.resultsTableView.backgroundColor = [UIColor blackColor];
    self.resultsTableView.dataSource = self;
    self.resultsTableView.delegate = self;
    self.resultsTableView.separatorColor = [UIColor colorWithWhite:0.2 alpha:1.0];
    self.resultsTableView.rowHeight = UITableViewAutomaticDimension;
    self.resultsTableView.estimatedRowHeight = 80;
    [self.view addSubview:self.resultsTableView];
    
    // 加载指示器
    self.spinner = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleLarge];
    self.spinner.color = [UIColor whiteColor];
    self.spinner.hidesWhenStopped = YES;
    self.spinner.hidden = YES;
    [self.view addSubview:self.spinner];
}

#pragma mark - 扫描操作

- (void)startScan {
    if (self.isScanning) return;
    
    NSString *searchString = self.searchField.text;
    if (searchString.length == 0) {
        [self showAlert:@"提示" message:@"请输入要搜索的字符串"];
        return;
    }
    
    if (!self.taskConnected || self.targetTask == MACH_PORT_NULL) {
        // 尝试重新连接
        kern_return_t kr = get_task_for_pid(self.targetPid, &self.targetTask);
        if (kr != KERN_SUCCESS) {
            [self showAlert:@"连接失败" message:@"无法连接到目标进程，请确认进程仍在运行"];
            return;
        }
        self.taskConnected = YES;
    }
    
    [self.results removeAllObjects];
    [self.resultsTableView reloadData];
    self.isScanning = YES;
    
    self.searchButton.enabled = NO;
    [self.searchButton setTitle:@"扫描中..." forState:UIControlStateNormal];
    [self.spinner startAnimating];
    self.statusLabel.text = @"正在扫描内存...";
    
    // 获取搜索选项
    BOOL caseSensitive = (self.caseControl.selectedSegmentIndex == 0);
    BOOL matchUnicode = (self.encodingControl.selectedSegmentIndex == 1);
    uint32_t maxResults = 1000; // 限制最大结果数
    
    // 区域过滤器
    char regionFilter = 0;
    switch (self.regionControl.selectedSegmentIndex) {
        case 0: regionFilter = 0; break;  // 所有可读
        case 1: regionFilter = 'D'; break; // 数据段
        case 2: regionFilter = 'T'; break; // 文本段
    }
    
    const char *filterPtr = (regionFilter != 0) ? &regionFilter : NULL;
    
    // 复制字符串确保安全
    const char *cString = [searchString UTF8String];
    char *searchCopy = strdup(cString ? cString : "");
    
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
        // 执行扫描
        ScanResultSet scanResult = scan_process_memory(self.targetTask,
                                                       searchCopy,
                                                       caseSensitive,
                                                       matchUnicode,
                                                       maxResults,
                                                       filterPtr);
        
        free(searchCopy);
        
        // 将 C 结果转换为 NSArray
        NSMutableArray *resultArray = [NSMutableArray array];
        if (scanResult.items && scanResult.count > 0) {
            for (uint32_t i = 0; i < scanResult.count; i++) {
                ScanResultItem *item = &scanResult.items[i];
                
                // 构建上下文十六进制字符串
                NSMutableString *hexStr = [NSMutableString string];
                NSMutableString *ascStr = [NSMutableString string];
                
                size_t ctxLen = (item->context_len > 64) ? 64 : item->context_len;
                for (size_t j = 0; j < ctxLen; j++) {
                    [hexStr appendFormat:@"%02x ", item->context[j]];
                    
                    char c = (char)item->context[j];
                    if (c >= 32 && c <= 126) {
                        [ascStr appendFormat:@"%c", c];
                    } else {
                        [ascStr appendString:@"."];
                    }
                }
                
                [resultArray addObject:@{
                    @"address": [NSString stringWithFormat:@"0x%016llx", item->address],
                    @"address_num": @(item->address),
                    @"hex": hexStr ?: @"",
                    @"ascii": ascStr ?: @""
                }];
            }
        }
        
        free_scan_results(&scanResult);
        
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.results addObjectsFromArray:resultArray];
            [self.resultsTableView reloadData];
            
            self.isScanning = NO;
            self.searchButton.enabled = YES;
            [self.searchButton setTitle:@"搜索" forState:UIControlStateNormal];
            [self.spinner stopAnimating];
            
            self.statusLabel.text = [NSString stringWithFormat:@"找到 %lu 个匹配结果",
                                     (unsigned long)self.results.count];
            
            if (self.results.count == 0) {
                [self showAlert:@"扫描完成" message:@"未找到匹配结果"];
            }
        });
    });
}

#pragma mark - 结果操作

- (void)clearResults {
    [self.results removeAllObjects];
    [self.resultsTableView reloadData];
    self.statusLabel.text = @"已清除";
}

- (void)exportResults {
    if (self.results.count == 0) {
        [self showAlert:@"提示" message:@"没有结果可导出"];
        return;
    }
    
    // 构建 JSON
    NSMutableDictionary *jsonDict = [NSMutableDictionary dictionary];
    jsonDict[@"target_pid"] = @(self.targetPid);
    jsonDict[@"target_process"] = self.targetProcessName ?: @"";
    jsonDict[@"search_string"] = self.searchField.text ?: @"";
    jsonDict[@"result_count"] = @(self.results.count);
    
    NSMutableArray *resultsArray = [NSMutableArray array];
    for (NSDictionary *item in self.results) {
        [resultsArray addObject:@{
            @"address": item[@"address"] ?: @"",
            @"context_hex": item[@"hex"] ?: @"",
            @"context_ascii": item[@"ascii"] ?: @"",
        }];
    }
    jsonDict[@"results"] = resultsArray;
    
    NSError *error = nil;
    NSData *jsonData = [NSJSONSerialization dataWithJSONObject:jsonDict
                                                       options:NSJSONWritingPrettyPrinted
                                                         error:&error];
    if (error || !jsonData) {
        [self showAlert:@"导出失败" message:error.localizedDescription ?: @"未知错误"];
        return;
    }
    
    // 保存到临时文件
    NSString *fileName = [NSString stringWithFormat:@"MemScan_%@_%d_%ld.json",
                          self.targetProcessName, self.targetPid, (long)[NSDate date].timeIntervalSince1970];
    NSString *filePath = [NSTemporaryDirectory() stringByAppendingPathComponent:fileName];
    
    if ([jsonData writeToFile:filePath atomically:YES]) {
        // 分享
        UIActivityViewController *activityVC = [[UIActivityViewController alloc]
                                                 initWithActivityItems:@[[NSURL fileURLWithPath:filePath]]
                                                 applicationActivities:nil];
        [self presentViewController:activityVC animated:YES completion:nil];
    } else {
        [self showAlert:@"导出失败" message:@"无法写入文件"];
    }
}

#pragma mark - UITextField 委托

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    [textField resignFirstResponder];
    [self startScan];
    return YES;
}

#pragma mark - 辅助方法

- (void)showAlert:(NSString *)title message:(NSString *)message {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:title
                                                                    message:message
                                                             preferredStyle:UIAlertControllerStyleAlert];
    UIAlertAction *okAction = [UIAlertAction actionWithTitle:@"确定"
                                                       style:UIAlertActionStyleDefault
                                                     handler:nil];
    [alert addAction:okAction];
    
    if (self.presentedViewController) {
        [self.presentedViewController dismissViewControllerAnimated:NO completion:nil];
    }
    
    [self presentViewController:alert animated:YES completion:nil];
}

#pragma mark - UITableView 数据源

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    return self.results.count;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    static NSString *cellId = @"ResultCell";
    UITableViewCell *cell = [tableView dequeueReusableCellWithIdentifier:cellId];
    if (!cell) {
        cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:cellId];
        cell.backgroundColor = [UIColor colorWithWhite:0.08 alpha:1.0];
        cell.textLabel.textColor = [UIColor colorWithRed:0.4 green:0.8 blue:0.4 alpha:1.0]; // 绿色
        cell.textLabel.font = [UIFont fontWithName:@"Menlo" size:11] ?: [UIFont systemFontOfSize:11];
        cell.textLabel.numberOfLines = 2;
        cell.detailTextLabel.textColor = [UIColor colorWithWhite:0.7 alpha:1.0];
        cell.detailTextLabel.font = [UIFont fontWithName:@"Menlo" size:10] ?: [UIFont systemFontOfSize:10];
        cell.detailTextLabel.numberOfLines = 3;
        
        // 选中背景色
        UIView *selectedView = [[UIView alloc] init];
        selectedView.backgroundColor = [UIColor colorWithWhite:0.2 alpha:0.5];
        cell.selectedBackgroundView = selectedView;
    }
    
    NSDictionary *item = self.results[indexPath.row];
    cell.textLabel.text = [NSString stringWithFormat:@"📍 %@", item[@"address"]];
    cell.detailTextLabel.text = [NSString stringWithFormat:@"HEX: %@\nASC: %@",
                                  item[@"hex"], item[@"ascii"]];
    
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    [tableView deselectRowAtIndexPath:indexPath animated:YES];
    
    NSDictionary *item = self.results[indexPath.row];
    NSString *message = [NSString stringWithFormat:@"地址: %@\n\n"
                          "HEX: %@\n\n"
                          "ASCII: %@",
                          item[@"address"], item[@"hex"], item[@"ascii"]];
    
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"结果详情"
                                                                    message:message
                                                             preferredStyle:UIAlertControllerStyleAlert];
    
    // 复制地址
    UIAlertAction *copyAction = [UIAlertAction actionWithTitle:@"复制地址" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        UIPasteboard *pasteboard = [UIPasteboard generalPasteboard];
        pasteboard.string = item[@"address"];
    }];
    
    // 复制完整上下文
    UIAlertAction *copyAllAction = [UIAlertAction actionWithTitle:@"复制全部" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        UIPasteboard *pasteboard = [UIPasteboard generalPasteboard];
        pasteboard.string = [NSString stringWithFormat:@"Address: %@\nHex: %@\nASCII: %@",
                              item[@"address"], item[@"hex"], item[@"ascii"]];
    }];
    
    UIAlertAction *dismissAction = [UIAlertAction actionWithTitle:@"关闭" style:UIAlertActionStyleCancel handler:nil];
    
    [alert addAction:copyAction];
    [alert addAction:copyAllAction];
    [alert addAction:dismissAction];
    
    [self presentViewController:alert animated:YES completion:nil];
}

@end
