plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.serialization")
    id("org.jetbrains.kotlin.plugin.compose")
}

val defaultGoogleOauthServerClientId =
    "650357306759-j0d85qpgcs3olou6nfbkjb3je35fdpse.apps.googleusercontent.com"
val googleOauthServerClientId = providers.gradleProperty("MAHO_GOOGLE_OAUTH_SERVER_CLIENT_ID")
    .orElse(defaultGoogleOauthServerClientId)

// Release signing is supplied entirely from the environment, so no key material is ever committed.
// All four values must be present together; a partially-set environment is treated as unset rather
// than producing a half-configured signing block.
val releaseKeystorePath = providers.environmentVariable("MAHO_ANDROID_KEYSTORE_PATH").orNull
val releaseKeystorePassword = providers.environmentVariable("MAHO_ANDROID_KEYSTORE_PASSWORD").orNull
val releaseKeyAlias = providers.environmentVariable("MAHO_ANDROID_KEY_ALIAS").orNull
val releaseKeyPassword = providers.environmentVariable("MAHO_ANDROID_KEY_PASSWORD").orNull
val releaseSigningConfigured = listOf(
    releaseKeystorePath,
    releaseKeystorePassword,
    releaseKeyAlias,
    releaseKeyPassword,
).all { !it.isNullOrBlank() }

// Building a release artifact without a signing key would produce an unsigned bundle, which the Play
// Console rejects at upload time - far too late to be useful. Fail at configuration instead, and say
// exactly what is missing. Debug builds are unaffected.
gradle.taskGraph.whenReady {
    val wantsReleaseArtifact = allTasks.any { task ->
        task.name.startsWith("assemble") || task.name.startsWith("bundle")
    } && allTasks.any { task -> task.name.contains("Release") }
    check(!wantsReleaseArtifact || releaseSigningConfigured) {
        "Release signing is not configured. Set MAHO_ANDROID_KEYSTORE_PATH, " +
            "MAHO_ANDROID_KEYSTORE_PASSWORD, MAHO_ANDROID_KEY_ALIAS and " +
            "MAHO_ANDROID_KEY_PASSWORD (all four) before building a release artifact."
    }
}

android {
    namespace = "dev.maho.browser"
    compileSdk = 35

    defaultConfig {
        applicationId = "dev.maho.browser"
        minSdk = 26
        targetSdk = 35
        // Keep these in step with maho/version.txt, which is the canonical product version used by
        // the desktop builds. A mismatch ships a mobile release that claims to be older than the
        // desktop build of the same commit.
        versionCode = providers.gradleProperty("versionCode").orNull?.toInt() ?: 20261014
        versionName = "2026.10.14"
        buildConfigField(
            "String",
            "GOOGLE_OAUTH_SERVER_CLIENT_ID",
            "\"${googleOauthServerClientId.get()}\"",
        )

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    signingConfigs {
        if (releaseSigningConfigured) {
            create("release") {
                storeFile = file(releaseKeystorePath!!)
                storePassword = releaseKeystorePassword
                keyAlias = releaseKeyAlias
                keyPassword = releaseKeyPassword
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("release")
            isMinifyEnabled = true
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

    buildFeatures {
        compose = true
        buildConfig = true
    }

    testOptions {
        unitTests {
            isIncludeAndroidResources = true
        }
    }

    sourceSets {
        getByName("main") {
            jniLibs.srcDirs("src/main/jniLibs")
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation("androidx.work:work-runtime-ktx:2.9.1")
    // Compose BOM
    val composeBom = platform("androidx.compose:compose-bom:2024.06.00")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")

    // Core
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.activity:activity-compose:1.8.2")
    // Declared explicitly, not left transitive: androidx.activity's
    // InvalidFragmentVersionForActivityResult lint check fails the release build when it cannot see a
    // concrete fragment version, and registerForActivityResult is used in MahoAppUpdateManager.
    implementation("androidx.fragment:fragment:1.6.2")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.7.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.7.0")
    implementation("androidx.lifecycle:lifecycle-process:2.7.0")
    implementation("androidx.biometric:biometric:1.1.0")

    // Browser
    implementation("androidx.browser:browser:1.7.0")
    implementation("androidx.webkit:webkit:1.11.0")

    // Google identity (Credential Manager)
    implementation("androidx.credentials:credentials:1.3.0")
    implementation("androidx.credentials:credentials-play-services-auth:1.3.0")
    implementation("com.google.android.libraries.identity.googleid:googleid:1.1.1")

    // Play Core in-app updates
    implementation("com.google.android.play:app-update:2.1.0")
    implementation("com.google.android.play:app-update-ktx:2.1.0")

    // CameraX
    val cameraVersion = "1.3.4"
    implementation("androidx.camera:camera-core:$cameraVersion")
    implementation("androidx.camera:camera-camera2:$cameraVersion")
    implementation("androidx.camera:camera-lifecycle:$cameraVersion")
    implementation("androidx.camera:camera-view:$cameraVersion")

    // Glance widget
    implementation("androidx.glance:glance-appwidget:1.1.1")

    // Networking
    implementation("com.squareup.okhttp3:okhttp:4.12.0")

    // Serialization
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.6.2")

    // Testing
    testImplementation("junit:junit:4.13.2")
    testImplementation("com.squareup.okhttp3:mockwebserver:4.12.0")
    testImplementation("org.robolectric:robolectric:4.11.1")
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.5.1")
    androidTestImplementation(composeBom)
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")

    // Debug
    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
