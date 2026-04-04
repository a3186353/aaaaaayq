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

        NSURL *url = [NSURL URLWithString:@"http://captive.apple.com"];
        NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.timeoutIntervalForRequest = 5.0;
        config.timeoutIntervalForResource = 5.0;

        NSURLSession *session = [NSURLSession sessionWithConfiguration:config];
        NSURLSessionDataTask *task = [session dataTaskWithURL:url
            completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
                if (error) {
                    NSLog(@"[gge.core] Network trigger error: %@", error.localizedDescription);
                } else {
                    NSLog(@"[gge.core] Network permission granted.");
                }
            }];
        [task resume];

        // 不需要阻塞主线程等待结果，因为弹窗本身是异步的
        // 且此网络请求的成功与否（如连不上apple等）并不代表用户拒绝了权限
        return 1;
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
