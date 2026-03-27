/*
 * ios_network.m — iOS 网络权限请求 (嵌入 libggelua)
 *
 * 自签 App 使用纯 BSD Socket 时，iOS 不弹出网络权限弹窗，
 * 必须通过 NSURLSession 高层 API 触发系统权限请求。
 *
 * 直接使用标准 Lua API（无需 lua_proxy）。
 * 由 core.c 通过弱引用注册到 gge.core.request_network()
 */
#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#include "lua.h"
#include "lauxlib.h"

static int ios_trigger_network_permission(void)
{
    @autoreleasepool {
        __block int result = 0;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);

        NSURL *url = [NSURL URLWithString:@"http://captive.apple.com"];
        NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.timeoutIntervalForRequest = 5.0;
        config.timeoutIntervalForResource = 5.0;

        NSURLSession *session = [NSURLSession sessionWithConfiguration:config];
        NSURLSessionDataTask *task = [session dataTaskWithURL:url
            completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                if (error) {
                    NSLog(@"[gge.core] Network trigger error: %@", error.localizedDescription);
                    result = 0;
                } else {
                    NSLog(@"[gge.core] Network permission granted.");
                    result = 1;
                }
                dispatch_semaphore_signal(sem);
            }];
        [task resume];

        long timeout = dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
        if (timeout != 0) {
            NSLog(@"[gge.core] Network permission request timed out.");
        }
        return result;
    }
}

/* Lua 绑定: gge.core.request_network() */
int lua_gge_request_network(lua_State* L)
{
    int result = ios_trigger_network_permission();
    lua_pushboolean(L, result);
    return 1;
}

#endif /* __APPLE__ */
