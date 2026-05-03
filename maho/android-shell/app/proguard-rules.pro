# Maho Browser ProGuard Rules

# Keep JNI bridge
-keep class dev.maho.browser.MahoBridge { *; }

# Keep all model classes used by kotlinx.serialization
-keep class dev.maho.browser.models.** { *; }

# Keep kotlinx.serialization
-keepattributes *Annotation*, InnerClasses
-dontnote kotlinx.serialization.AnnotationsKt
-keepclassmembers class kotlinx.serialization.json.** {
    *** Companion;
}
-keepclasseswithmembers class kotlinx.serialization.json.** {
    kotlinx.serialization.KSerializer serializer(...);
}
-keepclassmembers @kotlinx.serialization.Serializable class dev.maho.browser.** {
    *** Companion;
    *** INSTANCE;
    kotlinx.serialization.KSerializer serializer(...);
}

# Keep Compose
-dontwarn androidx.compose.**
