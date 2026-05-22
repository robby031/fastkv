use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let root = manifest.join("../..").canonicalize().unwrap();
    let include = root.join("include");
    let lib_dir = root.join("build/src");

    // link ke library fastkv yang sudah di-build oleh CMake
    println!("cargo:rustc-link-search=native={}", lib_dir.display());
    println!("cargo:rustc-link-lib=static=fastkv");
    println!("cargo:rustc-link-lib=dylib=pthread");
    println!("cargo:rustc-link-lib=dylib=m");

    // rebuild kalau header berubah
    println!("cargo:rerun-if-changed={}", include.join("fastkv.h").display());
    println!("cargo:rerun-if-changed={}", include.join("fastkv/types.h").display());
    println!("cargo:rerun-if-changed={}", include.join("fastkv/error.h").display());

    // perintahkan linker ke rpath supaya binary bisa menemukan shared lib (opsional)
    println!(
        "cargo:rustc-link-arg=-Wl,-rpath,{}",
        lib_dir.display()
    );
    let _ = include;
}
