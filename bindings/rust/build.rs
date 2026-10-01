//! Compiles the C library into the crate. There is nothing to install and nothing to link against.

fn main() {
    let root = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let sources = [
        "src/speechwarp.c",
        "src/fft.c",
        "src/third_party_kissfft.c",
        "src/third_party_sonic.c",
        "src/third_party_speedy.c",
    ];

    let mut build = cc::Build::new();
    build
        .include(root.join("include"))
        .include(root.join("third_party/kissfft"))
        // Upstream is full of assertions.
        .define("NDEBUG", None)
        // Upstream code is compiled as it comes; its warnings are not ours to fix.
        .warnings(false);
    for source in sources {
        build.file(root.join(source));
        println!("cargo:rerun-if-changed={source}");
    }
    println!("cargo:rerun-if-changed=include/speechwarp.h");
    println!("cargo:rerun-if-changed=third_party");
    build.compile("speechwarp");
}
