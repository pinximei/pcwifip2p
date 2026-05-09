// AGP 8.1 + Gradle 8.5 + Kotlin 1.9 — matches Android Studio Iguana/Jellyfish.
// Requires JDK 17+; we use Android Studio's bundled JBR 21 via local.properties.
plugins {
    id("com.android.application") version "8.2.2" apply false
    id("org.jetbrains.kotlin.android") version "1.9.22" apply false
}
