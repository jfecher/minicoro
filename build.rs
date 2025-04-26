fn main() -> std::io::Result<()> {
    println!("cargo::rerun-if-changed=minicoro.c");
    cc::Build::new().file("minicoro.c").compile("aminicoro");
    Ok(())
}
