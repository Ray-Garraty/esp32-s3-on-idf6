fn main() {
    if let Ok(link_args) = std::env::var("DEP_ESP_IDF_EMBUILD_LINK_ARGS") {
        let mut args = Vec::new();
        let mut current = String::new();
        let mut in_quotes = false;

        for c in link_args.chars() {
            match c {
                '"' => in_quotes = !in_quotes,
                ' ' if !in_quotes => {
                    if !current.is_empty() {
                        args.push(std::mem::take(&mut current));
                    }
                }
                _ => current.push(c),
            }
        }
        if !current.is_empty() {
            args.push(current);
        }

        for arg in &args {
            println!("cargo:rustc-link-arg-bins={}", arg);
        }
    }
}
