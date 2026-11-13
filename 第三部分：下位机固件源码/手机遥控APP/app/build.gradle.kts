plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.yeziyi.garageble"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.yeziyi.garageble"
        minSdk = 26
        targetSdk = 34
        versionCode = 7
        versionName = "2.2"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // 说明：minSdk 定为 26，一是自适应图标可以用纯矢量实现（不依赖 PNG），
    // 二是 Android 8.0 及以上手机占比已经非常高。
    // 如果必须支持 7.x，把 minSdk 改成 24，并把 mipmap-anydpi-v26 图标补充为
    // 普通 mipmap-*dpi 的 PNG 即可（对本功能没有影响）。

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        viewBinding = true
    }

    lint {
        // 把 lint 的非致命问题降级，避免第一次 build 被 lint 中断
        abortOnError = false
        checkReleaseBuilds = false
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("com.google.android.material:material:1.12.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("androidx.recyclerview:recyclerview:1.3.2")
    implementation("androidx.activity:activity-ktx:1.9.2")
}
