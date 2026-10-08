import com.vanniktech.maven.publish.AndroidSingleVariantLibrary
import com.vanniktech.maven.publish.JavadocJar
import com.vanniktech.maven.publish.SourcesJar
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    id("com.vanniktech.maven.publish")
}

// The version lives in the public C header, as it does for every other build of the library.
val header = rootDir.resolve("../../include/speechwarp.h").readText()
val libraryVersion = Regex("#define SPEECHWARP_VERSION \"(.*)\"").find(header)!!.groupValues[1]

group = "io.github.fredgaffey"
version = libraryVersion

android {
    namespace = "io.github.fredgaffey.speechwarp"
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
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_11)
    }
}

// The native code carries Sonic, Speedy and KISS FFT, so the licences travel in the AAR (inside classes.jar).
val licenceFiles = layout.buildDirectory.dir("licence-resources")
val collectLicences by tasks.registering(Sync::class) {
    into(licenceFiles)
    from(rootDir.resolve("../..")) {
        include("LICENSE", "NOTICE")
        into("META-INF/licenses/speechwarp")
    }
    from(rootDir.resolve("../../third_party")) {
        include("*/LICENSE", "kissfft/COPYING", "kissfft/LICENSES/*")
        into("META-INF/licenses/speechwarp/third_party")
    }
}
android.sourceSets.getByName("main").resources.srcDir(licenceFiles)
tasks.matching { it.name.endsWith("JavaRes") }.configureEach { dependsOn(collectLicences) }

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

// Published to Maven Central as io.github.fredgaffey:speechwarp-android (the desktop JAR is
// io.github.fredgaffey:speechwarp). The sources and javadoc JARs and the POM details are what Central requires.
mavenPublishing {
    configure(AndroidSingleVariantLibrary(variant = "release", sourcesJar = SourcesJar.Sources(), javadocJar = JavadocJar.Javadoc()))
    coordinates("io.github.fredgaffey", "speechwarp-android", libraryVersion)
    publishToMavenCentral(automaticRelease = true)
    pom {
        name.set("speechwarp-android")
        description.set("Nonlinear speed-up for speech on Android: listen faster and still follow it")
        inceptionYear.set("2026")
        url.set("https://github.com/fredgaffey/speechwarp")
        licenses {
            license {
                name.set("Apache-2.0")
                url.set("https://www.apache.org/licenses/LICENSE-2.0.txt")
                distribution.set("repo")
            }
        }
        developers {
            developer {
                id.set("fredgaffey")
                name.set("Fred Gaffey")
                url.set("https://github.com/fredgaffey")
            }
        }
        scm {
            url.set("https://github.com/fredgaffey/speechwarp")
            connection.set("scm:git:https://github.com/fredgaffey/speechwarp.git")
            developerConnection.set("scm:git:ssh://git@github.com/fredgaffey/speechwarp.git")
        }
    }

    // Maven Central needs signed files, but a local build has no key. The release workflow supplies one
    // (SIGNING_KEY and SIGNING_PASSWORD) as the signingInMemoryKey properties; without it nothing is signed, so
    // publishToMavenLocal still works.
    if (providers.gradleProperty("signingInMemoryKey").isPresent) {
        signAllPublications()
    }
}
