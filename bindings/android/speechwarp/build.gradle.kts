import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    `maven-publish`
}

// The version lives in the public C header, as it does for every other build of the library.
val header = rootDir.resolve("../../include/speechwarp.h").readText()
val libraryVersion = Regex("#define SPEECHWARP_VERSION \"(.*)\"").find(header)!!.groupValues[1]

group = "io.github.freddygaffey"
version = libraryVersion

android {
    namespace = "io.github.freddygaffey.speechwarp"
    compileSdk = 36
    ndkVersion = "27.1.12297006"

    defaultConfig {
        minSdk = 21
        consumerProguardFiles("consumer-rules.pro")
        externalNativeBuild {
            cmake {
                arguments += "-DCMAKE_BUILD_TYPE=Release"
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64", "x86")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_11)
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}

// The unit tests run on this computer's JVM, not on a phone, so they need the native library built for this
// computer. It is the same CMake project the Android build uses.
val hostJniDir = layout.buildDirectory.dir("host-jni")
val buildHostJni by tasks.registering {
    val source = file("src/main/cpp")
    inputs.dir(source)
    inputs.dir(rootDir.resolve("../../src"))
    outputs.dir(hostJniDir)
    doLast {
        val out = hostJniDir.get().asFile
        providers.exec { commandLine("cmake", "-S", source.path, "-B", out.path, "-DCMAKE_BUILD_TYPE=Release") }.result.get()
        providers.exec { commandLine("cmake", "--build", out.path, "--config", "Release") }.result.get()
    }
}
tasks.withType<Test>().configureEach {
    dependsOn(buildHostJni)
    systemProperty("java.library.path", hostJniDir.get().asFile.path)
    systemProperty("speechwarp.header", rootDir.resolve("../../include/speechwarp.h").path)
}

publishing {
    publications {
        register<MavenPublication>("release") {
            artifactId = "speechwarp"
            afterEvaluate { from(components["release"]) }
            pom {
                name.set("speechwarp")
                description.set("Nonlinear speed-up for speech: listen faster and still follow it")
                url.set("https://github.com/freddygaffey/speechwarp")
                licenses {
                    license {
                        name.set("Apache-2.0")
                        url.set("https://www.apache.org/licenses/LICENSE-2.0")
                    }
                }
            }
        }
    }
}
