
package com.GGELUA.game;

import android.content.Intent;
import org.libsdl.app.SDLActivity;

/**
 * GGEActivity - GGELUA 游戏入口 Activity
 * 覆写 getArguments() 以支持调试模式下通过 Intent Extra 传递 host 参数
 */

public class GGEActivity extends SDLActivity
{
    /**
     * 覆写 SDLActivity.getArguments()，将 Intent Extra 中的 host 参数
     * 作为 argv[2] 传给 C 层，使 ggelua.lua 的 HTTP 资源下载逻辑能够触发。
     *
     * 调试模式调用链：
     * run.lua: adb shell am start -e host 192.168.x.x:12138
     *   → getArguments() 返回 {nativeLibraryDir, host}
     *     → SDLMain.nativeRunMain(lib, func, arguments)
     *       → main.c argv[] → gge.arg[2] = host
     *         → ggelua.lua:424 触发 HTTP 资源下载
     */
    @Override
    protected String[] getArguments() {
        String host = null;
        Intent intent = getIntent();
        if (intent != null) {
            host = intent.getStringExtra("host");
        }
        if (host != null && !host.isEmpty()) {
            return new String[]{
                getApplicationInfo().nativeLibraryDir,
                host
            };
        }
        return new String[]{getApplicationInfo().nativeLibraryDir};
    }
}
