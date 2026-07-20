// U6: struct methods run correctly under -g (also golden-checked at -O0). golden.
struct Rect {
    int w;
    int h;
}
impl Rect {
    fn area(self) -> int { return self.w * self.h; }
    fn scale(self, int f) -> int { return self.w * f * self.h * f; }
}
fn main() -> int {
    Rect r = Rect { w: 3, h: 4 };
    println("area = {}", r.area());
    println("scaled(2) = {}", r.scale(2));
    return 0;
}
