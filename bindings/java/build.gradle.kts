plugins {
    `java-library`
    `maven-publish`
}

// The version lives in the public C header, as it does for every other build of the library.
val header = rootDir.resolve("../../include/speechwarp.h").readText()
group = "io.github.freddygaffey"
version = Regex("#define SPEECHWARP_VERSION \"(.*)\"").find(header)!!.groupValues[1]

repositories {
    mavenCentral()
}

java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(providers.gradleProperty("speechwarp.jdk").getOrElse("21")))
    }
    withSourcesJar()
    withJavadocJar()
}
tasks.withType<JavaCompile>().configureEach { options.release.set(11) }

tasks.withType<Javadoc>().configureEach {
    (options as StandardJavadocDocletOptions).addStringOption("Xdoclint:none", "-quiet")
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}

// The JAR carries the native library inside it, under natives/<system>-<processor>/, and unpacks the right
// one when the class loads. This task builds it for the computer it runs on, from the same CMake project and
// JNI code as the Android binding. CI does that on each system and puts them all in one JAR.
val system = System.getProperty("os.name").lowercase().let {
    when {
        it.contains("mac") -> "macos"
        it.contains("win") -> "windows"
        else -> "linux"
    }
}
val processor = System.getProperty("os.arch").let { if (it == "aarch64" || it == "arm64") "arm64" else "x64" }
val nativeBuild = layout.buildDirectory.dir("native-build")
val nativeResources = layout.buildDirectory.dir("native-resources")

val buildNative by tasks.registering {
    val source = rootDir.resolve("../android/speechwarp/src/main/cpp")
    inputs.dir(source)
    inputs.dir(rootDir.resolve("../../src"))
    outputs.dir(nativeResources)
    doLast {
        val build = nativeBuild.get().asFile
        providers.exec { commandLine("cmake", "-S", source.path, "-B", build.path, "-DCMAKE_BUILD_TYPE=Release") }.result.get()
        providers.exec { commandLine("cmake", "--build", build.path, "--config", "Release") }.result.get()
        copy {
            from(build) { include("libspeechwarp_jni.dylib", "libspeechwarp_jni.so") }
            from(build.resolve("Release")) { include("speechwarp_jni.dll") }
            into(nativeResources.get().dir("natives/$system-$processor"))
        }
    }
}
sourceSets.main { resources.srcDir(nativeResources) }
tasks.processResources { dependsOn(buildNative) }
tasks.named("sourcesJar") { dependsOn(buildNative) }

// Native libraries built elsewhere (by CI, for the other systems) go in extra-natives/natives/... and are
// packed too.
sourceSets.main { resources.srcDir("extra-natives") }

tasks.test {
    systemProperty("speechwarp.header", rootDir.resolve("../../include/speechwarp.h").path)
}

tasks.jar {
    from(rootDir.resolve("../..")) {
        include("LICENSE", "NOTICE")
        into("META-INF/licenses/speechwarp")
    }
    from(rootDir.resolve("../../third_party")) {
        include("*/LICENSE", "kissfft/COPYING", "kissfft/LICENSES/*")
        into("META-INF/licenses/speechwarp/third_party")
    }
}

publishing {
    publications {
        register<MavenPublication>("release") {
            from(components["java"])
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
