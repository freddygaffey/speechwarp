package io.github.freddygaffey.speechwarp;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Locale;

/** Loads the native library, once, for every class of this package that has native methods. */
final class NativeLibrary {
    private NativeLibrary() {}

    static {
        load();
    }

    /** Makes sure the library is loaded: a class with native methods calls this from its static initializer. */
    static void ensureLoaded() {}

    /**
     * The JAR holds the native library for each system under natives/. Copy this system's to a temporary file
     * and load it. If it is not in the JAR, fall back to java.library.path, so a library installed some other
     * way still works.
     */
    private static void load() {
        String os = System.getProperty("os.name").toLowerCase(Locale.ROOT);
        String arch = System.getProperty("os.arch").toLowerCase(Locale.ROOT);
        String system = os.contains("mac") ? "macos" : os.contains("win") ? "windows" : "linux";
        String processor = arch.equals("aarch64") || arch.equals("arm64") ? "arm64" : "x64";
        String file = system.equals("windows") ? "speechwarp_jni.dll"
                : system.equals("macos") ? "libspeechwarp_jni.dylib" : "libspeechwarp_jni.so";
        String resource = "/natives/" + system + "-" + processor + "/" + file;

        try (InputStream in = NativeLibrary.class.getResourceAsStream(resource)) {
            if (in == null) {
                System.loadLibrary("speechwarp_jni");
                return;
            }
            Path directory = Files.createTempDirectory("speechwarp");
            Path library = directory.resolve(file);
            Files.copy(in, library, StandardCopyOption.REPLACE_EXISTING);
            library.toFile().deleteOnExit();
            directory.toFile().deleteOnExit();
            System.load(library.toString());
        } catch (IOException e) {
            throw new UnsatisfiedLinkError("speechwarp: could not unpack " + resource + ": " + e);
        }
    }
}
