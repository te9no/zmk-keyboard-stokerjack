#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rgb {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}
#[derive(Clone, Copy, Default)]
pub struct Status {
    pub transport: u8,
    pub profile: u8,
    pub battery: Option<u8>,
    pub layer: u8,
    pub caps: bool,
}
pub const FRAMES: u32 = 120;
const BLACK: Rgb = Rgb { r: 0, g: 0, b: 0 };
const COLORS: [Rgb; 5] = [
    Rgb { r: 0, g: 0, b: 16 },
    Rgb { r: 0, g: 12, b: 12 },
    Rgb { r: 0, g: 16, b: 0 },
    Rgb { r: 12, g: 7, b: 0 },
    Rgb { r: 12, g: 0, b: 12 },
];
fn ease(x: u32) -> u32 {
    let x = x.min(256);
    x * x * (768 - 2 * x) / 65536
}
fn mix(a: Rgb, b: Rgb, t: u32) -> Rgb {
    fn c(a: u8, b: u8, t: u32) -> u8 {
        ((a as u32 * (256 - t) + b as u32 * t + 128) / 256) as u8
    }
    Rgb {
        r: c(a.r, b.r, t),
        g: c(a.g, b.g, t),
        b: c(a.b, b.b, t),
    }
}
pub fn render(s: Status, f: u32) -> [Rgb; 4] {
    if f >= FRAMES {
        return [BLACK; 4];
    }
    let wheel = [
        Rgb { r: 16, g: 0, b: 0 },
        Rgb { r: 16, g: 16, b: 0 },
        Rgb { r: 0, g: 16, b: 0 },
        Rgb { r: 0, g: 16, b: 16 },
        Rgb { r: 0, g: 0, b: 16 },
        Rgb { r: 16, g: 0, b: 16 },
    ];
    let status = [
        match s.transport {
            1 => Rgb { r: 0, g: 12, b: 12 },
            2 => Rgb { r: 0, g: 16, b: 0 },
            _ => Rgb { r: 12, g: 5, b: 0 },
        },
        COLORS[s.profile.min(4) as usize],
        COLORS[s.layer.min(4) as usize],
        if s.caps {
            Rgb { r: 14, g: 0, b: 10 }
        } else {
            BLACK
        },
    ];
    core::array::from_fn(|i| {
        let phase = (f * 16 + i as u32 * 256) % 1536;
        let mut c = mix(
            wheel[(phase / 256) as usize],
            wheel[((phase / 256 + 1) % 6) as usize],
            ease(phase % 256),
        );
        let gauge = match s.battery {
            None => Rgb { r: 0, g: 0, b: 3 },
            Some(b) if i < (b as usize).div_ceil(25) => match b {
                0..=20 => Rgb { r: 12, g: 0, b: 0 },
                21..=50 => Rgb { r: 10, g: 6, b: 0 },
                _ => Rgb { r: 0, g: 12, b: 0 },
            },
            _ => BLACK,
        };
        if f >= 45 {
            c = mix(c, gauge, ease((f - 45) * 256 / 15));
        }
        if f >= 75 {
            c = mix(c, status[i], ease((f - 75) * 256 / 15));
        }
        let envelope = if f < 15 {
            ease(f * 256 / 15)
        } else if f >= 100 {
            ease((119 - f) * 256 / 19)
        } else {
            256
        };
        mix(BLACK, c, envelope)
    })
}
pub fn nimh_percent(mv: u32) -> Option<u8> {
    if !(1000..=3300).contains(&mv) {
        return None;
    }
    let t = [
        2200, 2300, 2400, 2440, 2480, 2520, 2560, 2600, 2640, 2700, 2800,
    ];
    if mv <= t[0] {
        return Some(0);
    }
    for i in 1..t.len() {
        if mv < t[i] {
            return Some(((i - 1) * 10) as u8 + ((mv - t[i - 1]) * 10 / (t[i] - t[i - 1])) as u8);
        }
    }
    Some(100)
}
pub fn battery_mv(raw: i16) -> u32 {
    ((raw.max(0) as u64 * 3600 * 1470 + 4096 * 470 / 2) / (4096 * 470)) as u32
}
pub fn encode(p: [Rgb; 4]) -> [u8; 12] {
    let mut b = [0; 12];
    for (i, p) in p.iter().enumerate() {
        b[i * 3] = p.b;
        b[i * 3 + 1] = p.g;
        b[i * 3 + 2] = p.r;
    }
    b
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn battery() {
        assert_eq!(nimh_percent(0), None);
        assert_eq!(nimh_percent(2200), Some(0));
        assert_eq!(nimh_percent(2500), Some(45));
        assert_eq!(nimh_percent(2800), Some(100));
        assert!((battery_mv(909) as i32 - 2500).abs() < 4);
    }
    #[test]
    fn gradient() {
        for b in 0..=100 {
            let s = Status {
                battery: Some(b),
                caps: true,
                ..Status::default()
            };
            let mut prev = [0; 12];
            for f in 0..=FRAMES {
                let p = render(s, f);
                let now = encode(p);
                for i in 0..12 {
                    assert!(now[i] <= 16);
                    assert!((now[i] as i16 - prev[i] as i16).abs() <= 3)
                }
                if f == 0 || f >= 119 {
                    assert_eq!(now, [0; 12])
                }
                if (15..45).contains(&f) {
                    assert!(p.iter().all(|c| *c != BLACK));
                }
                prev = now;
            }
        }
    }
    #[test]
    fn bgr() {
        assert_eq!(&encode([Rgb { r: 1, g: 2, b: 3 }; 4])[..3], &[3, 2, 1]);
    }
}
