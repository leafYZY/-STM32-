// 顶层构建文件：只声明插件版本，不在这里引入依赖
//
// 版本组合说明（重要）：
//   Gradle 8.11.1 + AGP 8.7.3 + Kotlin 2.0.21  —— 适配 Android Studio Ladybug(2024.2)
//   及之后的版本，兼容 JDK 17 ~ 23。
//   如果你的 Android Studio 打开后提示版本不匹配，见 README「方式 B：直接打开本工程」。
plugins {
    id("com.android.application") version "8.7.3" apply false
    id("org.jetbrains.kotlin.android") version "2.0.21" apply false
}
