# GGELUA ProGuard Rules
# 保留所有 native 方法
-keepclasseswithmembernames class * {
    native <methods>;
}

# 保留 SDL Activity 及相关类
-keep class org.libsdl.app.** { *; }
-keep class com.GGELUA.game.** { *; }
