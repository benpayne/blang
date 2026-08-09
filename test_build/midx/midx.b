// U5 spike: a MIDDLE module that imports boxq and returns its foreign generic
// Box<int> from a pub fn. midx.bmod must reference boxq's Box by identity so a
// consumer that imports midx (but NOT boxq) can monomorphize Box<int>.
import boxq;
pub fn get_box(int n) -> Box<int> { return Box<int>(n); }
