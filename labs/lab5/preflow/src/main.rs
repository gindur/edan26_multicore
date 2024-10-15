#[macro_use] extern crate text_io;

mod parallel;
mod sequential;

fn main() {
    let method = 1;

    if method == 0 {
        sequential::run();
    } else {
        parallel::run();
    }
}